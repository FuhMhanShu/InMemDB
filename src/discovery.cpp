#include "memdb/discovery.hpp"

#include "memdb/references.hpp"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace memdb {
namespace {

bool isScalar(const Value& v) {
    return v.isNull() == false && (v.isNumber() || v.isString() || v.isBool());
}

/// Normalisierte Edit-Distanz (Levenshtein) als Ähnlichkeit in [0,1];Strings auf 64 Zeichen begrenzt.
double similarity(const std::string& a, const std::string& b) {
    if (a == b)
        return 1.0;
    std::string x = a.substr(0, 64), y = b.substr(0, 64);
    if (x.empty() || y.empty())
        return 0.0;
    std::vector<std::size_t> prev(y.size() + 1), cur(y.size() + 1);
    for (std::size_t j = 0; j <= y.size(); ++j)
        prev[j] = j;
    for (std::size_t i = 1; i <= x.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= y.size(); ++j) {
            std::size_t cost = x[i - 1] == y[j - 1] ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }
    const double dist = static_cast<double>(prev[y.size()]);
    return 1.0 - dist / static_cast<double>(std::max(x.size(), y.size()));
}

std::string normalize(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c == ' ' || c == '\t' || c == '\n')
            continue;
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

bool nameHeuristic(const std::string& field) { // Quellseite eines FK
    if (field.size() >= 3 && field.substr(field.size() - 3) == "_id")
        return true;
    return field.size() >= 4 && field.substr(field.size() - 4) == "_ref";
}
bool targetHeuristic(const std::string& field) { return field == "pk" || field == "id"; }

/// Einmalig pro Collection: Feldnamen + pro Feld (Wert-Set, Anzahl Docs mit Feld), gesamplelt.
struct FieldStats {
    std::set<Value, ValueLess> values;
    std::size_t docsWithField = 0;
};
struct CollectionScan {
    std::string name;
    std::size_t docCount = 0;
    std::vector<std::string> fields;
    std::map<std::string, FieldStats> byField;
};

CollectionScan scanCollection(const Collection& c, const DiscoveryOptions& opts) {
    CollectionScan scan;
    scan.name = c.name();
    scan.docCount = c.size();
    std::vector<DocumentId> ids;
    if (opts.sampleSize > 0 && opts.sampleSize < scan.docCount)
        ids = c.sample(opts.sampleSize, opts.seed);
    else
        ids = c.findIds(); // match-all

    std::set<std::string> fieldNames;
    auto collect = [&](const Document& d) {
        d.forEachField([&](const std::string& k, const Value& v) { fieldNames.insert(k); });
    };
    if (opts.sampleSize > 0 && opts.sampleSize < scan.docCount) {
        for (auto id : ids)
            if (auto d = c.findById(id))
                collect(*d);
    } else {
        c.forEach(Expression{}, collect);
    }
    scan.fields.assign(fieldNames.begin(), fieldNames.end());
    if (scan.fields.size() > opts.maxFieldsPerCollection)
        scan.fields.resize(opts.maxFieldsPerCollection);

    // Werte pro Feld (ein Durchlauf über die Docs)
    std::map<std::string, std::set<Value, ValueLess>> vals;
    std::map<std::string, std::size_t> withField;
    auto collectVals = [&](const Document& d) {
        d.forEachField([&](const std::string& k, const Value& v) {
            if (!isScalar(v))
                return;
            if (std::find(scan.fields.begin(), scan.fields.end(), k) == scan.fields.end())
                return;
            vals[k].insert(v);
            ++withField[k];
        });
    };
    if (opts.sampleSize > 0 && opts.sampleSize < scan.docCount) {
        for (auto id : ids)
            if (auto d = c.findById(id))
                collectVals(*d);
    } else {
        c.forEach(Expression{}, collectVals);
    }
    for (auto& f : scan.fields) {
        scan.byField[f].values = std::move(vals[f]);
        scan.byField[f].docsWithField = withField[f];
    }
    return scan;
}

const char* kindName(RelationKind k) {
    switch (k) {
        case RelationKind::ForeignKey: return "foreign_key";
        case RelationKind::FunctionalDependency: return "functional_dependency";
        case RelationKind::Correlation: return "correlation";
        case RelationKind::NearDuplicate: return "near_duplicate";
    }
    return "?";
}

std::string num(double d) {
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof(buf), d);
    return std::string(buf, r.ptr);
}

} // namespace

