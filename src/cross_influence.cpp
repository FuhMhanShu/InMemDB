#include "memdb/cross_influence.hpp"

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

std::string fmtNum(double d) {
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof(buf), d);
    return std::string(buf, r.ptr);
}

/// Lineargleichungssystem A*x = b per Gauss-Elimination mit Teilpivotisierung.
/// A wird in-place veraendert; false bei singulaerem System.
/// (Eigene Benennung: keine Kollision mit influence.cpp im Unity-Build.)
bool ciSolveLinearSystem(std::vector<std::vector<double>>& A, std::vector<double>& b) {
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
    return true;
}

/// Quadratsummen pro Zeilenpaar: n, sx, sy, sxy, sx2, sy2 (Pearson wie FA-30/31).
struct CiPairAcc {
    std::size_t n = 0;
    double sx = 0.0, sy = 0.0, sxy = 0.0, sx2 = 0.0, sy2 = 0.0;
};

/// Eine verknuepfte Zeile (A-Dokument x B-Dokument): numerische Werte mit Feldindex.
struct CiRow {
    std::vector<std::pair<std::size_t, Value>> vals;
};

/// Kernanalyse eines verknuepften Collection-Paares: FA-31-Maschinerie auf
/// der Join-Zeilenmenge, mit collection-qualifizierten Feldnamen.
CrossPairAnalysis analyzeJoinedPair(const std::string& origin, const std::string& relation,
                                    const Collection& collA, const std::string& joinFieldA,
                                    const Collection& collB, const std::string& keyFieldB,
                                    const std::vector<std::pair<DocumentId, DocumentId>>& pairs,
                                    const CrossInfluenceOptions& opts) {
    CrossPairAnalysis res;
    res.origin = origin;
    res.relation = relation;
    res.collectionA = collA.name();
    res.joinFieldA = joinFieldA;
    res.collectionB = collB.name();
    res.keyFieldB = keyFieldB;
    res.docsA = collA.size();
    res.rows = pairs.size();

    // Join-Statistik: Abdeckung in A, Kardinalitaet aus A-Sicht
    std::set<DocumentId> matched;
    std::map<DocumentId, std::set<DocumentId>> aPartners;
    for (const auto& p : pairs) {
        matched.insert(p.first);
        aPartners[p.first].insert(p.second);
    }
    res.matchedA = matched.size();
    res.joinCoverage =
        res.docsA > 0 ? static_cast<double>(res.matchedA) / static_cast<double>(res.docsA) : 0.0;
    res.cardinality = "n:1";
    for (const auto& kv : aPartners)
        if (kv.second.size() > 1) {
            res.cardinality = "1:n";
            break;
        }

    if (res.rows < opts.minJoinPairs) {
        res.note = "uebersprungen: nur " + std::to_string(res.rows) +
                   " verknuepfte Zeile(n), minJoinPairs " +
                   std::to_string(opts.minJoinPairs);
        return res;
    }
    if (opts.minJoinCoverage > 0.0 && res.joinCoverage < opts.minJoinCoverage) {
        res.note = "uebersprungen: Join-Abdeckung " + fmtNum(res.joinCoverage) +
                   " unter minJoinCoverage " + fmtNum(opts.minJoinCoverage);
        return res;
    }

    // Verknuepfte Zeilen einsammeln (Feldindex bei Erstsicht, qualifiziert "coll.feld").
    const std::string prefixA = res.collectionA + ".";
    const std::string prefixB = res.collectionB + ".";
    std::vector<CiRow> rows;
    std::vector<std::string> fieldNames; // collection-qualifiziert
    std::vector<char> fieldColl;         // 0 = Collection A, 1 = Collection B
    auto collectDoc = [&](const Document& d, char tag) {
        CiRow& row = rows.back();
        d.forEachField([&](const std::string& k, const Value& v) {
            if (!v.isNumber() || !std::isfinite(v.asDouble()))
                return;
            std::size_t idx = fieldNames.size();
            for (std::size_t i = 0; i < fieldNames.size(); ++i) {
                if (fieldColl[i] == tag && fieldNames[i] == (tag == 0 ? prefixA : prefixB) + k) {
                    idx = i;
                    break;
                }
            }
            if (idx == fieldNames.size()) {
                fieldNames.push_back((tag == 0 ? prefixA : prefixB) + k);
                fieldColl.push_back(tag);
            }
            row.vals.emplace_back(idx, v);
        });
    };
    for (const auto& p : pairs) {
        auto da = collA.findById(p.first);
        auto db = collB.findById(p.second);
        if (!da || !db)
            continue; // Kante zeigt auf (mittlerweile) geloeschtes Dokument
        rows.emplace_back();
        collectDoc(*da, 0);
        collectDoc(*db, 1);
    }

    // Triviale Join-Abhaengigkeit (Join- und Zielfeld sind per Konstruktion identisch):
    // bei Discovery-Joins aus Korrelation, FD und R2-Praediktoren unterdruecken.
    bool trivialSuppress = opts.suppressTrivialJoinDependency && origin == "discovered" &&
                           !joinFieldA.empty() && !keyFieldB.empty();
    std::size_t idxJoinA = fieldNames.size(), idxKeyB = fieldNames.size();
    if (trivialSuppress) {
        const std::string qa = prefixA + joinFieldA;
        const std::string qb = prefixB + keyFieldB;
        for (std::size_t i = 0; i < fieldNames.size(); ++i) {
            if (fieldNames[i] == qa)
                idxJoinA = i;
            if (fieldNames[i] == qb)
                idxKeyB = i;
        }
        if (idxJoinA == fieldNames.size() || idxKeyB == fieldNames.size())
            trivialSuppress = false; // Join-Feld nicht (numerisch) in der Menge
    }
    auto isTrivial = [&](std::size_t f1, std::size_t f2) {
        if (!trivialSuppress)
            return false;
        return (f1 == idxJoinA && f2 == idxKeyB) || (f1 == idxKeyB && f2 == idxJoinA);
    };

    const InfluenceOptions& inf = opts.influence;
    const std::size_t cap = std::min(fieldNames.size(), inf.numeric.maxFieldsPerCollection);
    std::vector<std::size_t> valueCount(cap, 0);
    for (const auto& row : rows)
        for (const auto& fv : row.vals)
            if (fv.first < cap)
                ++valueCount[fv.first];
    std::vector<std::size_t> fieldIdx;
    for (std::size_t i = 0; i < cap; ++i)
        if (valueCount[i] >= inf.numeric.minValuesPerField)
            fieldIdx.push_back(i);

    if (fieldIdx.empty()) {
        res.note = "keine numerischen Felder mit genug Werten auf der verknuepften Menge";
        return res;
    }

    // 1) Paarweise Pearson-Korrelationen (Felder mit >= 3 gemeinsamen Zeilen)
    std::map<std::pair<std::size_t, std::size_t>, CiPairAcc> pairAcc;
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
                CiPairAcc& pa = pairAcc[key];
                ++pa.n;
                const double xv = row.vals[a].second.asDouble();
                const double yv = row.vals[b].second.asDouble();
                pa.sx += xv;
                pa.sy += yv;
                pa.sxy += xv * yv;
                pa.sx2 += xv * xv;
                pa.sy2 += yv * yv;
            }
        }
    }
    std::map<std::pair<std::size_t, std::size_t>, double> pearson;
    for (const auto& kv : pairAcc) {
        const CiPairAcc& pa = kv.second;
        if (pa.n < 3)
            continue;
        const double cov = static_cast<double>(pa.n) * pa.sxy - pa.sx * pa.sy;
        const double varx = static_cast<double>(pa.n) * pa.sx2 - pa.sx * pa.sx;
        const double vary = static_cast<double>(pa.n) * pa.sy2 - pa.sy * pa.sy;
        if (varx <= 0.0 || vary <= 0.0)
            continue;
        const double r = cov / std::sqrt(varx * vary);
        if (std::isfinite(r))
            pearson[kv.first] = r;
    }

    // 2) Funktionale Abhaengigkeiten (beide Richtungen, Evidenz-Gruppen wie FA-31).
    //    Aufgenommen in `dependencies` werden nur Paare ueber Collection-Grenzen.
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
        // Evidenz sind nur Gruppen mit wiederholtem Quell-Wert (wie FA-31).
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
            return out;
        out.coverage =
            static_cast<double>(uniqueRepeatGroups) / static_cast<double>(repeatGroups);
        return out;
    };
    std::vector<CrossDependency> allDeps; // alle FDs (auch innerhalb einer Collection)
    for (std::size_t ii = 0; ii < fieldIdx.size(); ++ii) {
        for (std::size_t jj = ii + 1; jj < fieldIdx.size(); ++jj) {
            const std::size_t f1 = fieldIdx[ii];
            const std::size_t f2 = fieldIdx[jj];
            if (isTrivial(f1, f2))
                continue;
            std::pair<std::size_t, std::size_t> key(f1, f2);
            if (key.first > key.second)
                std::swap(key.first, key.second);
            if (pairAcc.count(key) == 0 || pairAcc.at(key).n < inf.minDependencyPairs)
                continue;
            const FdCandidate c12 = checkDirection(f1, f2);
            const FdCandidate c21 = checkDirection(f2, f1);
            const bool fwd = c12.coverage >= inf.dependencyRatio;
            const bool bwd = c21.coverage >= inf.dependencyRatio;
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
            CrossDependency dep;
            dep.source = fieldNames[useFwd ? f1 : f2];
            dep.target = fieldNames[useFwd ? f2 : f1];
            dep.coverage = cand.coverage;
            dep.groups = cand.groups;
            dep.pairs = cand.pairs;
            allDeps.push_back(dep);
            if (fieldColl[useFwd ? f1 : f2] != fieldColl[useFwd ? f2 : f1])
                res.dependencies.push_back(dep);
        }
    }
    auto depLess = [](const CrossDependency& x, const CrossDependency& y) {
        if (x.source != y.source)
            return x.source < y.source;
        return x.target < y.target;
    };
    std::sort(res.dependencies.begin(), res.dependencies.end(), depLess);
    std::sort(allDeps.begin(), allDeps.end(), depLess);

    // 3) Multiples R^2 je Feld gegen die uebrigen (Complete Cases, Ridge-Stabilisierung).
    auto computeRSquared = [&](std::size_t y, std::vector<std::size_t> preds, double& r2Out,
                               std::size_t& rowsOut) {
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
                    if (fv.first == preds[p]) {
                        xs[p] = fv.second.asDouble();
                        ++found;
                    }
            }
            if (!hasY || found != preds.size())
                continue;
            X.push_back(std::move(xs));
            yv.push_back(yval);
        }
        rowsOut = yv.size();
        const std::size_t k = preds.size();
        if (yv.size() < std::max(inf.minRegressionRows, k + 2))
            return false;
        double mean = 0.0;
        for (double v : yv)
            mean += v;
        mean /= static_cast<double>(yv.size());
        double sst = 0.0;
        for (double v : yv)
            sst += (v - mean) * (v - mean);
        if (!(sst > 0.0))
            return false;
        const std::size_t m = k + 1;
        std::vector<std::vector<double>> A(m, std::vector<double>(m, 0.0));
        std::vector<double> b(m, 0.0);
        for (std::size_t r = 0; r < X.size(); ++r) {
            A[0][0] += 1.0;
            for (std::size_t i = 0; i < k; ++i) {
                A[0][i + 1] += X[r][i];
                A[i + 1][0] += X[r][i];
                b[i + 1] += X[r][i] * yv[r];
                for (std::size_t j = 0; j < k; ++j)
                    A[i + 1][j + 1] += X[r][i] * X[r][j];
            }
            b[0] += yv[r];
        }
        double diag = 0.0;
        for (std::size_t i = 0; i < m; ++i)
            diag += std::fabs(A[i][i]);
        const double lambda = 1e-9 * std::max(1.0, diag / static_cast<double>(m));
        for (std::size_t i = 0; i < m; ++i)
            A[i][i] += lambda;
        if (!ciSolveLinearSystem(A, b))
            return false;
        double sse = 0.0;
        for (std::size_t r = 0; r < X.size(); ++r) {
            double pred = b[0];
            for (std::size_t i = 0; i < k; ++i)
                pred += b[i + 1] * X[r][i];
            const double e = yv[r] - pred;
            sse += e * e;
        }
        r2Out = 1.0 - sse / sst;
        if (r2Out < 0.0)
            r2Out = 0.0;
        if (r2Out > 1.0)
            r2Out = 1.0;
        return true;
    };

    // 4) Einfluss-Profile je Feld (Rollen/Score wie FA-31)
    std::map<std::size_t, std::size_t> determines, determinedBy;
    for (const auto& fd : allDeps) {
        for (std::size_t i = 0; i < fieldNames.size(); ++i) {
            if (fieldNames[i] == fd.source)
                ++determines[i];
            if (fieldNames[i] == fd.target)
                ++determinedBy[i];
        }
    }
    for (std::size_t fi : fieldIdx) {
        FieldInfluence inf1;
        inf1.field = fieldNames[fi];
        for (std::size_t fj : fieldIdx) {
            if (fj == fi || isTrivial(fi, fj))
                continue;
            std::pair<std::size_t, std::size_t> key(fi, fj);
            if (key.first > key.second)
                std::swap(key.first, key.second);
            const auto it = pearson.find(key);
            if (it == pearson.end())
                continue;
            ++inf1.partners;
            const double ar = std::fabs(it->second);
            inf1.hubScore += ar;
            if (ar >= inf.strongCorrelation)
                ++inf1.strongCorrelations;
            if (ar > inf1.maxAbsPearson) {
                inf1.maxAbsPearson = ar;
                inf1.strongestPartner = fieldNames[fj];
            }
        }
        inf1.hubRatio =
            inf1.partners > 0 ? inf1.hubScore / static_cast<double>(inf1.partners) : 0.0;
        inf1.determines = determines.count(fi) ? determines[fi] : 0;
        inf1.determinedBy = determinedBy.count(fi) ? determinedBy[fi] : 0;

        {
            std::vector<std::pair<double, std::size_t>> byR;
            for (std::size_t fj : fieldIdx) {
                if (fj == fi || isTrivial(fi, fj))
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
            for (std::size_t i = 0; i < byR.size() && i < inf.maxPredictors; ++i)
                preds.push_back(byR[i].second);
            if (!preds.empty())
                inf1.rSquaredComputed =
                    computeRSquared(fi, preds, inf1.rSquared, inf1.regressionRows);
        }

        const bool isDriver = inf1.determines > 0 ||
                              (inf1.strongCorrelations >= 2 && inf1.rSquaredComputed &&
                               inf1.rSquared < inf.rSquaredHigh);
        const bool isDependent =
            inf1.determinedBy > 0 || (inf1.rSquaredComputed && inf1.rSquared >= inf.rSquaredHigh);
        inf1.role = isDriver && isDependent ? FieldRole::Mediator
                   : isDriver               ? FieldRole::Driver
                   : isDependent            ? FieldRole::Dependent
                                            : FieldRole::Independent;
        const double fdRatio =
            inf1.partners > 0
                ? static_cast<double>(inf1.determines) / static_cast<double>(inf1.partners)
                : 0.0;
        const double r2Part = inf1.rSquaredComputed ? (1.0 - inf1.rSquared) : 0.5;
        inf1.influenceScore = 0.5 * inf1.hubRatio + 0.3 * fdRatio + 0.2 * r2Part;

        if (inf1.determines > 0) {
            inf1.rationale += "bestimmt funktional " + std::to_string(inf1.determines) +
                              " Feld(er)";
            for (const auto& fd : allDeps)
                if (fd.source == inf1.field)
                    inf1.rationale += " (-> " + fd.target + ")";
            inf1.rationale += "; ";
        }
        if (inf1.strongCorrelations > 0)
            inf1.rationale += std::to_string(inf1.strongCorrelations) +
                              " starke Korrelation(en), staerkster |r| " +
                              fmtNum(inf1.maxAbsPearson) + " mit " + inf1.strongestPartner + "; ";
        if (inf1.rSquaredComputed)
            inf1.rationale += "R2 " + fmtNum(inf1.rSquared) +
                              (inf1.rSquared >= inf.rSquaredHigh
                                   ? " (von uebrigen erklaert)"
                                   : " (nicht vollstaendig erklaert)") +
                              "; ";
        else
            inf1.rationale += "R2 nicht bestimmbar; ";
        if (inf1.determinedBy > 0) {
            inf1.rationale += "funktional abhaengig von " + std::to_string(inf1.determinedBy) +
                              " Feld(er)";
            for (const auto& fd : allDeps)
                if (fd.target == inf1.field)
                    inf1.rationale += " (" + fd.source + " -> " + inf1.field + ")";
            inf1.rationale += "; ";
        }
        while (!inf1.rationale.empty() &&
               (inf1.rationale.back() == ' ' || inf1.rationale.back() == ';'))
            inf1.rationale.pop_back();
        res.fields.push_back(std::move(inf1));
    }
    std::sort(res.fields.begin(), res.fields.end(),
              [](const FieldInfluence& x, const FieldInfluence& y) {
                  if (x.influenceScore != y.influenceScore)
                      return x.influenceScore > y.influenceScore;
                  return x.field < y.field;
              });
    if (!res.fields.empty()) {
        const FieldInfluence& top = res.fields.front();
        const bool dominanceSignal = top.determines > 0 || top.strongCorrelations > 0 ||
                                     top.hubRatio >= inf.strongCorrelation;
        if (dominanceSignal)
            res.topDriver = top.field;
    }

    // Kurzfassung (nur Abhaengigkeiten ueber Collection-Grenzen)
    if (!res.dependencies.empty()) {
        res.note += "Cross-Abhaengigkeiten: ";
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
        res.note =
            "keine starken Zusammenhaenge zwischen den numerischen Feldern der verknuepften Menge";
    while (!res.note.empty() && (res.note.back() == ' ' || res.note.back() == ';'))
        res.note.pop_back();
    return res;
}

} // namespace

