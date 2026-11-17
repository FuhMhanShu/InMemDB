#include "memdb/influence.hpp"

#include "json.hpp"
#include "memdb/document_id.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <functional>
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

/// Lineargleichungssystem A*x = b per Gauß-Elimination mit Teilpivotisierung.
/// A wird in-place veraendert; false bei singulaerem System.
bool solveLinearSystem(std::vector<std::vector<double>>& A, std::vector<double>& b) {
    const std::size_t m = A.size();
    for (std::size_t col = 0; col < m; ++col) {
        std::size_t piv = col;
        for (std::size_t r = col + 1; r < m; ++r)
            if (std::fabs(A[r][col]) > std::fabs(A[piv][col]))
                piv = r;
        if (!(std::fabs(A[piv][col]) > 0.0))
            return false;
        if (piv != col) {
            std::swap(A[col], A[piv]);
            std::swap(b[col], b[piv]);
        }
        for (std::size_t r = col + 1; r < m; ++r) {
            const double f = A[r][col] / A[col][col];
            if (f == 0.0)
                continue;
            for (std::size_t cc = col; cc < m; ++cc)
                A[r][cc] -= f * A[col][cc];
            b[r] -= f * b[col];
        }
    }
    for (std::size_t i = m; i-- > 0;) {
        double s = b[i];
        for (std::size_t j = i + 1; j < m; ++j)
            s -= A[i][j] * b[j];
        b[i] = s / A[i][i];
    }
    // Härtung: bei Extremwerten können Zwischenergebnisse unendlich werden –
    // dann lieber "nicht lösbar" melden, als NaN in die Statistik zu schreiben.
    for (std::size_t i = 0; i < m; ++i)
        if (!std::isfinite(b[i]))
            return false;
    return true;
}

/// Härtung: Pearson über zentrierte zweistufige Summen in long double – die
/// Summenformel (n*Sxy - Sx*Sy) verliert bei großen Werten mit kleinem
/// Variationsbereich massiv an Genauigkeit (katastrophale Auslöschung).
struct PairAcc {
    std::size_t n = 0;
    long double sx = 0.0L, sy = 0.0L;
    long double mx = 0.0L, my = 0.0L;
    long double cxy = 0.0L, cxx = 0.0L, cyy = 0.0L;
};

} // namespace

std::string toString(FieldRole role) {
    switch (role) {
        case FieldRole::Driver: return "driver";
        case FieldRole::Dependent: return "dependent";
        case FieldRole::Mediator: return "mediator";
        case FieldRole::Independent: return "independent";
    }
    return "?";
}