std::vector<DiscoveredRelation> discoverForeignKeys(const std::vector<Collection>& colls,
                                                    const DiscoveryOptions& opts) {
    std::vector<DiscoveredRelation> out;
    std::vector<CollectionScan> scans;
    scans.reserve(colls.size());
    for (const auto& c : colls)
        scans.push_back(scanCollection(c, opts));

    for (std::size_t a = 0; a < scans.size(); ++a) {
        for (std::size_t b = 0; b < scans.size(); ++b) {
            const auto& A = scans[a];
            const auto& B = scans[b];
            for (const auto& f : A.fields) {
                const auto& fa = A.byField.at(f);
                if (fa.values.empty())
                    continue;
                for (const auto& g : B.fields) {
                    if (a == b && f == g)
                        continue;
                    const auto& fb = B.byField.at(g);
                    if (fb.values.empty())
                        continue;
                    // Schnittmenge
                    std::size_t matched = 0;
                    for (const auto& v : fa.values)
                        if (fb.values.count(v))
                            ++matched;
                    if (matched < 2)
                        continue; // zufällige Einzeltreffer ignorieren
                    const double confidence = static_cast<double>(matched) / fa.values.size();
                    if (confidence < opts.minConfidence)
                        continue;
                    const double support = A.docCount ? static_cast<double>(fa.docsWithField) / A.docCount : 0.0;
                    if (support < opts.minSupport && opts.minSupport > 0.0)
                        continue;
                    DiscoveredRelation r;
                    r.kind = RelationKind::ForeignKey;
                    r.fromCollection = A.name;
                    r.field = f;
                    r.toCollection = B.name;
                    r.toField = g;
                    r.support = support;
                    r.confidence = confidence;
                    r.sampleSize = A.docCount;
                    if (nameHeuristic(f) || targetHeuristic(g))
                        r.note = "name-heuristik";
                    out.push_back(std::move(r));
                }
            }
        }
    }
    // Gegenrichtung (A.f->B.g und B.g->A.f mit gleicher Konfidenz): nur die Seite mit
    // Namens-Heuristik behalten (z. B. user_id -> id, nicht id -> user_id)
    std::vector<DiscoveredRelation> dedup;
    for (std::size_t i = 0; i < out.size(); ++i) {
        bool drop = false;
        for (std::size_t j = 0; j < out.size(); ++j) {
            if (i == j)
                continue;
            const auto& a = out[i];
            const auto& b = out[j];
            if (a.confidence == b.confidence && a.fromCollection == b.toCollection && a.toCollection == b.fromCollection &&
                a.field == b.toField && a.toField == b.field) {
                const bool ai = nameHeuristic(a.field), bi = nameHeuristic(b.field);
                if (bi && !ai)
                    drop = true; // Gegenrichtung ist die FK-Seite (*_id) -> diese hier verwerfen
            }
        }
        if (!drop)
            dedup.push_back(out[i]);
    }
    out = std::move(dedup);
    std::sort(out.begin(), out.end(), [](const DiscoveredRelation& x, const DiscoveredRelation& y) {
        if (x.confidence != y.confidence)
            return x.confidence > y.confidence;
        return x.field < y.field;
    });
    return out;
}