CrossInfluenceResult analyzeCrossInfluence(Database& db, const CrossInfluenceOptions& opts) {
    CrossInfluenceResult res;
    std::vector<CrossPairAnalysis>& out = res.pairs;
    std::vector<std::string> skipped;

    // 1) Deklarierte FA-18-Referenzen: Dokument-Kanten aus __references (read-only).
    //    Collection-Gesamt-Referenzen (id = null) sind keine Joins und werden uebersprungen.
    if (opts.includeDeclared && db.hasCollection("__references")) {
        const Collection& refs = db.collection("__references");
        // (fromColl, toColl) -> relation -> deduplizierte (aId, bId)-Paare
        std::map<std::pair<std::string, std::string>,
                 std::map<std::string, std::set<std::pair<DocumentId, DocumentId>>>>
            groups;
        refs.forEach(Expression{}, [&](const Document& d) {
            const Value* fc = d.find("from_coll");
            const Value* fi = d.find("from_id");
            const Value* tc = d.find("to_coll");
            const Value* ti = d.find("to_id");
            const Value* rel = d.find("relation");
            if (!fc || !fc->isString() || !fi || !fi->isNumber() || !tc || !tc->isString() ||
                !ti || !ti->isNumber())
                return;
            const DocumentId aid(static_cast<std::uint64_t>(fi->asInt()));
            const DocumentId bid(static_cast<std::uint64_t>(ti->asInt()));
            groups[std::make_pair(fc->asString(), tc->asString())]
                  [rel && rel->isString() ? rel->asString() : std::string()]
                      .insert(std::make_pair(aid, bid));
        });
        for (const auto& gk : groups) {
            const std::string& from = gk.first.first;
            const std::string& to = gk.first.second;
            for (const auto& rk : gk.second) {
                const std::string relation =
                    rk.first.empty() ? "(unbenannt)" : rk.first;
                if (from == to) {
                    skipped.push_back(from + " <-> " + to + " (Selbst-Referenz, FA-31)");
                    continue;
                }
                if (!db.hasCollection(from) || !db.hasCollection(to)) {
                    skipped.push_back(from + " -> " + to + " (Collection fehlt)");
                    continue;
                }
                std::vector<std::pair<DocumentId, DocumentId>> pairs(rk.second.begin(),
                                                                    rk.second.end());
                out.push_back(analyzeJoinedPair("declared", relation, db.collection(from), "",
                                               db.collection(to), "", pairs, opts));
            }
        }
    }

    // 2) Entdeckte Fremdschluessel (FA-27) als Join-Quelle: A.field -> B.toField per
    //    Wert-Index; Selbst-Joins uebersprungen (FA-31-Territorium).
    if (opts.includeDiscovered) {
        std::vector<Collection> colls;
        for (const auto& name : db.collectionNames())
            if (name != "__references")
                colls.push_back(db.collection(name));
        if (!colls.empty()) {
            for (const auto& rel : discoverForeignKeys(colls)) {
                if (rel.kind != RelationKind::ForeignKey)
                    continue;
                if (rel.confidence < opts.minFkConfidence)
                    continue;
                if (rel.fromCollection == rel.toCollection)
                    continue;
                const Collection& collA = db.collection(rel.fromCollection);
                const Collection& collB = db.collection(rel.toCollection);
                std::map<Value, std::vector<DocumentId>, ValueLess> index;
                collB.forEach(Expression{}, [&](const Document& d) {
                    const Value* v = d.find(rel.toField);
                    if (v && !v->isNull())
                        index[*v].push_back(d.id());
                });
                std::vector<std::pair<DocumentId, DocumentId>> pairs;
                collA.forEach(Expression{}, [&](const Document& d) {
                    const Value* v = d.find(rel.field);
                    if (!v || v->isNull())
                        return;
                    const auto it = index.find(*v);
                    if (it == index.end())
                        return;
                    for (DocumentId bid : it->second)
                        pairs.emplace_back(d.id(), bid);
                });
                const std::string desc = "FK " + rel.fromCollection + "." + rel.field +
                                         " -> " + rel.toCollection + "." + rel.toField +
                                         " (confidence " + fmtNum(rel.confidence) + ")";
                out.push_back(analyzeJoinedPair("discovered", desc, collA, rel.field, collB,
                                               rel.toField, pairs, opts));
            }
        }
    }

    // Deterministische Reihenfolge: A-Collection, Ursprung (deklariert vor entdeckt),
    // B-Collection, Join-Feld.
    std::sort(out.begin(), out.end(), [](const CrossPairAnalysis& x, const CrossPairAnalysis& y) {
        if (x.collectionA != y.collectionA)
            return x.collectionA < y.collectionA;
        if (x.origin != y.origin)
            return x.origin < y.origin;
        if (x.collectionB != y.collectionB)
            return x.collectionB < y.collectionB;
        return x.joinFieldA < y.joinFieldA;
    });

    std::size_t declaredCount = 0, discoveredCount = 0;
    for (const auto& p : out) {
        if (p.origin == "declared")
            ++declaredCount;
        else
            ++discoveredCount;
    }
    std::string note;
    if (out.empty()) {
        note = skipped.empty() ? "keine verknuepfbaren Collection-Paare gefunden"
                                : "keine auswertbaren Paare";
    } else {
        note = std::to_string(out.size()) + " Paar-Analyse(n): " +
               std::to_string(declaredCount) + " deklariert, " +
               std::to_string(discoveredCount) + " entdeckt";
    }
    if (!skipped.empty()) {
        note += "; uebersprungen: ";
        for (std::size_t i = 0; i < skipped.size(); ++i) {
            if (i > 0)
                note += ", ";
            note += skipped[i];
        }
    }
    res.note = note;
    return res;
}