InfluenceAnalysis analyzeInfluence(const Collection& c, const InfluenceOptions& opts) {
    InfluenceAnalysis res;
    res.collection = c.name();
    res.docCount = c.size();
    const std::size_t n = res.docCount;
    res.sampleSize =
        (opts.numeric.sampleSize > 0 && opts.numeric.sampleSize < n) ? opts.numeric.sampleSize : n;

    // Ein Scan: pro Dokument ID + numerische Werte (Feldindex bei Erstsicht vergeben).
    const bool sampled = opts.numeric.sampleSize > 0 && opts.numeric.sampleSize < n;
    struct DocRow {
        DocumentId id;
        std::vector<std::pair<std::size_t, Value>> vals;
    };
    std::vector<DocRow> rows;
    std::vector<std::string> fieldNames;

    auto collect = [&](const Document& d) {
        rows.emplace_back();
        DocRow& row = rows.back();
        row.id = d.id();
        d.forEachField([&](const std::string& k, const Value& v) {
            if (!v.isNumber() || !std::isfinite(v.asDouble()))
                return;
            std::size_t idx = fieldNames.size();
            for (std::size_t i = 0; i < fieldNames.size(); ++i) {
                if (fieldNames[i] == k) {
                    idx = i;
                    break;
                }
            }
            if (idx == fieldNames.size())
                fieldNames.push_back(k);
            row.vals.emplace_back(idx, v);
        });
    };
    if (sampled) {
        for (auto id : c.sample(opts.numeric.sampleSize, opts.numeric.seed))
            if (auto d = c.findById(id))
                collect(*d);
    } else {
        c.forEach(Expression{}, collect);
    }
    // Einfuegereihenfolge rekonstruieren (IDs sequenziell via nextId++, Map iteriert unsortiert).
    std::sort(rows.begin(), rows.end(),
              [](const DocRow& a, const DocRow& b) { return a.id < b.id; });

    const std::size_t cap = std::min(fieldNames.size(), opts.numeric.maxFieldsPerCollection);
    res.numericFieldCount = cap;

    // Wertzahl je Feld (Felder unter minValuesPerField ueberspringen, konsistent zu FA-30)
    std::vector<std::size_t> valueCount(cap, 0);
    for (const auto& row : rows)
        for (const auto& fv : row.vals)
            if (fv.first < cap)
                ++valueCount[fv.first];
    std::vector<std::size_t> fieldIdx; // gueltige Feldindesse
    for (std::size_t i = 0; i < cap; ++i)
        if (valueCount[i] >= opts.numeric.minValuesPerField)
            fieldIdx.push_back(i);

    if (fieldIdx.empty()) {
        res.note = "keine numerischen Felder mit genug Werten";
        return res;
    }

    // 1) Paarweise Pearson-Korrelationen (nur Felder mit >= 3 gemeinsamen Dokumenten)
    std::map<std::pair<std::size_t, std::size_t>, PairAcc> pairAcc;
    auto eachPair = [&](auto&& cb) {
        for (const auto& row : rows) {
            if (row.vals.size() < 2)
                continue;
            for (std::size_t a = 0; a < row.vals.size(); ++a) {
                if (row.vals[a].first >= cap)
                    continue;
                for (std::size_t b = a + 1; b < row.vals.size(); ++b) {
                    if (row.vals[b].first >= cap)
                        continue;
                    std::pair<std::size_t, std::size_t> key(row.vals[a].first, row.vals[b].first);
                    if (key.first > key.second)
                        std::swap(key.first, key.second);
                    // Werte-Slots konsistent zum sortierten Schlüssel zuordnen
                    // (Feldreihenfolge kann je Dokument wechseln)
                    double xv = row.vals[a].second.asDouble();
                    double yv = row.vals[b].second.asDouble();
                    if (key.first != row.vals[a].first)
                        std::swap(xv, yv);
                    cb(key, xv, yv);
                }
            }
        }
    };
    eachPair([&](std::pair<std::size_t, std::size_t> key, double xv, double yv) {
        PairAcc& p = pairAcc[key];
        ++p.n;
        p.sx += static_cast<long double>(xv);
        p.sy += static_cast<long double>(yv);
    });
    for (auto& kv : pairAcc) {
        PairAcc& p = kv.second;
        p.mx = p.sx / static_cast<long double>(p.n);
        p.my = p.sy / static_cast<long double>(p.n);
    }
    eachPair([&](std::pair<std::size_t, std::size_t> key, double xv, double yv) {
        PairAcc& p = pairAcc[key];
        const long double dx = static_cast<long double>(xv) - p.mx;
        const long double dy = static_cast<long double>(yv) - p.my;
        p.cxy += dx * dy;
        p.cxx += dx * dx;
        p.cyy += dy * dy;
    });
    std::map<std::pair<std::size_t, std::size_t>, double> pearson; // geordnete Indexpaare
    for (const auto& kv : pairAcc) {
        const PairAcc& p = kv.second;
        if (p.n < 3)
            continue;
        if (!(p.cxx > 0.0L) || !(p.cyy > 0.0L))
            continue; // konstantes Feld: Korrelation bedeutungslos
        long double lr = p.cxy / std::sqrt(p.cxx * p.cyy);
        double r = static_cast<double>(lr);
        if (!std::isfinite(r))
            continue;
        if (r > 1.0)
            r = 1.0;
        if (r < -1.0)
            r = -1.0;
        pearson[kv.first] = r;
    }

    // 2) Funktionale Abhaengigkeiten: gleiche source-Werte => eindeutiger target-Wert?
    //    Beide Richtungen pruefen; bei beidseitiger Funktionalitaet gewinnt die Richtung
    //    mit mehr distinct source-Werten (bei Gleichstand der alphabetisch kleinere Source).
    struct FdCandidate {
        double coverage = 0.0;
        std::size_t groups = 0;
        std::size_t pairs = 0;
        std::size_t distinctSource = 0;
        std::size_t distinctTarget = 0;
    };
    auto checkDirection = [&](std::size_t s, std::size_t t) {
        FdCandidate out;
        struct GroupData {
            std::size_t count = 0;
            std::set<Value, ValueLess> targets;
        };
        std::map<Value, GroupData, ValueLess> groups;
        std::size_t pairs = 0;
        for (const auto& row : rows) {
            const Value* sv = nullptr;
            const Value* tv = nullptr;
            for (const auto& fv : row.vals) {
                if (fv.first == s)
                    sv = &fv.second;
                else if (fv.first == t)
                    tv = &fv.second;
            }
            if (sv && tv) {
                ++pairs;
                GroupData& g = groups[*sv];
                ++g.count;
                g.targets.insert(*tv);
            }
        }
        out.pairs = pairs;
        out.distinctSource = groups.size();
        std::set<Value, ValueLess> targets;
        for (const auto& g : groups)
            targets.insert(g.second.targets.begin(), g.second.targets.end());
        out.distinctTarget = targets.size();
        // Evidenz sind nur Gruppen mit wiederholtem source-Wert: bei lauter
        // eindeutigen source-Werten ist jedes Ziel "eindeutig" (kein Beweis).
        std::size_t repeatGroups = 0, uniqueRepeatGroups = 0;
        for (const auto& g : groups) {
            if (g.second.count < 2)
                continue;
            ++repeatGroups;
            if (g.second.targets.size() == 1)
                ++uniqueRepeatGroups;
        }
        out.groups = repeatGroups;
        if (repeatGroups < 2 || out.distinctTarget < 2)
            return out; // zu wenig Evidenz bzw. Ziel konstant
        out.coverage =
            static_cast<double>(uniqueRepeatGroups) / static_cast<double>(repeatGroups);
        return out;
    };
    for (std::size_t ii = 0; ii < fieldIdx.size(); ++ii) {
        for (std::size_t jj = ii + 1; jj < fieldIdx.size(); ++jj) {
            const std::size_t f1 = fieldIdx[ii];
            const std::size_t f2 = fieldIdx[jj];
            std::pair<std::size_t, std::size_t> key(f1, f2);
            if (f1 > f2)
                std::swap(key.first, key.second);
            if (pairAcc.count(key) == 0 || pairAcc.at(key).n < opts.minDependencyPairs)
                continue;
            const FdCandidate c12 = checkDirection(f1, f2);
            const FdCandidate c21 = checkDirection(f2, f1);
            const bool fwd = c12.coverage >= opts.dependencyRatio;
            const bool bwd = c21.coverage >= opts.dependencyRatio;
            if (!fwd && !bwd)
                continue;
            bool useFwd;
            if (fwd && bwd)
                useFwd = c12.distinctSource != c21.distinctSource
                             ? c12.distinctSource > c21.distinctSource
                             : fieldNames[f1] < fieldNames[f2];
            else
                useFwd = fwd;
            const FdCandidate& cand = useFwd ? c12 : c21;
            FunctionalDependency fd;
            fd.source = fieldNames[useFwd ? f1 : f2];
            fd.target = fieldNames[useFwd ? f2 : f1];
            fd.coverage = cand.coverage;
            fd.groups = cand.groups;
            fd.pairs = cand.pairs;
            res.dependencies.push_back(std::move(fd));
        }
    }
    std::sort(res.dependencies.begin(), res.dependencies.end(),
              [](const FunctionalDependency& x, const FunctionalDependency& y) {
                  if (x.source != y.source)
                      return x.source < y.source;
                  return x.target < y.target;
              });

    // 3) Multiples R^2 je Feld: Regression gegen die uebrigen Felder (Complete Cases).
    //    Bei mehr als maxPredictors Kandidaten zaehlen die staerksten |r|-Partner.
    auto computeRSquared = [&](std::size_t y, std::vector<std::size_t> preds, double& r2Out,
                               std::size_t& rowsOut) {
        // Praediktoren auf Complete Cases sammeln
        std::vector<std::vector<double>> X;
        std::vector<double> yv;
        for (const auto& row : rows) {
            double yval = 0.0;
            bool hasY = false;
            std::vector<double> xs(preds.size(), 0.0);
            std::size_t found = 0;
            for (const auto& fv : row.vals) {
                if (fv.first == y) {
                    hasY = true;
                    yval = fv.second.asDouble();
                }
                for (std::size_t p = 0; p < preds.size(); ++p)
                    if (fv.first == preds[p])
                        xs[p] = fv.second.asDouble(), ++found;
            }
            if (!hasY || found != preds.size())
                continue;
            X.push_back(std::move(xs));
            yv.push_back(yval);
        }
        rowsOut = yv.size();
        const std::size_t k = preds.size();
        if (yv.size() < std::max(opts.minRegressionRows, k + 2))
            return false;
        // Härtung: Prädiktoren und Ziel mittelwertzentrieren. Bei großen Offsets
        // (z. B. 1e12) würde die Intercept-Spalte in den unzentrierten
        // Normalgleichungen die kleine Restvarianz im Rundungsrauschen ertrinken
        // lassen; zentriert entfällt der Intercept und die Kondition bleibt beherrschbar.
        const double rn = static_cast<double>(yv.size());
        std::vector<double> mx(k, 0.0);
        double my = 0.0;
        for (std::size_t r = 0; r < X.size(); ++r) {
            for (std::size_t i = 0; i < k; ++i)
                mx[i] += X[r][i];
            my += yv[r];
        }
        for (std::size_t i = 0; i < k; ++i)
            mx[i] /= rn;
        my /= rn;
        if (!std::isfinite(my))
            return false;
        for (std::size_t i = 0; i < k; ++i)
            if (!std::isfinite(mx[i]))
                return false; // Härtung: Extremwerte, Arithmetik übergelaufen
        double sst = 0.0;
        for (double v : yv)
            sst += (v - my) * (v - my);
        if (!std::isfinite(sst))
            return false; // Härtung: Extremwerte, Arithmetik übergelaufen
        if (!(sst > 0.0))
            return false; // konstantes Ziel: R^2 bedeutungslos
        // Normalgleichungen (Xc'Xc + lambda*I) beta = Xc'yc auf zentrierten Werten
        std::vector<std::vector<double>> A(k, std::vector<double>(k, 0.0));
        std::vector<double> b(k, 0.0);
        for (std::size_t r = 0; r < X.size(); ++r) {
            for (std::size_t i = 0; i < k; ++i) {
                const double xi = X[r][i] - mx[i];
                b[i] += xi * (yv[r] - my);
                for (std::size_t j = 0; j < k; ++j)
                    A[i][j] += xi * (X[r][j] - mx[j]);
            }
        }
        double diag = 0.0;
        for (std::size_t i = 0; i < k; ++i)
            diag += std::fabs(A[i][i]);
        const double lambda = 1e-9 * std::max(1.0, diag / static_cast<double>(k));
        for (std::size_t i = 0; i < k; ++i)
            A[i][i] += lambda;
        if (!solveLinearSystem(A, b))
            return false;
        double sse = 0.0;
        for (std::size_t r = 0; r < X.size(); ++r) {
            double e = yv[r] - my;
            for (std::size_t i = 0; i < k; ++i)
                e -= b[i] * (X[r][i] - mx[i]);
            sse += e * e;
        }
        r2Out = 1.0 - sse / sst;
        if (!std::isfinite(r2Out))
            return false; // Härtung: kein NaN/Inf in Score/Sortierung sickern lassen
        if (r2Out < 0.0)
            r2Out = 0.0;
        if (r2Out > 1.0)
            r2Out = 1.0;
        return true;
    };

    // 4) Einfluss-Profile je Feld
    std::map<std::size_t, std::size_t> determines, determinedBy;
    for (const auto& fd : res.dependencies) {
        for (std::size_t i = 0; i < fieldNames.size(); ++i) {
            if (fieldNames[i] == fd.source)
                ++determines[i];
            if (fieldNames[i] == fd.target)
                ++determinedBy[i];
        }
    }
    for (std::size_t fi : fieldIdx) {
        FieldInfluence inf;
        inf.field = fieldNames[fi];
        for (std::size_t fj : fieldIdx) {
            if (fj == fi)
                continue;
            std::pair<std::size_t, std::size_t> key(fi, fj);
            if (key.first > key.second)
                std::swap(key.first, key.second);
            const auto it = pearson.find(key);
            if (it == pearson.end())
                continue;
            ++inf.partners;
            const double ar = std::fabs(it->second);
            inf.hubScore += ar;
            if (ar >= opts.strongCorrelation)
                ++inf.strongCorrelations;
            if (ar > inf.maxAbsPearson) {
                inf.maxAbsPearson = ar;
                inf.strongestPartner = fieldNames[fj];
            }
        }
        inf.hubRatio = inf.partners > 0 ? inf.hubScore / static_cast<double>(inf.partners) : 0.0;
        inf.determines = determines.count(fi) ? determines[fi] : 0;
        inf.determinedBy = determinedBy.count(fi) ? determinedBy[fi] : 0;

        // R^2 gegen die uebrigen Felder (bei vielen Feldern nur die staerksten Partner)
        {
            std::vector<std::pair<double, std::size_t>> byR;
            for (std::size_t fj : fieldIdx) {
                if (fj == fi)
                    continue;
                std::pair<std::size_t, std::size_t> key(fi, fj);
                if (key.first > key.second)
                    std::swap(key.first, key.second);
                const auto it = pearson.find(key);
                if (it == pearson.end())
                    continue;
                byR.emplace_back(std::fabs(it->second), fj);
            }
            std::sort(byR.begin(), byR.end(),
                      [](const auto& x, const auto& y) { return x.first > y.first; });
            std::vector<std::size_t> preds;
            for (std::size_t i = 0; i < byR.size() && i < opts.maxPredictors; ++i)
                preds.push_back(byR[i].second);
            if (!preds.empty())
                inf.rSquaredComputed =
                    computeRSquared(fi, preds, inf.rSquared, inf.regressionRows);
        }

        // Rolle und kombinierter Score: Treiber ist, wer andere Felder bestimmt
        // oder mehrere starke Korrelationen hat, ohne selbst erklaert zu sein.
        const bool isDriver = inf.determines > 0 ||
                              (inf.strongCorrelations >= 2 && inf.rSquaredComputed &&
                               inf.rSquared < opts.rSquaredHigh);
        const bool isDependent =
            inf.determinedBy > 0 || (inf.rSquaredComputed && inf.rSquared >= opts.rSquaredHigh);
        inf.role = isDriver && isDependent ? FieldRole::Mediator
                   : isDriver               ? FieldRole::Driver
                   : isDependent            ? FieldRole::Dependent
                                            : FieldRole::Independent;
        const double fdRatio =
            inf.partners > 0 ? static_cast<double>(inf.determines) / static_cast<double>(inf.partners)
                            : 0.0;
        const double r2Part = inf.rSquaredComputed ? (1.0 - inf.rSquared) : 0.5;
        inf.influenceScore = 0.5 * inf.hubRatio + 0.3 * fdRatio + 0.2 * r2Part;
        if (!std::isfinite(inf.influenceScore))
            inf.influenceScore = 0.0; // Härtung: NaN bricht die Sortierung (strict weak ordering)

        // Begruendung (Deutsch)
        if (inf.determines > 0) {
            inf.rationale += "bestimmt funktional " + std::to_string(inf.determines) +
                             " Feld(er)";
            for (const auto& fd : res.dependencies)
                if (fd.source == inf.field)
                    inf.rationale += " (-> " + fd.target + ")";
            inf.rationale += "; ";
        }
        if (inf.strongCorrelations > 0)
            inf.rationale += std::to_string(inf.strongCorrelations) +
                             " starke Korrelation(en), staerkster |r| " +
                             num(inf.maxAbsPearson) + " mit " + inf.strongestPartner + "; ";
        if (inf.rSquaredComputed)
            inf.rationale += "R2 " + num(inf.rSquared) +
                             (inf.rSquared >= opts.rSquaredHigh ? " (von uebrigen erklaert)"
                                                                : " (nicht vollstaendig erklaert)") +
                             "; ";
        else
            inf.rationale += "R2 nicht bestimmbar; ";
        if (inf.determinedBy > 0) {
            inf.rationale += "funktional abhaengig von " + std::to_string(inf.determinedBy) +
                             " Feld(er)";
            for (const auto& fd : res.dependencies)
                if (fd.target == inf.field)
                    inf.rationale += " (" + fd.source + " -> " + inf.field + ")";
            inf.rationale += "; ";
        }
        while (!inf.rationale.empty() &&
               (inf.rationale.back() == ' ' || inf.rationale.back() == ';'))
            inf.rationale.pop_back();
        res.fields.push_back(std::move(inf));
    }
    std::sort(res.fields.begin(), res.fields.end(),
              [](const FieldInfluence& x, const FieldInfluence& y) {
                  if (x.influenceScore != y.influenceScore)
                      return x.influenceScore > y.influenceScore;
                  return x.field < y.field;
              });
    if (!res.fields.empty()) {
        const FieldInfluence& top = res.fields.front();
        const bool dominanceSignal =
            top.determines > 0 || top.strongCorrelations > 0 ||
            top.hubRatio >= opts.strongCorrelation;
        if (dominanceSignal)
            res.topDriver = top.field;
    }

    // Kurzfassung
    if (!res.dependencies.empty()) {
        res.note += "Funktionale Abhaengigkeiten: ";
        bool first = true;
        for (const auto& fd : res.dependencies) {
            if (!first)
                res.note += ", ";
            first = false;
            res.note += fd.source + " -> " + fd.target;
        }
        res.note += "; ";
    }
    std::string drivers, dependents;
    for (const auto& f : res.fields) {
        if (f.role == FieldRole::Driver || f.role == FieldRole::Mediator) {
            if (!drivers.empty())
                drivers += ", ";
            drivers += f.field;
        } else if (f.role == FieldRole::Dependent) {
            if (!dependents.empty())
                dependents += ", ";
            dependents += f.field;
        }
    }
    if (!drivers.empty())
        res.note += "Treiber: " + drivers + "; ";
    if (!dependents.empty())
        res.note += "abhaengig: " + dependents + "; ";
    if (res.note.empty())
        res.note = "keine starken Zusammenhaenge zwischen den numerischen Feldern erkannt";
    while (!res.note.empty() && (res.note.back() == ' ' || res.note.back() == ';'))
        res.note.pop_back();
    return res;
}

