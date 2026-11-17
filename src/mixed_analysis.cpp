#include "memdb/mixed_analysis.hpp"

#include "json.hpp"
#include "memdb/document_id.hpp"

#include <algorithm>
#include <cctype>
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

/// Ganzzahl-Anteil in Prozent als Klartext (für Notes), z. B. 0.256 -> "26 %".
std::string pct(double ratio) {
    if (!std::isfinite(ratio) || ratio < 0.0)
        return "0 %";
    return std::to_string(static_cast<std::size_t>(ratio * 100.0 + 0.5)) + " %";
}

/// Quantil per linearer Interpolation, xs muss sortiert sein.
double quantile(const std::vector<double>& xs, double p) {
    if (xs.empty())
        return 0.0;
    const double pos = p * static_cast<double>(xs.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(pos);
    const double frac = pos - static_cast<double>(lo);
    if (lo + 1 >= xs.size())
        return xs.back();
    // Härtung: Interpolation in long double – die Differenz xs[lo+1] - xs[lo]
    // läuft bei gegenläufigen Extremwerten (±1e308) in double nach unendlich
    // über und machte den Median unendlich (ungültiges JSON); in long double
    // bleibt sie endlich, das Ergebnis liegt per Konstruktion zwischen
    // xs[lo] und xs[lo+1]. Absicherung beim Zurückschreiben für alle Fälle.
    const long double v = static_cast<long double>(xs[lo]) +
                          frac * (static_cast<long double>(xs[lo + 1]) -
                                 static_cast<long double>(xs[lo]));
    const double m = static_cast<double>(v);
    return std::isfinite(m) ? m : xs[lo];
}

bool isIntegral(double v) {
    return std::isfinite(v) && std::floor(v) == v;
}

bool allDigits(const std::string& s, std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to; ++i)
        if (s[i] < '0' || s[i] > '9')
            return false;
    return true;
}

/// ISO-Datum-artig: JJJJ-MM-TT (10 Zeichen, Ziffern mit Trennstrichen bei 4 und 7).
bool looksLikeDate(const std::string& s) {
    return s.size() == 10 && s[4] == '-' && s[7] == '-' && allDigits(s, 0, 4) &&
           allDigits(s, 5, 7) && allDigits(s, 8, 10);
}

/// Hex-String: nicht leer, nur Hex-Ziffern.
bool looksLikeHex(const std::string& s) {
    if (s.empty())
        return false;
    for (char ch : s)
        if (!std::isxdigit(static_cast<unsigned char>(ch)))
            return false;
    return true;
}

/// Zahl-String: optionales Vorzeichen, Ziffern, hoechstens ein Punkt mit Nachkommastellen.
bool looksLikeNumber(const std::string& s) {
    std::size_t i = 0;
    if (!s.empty() && (s[0] == '+' || s[0] == '-'))
        i = 1;
    bool digits = false;
    bool seenDot = false;
    for (; i < s.size(); ++i) {
        if (s[i] >= '0' && s[i] <= '9') {
            digits = true;
        } else if (s[i] == '.' && !seenDot && i + 1 < s.size()) {
            seenDot = true;
        } else {
            return false;
        }
    }
    return digits;
}

const char* kindName(MixedKind k) {
    switch (k) {
    case MixedKind::Number:
        return "number";
    case MixedKind::Text:
        return "text";
    case MixedKind::Boolean:
        return "boolean";
    }
    return "text";
}

const char* textClassName(TextClass c) {
    switch (c) {
    case TextClass::None:
        return "none";
    case TextClass::Categorical:
        return "categorical";
    case TextClass::Identifier:
        return "identifier";
    case TextClass::FreeText:
        return "freeText";
    }
    return "none";
}

} // namespace

std::vector<MixedFieldAnalysis> analyzeMixedFields(const Collection& c,
                                                    const MixedAnalysisOptions& opts) {
    MixedCollectionAnalysis full = analyzeMixed(c, opts);
    return std::move(full.fields);
}