std::vector<DiscoveredRelation> discoverFunctionalDependencies(const Collection& c,
                                                               const DiscoveryOptions& opts) {
    std::vector<DiscoveredRelation> out;
    CollectionScan scan = scanCollection(c, opts);
    const std::size_t n = scan.docCount;
    if (n < 2)
        return out;

    auto eachDoc = [&](const std::function<void(const Document&)>& cb) {
        if (opts.sampleSize > 0 && opts.sampleSize < n) {
            for (auto id : c.sample(opts.sampleSize, opts.seed))
                if (auto d = c.findById(id))
                    cb(*d);
        } else {
            c.forEach(Expression{}, cb);
        }
    };

    for (std::size_t i = 0; i < scan.fields.size(); ++i) {
        for (std::size_t j = 0; j < scan.fields.size(); ++j) {
            if (i == j)
                continue;
            const std::string X = scan.fields[i];
            const std::string Y = scan.fields[j];
            std::map<Value, std::set<Value, ValueLess>, ValueLess> groups;
            std::size_t both = 0;
            eachDoc([&](const Document& d) {
                const Value* xv = d.findPath(X);
                const Value* yv = d.findPath(Y);
                if (!xv || !yv || !isScalar(*xv) || !isScalar(*yv))
                    return;
                ++both;
                groups[*xv].insert(*yv);
            });
            if (groups.size() < 2 || both < 2)
                continue;
            const double support = static_cast<double>(both) / n;
            if (opts.minSupport > 0.0 && support < opts.minSupport)
                continue;
            std::size_t oneDistinct = 0;
            for (auto& [k, ys] : groups)
                if (ys.size() == 1)
                    ++oneDistinct;
            const double confidence = static_cast<double>(oneDistinct) / groups.size();
            const double thresh = std::max(opts.minConfidence, 0.999);
            if (confidence < thresh)
                continue;
            DiscoveredRelation r;
            r.kind = RelationKind::FunctionalDependency;
            r.fromCollection = scan.name;
            r.field = X;
            r.toCollection = scan.name;
            r.toField = Y;
            r.support = support;
            r.confidence = confidence;
            r.sampleSize = n;
            if (groups.size() == both)
                r.note = "trivial (X eindeutig)";
            out.push_back(std::move(r));
        }
    }
    return out;
}

