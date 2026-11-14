// Tests: Metriken/Diagnose (v0.22) - Collection::metrics()/resetMetrics(), Database::metrics(),
// Query-Metering (Index vs. Vollscan), Schreib-/Tx-/Event-/Spill-Zaehler.
#include <memdb/memdb.hpp>

#include <cstdio>
#include <iostream>
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
    // --- 1) Schreib-/Lesezaehler + Index-Metering ---
    {
        Database db("metrics");
        Collection c = db.createCollection("c");
        c.createIndex("k");
        for (int i = 0; i < 10; ++i)
            c.insert({{"k", i}, {"v", i}});
        auto m = c.metrics();
        CHECK(m.inserts == 10);
        CHECK(m.docs == 10 && m.indexCount == 1);
        CHECK(m.queries == 0);

        c.count(Field("k") == 3);          // Index-Query
        c.count(Field("v") >= 5);          // Vollscan (kein Index auf v)
        c.find("k < 4");                   // Index-Range
        m = c.metrics();
        CHECK(m.queries == 3);
        CHECK(m.indexQueries == 2);        // k==3 und k<4
        CHECK(m.lastQueryUsedIndex == true);
        CHECK(m.scannedDocs == 1 + 10 + 4); // 1 (eq) + 10 (scan) + 4 (range)
        CHECK(m.lastCandidateCount == 4);
        CHECK(m.lastQueryMs >= 0.0 && m.totalQueryMs >= m.lastQueryMs);

        // Update/Remove zaehlen als Schreibzugriffe, nicht als Query
        CHECK(c.update("k == 0", "v = 100") == 1);         // DSL-Update mit Set-DSL
        CHECK(c.update("k == 1", "v = 101") == 1);      // DSL-Update
        CHECK(c.removeById(DocumentId(2)));
        m = c.metrics();
        CHECK(m.updates == 2);
        CHECK(m.removes == 1);
        CHECK(m.queries == 3);             // Schreib-Scans zaehlen nicht als Query
        CHECK(m.docs == 9);
    }

    // --- 2) resetMetrics ---
    {
        Database db("metrics-reset");
        Collection c = db.createCollection("c");
        c.insert({{"a", 1}});
        c.count();
        CHECK(c.metrics().inserts == 1 && c.metrics().queries == 1);
        c.resetMetrics();
        auto m = c.metrics();
        CHECK(m.inserts == 0 && m.queries == 0 && m.scannedDocs == 0);
        CHECK(m.docs == 1);                // Ist-Werte bleiben
        c.count();
        CHECK(c.metrics().queries == 1);
    }

    // --- 3) Transaktions-Zaehler + Change-Events ---
    {
        Database db("metrics-tx");
        Collection c = db.createCollection("c");
        c.insert({{"a", 1}});
        int events = 0;
        c.onChange([&](const ChangeEvent&) { ++events; });
        {
            Transaction tx = c.beginTransaction();
            tx.insert({{"a", 2}});
            tx.commit();
        }
        {
            Transaction tx = c.beginTransaction();
            tx.insert({{"a", 3}});
            tx.rollback();
        }
        { // impliziter Rollback per Destruktor
            Transaction tx = c.beginTransaction();
            tx.insert({{"a", 4}});
        }
        auto m = c.metrics();
        CHECK(m.txCommits == 1);
        CHECK(m.txRollbacks == 2);         // explizit + Destruktor
        CHECK(m.inserts == 4);             // 1 initial + 3 Tx-Inserts (Rollback zaehlt als Op, docs bleibt 2)
        CHECK(m.changeEvents == 4);        // 1 initial + 3 Tx-Inserts (Rollback-Events werden verworfen)
        CHECK(events == 1);                // Observer sieht nur den Commit
        CHECK(m.docs == 2);
    }

    // --- 4) Hybrid: Spill-Reads/Writes ---
    {
        const std::string spill = "inmemdb_metrics_test.spill";
        std::remove(spill.c_str());
        Database db("metrics-hybrid");
        Collection c = db.createCollection("big");
        c.setOverflow(spill, 2);
        std::vector<DocumentId> ids;
        for (int i = 0; i < 5; ++i)
            ids.push_back(c.insert({{"i", i}}));
        auto m = c.metrics();
        CHECK(m.spillWrites == 3);         // 3 evictet
        CHECK(m.inserts == 5);
        c.findById(ids[0]);                // gespilltes Doc lesen
        c.count();                         // Vollscan liest Spill
        m = c.metrics();
        CHECK(m.spillReads >= 2);          // findById(ids[0]) + Scan
        c.disableOverflow();
        std::remove(spill.c_str());
    }

    // --- 5) Database-Aggregation ---
    {
        Database db("metrics-db");
        Collection a = db.createCollection("a");
        Collection b = db.createCollection("b");
        a.insert({{"x", 1}});
        a.insert({{"x", 2}});
        b.insert({{"y", 1}});
        a.count();
        b.count();
        auto dm = db.metrics();
        CHECK(dm.collectionCount == 2);
        CHECK(dm.totalDocs == 3);
        CHECK(dm.sum.inserts == 3);
        CHECK(dm.sum.queries == 2);
        auto am = db.metrics("a");
        CHECK(am.inserts == 2 && am.docs == 2 && am.queries == 1);
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
