// Komplexe Metadaten-Analyse (FA-34, Kategorie 3): rekursives Pfad-Profil
// (verschachtelte Dokumente, Arrays, Binär, Typ-Drift) und Schema-Varianten.
#include "memdb/complex_analysis.hpp"

#include "json.hpp"
#include "memdb/document_id.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace memdb {
namespace {

std::string num(double d) {
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof(buf), d);
    return std::string(buf, r.ptr);
}

/// Anteil in Prozent als Klartext (für Notes), z. B. 0.256 -> "26 %".
std::string pct(double ratio) {
    if (!std::isfinite(ratio) || ratio < 0.0)
        return "0 %";
    if (ratio > 1.0)
        ratio = 1.0;
    return std::to_string(static_cast<std::size_t>(ratio * 100.0 + 0.5)) + " %";
}

bool isScalarKind(PathKind k) {
    return k == PathKind::Null || k == PathKind::Boolean || k == PathKind::Number ||
           k == PathKind::Text || k == PathKind::Binary;
}

/// Aggregatzustand eines Pfades über alle untersuchten Dokumente.
struct PathState {
    std::size_t kinds[7] = {};       // indexiert mit static_cast<std::size_t>(PathKind)
    std::size_t scalar = 0;          // Sichtungen skalarer Typen
    std::size_t depth = 0;           // Tiefe bei Erstsicht
    bool isElementPath = false;      // endet auf "[]"
    bool distinctCapped = false;
    std::set<Value, ValueLess> distinctSet;
    std::set<DocumentId> docs;       // Dokumente mit mindestens einer Sichtung
};

/// Rekursiver Pfad-Walker mit Cap- und Tiefen-Guards.
struct Walker {
    const ComplexAnalysisOptions& opts;
    std::map<std::string, PathState>& paths;
    std::vector<std::string> docPaths; ///< Pfade des aktuellen Dokuments (Fingerprint)
    bool capped = false;
    bool depthLimited = false;

    bool record(const std::string& path, PathKind k, std::size_t depth, bool elem,
                const Value* scalar, DocumentId id) {
        auto it = paths.find(path);
        if (it == paths.end()) {
            if (paths.size() >= opts.maxPathsPerCollection) {
                capped = true;
                return false; // Härtung: Pfad-Cap – neue Pfade werden ignoriert
            }
            PathState st;
            st.depth = depth;
            st.isElementPath = elem;
            it = paths.emplace(path, std::move(st)).first;
        }
        PathState& st = it->second;
        st.kinds[static_cast<std::size_t>(k)] += 1;
        if (isScalarKind(k)) {
            st.scalar += 1;
            if (scalar && !st.distinctCapped) { // Härtung: Speicher-Guard für distinct
                st.distinctSet.insert(*scalar);
                if (st.distinctSet.size() >= opts.maxDistinctPerPath)
                    st.distinctCapped = true;
            }
        }
        st.docs.insert(id);
        docPaths.push_back(path);
        return true;
    }

    void walkValue(const std::string& path, const Value& v, std::size_t depth,
                  DocumentId id) {
        if (v.isDocument()) {
            record(path, PathKind::Document, depth, false, nullptr, id);
            if (depth >= opts.maxDepth) { // Härtung: Tiefen-Guard
                depthLimited = true;
                return;
            }
            if (auto d = v.asDocument())
                d->forEachField([&](const std::string& k, const Value& sv) {
                    walkValue(path + "." + k, sv, depth + 1, id);
                });
            return;
        }
        if (v.isArray()) {
            record(path, PathKind::Array, depth, false, nullptr, id);
            if (depth >= opts.maxDepth) {
                depthLimited = true;
                return;
            }
            const std::string ep = path + "[]";
            for (const auto& elem : v.asArray()) {
                if (elem.isDocument()) {
                    record(ep, PathKind::Document, depth + 1, true, nullptr, id);
                    if (auto d = elem.asDocument())
                        d->forEachField([&](const std::string& k, const Value& sv) {
                            walkValue(ep + "." + k, sv, depth + 2, id);
                        });
                } else if (elem.isArray()) {
                    walkValue(ep, elem, depth + 1, id);
                } else {
                    record(ep, scalarKindOf(elem), depth + 1, true, &elem, id);
                }
            }
            return;
        }
        record(path, scalarKindOf(v), depth, false, &v, id);
    }

