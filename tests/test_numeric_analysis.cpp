// Tests: Numerische Metadaten-Analyse (FA-30, Kategorie 1): Kennzahlen (Mean/Stddev/
// Quantile/Skewness/Kurtosis), Diskretheit, Raster-Erkennung, Sequenz (monoton/Lücken/
// konsekutiv), Benford (erste Ziffer, MAD), Histogramm, Pearson-Korrelationen, Reports.
#include <memdb/memdb.hpp>
#include <memdb/numeric_analysis.hpp>

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;
#define CHECK(cond)                                                                              \
    do {                                                                                         \
        ++g_checks;                                                                              \
        if (!(cond)) {                                                                           \
            ++g_failures;                                                                        \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ")\n";        \
        }                                                                                        \
    } while (0)

using namespace memdb;

const NumericFieldAnalysis* findField(const std::vector<NumericFieldAnalysis>& v,
                                      const std::string& name) {
    for (const auto& f : v)
        if (f.field == name)
            return &f;
    return nullptr;
}

bool near(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps;
}

} // namespace

int main() {
    // 1) Kennzahlen: 1..10 -> Mean 5.5, Median 5.5, diskret, konsekutive Sequenz
    {
        Database db("n1");
        auto c = db.createCollection("basic");
        for (int i = 1; i <= 10; ++i)
            c.insert({{"v", i}});
        NumericAnalysisOptions opts;
        auto fields = analyzeNumericFields(c, opts);
        CHECK(fields.size() == 1);
        const auto& f = fields[0];
        CHECK(f.collection == "basic" && f.field == "v");
        CHECK(f.count == 10 && f.distinct == 10);
        CHECK(near(f.min, 1.0) && near(f.max, 10.0));
        CHECK(near(f.mean, 5.5));
        CHECK(near(f.median, 5.5));
        CHECK(near(f.q1, 3.25) && near(f.q3, 7.75) && near(f.iqr, 4.5));
        CHECK(near(f.stddev, std::sqrt(8.25), 1e-12));
        CHECK(near(f.integralRatio, 1.0));
        CHECK(f.discrete); // Spannweite 9 <= 100, alle ganzzahlig
        CHECK(f.sequenceAnalyzed && f.monotonic && f.consecutive);
        CHECK(f.gaps == 0);
        CHECK(near(f.stepMedian, 1.0));
        CHECK(!f.hasGrid); // rein ganzzahlig: Raster <= 1 wird nicht gemeldet
        CHECK(near(f.skewness, 0.0)); // symmetrische Folge
        CHECK(f.note.find("diskret") != std::string::npos);
        CHECK(f.note.find("sequenzielle ID") != std::string::npos);
    }

    // 2) Raster-Erkennung: 100er-Raster bei Ganzzahlen
    {
        Database db("n2");
        auto c = db.createCollection("prices");
        for (int i = 1; i <= 8; ++i)
            c.insert({{"bulk", 100 * i}});
        NumericAnalysisOptions opts;
        opts.minValuesPerField = 5;
        auto fields = analyzeNumericFields(c, opts);
        const auto* bulk = findField(fields, "bulk");
        CHECK(bulk != nullptr);
        CHECK(bulk->hasGrid && near(bulk->grid, 100.0));
        CHECK(near(bulk->gridRatio, 1.0));
    }

    // 2b) Cent-Raster bei Nachkommastellen: 0.03er-Schritte
    {
        Database db("n2b");
        auto c = db.createCollection("cents");
        const double ps[] = {9.99, 19.98, 29.97, 39.96, 49.95, 59.94, 69.93, 79.92};
        for (double p : ps)
            c.insert({{"price", p}});
        NumericAnalysisOptions opts;
        auto fields = analyzeNumericFields(c, opts);
        const auto* f = findField(fields, "price");
        CHECK(f != nullptr);
        CHECK(f->hasGrid && near(f->grid, 0.01)); // alle Beträge liegen auf dem Cent-Raster
        CHECK(near(f->gridRatio, 1.0));
    }

    // 3) Sequenz mit Lücken: 1,2,3,5,6,7 -> monoton, 1 Lücke
    {
        Database db("n3");
        auto c = db.createCollection("seq");
        for (int v : {1, 2, 3, 5, 6, 7})
            c.insert({{"id", v}});
        NumericAnalysisOptions opts;
        opts.minValuesPerField = 5;
        auto fields = analyzeNumericFields(c, opts);
        const auto* f = findField(fields, "id");
        CHECK(f != nullptr);
        CHECK(f->monotonic && !f->consecutive);
        CHECK(f->gaps == 1); // Sprung 3->5 ist groesser als mediane Schrittweite 1
        CHECK(near(f->stepMedian, 1.0));
        CHECK(f->note.find("monoton") != std::string::npos);
    }

    // 4) Benford: Gleichverteilung 1..2000 -> auffällig; exakt konforme Daten -> unauffällig
    {
        Database db("n4");
        auto c = db.createCollection("benford");
        for (int v = 1; v <= 2000; ++v)
            c.insert({{"uniform", v}});
        NumericAnalysisOptions opts; // benfordMinSamples = 30
        auto fields = analyzeNumericFields(c, opts);
        const auto* u = findField(fields, "uniform");
        CHECK(u != nullptr);
        CHECK(u->benfordApplicable);
        CHECK(u->benfordSuspicious); // Gleichverteilung weicht stark von Benford ab
        CHECK(u->firstDigitFreq.size() == 9);
        CHECK(near(u->firstDigitFreq[0], 1111.0 / 2000.0, 1e-12)); // Ziffer 1
        CHECK(u->benfordMad > 0.015);
        CHECK(u->note.find("Benford") != std::string::npos);

        // deterministisch benford-konforme Daten: erste Ziffer d, Magnituden 10^k
        Database db2("n4b");
        auto g = db2.createCollection("conform");
        for (int d = 1; d <= 9; ++d) {
            const int cnt = static_cast<int>(std::lround(1000.0 * std::log10(1.0 + 1.0 / d)));
            for (int i = 0; i < cnt; ++i)
                g.insert({{"val", static_cast<double>(d) * std::pow(10.0, i % 7)}});
        }
        auto fields2 = analyzeNumericFields(g, opts);
        const auto* w = findField(fields2, "val");
        CHECK(w != nullptr);
        CHECK(w->benfordApplicable);
        CHECK(w->benfordMad < 0.005); // Rundungsfehler der Konstruktion nur ~1 Promille
        CHECK(!w->benfordSuspicious);
    }

    // 5) Schiefe: rechtschief (ein großer Ausreißer)
    {
        Database db("n5");
        auto c = db.createCollection("skew");
        for (int v : {1, 1, 1, 1, 1, 1, 1, 2, 3, 50})
            c.insert({{"v", v}});
        NumericAnalysisOptions opts;
        auto fields = analyzeNumericFields(c, opts);
        const auto* f = findField(fields, "v");
        CHECK(f != nullptr);
        CHECK(f->skewness > 1.0);
        CHECK(f->note.find("rechtschief") != std::string::npos);
    }

    // 6) Histogramm: Summe der Bins = Anzahl Werte
    {
        Database db("n6");
        auto c = db.createCollection("hist");
        for (int i = 0; i < 50; ++i)
            c.insert({{"v", i % 10}});
        NumericAnalysisOptions opts;
        opts.histogramBins = 5;
        auto fields = analyzeNumericFields(c, opts);
        const auto* f = findField(fields, "v");
        CHECK(f != nullptr);
        CHECK(f->histogram.size() == 5);
        std::size_t sum = 0;
        for (std::size_t h : f->histogram)
            sum += h;
        CHECK(sum == 50);
        CHECK(near(f->binWidth, 1.8)); // Spannweite 9 / 5 Bins
    }

    // 7) Korrelationen: perfekter linearer Zusammenhang |r| ~ 1, negativ ~ -1
    {
        Database db("n7");
        auto c = db.createCollection("corr");
        for (int i = 0; i < 20; ++i)
            c.insert({{"x", 1.0 * i}, {"y", 2.0 * i + 1.0}, {"z", -3.0 * i + 7.0}});
        NumericAnalysisOptions opts;
        auto res = analyzeNumeric(c, opts);
        CHECK(res.collection == "corr");
        CHECK(res.numericFieldCount == 3);
        CHECK(res.numericFieldRatio == 1.0);
        CHECK(res.correlations.size() == 3); // 3 Paare
        bool xy = false, xz = false, yz = false;
        for (const auto& p : res.correlations) {
            if (p.fieldX == "x" && p.fieldY == "y") {
                CHECK(near(std::fabs(p.pearson), 1.0, 1e-12));
                CHECK(p.pairs == 20);
                xy = true;
            }
            if (p.fieldX == "x" && p.fieldY == "z") {
                CHECK(near(p.pearson, -1.0, 1e-12));
                xz = true;
            }
            if (p.fieldX == "y" && p.fieldY == "z") {
                CHECK(near(p.pearson, -1.0, 1e-12));
                yz = true;
            }
        }
        CHECK(xy && xz && yz);
    }

    // 8) minValuesPerField: kleines Feld wird übersprungen; Strings zählen nicht als numerisch
    {
        Database db("n8");
        auto c = db.createCollection("small");
        for (int v : {1, 2, 3})
            c.insert({{"tiny", v}});
        c.insert({{"label", std::string("a")}});
        c.insert({{"label", std::string("b")}});
        NumericAnalysisOptions opts; // minValuesPerField = 5
        auto res = analyzeNumeric(c, opts);
        CHECK(res.fields.empty()); // tiny: nur 3 Werte
        CHECK(res.numericFieldCount == 1); // tiny hat numerische Werte, label nicht
        CHECK(res.fieldCount == 2);
        CHECK(near(res.numericFieldRatio, 0.5));
        CHECK(res.correlations.empty());
        opts.minValuesPerField = 3;
        auto res2 = analyzeNumeric(c, opts);
        CHECK(res2.fields.size() == 1);
        CHECK(res2.fields[0].field == "tiny");
    }

    // 9) Sampling: Sequenzanalyse aus; Database-/vector-Überladungen, __references ignoriert
    {
        Database db("n9");
        auto c = db.createCollection("lots");
        for (int i = 0; i < 100; ++i)
            c.insert({{"v", i}});
        NumericAnalysisOptions opts;
        opts.sampleSize = 20;
        auto res = analyzeNumeric(c, opts);
        CHECK(res.sampleSize == 20);
        CHECK(res.docCount == 100);
        CHECK(res.fields.size() == 1);
        CHECK(!res.fields[0].sequenceAnalyzed); // Sequenz nur im Vollscan aussagekräftig
        CHECK(!res.fields[0].monotonic);
        auto all = analyzeNumeric(db, opts); // Database-Überladung
        CHECK(all.size() == 1 && all[0].collection == "lots");
        Database db2("n9b");
        auto t = db2.createCollection("texts");
        for (int i = 0; i < 5; ++i)
            t.insert({{"s", std::string("a") + std::to_string(i)}, {"n", 2.0 * i}});
        auto both = analyzeNumeric(std::vector<Collection>{c, t}, opts); // cross-database
        CHECK(both.size() == 2);
        CHECK(both[0].numericFieldCount == 1);
        CHECK(both[1].numericFieldCount == 1); // nur "n" ist numerisch
        CHECK(near(both[1].numericFieldRatio, 0.5));
    }

    // 10) Reports: JSON-Schlüssel, Markdown-Tabellen
    {
        Database db("n10");
        auto c = db.createCollection("rep");
        for (int i = 0; i < 10; ++i)
            c.insert({{"a", i}, {"b", 2.0 * i}});
        NumericAnalysisOptions opts;
        auto res = analyzeNumeric(c, opts);
        std::string j = toJson(res);
        CHECK(j.find("\"fields\":[") != std::string::npos);
        CHECK(j.find("\"correlations\":[") != std::string::npos);
        CHECK(j.find("\"field\":\"a\"") != std::string::npos);
        CHECK(j.find("\"pearson\"") != std::string::npos);
        CHECK(j.find("\"histogram\"") != std::string::npos);
        std::string md = toMarkdown(res);
        CHECK(md.find("Numerische Analyse") != std::string::npos);
        CHECK(md.find("Pearson-Korrelationen") != std::string::npos);
        auto fields = analyzeNumericFields(c, opts);
        std::string jf = toJson(fields[0]);
        CHECK(jf.find("\"skewness\"") != std::string::npos);
        CHECK(jf.find("\"firstDigitFreq\"") != std::string::npos);
    }

    // 11) Haertung: grosse Offsets – Pearson trotz Ausloeschung exakt
    //     (zentrierte zweistufige Berechnung in long double)
    {
        Database db("n11");
        auto c = db.createCollection("offsets");
        for (int i = 0; i < 20; ++i)
            c.insert({{"x", 1e12 + 1.0 * i}, {"y", 1e12 + 3.0 * i}});
        NumericAnalysisOptions opts;
        auto res = analyzeNumeric(c, opts);
        CHECK(res.correlations.size() == 1);
        const NumericPairCorrelation* pc = nullptr;
        for (const auto& p : res.correlations)
            if (p.fieldX == "x" && p.fieldY == "y")
                pc = &p;
        CHECK(pc != nullptr);
        CHECK(pc != nullptr && near(pc->pearson, 1.0, 1e-9));
        CHECK(pc != nullptr && pc->pairs == 20);
        const auto* fx = findField(res.fields, "x");
        CHECK(fx != nullptr);
        CHECK(fx != nullptr && std::isfinite(fx->mean));
        CHECK(fx != nullptr && std::isfinite(fx->stddev));
        CHECK(fx != nullptr && std::isfinite(fx->skewness));
        CHECK(fx != nullptr && std::isfinite(fx->kurtosis));
        CHECK(fx != nullptr && fx->histogram.size() == 10);
        std::size_t hsum = 0;
        for (std::size_t h : fx->histogram)
            hsum += h;
        CHECK(hsum == 20);
    }

    // 12) Haertung: Extremwerte (kein inf/nan in Reports), konstantes Feld,
    //     minValuesPerField 0/1 (Guard gegen leere Wertmengen)
    {
        Database db("n12");
        auto c = db.createCollection("extrem");
        c.insert({{"big", 1.7e308}});
        c.insert({{"big", -1.7e308}});
        for (int i = 0; i < 8; ++i)
            c.insert({{"big", 1000.0 * i - 3500.0}});
        NumericAnalysisOptions opts;
        auto fields = analyzeNumericFields(c, opts);
        CHECK(fields.size() == 1);
        const auto& f = fields[0];
        CHECK(f.field == "big");
        CHECK(std::isfinite(f.min) && std::isfinite(f.max));
        CHECK(std::isfinite(f.mean) && std::isfinite(f.stddev));
        CHECK(std::isfinite(f.skewness) && std::isfinite(f.kurtosis));
        CHECK(std::isfinite(f.iqr));
        CHECK(f.histogram.size() == 10);
        std::size_t hsum = 0;
        for (std::size_t h : f.histogram)
            hsum += h;
        CHECK(hsum == 10);
        const std::string j = toJson(f);
        CHECK(j.find(":inf") == std::string::npos);
        CHECK(j.find(":nan") == std::string::npos);

        // konstantes Feld: Stdabw 0, kein Benford (Spannweite 0), alles in Bin 0
        auto c2 = db.createCollection("konst");
        for (int i = 0; i < 8; ++i)
            c2.insert({{"k", 5}});
        auto f2s = analyzeNumericFields(c2, opts);
        CHECK(f2s.size() == 1);
        const auto& k = f2s[0];
        CHECK(near(k.stddev, 0.0));
        CHECK(near(k.skewness, 0.0));
        CHECK(near(k.kurtosis, 0.0));
        CHECK(!k.benfordApplicable);
        CHECK(k.histogram.size() == 10);
        CHECK(k.histogram[0] == 8);

        // minValuesPerField 0/1: Guard greift, keine Crashes
        NumericAnalysisOptions o0;
        o0.minValuesPerField = 0;
        auto r0 = analyzeNumeric(c2, o0);
        CHECK(r0.fields.size() == 1);
        NumericAnalysisOptions o1;
        o1.minValuesPerField = 1;
        auto c3 = db.createCollection("mini");
        c3.insert({{"m", 1}});
        auto r1 = analyzeNumeric(c3, o1);
        CHECK(r1.fields.size() == 1);
        CHECK(near(r1.fields[0].min, 1.0) && near(r1.fields[0].max, 1.0));
    }

    // 13) Haertung: gegenlaeufige Extremwerte (±1e308) – Quantile in long double
    //     interpoliert, IQR mit Finite-Fallback, nicht darstellbare
    //     Sequenzspruenge (> DBL_MAX) uebersprungen
    {
        Database db("n13");
        auto c = db.createCollection("extrem2");
        for (int i = 0; i < 6; ++i)
            c.insert({{"v", i < 3 ? -1e308 : 1e308}});
        NumericAnalysisOptions opts;
        auto fields = analyzeNumericFields(c, opts);
        CHECK(fields.size() == 1);
        const auto& f = fields[0];
        CHECK(f.field == "v");
        CHECK(f.count == 6 && f.distinct == 2);
        CHECK(f.q1 == -1e308);
        CHECK(f.median == 0.0); // Interpolation in long double, exakt
        CHECK(f.q3 == 1e308);
        CHECK(near(f.iqr, 0.0)); // q3 - q1 laeuft nach inf => Finite-Fallback
        CHECK(std::isfinite(f.mean) && f.mean == 0.0);
        CHECK(near(f.stddev, 0.0)); // lm2 laeuft nach inf => Finite-Fallback
        CHECK(std::isfinite(f.q1) && std::isfinite(f.median) && std::isfinite(f.q3));
        CHECK(std::isfinite(f.iqr));
        CHECK(std::isfinite(f.skewness) && std::isfinite(f.kurtosis));
        CHECK(f.histogram.size() == 10 && f.histogram[0] == 6); // Range inf => alle in Bin 0
        const std::string j = toJson(f);
        CHECK(j.find(":inf") == std::string::npos);
        CHECK(j.find(":nan") == std::string::npos);

        // Sequenz mit Riesensprung: Differenz 2e308 (ueber DBL_MAX) wird
        // uebersprungen, stepMedian aus den endlichen Differenzen gebildet
        auto c2 = db.createCollection("seqext");
        for (double v : {-1.7e308, -1.2e308, -1e308, 1e308, 1.7e308})
            c2.insert({{"s", v}});
        auto f2s = analyzeNumericFields(c2, opts);
        CHECK(f2s.size() == 1);
        const auto* fs = findField(f2s, "s");
        CHECK(fs != nullptr);
        CHECK(fs != nullptr && fs->monotonic && !fs->consecutive);
        CHECK(fs != nullptr && std::isfinite(fs->stepMedian));
        CHECK(fs != nullptr && near(fs->stepMedian, 5e307, 1e-9));
        CHECK(fs != nullptr && fs->gaps == 1);
        CHECK(fs != nullptr && fs->histogram.size() == 10 && fs->histogram[0] == 5);
        const std::string j2 = toJson(*fs);
        CHECK(j2.find(":inf") == std::string::npos);
        CHECK(j2.find(":nan") == std::string::npos);
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