std::string toJson(const CrossPairAnalysis& a) {
    std::string out = "{\"origin\":" + detail::writeJsonString(a.origin) +
                      ",\"relation\":" + detail::writeJsonString(a.relation) +
                      ",\"collectionA\":" + detail::writeJsonString(a.collectionA) +
                      ",\"joinFieldA\":" + detail::writeJsonString(a.joinFieldA) +
                      ",\"collectionB\":" + detail::writeJsonString(a.collectionB) +
                      ",\"keyFieldB\":" + detail::writeJsonString(a.keyFieldB) +
                      ",\"docsA\":" + std::to_string(a.docsA) +
                      ",\"matchedA\":" + std::to_string(a.matchedA) +
                      ",\"rows\":" + std::to_string(a.rows) +
                      ",\"joinCoverage\":" + fmtNum(a.joinCoverage) +
                      ",\"cardinality\":" + detail::writeJsonString(a.cardinality) +
                      ",\"topDriver\":" + detail::writeJsonString(a.topDriver) +
                      ",\"note\":" + detail::writeJsonString(a.note) + ",\"dependencies\":[";
    bool first = true;
    for (const auto& fd : a.dependencies) {
        if (!first)
            out += ',';
        first = false;
        out += "{\"source\":" + detail::writeJsonString(fd.source) +
               ",\"target\":" + detail::writeJsonString(fd.target) +
               ",\"coverage\":" + fmtNum(fd.coverage) +
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
               ",\"hubScore\":" + fmtNum(f.hubScore) +
               ",\"hubRatio\":" + fmtNum(f.hubRatio) +
               ",\"maxAbsPearson\":" + fmtNum(f.maxAbsPearson) +
               ",\"strongestPartner\":" + detail::writeJsonString(f.strongestPartner) +
               ",\"rSquaredComputed\":" + (f.rSquaredComputed ? "true" : "false") +
               ",\"rSquared\":" + fmtNum(f.rSquared) +
               ",\"regressionRows\":" + std::to_string(f.regressionRows) +
               ",\"determines\":" + std::to_string(f.determines) +
               ",\"determinedBy\":" + std::to_string(f.determinedBy) +
               ",\"influenceScore\":" + fmtNum(f.influenceScore) +
               ",\"role\":" + detail::writeJsonString(toString(f.role)) +
               ",\"rationale\":" + detail::writeJsonString(f.rationale) + "}";
    }
    out += "]}";
    return out;
}