    static PathKind scalarKindOf(const Value& v) {
        if (v.isNull())
            return PathKind::Null;
        if (v.isBool())
            return PathKind::Boolean;
        if (v.isNumber())
            return PathKind::Number;
        if (v.isString())
            return PathKind::Text;
        return PathKind::Binary;
    }
};

} // namespace

std::string pathKindName(PathKind k) {
    switch (k) {
    case PathKind::Null: return "null";
    case PathKind::Boolean: return "boolean";
    case PathKind::Number: return "number";
    case PathKind::Text: return "text";
    case PathKind::Binary: return "binary";
    case PathKind::Document: return "document";
    case PathKind::Array: return "array";
    }
    return "unknown";
}

std::vector<ComplexPathAnalysis> analyzeComplexFields(const Collection& c,
                                                      const ComplexAnalysisOptions& opts) {
    ComplexCollectionAnalysis full = analyzeComplex(c, opts);
    return std::move(full.paths);
}

ComplexCollectionAnalysis analyzeComplex(const Collection& c,
                                         const ComplexAnalysisOptions& opts) {
    ComplexCollectionAnalysis res;
    res.collection = c.name();
    res.docCount = c.size();
    const std::size_t n = res.docCount;
    res.sampleSize = (opts.sampleSize > 0 && opts.sampleSize < n) ? opts.sampleSize : n;

    const bool sampled = opts.sampleSize > 0 && opts.sampleSize < n;
    std::map<std::string, PathState> paths;
    std::set<std::string> topFields;
    std::map<std::string, std::size_t> variantCounts; // Fingerprint -> Dokumente
    Walker walker{opts, paths, {}, false, false};

    auto collect = [&](const Document& d) {
        walker.docPaths.clear();
        d.forEachField([&](const std::string& k, const Value& v) {
            topFields.insert(k);
            walker.walkValue(k, v, 1, d.id());
        });
        std::vector<std::string> fps(walker.docPaths.begin(), walker.docPaths.end());
        std::sort(fps.begin(), fps.end());
        fps.erase(std::unique(fps.begin(), fps.end()), fps.end());
        std::string fp;
        for (const auto& p : fps) {
            if (!fp.empty())
                fp += '|';
            fp += p;
        }
        ++variantCounts[fp];
    };
    if (sampled) {
        for (auto id : c.sample(opts.sampleSize, opts.seed))
            if (auto d = c.findById(id))
                collect(*d);
    } else {
        c.forEach(Expression{}, collect);
    }

    res.fieldCount = topFields.size();
    res.variantCount = variantCounts.size();

    // Pfad-Analysen (alphabetisch, map ist bereits sortiert)
    for (const auto& [path, st] : paths) {
        const std::size_t count = st.scalar + st.kinds[static_cast<std::size_t>(PathKind::Document)] +
                                  st.kinds[static_cast<std::size_t>(PathKind::Array)];
        if (count < opts.minSightingsPerPath) // Härtung: seltene Pfade überspringen
            continue;
        ComplexPathAnalysis a;
        a.collection = res.collection;
        a.path = path;
        a.depth = st.depth;
        a.isElementPath = st.isElementPath;
        std::size_t dominant = 0;
        for (std::size_t k = 0; k < 7; ++k) {
            if (st.kinds[k] == 0)
                continue;
            a.kinds.push_back({static_cast<PathKind>(k), st.kinds[k]});
            if (st.kinds[k] > st.kinds[dominant])
                dominant = k; // Tie-Break: erster (niedrigster) Typ gewinnt
        }
        a.dominantKind = static_cast<PathKind>(dominant);
        a.count = count;
        a.distinct = st.distinctSet.size();
        a.missing = res.sampleSize > st.docs.size() ? res.sampleSize - st.docs.size() : 0;
        a.missingRatio = res.sampleSize > 0
                              ? static_cast<double>(a.missing) / static_cast<double>(res.sampleSize)
                              : 0.0;
        a.dominantShare =
            count > 0 ? static_cast<double>(st.kinds[dominant]) / static_cast<double>(count) : 1.0;
        a.scalarShare = count > 0
                            ? static_cast<double>(st.scalar) / static_cast<double>(count)
                            : 1.0;
        a.typeDrift = a.kinds.size() > 1;

        if (a.typeDrift) {
            a.note = "Typ-Drift:";
            std::vector<PathKindCount> ks = a.kinds;
            std::sort(ks.begin(), ks.end(), [](const PathKindCount& x, const PathKindCount& y) {
                return x.count != y.count ? x.count > y.count
                                          : pathKindName(x.kind) < pathKindName(y.kind);
            });
            for (const auto& kc : ks)
                a.note += " " + pathKindName(kc.kind) + " (" +
                          pct(static_cast<double>(kc.count) / static_cast<double>(count)) + "),";
            if (!a.note.empty() && a.note.back() == ',')
                a.note.back() = ' ';
        } else if (a.dominantKind == PathKind::Binary) {
            a.note = "Binärdaten (" + std::to_string(a.count) + " Werte)";
        } else if (a.isElementPath && a.dominantKind == PathKind::Document) {
            a.note = "Array aus Dokumenten (" + std::to_string(a.count) + " Elemente)";
        }
        res.maxDepth = std::max(res.maxDepth, a.depth);
        res.paths.push_back(std::move(a));
    }
    res.pathCount = res.paths.size();

    // Schema-Varianten: nach Dokumentzahl absteigend, dann Fingerprint aufsteigend
    std::vector<std::pair<std::string, std::size_t>> vs(variantCounts.begin(),
                                                        variantCounts.end());
    std::sort(vs.begin(), vs.end(), [](const auto& x, const auto& y) {
        return x.second != y.second ? x.second > y.second : x.first < y.first;
    });
    const std::size_t kept = std::min(vs.size(), opts.maxSchemaVariants);
    for (std::size_t i = 0; i < kept; ++i) {
        ComplexSchemaVariant v;
        v.fingerprint = vs[i].first;
        v.docCount = vs[i].second;
        v.share = res.sampleSize > 0
                      ? static_cast<double>(v.docCount) / static_cast<double>(res.sampleSize)
                      : 0.0;
        std::string fp = v.fingerprint;
        std::size_t start = 0;
        while (start <= fp.size()) {
            const std::size_t bar = fp.find('|', start);
            v.fields.push_back(fp.substr(start, (bar == std::string::npos ? fp.size() : bar) - start));
            if (bar == std::string::npos)
                break;
            start = bar + 1;
        }
        res.variants.push_back(std::move(v));
    }
    if (!vs.empty())
        res.schemaConsistency =
            res.sampleSize > 0
                ? static_cast<double>(vs[0].second) / static_cast<double>(res.sampleSize)
                : 0.0;

    // Notes
    if (res.variantCount > 1)
        res.notes.push_back("Schema-Konsistenz " + pct(res.schemaConsistency) + ": " +
                            std::to_string(res.variantCount) + " Varianten");
    if (walker.capped)
        res.notes.push_back("Pfad-Cap erreicht (" + std::to_string(opts.maxPathsPerCollection) +
                           "): weitere Pfade ignoriert");
    if (walker.depthLimited)
        res.notes.push_back("Tiefen-Guard aktiv (" + std::to_string(opts.maxDepth) +
                            "): tiefere Pfade ignoriert");
    return res;
}

