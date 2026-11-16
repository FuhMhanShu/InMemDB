// Tests: Discovery v0.40 – Ausreißer-Erkennung (discoverOutliers: z-Score + IQR/Tukey pro
// numerischem Feld), Integration in analyze()/AnalysisReport, Health-Score-Faktor, Reports.
#include <memdb/memdb.hpp>
#include <memdb/discovery.hpp>

#include <iostream>
#include <sstream>
#include <string>

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

} // namespace

int main() {
    // 1) Basis: Feld mit klarem Ausreißer (500) wird erkannt, sauberes Feld nicht, Strings ignoriert
    {
        Database db("o1");
        auto c = db.createCollection("sales");
        for (int v : {10, 11, 9, 10, 11, 10, 9, 11, 10, 500})
            c.insert({{"price", v}, {"name", "p" + std::to_string(v)}});
        DiscoveryOptions opts;
        opts.minOutlierSamples = 4; // Testdaten sind klein
        auto outs = discoverOutliers(c, opts);
        CHECK(outs.size() == 1); // nur "price" (name ist String)
        const auto& fo = outs[0];
        CHECK(fo.collection == "sales" && fo.field == "price");
        CHECK(fo.count == 10);
        CHECK(fo.outliers == 1);
        CHECK(fo.values.size() == 1 && fo.values[0].asInt() == 500);
        CHECK(fo.outlierRate > 0.09 && fo.outlierRate < 0.11);
        CHECK(!fo.method.empty());
        CHECK(fo.q3 >= fo.median && fo.median >= fo.q1); // Quantile konsistent
        CHECK(fo.iqr == fo.q3 - fo.q1);
    }

    // 2) Sauberes Feld ohne Ausreißer -> leeres Ergebnis; minOutlierSamples greift
    {
        Database db("o2");
        auto c = db.createCollection("c");
        for (int i = 1; i <= 20; ++i)
            c.insert({{"v", i}});
        DiscoveryOptions opts; // default minOutlierSamples = 8
        auto outs = discoverOutliers(c, opts);
        CHECK(outs.empty()); // gleichmäßige Werte -> keine Ausreißer
        // nur 3 Werte + extremer Ausreißer, aber minSamples=8 -> wird übersprungen
        Database db2("o2b");
        auto c2 = db2.createCollection("c");
        c2.insert({{"v", 1}});
        c2.insert({{"v", 2}});
        c2.insert({{"v", 1000}});
        CHECK(discoverOutliers(c2, opts).empty());
        // mit 4 Werten {1,2,3,1000} greift die IQR-Fence (Q3+1.5*IQR = 252.25+375.75 = 628 < 1000)
        c2.insert({{"v", 3}});
        opts.minOutlierSamples = 4;
        auto outs2 = discoverOutliers(c2, opts);
        CHECK(outs2.size() == 1 && outs2[0].outliers == 1);
        CHECK(outs2[0].values.size() == 1 && outs2[0].values[0].asInt() == 1000);
    }

    // 3) Schwellen konfigurierbar: niedrigeres outlierZ -> mehr Ausreißer
    {
        Database db("o3");
        auto c = db.createCollection("c");
        for (int v : {10, 10, 10, 11, 12, 13, 14, 20})
            c.insert({{"v", v}});
        DiscoveryOptions opts;
        opts.minOutlierSamples = 4;
        opts.outlierZ = 3.0;
        auto a = discoverOutliers(c, opts);
        opts.outlierZ = 1.0; // strenger
        auto b = discoverOutliers(c, opts);
        std::size_t na = 0, nb = 0;
        for (auto& f : a) na += f.outliers;
        for (auto& f : b) nb += f.outliers;
        CHECK(nb >= na);
    }

    // 4) Cross-database: vector<Collection> aus zwei Databases
    {
        Database d1("a"), d2("b");
        auto c1 = d1.createCollection("t1");
        auto c2 = d2.createCollection("t2");
        for (int v : {1, 2, 2, 3, 3, 3, 2, 999})
            c1.insert({{"x", v}});
        for (int v : {5, 5, 6, 5, 6, 5, 6, 5})
            c2.insert({{"y", v}});
        DiscoveryOptions opts;
        opts.minOutlierSamples = 4;
        auto outs = discoverOutliers({c1, c2}, opts);
        CHECK(outs.size() == 1);
        CHECK(outs[0].collection == "t1" && outs[0].field == "x" && outs[0].values[0].asInt() == 999);
    }

    // 5) analyze(): outliers im Report + Health-Score sinkt mit Ausreißer
    {
        DiscoveryOptions opts;
        opts.minOutlierSamples = 4;
        opts.minSupport = 0.0;
        opts.minCorrelation = 0.99;
        Database clean("clean");
        auto c = clean.createCollection("m");
        for (int i = 1; i <= 10; ++i)
            c.insert({{"v", i}});
        Database dirty("dirty");
        auto d = dirty.createCollection("m");
        for (int i = 1; i <= 10; ++i)
            d.insert({{"v", i}});
        d.insert({{"v", 500}}); // Ausreißer
        auto rc = analyze(clean, opts);
        auto rd = analyze(dirty, opts);
        CHECK(rc.outliers.empty());
        CHECK(rd.outliers.size() == 1 && rd.outliers[0].outliers == 1);
        CHECK(rd.healthScore < rc.healthScore); // Ausreißer senkt Health-Score
        // Reports enthalten Outliers
        CHECK(toJson(rd).find("\"outliers\"") != std::string::npos);
        CHECK(toJson(rd).find("\"price\"") == std::string::npos); // Feld heißt v
        CHECK(toJson(rd).find("\"field\":\"v\"") != std::string::npos);
        std::string md = toMarkdown(rd);
        CHECK(md.find("Ausreißer") != std::string::npos);
        CHECK(md.find("m.v") != std::string::npos);
        std::ostringstream ss;
        printSummary(rd, ss);
        CHECK(ss.str().find("Outliers: 1") != std::string::npos);
        // sauberer Report: keine Ausreißer-Sektion
        CHECK(toMarkdown(rc).find("Ausreißer (Outliers") == std::string::npos);
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