std::string toJson(const CrossInfluenceResult& r) {
    std::string out = "{\"note\":" + detail::writeJsonString(r.note) + ",\"pairs\":[";
    bool first = true;
    for (const auto& p : r.pairs) {
        if (!first)
            out += ',';
        first = false;
        out += toJson(p);
    }
    out += "]}";
    return out;
}

std::string toMarkdown(const CrossPairAnalysis& a) {
    std::string out = "# Cross-Einfluss: " + a.collectionA + " -> " + a.collectionB + " (FA-32)\n\n";
    out += "- Ursprung: " + a.origin + " (" + a.relation + ")\n";
    out += "- Join: " + a.collectionA + (a.joinFieldA.empty() ? " (Dokument-Kante)"
                                                            : "." + a.joinFieldA) +
           " -> " + a.collectionB + (a.keyFieldB.empty() ? " (Dokument-Kante)"
                                                         : "." + a.keyFieldB) +
           ", Kardinalitaet (A-Sicht): " + a.cardinality + "\n";
    out += "- Zeilen: " + std::to_string(a.rows) + " (von " + std::to_string(a.docsA) +
           " Dokumenten in " + a.collectionA + " sind " + std::to_string(a.matchedA) +
           " verknuepft, Abdeckung " + fmtNum(a.joinCoverage) + ")\n";
    out += "- " + a.note + "\n";
    if (!a.dependencies.empty()) {
        out += "\n## Cross-Abhaengigkeiten\n\n";
        out += "| Quelle | Ziel | Coverage | Gruppen | Paare |\n|---|---|---|---|---|\n";
        for (const auto& fd : a.dependencies) {
            out += "| " + fd.source + " | " + fd.target + " | " + fmtNum(fd.coverage) + " | " +
                   std::to_string(fd.groups) + " | " + std::to_string(fd.pairs) + " |\n";
        }
    }
    if (!a.fields.empty()) {
        out += "\n## Feld-Rollen auf der verknuepften Menge (nach Einfluss-Score)\n\n";
        out += "| Feld | Rolle | Score | starke r | Hub | R2 | bestimmt | bestimmt von | Begruendung |\n";
        out += "|---|---|---|---|---|---|---|---|---|\n";
        for (const auto& f : a.fields) {
            out += "| " + f.field + " | " + toString(f.role) + " | " + fmtNum(f.influenceScore) +
                   " | " + std::to_string(f.strongCorrelations) + " | " + fmtNum(f.hubRatio) +
                   " | " + (f.rSquaredComputed ? fmtNum(f.rSquared) : "-") + " | " +
                   std::to_string(f.determines) + " | " + std::to_string(f.determinedBy) +
                   " | " + (f.rationale.empty() ? "-" : f.rationale) + " |\n";
        }
    }
    return out;
}

std::string toMarkdown(const CrossInfluenceResult& r) {
    std::string out = "# Cross-Collection-Einfluss-Analyse (FA-32)\n\n";
    out += "- " + r.note + "\n";
    out += "\n| A | Ursprung | B | Zeilen | Abdeckung | Kardinalitaet | Top-Treiber | Kurzfassung |\n";
    out += "|---|---|---|---|---|---|---|---|\n";
    for (const auto& p : r.pairs) {
        out += "| " + p.collectionA + " | " + p.origin + " | " + p.collectionB + " | " +
               std::to_string(p.rows) + " | " + fmtNum(p.joinCoverage) + " | " + p.cardinality +
               " | " + (p.topDriver.empty() ? "-" : p.topDriver) + " | " + p.note + " |\n";
    }
    return out;
}

} // namespace memdb