MixedCollectionAnalysis analyzeMixed(const Collection& c, const MixedAnalysisOptions& opts) {
    MixedCollectionAnalysis res;
    res.collection = c.name();
    res.docCount = c.size();
    const std::size_t n = res.docCount;
    res.sampleSize = (opts.sampleSize > 0 && opts.sampleSize < n) ? opts.sampleSize : n;

    // Ein Scan: pro Dokument alle analysierbaren Werte (Zahl/String/Boolean,
    // endliche Zahlen); Feldindex bei Erstsicht vergeben. Andere Typen
    // (null/Binary/Dokument/Array) zaehlen als fehlend und senken dominantShare.
    const bool sampled = opts.sampleSize > 0 && opts.sampleSize < n;
    struct DocRow {
        DocumentId id;
        std::vector<std::pair<std::size_t, Value>> vals;
    };
    std::vector<DocRow> rows;
    std::vector<std::string> fieldNames2; // Feldindex -> Name (Erstsicht-Reihenfolge)
    std::set<std::string> allFields;

    auto collect = [&](const Document& d) {
        rows.emplace_back();
        DocRow& row = rows.back();
        row.id = d.id();
        d.forEachField([&](const std::string& k, const Value& v) {
            allFields.insert(k);
            if (v.isNull() || !(v.isNumber() || v.isString() || v.isBool()))
                return; // null oder nicht-analysierbarer Typ
            if (v.isNumber() && !std::isfinite(v.asDouble()))
                return; // Härtung: inf/nan nicht in die Statistik lassen
            std::size_t idx = fieldNames2.size();
            for (std::size_t i = 0; i < fieldNames2.size(); ++i) {
                if (fieldNames2[i] == k) {
                    idx = i;
                    break;
                }
            }
            if (idx == fieldNames2.size())
                fieldNames2.push_back(k);
            row.vals.emplace_back(idx, v);
        });
    };
    if (sampled) {
        for (auto id : c.sample(opts.sampleSize, opts.seed))
            if (auto d = c.findById(id))
                collect(*d);
    } else {
        c.forEach(Expression{}, collect);
    }
    // Einfügereihenfolge rekonstruieren (IDs sequenziell): stabile
    // Erstsicht-Reihenfolge der Felder unabhaengig von der Map-Iteration.
    std::sort(rows.begin(), rows.end(),
              [](const DocRow& a, const DocRow& b) { return a.id < b.id; });

    res.fieldCount = allFields.size();
    const std::size_t cap = std::min(fieldNames2.size(), opts.maxFieldsPerCollection);

    // Werte je Feld (in Dokumentreihenfolge) fuer Feld- und Paaranalysen.
    std::vector<std::vector<Value>> fieldValues(fieldNames2.size());
    for (const auto& row : rows)
        for (const auto& fv : row.vals)
            fieldValues[fv.first].push_back(fv.second);

    // Feld-Analysen
    for (std::size_t idx = 0; idx < cap; ++idx) {
        const auto& vals = fieldValues[idx];
        if (vals.empty() || vals.size() < opts.minValuesPerField) // Härtung: nie ohne Werte
            continue;

        // dominanter Typ (Zahl > Text > Boolean bei Gleichstand, deterministisch)
        std::size_t nNum = 0, nStr = 0, nBool = 0;
        for (const auto& v : vals) {
            if (v.isNumber())
                ++nNum;
            else if (v.isString())
                ++nStr;
            else if (v.isBool())
                ++nBool;
        }
        MixedKind kind = MixedKind::Number;
        std::size_t count = nNum;
        if (nStr > count) {
            kind = MixedKind::Text;
            count = nStr;
        }
        if (nBool > count) {
            kind = MixedKind::Boolean;
            count = nBool;
        }
        if (count == 0)
            continue;

        MixedFieldAnalysis a;
        a.collection = res.collection;
        a.field = fieldNames2[idx];
        a.kind = kind;
        a.count = count;
        a.missing = res.sampleSize - vals.size();
        a.missingRatio =
            res.sampleSize > 0
                ? static_cast<double>(a.missing) / static_cast<double>(res.sampleSize)
                : 0.0;
        a.dominantShare = static_cast<double>(count) / static_cast<double>(vals.size());

        // distinct ueber die Werte des dominanten Typs
        {
            std::set<Value, ValueLess> distinctSet;
            for (const auto& v : vals) {
                if ((kind == MixedKind::Number && v.isNumber()) ||
                    (kind == MixedKind::Text && v.isString()) ||
                    (kind == MixedKind::Boolean && v.isBool()))
                    distinctSet.insert(v);
            }
            a.distinct = distinctSet.size();
        }

        if (kind == MixedKind::Number) {
            std::vector<double> xs;
            xs.reserve(count);
            for (const auto& v : vals)
                if (v.isNumber())
                    xs.push_back(v.asDouble());
            std::sort(xs.begin(), xs.end());
            a.min = xs.front();
            a.max = xs.back();
            const double med = quantile(xs, 0.5);
            a.median = std::isfinite(med) ? med : 0.0; // Härtung: Finite-Check beim Zurückschreiben
            // Härtung: Momente in long double akkumulieren (kein Ueberlauf bei
            // Extremwerten), beim Zurueckschreiben Endlichkeit erzwingen (gueltiges JSON).
            long double lmean = 0.0L;
            for (double x : xs)
                lmean += static_cast<long double>(x);
            lmean /= static_cast<long double>(xs.size());
            long double lm2 = 0.0L;
            for (double x : xs) {
                const long double d = static_cast<long double>(x) - lmean;
                lm2 += d * d;
            }
            lm2 /= static_cast<long double>(xs.size());
            const double mean = static_cast<double>(lmean);
            a.mean = std::isfinite(mean) ? mean : 0.0;
            const double m2d = static_cast<double>(lm2);
            a.stddev = std::isfinite(m2d) ? std::sqrt(m2d) : 0.0;
            std::size_t integral = 0;
            for (double x : xs)
                if (isIntegral(x))
                    ++integral;
            a.integralRatio = static_cast<double>(integral) / static_cast<double>(xs.size());
            a.discrete = (integral == xs.size()) && (a.max - a.min) <= opts.discreteMaxRange;

            if (a.distinct == 1)
                a.note += "konstantes Feld; ";
            if (a.discrete)
                a.note += "diskret (ganzzahlig, Spannweite <= " + num(opts.discreteMaxRange) +
                          "); ";
            else if (a.integralRatio < 1.0)
                a.note += "Dezimalanteil " + pct(1.0 - a.integralRatio) + "; ";
        } else if (kind == MixedKind::Text) {
            std::vector<const std::string*> strs;
            strs.reserve(count);
            for (const auto& v : vals)
                if (v.isString())
                    strs.push_back(&v.asString());
            std::size_t empty = 0;
            long double lenSum = 0.0L;
            a.lenMin = strs.empty() ? 0 : strs.front()->size();
            a.lenMax = 0;
            bool date = true, hex = true, number = true, any = false;
            for (const std::string* s : strs) {
                const std::size_t len = s->size();
                lenSum += static_cast<long double>(len);
                a.lenMin = std::min(a.lenMin, len);
                a.lenMax = std::max(a.lenMax, len);
                if (len == 0) {
                    ++empty;
                    date = hex = number = false;
                    continue;
                }
                any = true;
                if (!looksLikeDate(*s))
                    date = false;
                if (!looksLikeHex(*s))
                    hex = false;
                if (!looksLikeNumber(*s))
                    number = false;
            }
            const double lm = static_cast<double>(lenSum / static_cast<long double>(count));
            a.lenMean = std::isfinite(lm) ? lm : 0.0;
            a.emptyShare = static_cast<double>(empty) / static_cast<double>(count);
            a.uniqueness = static_cast<double>(a.distinct) / static_cast<double>(count);
            a.dateLike = any && date;
            a.hexLike = any && hex;
            a.numericLike = any && number;

            // semantische Klasse (deterministische Reihenfolge)
            if (a.uniqueness >= opts.identifierUniqueness)
                a.textClass = TextClass::Identifier;
            else if (a.distinct <= opts.maxCategoryDistinct)
                a.textClass = TextClass::Categorical;
            else if (a.lenMean >= static_cast<double>(opts.freeTextMinLength))
                a.textClass = TextClass::FreeText;

            if (a.distinct == 1)
                a.note += "konstantes Feld; ";
            switch (a.textClass) {
            case TextClass::Categorical:
                a.note += "kategorisch (" + std::to_string(a.distinct) + " Auspraegungen); ";
                break;
            case TextClass::Identifier:
                a.note += "Kennung (" + pct(a.uniqueness) + " einzigartig); ";
                break;
            case TextClass::FreeText:
                a.note += "Freitext (mittlere Laenge " +
                          std::to_string(static_cast<std::size_t>(a.lenMean + 0.5)) + "); ";
                break;
            case TextClass::None:
                break;
            }
            if (a.dateLike)
                a.note += "ISO-Datum-artig; ";
            if (a.hexLike)
                a.note += "hex-artig; ";
            if (a.numericLike)
                a.note += "zahlartige Strings; ";
            if (a.emptyShare > 0.0)
                a.note += "leere Strings (" + pct(a.emptyShare) + "); ";
        } else { // MixedKind::Boolean
            std::size_t trues = 0;
            for (const auto& v : vals)
                if (v.isBool() && v.asBool())
                    ++trues;
            a.trueShare = static_cast<double>(trues) / static_cast<double>(count);
            a.note += "Boolean (" + pct(a.trueShare) + " true); ";
            if (a.distinct == 1)
                a.note += "konstantes Feld; ";
        }

        if (a.missing > 0)
            a.note += "fehlt in " + pct(a.missingRatio) + " der Dokumente; ";
        if (a.dominantShare < 1.0)
            a.note += "Typ-Drift (" + pct(1.0 - a.dominantShare) + " abweichend); ";
        while (!a.note.empty() && (a.note.back() == ' ' || a.note.back() == ';'))
            a.note.pop_back();
        res.fields.push_back(std::move(a));
    }
    res.mixedFieldCount = res.fields.size();
    std::sort(res.fields.begin(), res.fields.end(),
              [](const MixedFieldAnalysis& x, const MixedFieldAnalysis& y) {
                  return x.field < y.field;
              });

    // Paarweise Assoziationen ueber Dokumente mit beiden Feldern.
    // Mass je Typ-Kombination: Pearson (Zahl/Boolean), eta^2 (kategorischer Text
    // gegen Zahl/Boolean), Cramers V (zwei kategorische Texte).
    if (opts.withAssociations && res.fields.size() >= 2) {
        // Feldindex -> Analyse (fuer Kind/TextClass-Lookup)
        std::map<std::string, std::size_t> analyzedIdx;
        for (std::size_t i = 0; i < res.fields.size(); ++i)
            analyzedIdx[res.fields[i].field] = i;

        // je Dokument: Feldname -> dominanter Wert (als double bzw. String)
        struct RowExtract {
            std::map<std::string, double> nums;   // Zahl/Boolean (0/1)
            std::map<std::string, const std::string*> strs; // dominante Strings
        };
        std::vector<RowExtract> ext(rows.size());
        for (std::size_t r = 0; r < rows.size(); ++r) {
            for (const auto& fv : rows[r].vals) {
                const std::string& fname = fieldNames2[fv.first];
                const auto it = analyzedIdx.find(fname);
                if (it == analyzedIdx.end())
                    continue; // Feld wurde nicht analysiert (Cap/minValues)
                const MixedFieldAnalysis& fa = res.fields[it->second];
                if (fa.kind == MixedKind::Number && fv.second.isNumber())
                    ext[r].nums[fname] = fv.second.asDouble();
                else if (fa.kind == MixedKind::Boolean && fv.second.isBool())
                    ext[r].nums[fname] = fv.second.asBool() ? 1.0 : 0.0;
                else if (fa.kind == MixedKind::Text && fv.second.isString())
                    ext[r].strs[fname] = &fv.second.asString();
            }
        }

        for (std::size_t i = 0; i + 1 < res.fields.size(); ++i) {
            for (std::size_t j = i + 1; j < res.fields.size(); ++j) {
                const MixedFieldAnalysis& fa = res.fields[i];
                const MixedFieldAnalysis& fb = res.fields[j];
                const bool faCat =
                    fa.kind == MixedKind::Text && fa.textClass == TextClass::Categorical;
                const bool fbCat =
                    fb.kind == MixedKind::Text && fb.textClass == TextClass::Categorical;
                const bool faNumish = fa.kind == MixedKind::Number || fa.kind == MixedKind::Boolean;
                const bool fbNumish = fb.kind == MixedKind::Number || fb.kind == MixedKind::Boolean;
                std::string measure;
                if (faNumish && fbNumish)
                    measure = "pearson";
                else if ((faCat && fbNumish) || (fbCat && faNumish))
                    measure = "etaSquared";
                else if (faCat && fbCat)
                    measure = "cramersV";
                else
                    continue; // Kennung/Freitext/None: kein aussagekraeftiges Mass

                const std::string& nameA = fa.field;
                const std::string& nameB = fb.field;

                if (measure == "pearson") {
                    // Härtung: zweistufig zentriert in long double – die Summenformel
                    // (n*Sxy - Sx*Sy) verliert bei grossen Werten massiv an Genauigkeit.
                    std::size_t pairs = 0;
                    long double sx = 0.0L, sy = 0.0L;
                    for (const auto& e : ext) {
                        const auto xa = e.nums.find(nameA);
                        const auto yb = e.nums.find(nameB);
                        if (xa == e.nums.end() || yb == e.nums.end())
                            continue;
                        ++pairs;
                        sx += static_cast<long double>(xa->second);
                        sy += static_cast<long double>(yb->second);
                    }
                    if (pairs < opts.minPairsForAssociation)
                        continue;
                    const long double mx = sx / static_cast<long double>(pairs);
                    const long double my = sy / static_cast<long double>(pairs);
                    long double cxy = 0.0L, cxx = 0.0L, cyy = 0.0L;
                    for (const auto& e : ext) {
                        const auto xa = e.nums.find(nameA);
                        const auto yb = e.nums.find(nameB);
                        if (xa == e.nums.end() || yb == e.nums.end())
                            continue;
                        const long double dx = static_cast<long double>(xa->second) - mx;
                        const long double dy = static_cast<long double>(yb->second) - my;
                        cxy += dx * dy;
                        cxx += dx * dx;
                        cyy += dy * dy;
                    }
                    if (!(cxx > 0.0L) || !(cyy > 0.0L))
                        continue; // konstantes Feld: Korrelation bedeutungslos
                    long double lr = cxy / std::sqrt(cxx * cyy);
                    double v = static_cast<double>(lr);
                    if (!std::isfinite(v))
                        continue;
                    if (v > 1.0)
                        v = 1.0;
                    if (v < -1.0)
                        v = -1.0;
                    MixedPairAssociation as;
                    as.fieldX = nameA;
                    as.fieldY = nameB;
                    as.measure = measure;
                    as.value = v;
                    as.pairs = pairs;
                    res.associations.push_back(std::move(as));
                } else if (measure == "etaSquared") {
                    // kategorisches Feld gruppiert, Zahl/Boolean ist Ziel
                    const std::string& catName = faCat ? nameA : nameB;
                    const std::string& numName = faCat ? nameB : nameA;
                    std::map<std::string, std::vector<double>> groups;
                    long double sum = 0.0L;
                    std::size_t pairs = 0;
                    for (const auto& e : ext) {
                        const auto cs = e.strs.find(catName);
                        const auto nv = e.nums.find(numName);
                        if (cs == e.strs.end() || nv == e.nums.end())
                            continue;
                        ++pairs;
                        groups[*cs->second].push_back(nv->second);
                        sum += static_cast<long double>(nv->second);
                    }
                    if (pairs < opts.minPairsForAssociation || groups.size() < 2 ||
                        groups.size() > opts.maxDistinctForAssociation)
                        continue;
                    const long double my = sum / static_cast<long double>(pairs);
                    long double sst = 0.0L, ssb = 0.0L;
                    for (const auto& g : groups)
                        for (double x : g.second) {
                            const long double d = static_cast<long double>(x) - my;
                            sst += d * d;
                        }
                    if (!(sst > 0.0L))
                        continue; // konstantes Ziel: eta^2 bedeutungslos
                    for (const auto& g : groups) {
                        long double gm = 0.0L;
                        for (double x : g.second)
                            gm += static_cast<long double>(x);
                        gm /= static_cast<long double>(g.second.size());
                        const long double d = gm - my;
                        ssb += static_cast<long double>(g.second.size()) * d * d;
                    }
                    double v = static_cast<double>(ssb / sst);
                    if (!std::isfinite(v))
                        continue;
                    if (v < 0.0)
                        v = 0.0;
                    if (v > 1.0)
                        v = 1.0;
                    MixedPairAssociation as;
                    as.fieldX = nameA;
                    as.fieldY = nameB;
                    as.measure = measure;
                    as.value = v;
                    as.pairs = pairs;
                    res.associations.push_back(std::move(as));
                } else { // cramersV
                    std::map<std::pair<std::string, std::string>, std::size_t> cells;
                    std::map<std::string, std::size_t> rowM, colM;
                    std::size_t pairs = 0;
                    for (const auto& e : ext) {
                        const auto xs = e.strs.find(nameA);
                        const auto ys = e.strs.find(nameB);
                        if (xs == e.strs.end() || ys == e.strs.end())
                            continue;
                        ++pairs;
                        ++cells[{*xs->second, *ys->second}];
                        ++rowM[*xs->second];
                        ++colM[*ys->second];
                    }
                    if (pairs < opts.minPairsForAssociation || rowM.size() < 2 ||
                        colM.size() < 2 || rowM.size() > opts.maxDistinctForAssociation ||
                        colM.size() > opts.maxDistinctForAssociation)
                        continue;
                    const long double nld = static_cast<long double>(pairs);
                    long double chi2 = 0.0L;
                    // Härtung/Korrektheit: chi^2 über die VOLLSTAENDIGE Kontingenztafel
                    // (rowM x colM) summieren – unbeobachtete Zellen (obs == 0)
                    // contribute (0 - expected)^2/expected und fehlten sonst.
                    for (const auto& rm : rowM)
                        for (const auto& cm : colM) {
                            const long double expected =
                                static_cast<long double>(rm.second) *
                                static_cast<long double>(cm.second) / nld;
                            const auto cit = cells.find({rm.first, cm.first});
                            const long double obs =
                                cit != cells.end()
                                    ? static_cast<long double>(cit->second)
                                    : 0.0L;
                            const long double d = obs - expected;
                            chi2 += d * d / expected;
                        }
                    if (!std::isfinite(static_cast<double>(chi2)) || chi2 < 0.0L)
                        continue;
                    const std::size_t denom =
                        std::min(rowM.size() - 1, colM.size() - 1);
                    const long double vc =
                        std::sqrt(chi2 / (nld * static_cast<long double>(denom)));
                    double v = static_cast<double>(vc);
                    if (!std::isfinite(v))
                        continue;
                    if (v < 0.0)
                        v = 0.0;
                    if (v > 1.0)
                        v = 1.0;
                    MixedPairAssociation as;
                    as.fieldX = nameA;
                    as.fieldY = nameB;
                    as.measure = measure;
                    as.value = v;
                    as.pairs = pairs;
                    res.associations.push_back(std::move(as));
                }
            }
        }
        std::sort(res.associations.begin(), res.associations.end(),
                  [](const MixedPairAssociation& x, const MixedPairAssociation& y) {
                      if (x.fieldX != y.fieldX)
                          return x.fieldX < y.fieldX;
                      return x.fieldY < y.fieldY;
                  });
    }
    return res;
}