std::vector<DiscoveredRelation> discoverCorrelations(const Collection& c, const DiscoveryOptions& opts) {
    std::vector<DiscoveredRelation> out;
    CollectionScan scan = scanCollection(c, opts);
    const std::size_t n = scan.docCount;
    if (n < 10)
        return out;

    auto eachDoc = [&](const std::function<void(const Document&)>& cb) {
        if (opts.sampleSize > 0 && opts.sampleSize < n) {
            for (auto id : c.sample(opts.sampleSize, opts.seed))
                if (auto d = c.findById(id))
                    cb(*d);
        } else {
            c.forEach(Expression{}, cb);
        }
    };

    for (std::size_t i = 0; i < scan.fields.size(); ++i) {
        for (std::size_t j = i + 1; j < scan.fields.size(); ++j) {
            const std::string X = scan.fields[i];
            const std::string Y = scan.fields[j];
            // numerisch: Pearson
            {
                double sx = 0, sy = 0, sxy = 0, sx2 = 0, sy2 = 0;
                std::size_t cnt = 0;
                eachDoc([&](const Document& d) {
                    const Value* xv = d.findPath(X);
                    const Value* yv = d.findPath(Y);
                    if (!xv || !yv || !xv->isNumber() || !yv->isNumber())
                        return;
                    const double x = xv->asDouble(), y = yv->asDouble();
                    ++cnt;
                    sx += x; sy += y; sxy += x * y; sx2 += x * x; sy2 += y * y;
                });
                if (cnt >= 10) {
                    const double denom = std::sqrt((cnt * sx2 - sx * sx) * (cnt * sy2 - sy * sy));
                    if (denom > 1e-12) {
                        const double r = (cnt * sxy - sx * sy) / denom;
                        if (std::fabs(r) >= opts.minCorrelation) {
                            DiscoveredRelation rel;
                            rel.kind = RelationKind::Correlation;
                            rel.fromCollection = scan.name;
                            rel.field = X;
                            rel.toCollection = scan.name;
                            rel.toField = Y;
                            rel.confidence = std::fabs(r);
                            rel.support = static_cast<double>(cnt) / n;
                            rel.sampleSize = n;
                            rel.note = (r > 0 ? "positiv pearson r=" : "negativ pearson r=") + num(r);
                            out.push_back(std::move(rel));
                        }
                    }
                }
            }
            // kategorial: Uncertainty-Coefficient-artig U(Y|X) = 1 - H(Y|X)/H(Y)
            {
                std::map<Value, std::map<Value, std::size_t, ValueLess>, ValueLess> tab;
                std::map<Value, std::size_t, ValueLess> yCount;
                std::size_t cnt = 0;
                eachDoc([&](const Document& d) {
                    const Value* xv = d.findPath(X);
                    const Value* yv = d.findPath(Y);
                    if (!xv || !yv || !xv->isString() || !yv->isString())
                        return;
                    ++cnt;
                    tab[*xv][*yv]++;
                    yCount[*yv]++;
                });
                if (cnt >= 10 && yCount.size() >= 2) {
                    auto h = [](const std::map<Value, std::size_t, ValueLess>& m, std::size_t tot) {
                        double e = 0;
                        for (auto& [k, v] : m) {
                            const double p = static_cast<double>(v) / tot;
                            e -= p * std::log(p);
                        }
                        return e;
                    };
                    const double hy = h(yCount, cnt);
                    if (hy > 1e-12) {
                        double hyx = 0;
                        for (auto& [xk, ys] : tab) {
                            std::size_t tot = 0;
                            for (auto& [yk, v] : ys)
                                tot += v;
                            hyx += static_cast<double>(tot) / cnt * h(ys, tot);
                        }
                        const double u = 1.0 - hyx / hy;
                        if (u >= opts.minCorrelation) {
                            DiscoveredRelation rel;
                            rel.kind = RelationKind::Correlation;
                            rel.fromCollection = scan.name;
                            rel.field = X;
                            rel.toCollection = scan.name;
                            rel.toField = Y;
                            rel.confidence = u;
                            rel.support = static_cast<double>(cnt) / n;
                            rel.sampleSize = n;
                            rel.note = "kategorial assoziation U=" + num(u);
                            out.push_back(std::move(rel));
                        }
                    }
                }
            }
        }
    }
    return out;
}