std::vector<ComplexCollectionAnalysis> analyzeComplex(const std::vector<Collection>& colls,
                                                      const ComplexAnalysisOptions& opts) {
    std::vector<ComplexCollectionAnalysis> out;
    for (const auto& c : colls) {
        if (c.name() == "__references")
            continue;
        out.push_back(analyzeComplex(c, opts));
    }
    return out;
}

std::vector<ComplexCollectionAnalysis> analyzeComplex(Database& db,
                                                      const ComplexAnalysisOptions& opts) {
    std::vector<Collection> colls;
    for (const auto& name : db.collectionNames())
        colls.push_back(db.collection(name));
    return analyzeComplex(colls, opts);
}

std::string toJson(const ComplexPathAnalysis& a) {
    std::string out = "{\"collection\":" + detail::writeJsonString(a.collection) +
                       ",\"path\":" + detail::writeJsonString(a.path) +
                       ",\"depth\":" + std::to_string(a.depth) +
                       ",\"isElementPath\":" + std::string(a.isElementPath ? "true" : "false") +
                       ",\"kinds\":[";
    bool first = true;
    for (const auto& kc : a.kinds) {
        if (!first)
            out += ',';
        first = false;
        out += "{\"kind\":" + detail::writeJsonString(pathKindName(kc.kind)) +
               ",\"count\":" + std::to_string(kc.count) + "}";
    }
    out += "],\"dominantKind\":" + detail::writeJsonString(pathKindName(a.dominantKind)) +
           ",\"count\":" + std::to_string(a.count) +
           ",\"distinct\":" + std::to_string(a.distinct) +
           ",\"missing\":" + std::to_string(a.missing) +
           ",\"missingRatio\":" + num(a.missingRatio) +
           ",\"dominantShare\":" + num(a.dominantShare) +
           ",\"scalarShare\":" + num(a.scalarShare) +
           ",\"typeDrift\":" + std::string(a.typeDrift ? "true" : "false") +
           ",\"note\":" + detail::writeJsonString(a.note) + "}";
    return out;
}

