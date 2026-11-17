#include "memdb/numeric_analysis.hpp"

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
    // über und machte das Quantil inf bzw. nan (0*inf, ungültiges JSON); in
    // long double bleibt sie endlich, das Ergebnis liegt per Konstruktion
    // zwischen xs[lo] und xs[lo+1]. Absicherung beim Zurückschreiben.
    const long double v = static_cast<long double>(xs[lo]) +
                          frac * (static_cast<long double>(xs[lo + 1]) -
                                 static_cast<long double>(xs[lo]));
    const double m = static_cast<double>(v);
    return std::isfinite(m) ? m : xs[lo];
}

bool isIntegral(double v) {
    return std::isfinite(v) && std::floor(v) == v;
}

/// Erste signifikante Ziffer von |v| (1..9); 0, wenn nicht bestimmbar (0, NaN, Inf, Unterlauf).
int firstSignificantDigit(double v) {
    if (!std::isfinite(v) || v == 0.0)
        return 0;
    double x = std::fabs(v);
    for (int guard = 0; guard < 400; ++guard) {
        if (x >= 10.0)
            x /= 10.0;
        else if (x < 1.0)
            x *= 10.0;
        else
            break;
    }
    const int d = static_cast<int>(std::floor(x));
    if (d < 1 || d > 9)
        return 0;
    return d;
}

/// Liegt v nahezu exakt auf einem Vielfachen von g?
bool onGrid(double v, double g) {
    if (g <= 0.0 || !std::isfinite(v))
        return false;
    const double units = v / g;
    const double rounded = std::round(units);
    return std::fabs(units - rounded) <= 1e-9 * std::max(1.0, std::fabs(units));
}

double benfordExpected(int d) {
    return std::log10(1.0 + 1.0 / static_cast<double>(d));
}

} // namespace

std::vector<NumericFieldAnalysis> analyzeNumericFields(const Collection& c,
                                                        const NumericAnalysisOptions& opts) {
    NumericCollectionAnalysis full = analyzeNumeric(c, opts);
    return std::move(full.fields);
}