std::vector<MixedCollectionAnalysis> analyzeMixed(const std::vector<Collection>& colls,
                                                  const MixedAnalysisOptions& opts) {
    std::vector<MixedCollectionAnalysis> out;
    for (const auto& c : colls) {
        if (c.name() == "__references")
            continue;
        out.push_back(analyzeMixed(c, opts));
    }
    return out;
}

std::vector<MixedCollectionAnalysis> analyzeMixed(Database& db,
                                                  const MixedAnalysisOptions& opts) {
    std::vector<Collection> colls;
    for (const auto& name : db.collectionNames())
        colls.push_back(db.collection(name));
    return analyzeMixed(colls, opts);
}

std::string toJson(const MixedFieldAnalysis& a) {
    std::string out = "{\"collection\":" + detail::writeJsonString(a.collection) +
                      ",\"field\":" + detail::writeJsonString(a.field) +
                      ",\"kind\":\"" + kindName(a.kind) + "\"" +
                      ",\"count\":" + std::to_string(a.count) +
                      ",\"distinct\":" + std::to_string(a.distinct) +
                      ",\"missing\":" + std::to_string(a.missing) +
                      ",\"missingRatio\":" + num(a.missingRatio) +
                      ",\"dominantShare\":" + num(a.dominantShare);
    if (a.kind == MixedKind::Number) {
        out += ",\"min\":" + num(a.min) + ",\"max\":" + num(a.max) +
               ",\"mean\":" + num(a.mean) + ",\"stddev\":" + num(a.stddev) +
               ",\"median\":" + num(a.median) +
               ",\"integralRatio\":" + num(a.integralRatio) +
               ",\"discrete\":" + (a.discrete ? "true" : "false");
    } else if (a.kind == MixedKind::Text) {
        out += ",\"lenMin\":" + std::to_string(a.lenMin) +
               ",\"lenMax\":" + std::to_string(a.lenMax) +
               ",\"lenMean\":" + num(a.lenMean) +
               ",\"uniqueness\":" + num(a.uniqueness) +
               ",\"emptyShare\":" + num(a.emptyShare) +
               ",\"textClass\":\"" + textClassName(a.textClass) + "\"" +
               ",\"dateLike\":" + (a.dateLike ? "true" : "false") +
               ",\"hexLike\":" + (a.hexLike ? "true" : "false") +
               ",\"numericLike\":" + (a.numericLike ? "true" : "false");
    } else {
        out += ",\"trueShare\":" + num(a.trueShare);
    }
    out += ",\"note\":" + detail::writeJsonString(a.note) + "}";
    return out;
}