std::vector<DiscoveredRelation> discoverNearDuplicates(const std::vector<Collection>& colls,
                                                       const DiscoveryOptions& opts) {
    std::vector<DiscoveredRelation> out;
    // pro Collection+Feld: gesamplete String-Werte (normalisiert)
    struct ValRef {
        std::string coll, field, value;
    };
    std::vector<ValRef> all;
    for (const auto& c : colls) {
        CollectionScan scan = scanCollection(c, opts);
        for (const auto& f : scan.fields) {
            std::vector<std::string> vals;
            for (const auto& v : scan.byField.at(f).values) {
                if (v.isString()) {
                    std::string nv = normalize(v.asString());
                    if (!nv.empty() && vals.size() < opts.maxValuesPerField)
                        vals.push_back(std::move(nv));
                }
            }
            for (auto& v : vals)
                all.push_back(ValRef{scan.name, f, std::move(v)});
        }
    }
    for (std::size_t i = 0; i < all.size(); ++i) {
        for (std::size_t j = i + 1; j < all.size(); ++j) {
            // gleiches Feld vergleichen (ggf. über Collections hinweg)
            if (all[i].field != all[j].field)
                continue;
            const double sim = similarity(all[i].value, all[j].value);
            if (sim >= opts.minSimilarity && sim < 1.0) {
                DiscoveredRelation r;
                r.kind = RelationKind::NearDuplicate;
                r.fromCollection = all[i].coll;
                r.field = all[i].field;
                r.toCollection = all[j].coll;
                r.toField = all[j].field;
                r.confidence = sim;
                r.support = 1.0;
                r.sampleSize = all.size();
                r.note = "'" + all[i].value + "' ~ '" + all[j].value + "'";
                out.push_back(std::move(r));
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const DiscoveredRelation& x, const DiscoveredRelation& y) {
        return x.confidence > y.confidence;
    });
    return out;
}

std::vector<DiscoveredRelation> discoverRelations(Database& db, const DiscoveryOptions& opts) {
    std::vector<Collection> colls;
    for (const auto& n : db.collectionNames())
        colls.push_back(db.collection(n));
    return discoverRelations(colls, opts);
}

std::vector<DiscoveredRelation> discoverRelations(const std::vector<Collection>& colls,
                                                  const DiscoveryOptions& opts) {
    std::vector<DiscoveredRelation> out = discoverForeignKeys(colls, opts);
    for (const auto& c : colls) {
        auto fd = discoverFunctionalDependencies(c, opts);
        auto corr = discoverCorrelations(c, opts);
        out.insert(out.end(), fd.begin(), fd.end());
        out.insert(out.end(), corr.begin(), corr.end());
    }
    auto nd = discoverNearDuplicates(colls, opts);
    out.insert(out.end(), nd.begin(), nd.end());
    return out;
}

std::size_t applyDiscovered(Database& db, const std::vector<DiscoveredRelation>& rels,
                            ReferenceIndex& refs, double minConfidence) {
    std::size_t applied = 0;
    for (const auto& r : rels) {
        if (r.kind != RelationKind::ForeignKey || r.confidence < minConfidence)
            continue;
        if (!db.hasCollection(r.fromCollection) || !db.hasCollection(r.toCollection))
            continue; // z. B. database-übergreifender Befund
        Collection source = db.collection(r.fromCollection);
        Collection target = db.collection(r.toCollection);
        std::map<Value, DocumentId, ValueLess> targetByValue;
        target.forEach(Expression{}, [&](const Document& d) {
            if (const Value* v = d.findPath(r.toField))
                targetByValue.emplace(*v, d.id());
        });
        const std::string relation = "discovered_" + r.field;
        source.forEach(Expression{}, [&](const Document& d) {
            const Value* v = d.findPath(r.field);
            if (!v || v->isNull())
                return;
            auto it = targetByValue.find(*v);
            if (it == targetByValue.end())
                return;
            try {
                refs.link(r.fromCollection, d.id(), r.toCollection, it->second, relation);
                ++applied;
            } catch (const ReferenceExistsError&) {
            }
        });
    }
    return applied;
}

std::string toJson(const std::vector<DiscoveredRelation>& rels) {
    std::string out = "{\"relations\":[";
    bool first = true;
    for (const auto& r : rels) {
        if (!first)
            out += ',';
        first = false;
        out += "{\"kind\":" + detail::writeJsonString(kindName(r.kind)) + ",\"from\":" +
               detail::writeJsonString(r.fromCollection + "." + r.field) + ",\"to\":" +
               detail::writeJsonString(r.toCollection + "." + r.toField) + ",\"support\":" + num(r.support) +
               ",\"confidence\":" + num(r.confidence) + ",\"sampleSize\":" + std::to_string(r.sampleSize);
        if (!r.note.empty())
            out += ",\"note\":" + detail::writeJsonString(r.note);
        out += '}';
    }
    out += "]}";
    return out;
}

std::string toMermaid(const std::vector<DiscoveredRelation>& rels) {
    std::string out = "graph LR\n";
    std::set<std::string> nodes;
    for (const auto& r : rels) {
        nodes.insert(r.fromCollection);
        nodes.insert(r.toCollection);
    }
    std::size_t id = 0;
    std::map<std::string, std::string> nodeId;
    for (const auto& n : nodes) {
        nodeId[n] = "N" + std::to_string(id++);
        out += "  " + nodeId[n] + "[\"" + n + "\"]\n";
    }
    for (const auto& r : rels) {
        out += "  " + nodeId[r.fromCollection] + " -->|\"" + r.field + " (" + num(r.confidence) + ")\"| " +
               nodeId[r.toCollection] + "\n";
    }
    return out;
}

} // namespace memdb