NumericCollectionAnalysis analyzeNumeric(const Collection& c, const NumericAnalysisOptions& opts) {
    NumericCollectionAnalysis res;
    res.collection = c.name();
    res.docCount = c.size();
    const std::size_t n = res.docCount;
    res.sampleSize = (opts.sampleSize > 0 && opts.sampleSize < n) ? opts.sampleSize : n;

    // Ein Scan: pro Dokument ID + numerische Werte (Feldindex bei Erstsicht vergeben),
    // zusätzlich alle Feldnamen für die numerische Feld-Quote.
    const bool sampled = opts.sampleSize > 0 && opts.sampleSize < n;
    struct DocRow {
        DocumentId id;
        std::vector<std::pair<std::size_t, Value>> vals;
    };
    std::vector<DocRow> rows;
    std::vector<std::string> fieldNames2; // Feldindex -> Name
    std::set<std::string> allFields;

    auto collect = [&](const Document& d) {
        rows.emplace_back();
        DocRow& row = rows.back();
        row.id = d.id();
        d.forEachField([&](const std::string& k, const Value& v) {
            allFields.insert(k);
            if (!v.isNumber() || !std::isfinite(v.asDouble()))
                return;
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
    // Einfügereihenfolge rekonstruieren: IDs sind sequenziell vergeben (nextId++),
    // die Dokument-Map iteriert unsortiert. Sequenz-Analysen brauchen die wahre Reihenfolge.
    std::sort(rows.begin(), rows.end(),
              [](const DocRow& a, const DocRow& b) { return a.id < b.id; });
    std::vector<std::vector<Value>> fieldValues(fieldNames2.size());
    for (const auto& row : rows)
        for (const auto& fv : row.vals)
            fieldValues[fv.first].push_back(fv.second);

    res.fieldCount = allFields.size();
    const std::size_t cap = std::min(fieldNames2.size(), opts.maxFieldsPerCollection);
    res.numericFieldCount = cap;
    res.numericFieldRatio =
        res.fieldCount > 0 ? static_cast<double>(cap) / static_cast<double>(res.fieldCount) : 0.0;

    // Feld-Analysen (alphabetisch stabil sortiert)
    for (std::size_t idx = 0; idx < cap; ++idx) {
        const auto& vals = fieldValues[idx];
        if (vals.empty() || vals.size() < opts.minValuesPerField) // Härtung: auch bei minValuesPerField == 0 nie ohne Werte
            continue;
        NumericFieldAnalysis a;
        a.collection = res.collection;
        a.field = fieldNames2[idx];
        a.count = vals.size();

        std::vector<double> xs;
        xs.reserve(vals.size());
        for (const auto& v : vals)
            xs.push_back(v.asDouble());
        std::sort(xs.begin(), xs.end());

        a.min = xs.front();
        a.max = xs.back();
        {
            std::set<Value, ValueLess> distinctSet;
            for (const auto& v : vals)
                distinctSet.insert(v);
            a.distinct = distinctSet.size();
        }
        const double cnt = static_cast<double>(xs.size());
        // Härtung: Momente in long double akkumulieren (kein Überlauf bei Extremwerten),
        // beim Zurückschreiben auf double Endlichkeit erzwingen (gültiges JSON).
        long double lmean = 0.0L;
        for (double x : xs)
            lmean += static_cast<long double>(x);
        lmean /= static_cast<long double>(xs.size());
        long double lm2 = 0.0L, lm3 = 0.0L, lm4 = 0.0L;
        for (double x : xs) {
            const long double d = static_cast<long double>(x) - lmean;
            lm2 += d * d;
            lm3 += d * d * d;
            lm4 += d * d * d * d;
        }
        lm2 /= static_cast<long double>(xs.size());
        lm3 /= static_cast<long double>(xs.size());
        lm4 /= static_cast<long double>(xs.size());
        const double mean = static_cast<double>(lmean);
        a.mean = std::isfinite(mean) ? mean : 0.0;
        const double m2d = static_cast<double>(lm2);
        a.stddev = std::isfinite(m2d) ? std::sqrt(m2d) : 0.0;
        a.q1 = quantile(xs, 0.25);
        a.median = quantile(xs, 0.5);
        a.q3 = quantile(xs, 0.75);
        const double iqr = a.q3 - a.q1; // Härtung: Finite-Check beim Zurückschreiben –
        a.iqr = std::isfinite(iqr) ? iqr : 0.0; // q3 - q1 läuft bei gegenläufigen Extremwerten über
        if (xs.size() >= 3 && lm2 > 0.0L) {
            const double sk = static_cast<double>(lm3 / (lm2 * std::sqrt(lm2)));
            const double ku = static_cast<double>(lm4 / (lm2 * lm2) - 3.0L);
            if (std::isfinite(sk))
                a.skewness = sk;
            if (std::isfinite(ku))
                a.kurtosis = ku;
        }
        std::size_t integral = 0;
        for (double x : xs)
            if (isIntegral(x))
                ++integral;
        a.integralRatio = static_cast<double>(integral) / cnt;
        a.discrete = (integral == xs.size()) && (a.max - a.min) <= opts.discreteMaxRange;

        // Rundungs-Raster: größtes Kandidaten-Raster, auf dem >= gridMinRatio der Werte liegen.
        // Rein ganzzahlige Felder melden kein Raster <= 1 (dort ist das Raster redundant).
        {
            const double candidates[] = {100000.0, 10000.0, 5000.0, 1000.0, 500.0, 100.0,
                                         50.0,     25.0,    10.0,   5.0,    2.5,   2.0,
                                         1.0,      0.5,     0.25,   0.1,    0.05,  0.01,
                                         0.001,    0.0001};
            for (double g : candidates) {
                std::size_t on = 0;
                for (double x : xs)
                    if (onGrid(x, g))
                        ++on;
                const double ratio = static_cast<double>(on) / cnt;
                if (ratio >= opts.gridMinRatio) {
                    if (!(integral == xs.size() && g <= 1.0)) {
                        a.hasGrid = true;
                        a.grid = g;
                        a.gridRatio = ratio;
                    }
                    break;
                }
            }
        }

        // Sequenz-Charakter (nur im Vollscan: braucht die Dokumentreihenfolge)
        if (!sampled && vals.size() >= 2) {
            a.sequenceAnalyzed = true;
            bool mono = true, consec = true;
            std::vector<double> diffs;
            diffs.reserve(vals.size() - 1);
            for (std::size_t i = 1; i < vals.size(); ++i) {
                const double prev = vals[i - 1].asDouble();
                const double cur = vals[i].asDouble();
                const double diff = cur - prev;
                if (std::isfinite(diff)) // Härtung: nicht darstellbare Sprünge (> DBL_MAX) überspringen
                    diffs.push_back(diff);
                if (cur <= prev)
                    mono = false;
                if (std::fabs(diff - 1.0) > 1e-9)
                    consec = false;
            }
            a.monotonic = mono;
            a.consecutive = mono && consec;
            if (mono && !diffs.empty()) {
                std::sort(diffs.begin(), diffs.end());
                a.stepMedian = quantile(diffs, 0.5);
                if (a.stepMedian > 0.0)
                    for (double dstep : diffs)
                        if (dstep > a.stepMedian + 1e-9)
                            ++a.gaps;
            }
        }

        // Benford-Analyse der ersten signifikanten Ziffer (nur bei enough Werten und Spannweite > 0)
        if (a.count >= opts.benfordMinSamples && (a.max - a.min) > 0.0) {
            std::size_t digitCount[10] = {};
            std::size_t used = 0;
            for (double x : xs) {
                const int d = firstSignificantDigit(x);
                if (d >= 1 && d <= 9) {
                    ++digitCount[d];
                    ++used;
                }
            }
            if (used >= opts.benfordMinSamples) {
                a.benfordApplicable = true;
                double mad = 0.0;
                for (int d = 1; d <= 9; ++d) {
                    const double observed = static_cast<double>(digitCount[d]) /
                                             static_cast<double>(used);
                    a.firstDigitFreq.push_back(observed);
                    mad += std::fabs(observed - benfordExpected(d));
                }
                a.benfordMad = mad / 9.0;
                a.benfordSuspicious = a.benfordMad > opts.benfordMadCritical;
            }
        }

        // Äquidistantes Histogramm
        if (opts.histogramBins > 0) {
            a.histogram.assign(opts.histogramBins, 0);
            a.binMin = a.min;
            const double range = a.max - a.min;
            a.binWidth = range / static_cast<double>(opts.histogramBins);
            // Härtung: nicht endliche Spannweite (Extremwerte) => alle Werte in Bin 0
            if (!std::isfinite(a.binWidth))
                a.binWidth = 0.0;
            for (double x : xs) {
                std::size_t idx2 = 0;
                if (a.binWidth > 0.0) {
                    const double rel = (x - a.min) / a.binWidth;
                    idx2 = static_cast<std::size_t>(rel);
                    if (idx2 >= opts.histogramBins)
                        idx2 = opts.histogramBins - 1;
                }
                ++a.histogram[idx2];
            }
        }

        // Interpretations-Hinweise
        if (a.discrete)
            a.note += "diskret: alle Werte ganzzahlig, Spannweite " + num(a.max - a.min) + "; ";
        if (a.consecutive)
            a.note += "sequenzielle ID (Schritt 1, keine Lücken); ";
        else if (a.monotonic)
            a.note += "monoton steigend, " + std::to_string(a.gaps) + " Luecke(n), Schrittweite " +
                      num(a.stepMedian) + "; ";
        if (a.hasGrid)
            a.note += "Raster " + num(a.grid) + " (" + num(a.gridRatio * 100.0) + "% der Werte); ";
        if (a.benfordSuspicious)
            a.note += "Benford-Auffaelligkeit (MAD " + num(a.benfordMad) + " > " +
                      num(opts.benfordMadCritical) + "); ";
        if (a.skewness > 1.0)
            a.note += "rechtschief (Skew " + num(a.skewness) + "); ";
        else if (a.skewness < -1.0)
            a.note += "linksschief (Skew " + num(a.skewness) + "); ";
        if (a.kurtosis > 3.0)
            a.note += "schwere Raender (Exzess-Kurtosis " + num(a.kurtosis) + "); ";
        while (!a.note.empty() && (a.note.back() == ' ' || a.note.back() == ';'))
            a.note.pop_back();
        res.fields.push_back(std::move(a));
    }
    std::sort(res.fields.begin(), res.fields.end(),
              [](const NumericFieldAnalysis& x, const NumericFieldAnalysis& y) {
                  return x.field < y.field;
              });

    // Paarweise Pearson-Korrelationen über Dokumente mit beiden Feldern.
    // Härtung: zentrierte zweistufige Berechnung in long double – die
    // Summenformel (n*Sxy - Sx*Sy) verliert bei großen Werten mit kleinem
    // Variationsbereich massiv an Genauigkeit (katastrophale Auslöschung).
    if (opts.withCorrelations && cap >= 2) {
        struct PairAcc {
            std::size_t n = 0;
            long double sx = 0.0L, sy = 0.0L;
            long double mx = 0.0L, my = 0.0L;
            long double cxy = 0.0L, cxx = 0.0L, cyy = 0.0L;
        };
        std::map<std::pair<std::size_t, std::size_t>, PairAcc> pairAcc;
        auto eachPair = [&](auto&& cb) {
            for (const auto& row : rows) {
                if (row.vals.size() < 2)
                    continue;
                for (std::size_t i = 0; i < row.vals.size(); ++i) {
                    if (row.vals[i].first >= cap)
                        continue;
                    for (std::size_t j = i + 1; j < row.vals.size(); ++j) {
                        if (row.vals[j].first >= cap)
                            continue;
                        std::pair<std::size_t, std::size_t> key(row.vals[i].first,
                                                               row.vals[j].first);
                        if (key.first > key.second)
                            std::swap(key.first, key.second);
                        // Werte-Slots konsistent zum sortierten Schlüssel zuordnen
                        // (Feldreihenfolge kann je Dokument wechseln)
                        double xv = row.vals[i].second.asDouble();
                        double yv = row.vals[j].second.asDouble();
                        if (key.first != row.vals[i].first)
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
            NumericPairCorrelation pc;
            pc.fieldX = fieldNames2[kv.first.first];
            pc.fieldY = fieldNames2[kv.first.second];
            pc.pearson = r;
            pc.pairs = p.n;
            res.correlations.push_back(std::move(pc));
        }
        std::sort(res.correlations.begin(), res.correlations.end(),
                  [](const NumericPairCorrelation& x, const NumericPairCorrelation& y) {
                      if (x.fieldX != y.fieldX)
                          return x.fieldX < y.fieldX;
                      return x.fieldY < y.fieldY;
                  });
    }
    return res;
}

std::vector<NumericCollectionAnalysis> analyzeNumeric(const std::vector<Collection>& colls,
                                                     const NumericAnalysisOptions& opts) {
    std::vector<NumericCollectionAnalysis> out;
    for (const auto& c : colls) {
        if (c.name() == "__references")
            continue;
        out.push_back(analyzeNumeric(c, opts));
    }
    return out;
}

std::vector<NumericCollectionAnalysis> analyzeNumeric(Database& db,
                                                      const NumericAnalysisOptions& opts) {
    std::vector<Collection> colls;
    for (const auto& name : db.collectionNames())
        colls.push_back(db.collection(name));
    return analyzeNumeric(colls, opts);
}

std::string toJson(const NumericFieldAnalysis& a) {
    std::string out = "{\"collection\":" + detail::writeJsonString(a.collection) +
                      ",\"field\":" + detail::writeJsonString(a.field) +
                      ",\"count\":" + std::to_string(a.count) +
                      ",\"distinct\":" + std::to_string(a.distinct) +
                      ",\"min\":" + num(a.min) + ",\"max\":" + num(a.max) +
                      ",\"mean\":" + num(a.mean) + ",\"stddev\":" + num(a.stddev) +
                      ",\"q1\":" + num(a.q1) + ",\"median\":" + num(a.median) +
                      ",\"q3\":" + num(a.q3) + ",\"iqr\":" + num(a.iqr) +
                      ",\"skewness\":" + num(a.skewness) + ",\"kurtosis\":" + num(a.kurtosis) +
                      ",\"integralRatio\":" + num(a.integralRatio) +
                      ",\"discrete\":" + (a.discrete ? "true" : "false") +
                      ",\"hasGrid\":" + (a.hasGrid ? "true" : "false") +
                      ",\"grid\":" + num(a.grid) + ",\"gridRatio\":" + num(a.gridRatio) +
                      ",\"sequenceAnalyzed\":" + (a.sequenceAnalyzed ? "true" : "false") +
                      ",\"monotonic\":" + (a.monotonic ? "true" : "false") +
                      ",\"consecutive\":" + (a.consecutive ? "true" : "false") +
                      ",\"stepMedian\":" + num(a.stepMedian) + ",\"gaps\":" + std::to_string(a.gaps) +
                      ",\"benfordApplicable\":" + (a.benfordApplicable ? "true" : "false") +
                      ",\"benfordSuspicious\":" + (a.benfordSuspicious ? "true" : "false") +
                      ",\"benfordMad\":" + num(a.benfordMad) + ",\"firstDigitFreq\":[";
    bool first = true;
    for (double f : a.firstDigitFreq) {
        if (!first)
            out += ',';
        first = false;
        out += num(f);
    }
    out += "],\"histogram\":[";
    first = true;
    for (std::size_t h : a.histogram) {
        if (!first)
            out += ',';
        first = false;
        out += std::to_string(h);
    }
    out += "],\"binWidth\":" + num(a.binWidth) + ",\"binMin\":" + num(a.binMin) +
           ",\"note\":" + detail::writeJsonString(a.note) + "}";
    return out;
}

std::string toJson(const NumericCollectionAnalysis& a) {
    std::string out = "{\"collection\":" + detail::writeJsonString(a.collection) +
                      ",\"docCount\":" + std::to_string(a.docCount) +
                      ",\"sampleSize\":" + std::to_string(a.sampleSize) +
                      ",\"fieldCount\":" + std::to_string(a.fieldCount) +
                      ",\"numericFieldCount\":" + std::to_string(a.numericFieldCount) +
                      ",\"numericFieldRatio\":" + num(a.numericFieldRatio) + ",\"fields\":[";
    bool first = true;
    for (const auto& f : a.fields) {
        if (!first)
            out += ',';
        first = false;
        out += toJson(f);
    }
    out += "],\"correlations\":[";
    first = true;
    for (const auto& c : a.correlations) {
        if (!first)
            out += ',';
        first = false;
        out += "{\"fieldX\":" + detail::writeJsonString(c.fieldX) +
               ",\"fieldY\":" + detail::writeJsonString(c.fieldY) +
               ",\"pearson\":" + num(c.pearson) + ",\"pairs\":" + std::to_string(c.pairs) + "}";
    }
    out += "]}";
    return out;
}

std::string toMarkdown(const NumericCollectionAnalysis& a) {
    std::string out = "# Numerische Analyse: " + a.collection + " (FA-30)\n\n";
    out += "- Dokumente: " + std::to_string(a.docCount) + " (untersucht: " +
           std::to_string(a.sampleSize) + "), Felder: " + std::to_string(a.fieldCount) +
           ", davon numerisch: " + std::to_string(a.numericFieldCount) + "\n\n";
    if (a.fields.empty()) {
        out += "Keine numerischen Felder gefunden (Mindestwertzahl pro Feld nicht erreicht).\n";
        return out;
    }
    out += "| Feld | n | distinct | Min | Max | Mean | Stddev | Median | Merkmale |\n";
    out += "|---|---|---|---|---|---|---|---|---|\n";
    for (const auto& f : a.fields) {
        out += "| " + f.field + " | " + std::to_string(f.count) + " | " +
               std::to_string(f.distinct) + " | " + num(f.min) + " | " + num(f.max) + " | " +
               num(f.mean) + " | " + num(f.stddev) + " | " + num(f.median) + " | " +
               (f.note.empty() ? "-" : f.note) + " |\n";
    }
    if (!a.correlations.empty()) {
        out += "\n## Pearson-Korrelationen\n\n| Feld X | Feld Y | r | Paare |\n|---|---|---|---|\n";
        for (const auto& c : a.correlations) {
            out += "| " + c.fieldX + " | " + c.fieldY + " | " + num(c.pearson) + " | " +
                   std::to_string(c.pairs) + " |\n";
        }
    }
    return out;
}

} // namespace memdb