std::string toJson(const MixedCollectionAnalysis& a) {
    std::string out = "{\"collection\":" + detail::writeJsonString(a.collection) +
                      ",\"docCount\":" + std::to_string(a.docCount) +
                      ",\"sampleSize\":" + std::to_string(a.sampleSize) +
                      ",\"fieldCount\":" + std::to_string(a.fieldCount) +
                      ",\"mixedFieldCount\":" + std::to_string(a.mixedFieldCount) +
                      ",\"fields\":[";
    bool first = true;
    for (const auto& f : a.fields) {
        if (!first)
            out += ',';
        first = false;
        out += toJson(f);
    }
    out += "],\"associations\":[";
    first = true;
    for (const auto& c : a.associations) {
        if (!first)
            out += ',';
        first = false;
        out += "{\"fieldX\":" + detail::writeJsonString(c.fieldX) +
               ",\"fieldY\":" + detail::writeJsonString(c.fieldY) +
               ",\"measure\":" + detail::writeJsonString(c.measure) +
               ",\"value\":" + num(c.value) + ",\"pairs\":" + std::to_string(c.pairs) + "}";
    }
    out += "]}";
    return out;
}

std::string toMarkdown(const MixedCollectionAnalysis& a) {
    std::string out = "# Gemischte Analyse: " + a.collection + " (FA-33)\n\n";
    out += "- Dokumente: " + std::to_string(a.docCount) + " (untersucht: " +
           std::to_string(a.sampleSize) + "), Felder: " + std::to_string(a.fieldCount) +
           ", davon analysiert: " + std::to_string(a.mixedFieldCount) + "\n\n";
    if (a.fields.empty()) {
        out += "Keine analysierbaren Felder gefunden (Mindestwertzahl pro Feld nicht erreicht).\n";
        return out;
    }
    out += "| Feld | Typ | n | distinct | Merkmale |\n";
    out += "|---|---|---|---|---|\n";
    for (const auto& f : a.fields) {
        out += "| " + f.field + " | " + kindName(f.kind) + " | " + std::to_string(f.count) +
               " | " + std::to_string(f.distinct) + " | " +
               (f.note.empty() ? "-" : f.note) + " |\n";
    }
    if (!a.associations.empty()) {
        out += "\n## Assoziationen\n\n| Feld X | Feld Y | Mass | Wert | Paare |\n|---|---|---|---|---|\n";
        for (const auto& c : a.associations) {
            out += "| " + c.fieldX + " | " + c.fieldY + " | " + c.measure + " | " +
                   num(c.value) + " | " + std::to_string(c.pairs) + " |\n";
        }
    }
    return out;
}

} // namespace memdb