std::vector<InfluenceAnalysis> analyzeInfluence(const std::vector<Collection>& colls,
                                               const InfluenceOptions& opts) {
    std::vector<InfluenceAnalysis> out;
    for (const auto& c : colls) {
        if (c.name() == "__references")
            continue;
        out.push_back(analyzeInfluence(c, opts));
    }
    return out;
}

std::vector<InfluenceAnalysis> analyzeInfluence(Database& db, const InfluenceOptions& opts) {
    std::vector<Collection> colls;
    for (const auto& name : db.collectionNames())
        colls.push_back(db.collection(name));
    return analyzeInfluence(colls, opts);
}

std::string toJson(const InfluenceAnalysis& a) {
    std::string out = "{\"collection\":" + detail::writeJsonString(a.collection) +
                      ",\"docCount\":" + std::to_string(a.docCount) +
                      ",\"sampleSize\":" + std::to_string(a.sampleSize) +
                      ",\"numericFieldCount\":" + std::to_string(a.numericFieldCount) +
                      ",\"topDriver\":" + detail::writeJsonString(a.topDriver) +
                      ",\"note\":" + detail::writeJsonString(a.note) + ",\"dependencies\":[";
    bool first = true;
    for (const auto& fd : a.dependencies) {
        if (!first)
            out += ',';
        first = false;
        out += "{\"source\":" + detail::writeJsonString(fd.source) +
               ",\"target\":" + detail::writeJsonString(fd.target) +
               ",\"coverage\":" + num(fd.coverage) +
               ",\"groups\":" + std::to_string(fd.groups) +
               ",\"pairs\":" + std::to_string(fd.pairs) + "}";
    }
    out += "],\"fields\":[";
    first = true;
    for (const auto& f : a.fields) {
        if (!first)
            out += ',';
        first = false;
        out += "{\"field\":" + detail::writeJsonString(f.field) +
               ",\"partners\":" + std::to_string(f.partners) +
               ",\"strongCorrelations\":" + std::to_string(f.strongCorrelations) +
               ",\"hubScore\":" + num(f.hubScore) +
               ",\"hubRatio\":" + num(f.hubRatio) +
               ",\"maxAbsPearson\":" + num(f.maxAbsPearson) +
               ",\"strongestPartner\":" + detail::writeJsonString(f.strongestPartner) +
               ",\"rSquaredComputed\":" + (f.rSquaredComputed ? "true" : "false") +
               ",\"rSquared\":" + num(f.rSquared) +
               ",\"regressionRows\":" + std::to_string(f.regressionRows) +
               ",\"determines\":" + std::to_string(f.determines) +
               ",\"determinedBy\":" + std::to_string(f.determinedBy) +
               ",\"influenceScore\":" + num(f.influenceScore) +
               ",\"role\":" + detail::writeJsonString(toString(f.role)) +
               ",\"rationale\":" + detail::writeJsonString(f.rationale) + "}";
    }
    out += "]}";
    return out;
}