std::string toJson(const ComplexCollectionAnalysis& a) {
    std::string out = "{\"collection\":" + detail::writeJsonString(a.collection) +
                       ",\"docCount\":" + std::to_string(a.docCount) +
                       ",\"sampleSize\":" + std::to_string(a.sampleSize) +
                       ",\"fieldCount\":" + std::to_string(a.fieldCount) +
                       ",\"pathCount\":" + std::to_string(a.pathCount) +
                       ",\"maxDepth\":" + std::to_string(a.maxDepth) + ",\"paths\":[";
    bool first = true;
    for (const auto& p : a.paths) {
        if (!first)
            out += ',';
        first = false;
        out += toJson(p);
    }
    out += "],\"variantCount\":" + std::to_string(a.variantCount) +
           ",\"schemaConsistency\":" + num(a.schemaConsistency) + ",\"variants\":[";
    first = true;
    for (const auto& v : a.variants) {
        if (!first)
            out += ',';
        first = false;
        out += "{\"fingerprint\":" + detail::writeJsonString(v.fingerprint) + ",\"fields\":[";
        bool f2 = true;
        for (const auto& f : v.fields) {
            if (!f2)
                out += ',';
            f2 = false;
            out += detail::writeJsonString(f);
        }
        out += "],\"docCount\":" + std::to_string(v.docCount) +
               ",\"share\":" + num(v.share) + "}";
    }
    out += "],\"notes\":[";
    first = true;
    for (const auto& n : a.notes) {
        if (!first)
            out += ',';
        first = false;
        out += detail::writeJsonString(n);
    }
    out += "]}";
    return out;
}

std::string toMarkdown(const ComplexCollectionAnalysis& a) {
    std::string out = "# Komplexe Analyse: " + a.collection + " (FA-34)\n\n";
    out += "- Dokumente: " + std::to_string(a.docCount) + " (untersucht: " +
           std::to_string(a.sampleSize) + "), Top-Level-Felder: " +
           std::to_string(a.fieldCount) + ", Pfade: " + std::to_string(a.pathCount) +
           ", maximale Tiefe: " + std::to_string(a.maxDepth) +
           ", Schema-Konsistenz: " + pct(a.schemaConsistency) + "\n\n";
    if (a.paths.empty()) {
        out += "Keine Pfade gefunden.\n";
        return out;
    }
    out += "| Pfad | Tiefe | Typ | n | distinct | fehlt | Hinweis |\n";
    out += "|---|---|---|---|---|---|---|\n";
    for (const auto& p : a.paths) {
        out += "| " + p.path + " | " + std::to_string(p.depth) + " | " +
               pathKindName(p.dominantKind) + " | " + std::to_string(p.count) + " | " +
               std::to_string(p.distinct) + " | " + pct(p.missingRatio) + " | " +
               (p.note.empty() ? "-" : p.note) + " |\n";
    }
    out += "\n## Schema-Varianten\n\n| Variante | Felder | Dokumente | Anteil |\n|---|---|---|---|\n";
    if (a.variants.empty()) {
        out += "| (keine) | - | 0 | 0 % |\n";
    }
    for (const auto& v : a.variants) {
        std::string fields;
        for (const auto& f : v.fields) {
            if (!fields.empty())
                fields += ", ";
            fields += f;
        }
        out += "| " + v.fingerprint + " | " + fields + " | " + std::to_string(v.docCount) +
               " | " + pct(v.share) + " |\n";
    }
    if (!a.notes.empty()) {
        out += "\n## Hinweise\n\n";
        for (const auto& n : a.notes)
            out += "- " + n + "\n";
    }
    return out;
}

} // namespace memdb