std::string toJson(const std::vector<InfluenceAnalysis>& as) {
    std::string out = "[";
    bool first = true;
    for (const auto& a : as) {
        if (!first)
            out += ',';
        first = false;
        out += toJson(a);
    }
    out += "]";
    return out;
}

std::string toMarkdown(const InfluenceAnalysis& a) {
    std::string out = "# Einfluss-Analyse: " + a.collection + " (FA-31)\n\n";
    out += "- Dokumente: " + std::to_string(a.docCount) + " (untersucht: " +
           std::to_string(a.sampleSize) + "), numerische Felder: " +
           std::to_string(a.numericFieldCount) + "\n";
    out += "- " + a.note + "\n";
    if (!a.dependencies.empty()) {
        out += "\n## Funktionale Abhaengigkeiten\n\n";
        out += "| Quelle | Ziel | Coverage | Gruppen | Paare |\n|---|---|---|---|---|\n";
        for (const auto& fd : a.dependencies) {
            out += "| " + fd.source + " | " + fd.target + " | " + num(fd.coverage) + " | " +
                   std::to_string(fd.groups) + " | " + std::to_string(fd.pairs) + " |\n";
        }
    }
    if (!a.fields.empty()) {
        out += "\n## Feld-Rollen (nach Einfluss-Score)\n\n";
        out += "| Feld | Rolle | Score | starke r | Hub | R2 | bestimmt | bestimmt von | Begruendung |\n";
        out += "|---|---|---|---|---|---|---|---|---|\n";
        for (const auto& f : a.fields) {
            out += "| " + f.field + " | " + toString(f.role) + " | " + num(f.influenceScore) +
                   " | " + std::to_string(f.strongCorrelations) + " | " + num(f.hubRatio) +
                   " | " + (f.rSquaredComputed ? num(f.rSquared) : "-") + " | " +
                   std::to_string(f.determines) + " | " + std::to_string(f.determinedBy) +
                   " | " + (f.rationale.empty() ? "-" : f.rationale) + " |\n";
        }
    }
    return out;
}

std::string toMarkdown(const std::vector<InfluenceAnalysis>& as) {
    std::string out = "# Einfluss-Analyse (FA-31)\n\n";
    out += "| Collection | Treiber | Abhaengigkeiten | Anzahl Felder | Kurzfassung |\n";
    out += "|---|---|---|---|---|\n";
    for (const auto& a : as) {
        out += "| " + a.collection + " | " + (a.topDriver.empty() ? "-" : a.topDriver) + " | " +
               std::to_string(a.dependencies.size()) + " | " +
               std::to_string(a.fields.size()) + " | " + a.note + " |\n";
    }
    return out;
}

} // namespace memdb
