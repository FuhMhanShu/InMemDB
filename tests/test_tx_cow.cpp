// Tests: Copy-on-Write-Transaktions-Backup (v0.19) - Rollback stellt nur angefasste
// Dokumente wieder her (kein Voll-Backup mehr), inkl. Hybrid/Spill-Sonderfaellen.
#include <memdb/memdb.hpp>

#include <cstdio>
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
#define CHECK_THROWS_AS(expr, ExType)                                                            \
    do {                                                                                         \
        ++g_checks;                                                                              \
        bool caught = false;                                                                     \
        try {                                                                                    \
            (void)(expr);                                                                        \
        } catch (const ExType&) {                                                                \
            caught = true;                                                                       \
        } catch (...) {                                                                          \
        }                                                                                        \
        if (!caught) {                                                                           \
            ++g_failures;                                                                        \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": erwartete " #ExType        \
                         " von " #expr "\n";                                                     \
        }                                                                                        \
    } while (0)

using namespace memdb;

} // namespace

int main() {
    // --- 1) Rollback stellt nur angefasste Docs wieder her (viele Docs, wenige Aenderungen) ---
    {
        Database db("cow-basic");
        Collection c = db.createCollection("c");
        c.createIndex("k");
        std::vector<DocumentId> ids;
        for (int i = 0; i < 1000; ++i)
            ids.push_back(c.insert({{"k", i}, {"v", std::string("v") + std::to_string(i)}}));

        {
            Transaction tx = c.beginTransaction();
            CHECK(tx.updateById(ids[5], [](Document& d) { d.set("v", std::string("CHANGED")); }));
            CHECK(tx.update(Field("k") == 9, [](Document& d) { d.set("v", std::string("Q")); }) == 1);
            tx.insert({{"k", 100000}, {"v", "neu"}});
            CHECK(tx.removeById(ids[7]));
            CHECK(tx.size() == 1000);
            tx.rollback(); // void - danach ist die Tx inaktiv
            CHECK(!tx.isActive());
        }
        CHECK(c.size() == 1000);
        CHECK(c.findById(ids[5])->at("v").asString() == "v5");
        CHECK(c.findById(ids[9])->at("v").asString() == "v9");
        CHECK(c.findById(ids[7]).has_value());
        CHECK(c.count(Field("k") == 100000) == 0);
        // Indizes nach Rollback intakt (Query laeuft ueber Index)
        CHECK(c.count(Field("k") == 5) == 1);
        CHECK(c.find(Field("k") >= 995).size() == 5);
        CHECK(c.count("k >= 995") == 5); // DSL
        // nextId wurde mit zurueckgesetzt
        DocumentId nid = c.insert({{"k", -1}});
        CHECK(nid.value() == 1001);
    }

    // --- 2) Commit haelt die Aenderungen ---
    {
        Database db("cow-commit");
        Collection c = db.createCollection("c");
        c.createIndex("k");
        std::vector<DocumentId> ids;
        for (int i = 0; i < 100; ++i)
            ids.push_back(c.insert({{"k", i}, {"v", i}}));
        {
            Transaction tx = c.beginTransaction();
            tx.updateById(ids[5], [](Document& d) { d.set("v", 555); });
            tx.insert({{"k", 100000}, {"v", 1}});
            tx.removeById(ids[7]);
            tx.commit();
            CHECK(!tx.isActive());
        }
        CHECK(c.size() == 100);
        CHECK(c.findById(ids[5])->at("v").asInt() == 555);
        CHECK(!c.findById(ids[7]).has_value());
        CHECK(c.count(Field("k") == 100000) == 1);
    }

    // --- 3) replace/upsertById im Tx + Rollback; Unique-Constraint in Tx wirft, Rollback intakt ---
    {
        Database db("cow-misc");
        Collection c = db.createCollection("c");
        c.createIndex("k", true); // unique
        DocumentId a = c.insert({{"k", 1}, {"v", "a"}});
        DocumentId b = c.insert({{"k", 2}, {"v", "b"}});
        {
            Transaction tx = c.beginTransaction();
            tx.replace(a, Document({{"k", 1}, {"v", "a2"}}));
            tx.upsertById(b, Document({{"k", 2}, {"v", "b2"}}));
            CHECK_THROWS_AS(tx.insert({{"k", 1}}), UniqueConstraintError);
            tx.rollback();
        }
        CHECK(c.findById(a)->at("v").asString() == "a");
        CHECK(c.findById(b)->at("v").asString() == "b");
        CHECK(c.size() == 2);
        CHECK(c.count(Field("k") == 1) == 1); // unique-Index nach Rollback korrekt
    }

    // --- 4) Observer-Events: Rollback verwirft, Commit feuert ---
    {
        Database db("cow-observer");
        Collection c = db.createCollection("c");
        int events = 0;
        c.onChange([&](const ChangeEvent&) { ++events; });
        {
            Transaction tx = c.beginTransaction();
            tx.insert({{"k", 1}});
            tx.rollback();
        }
        CHECK(events == 0);
        {
            Transaction tx = c.beginTransaction();
            tx.insert({{"k", 2}});
            tx.commit();
        }
        CHECK(events == 1);
        CHECK(c.size() == 1);
    }

    // --- 5) Hybrid/Spill: Rollback eines Updates auf gespilltes Doc stellt alte Version her ---
    {
        const std::string spill = "inmemdb_tx_cow_test.spill";
        std::remove(spill.c_str());
        Database db("cow-hybrid");
        Collection c = db.createCollection("big");
        c.setOverflow(spill, 2);
        std::vector<DocumentId> ids;
        for (int i = 0; i < 5; ++i)
            ids.push_back(c.insert({{"i", i}, {"v", std::string("v") + std::to_string(i)}}));
        auto info = c.overflowInfo();
        CHECK(info.inMemory == 2 && info.spilled == 3);
        DocumentId spilledId = ids[0]; // aeltestes -> ausgespillt
        CHECK(c.findById(spilledId)->at("v").asString() == "v0");
        {
            Transaction tx = c.beginTransaction();
            CHECK(tx.updateById(spilledId, [](Document& d) { d.set("v", std::string("SPILL-CHANGED")); }));
            CHECK(tx.updateById(ids[4], [](Document& d) { d.set("v", std::string("RAM-CHANGED")); }));
            CHECK(tx.removeById(ids[1])); // gespilltes Doc entfernen
            // waehrend Tx kein Evict (txLocks)
            tx.insert({{"i", 99}, {"v", "x"}});
            tx.insert({{"i", 98}, {"v", "y"}});
            CHECK(tx.size() == 6); // 5 - 1 entfernt + 2 eingefuegt; Evict waehrend Tx blockiert
            tx.rollback();
        }
        CHECK(c.findById(spilledId)->at("v").asString() == "v0");   // alte Spill-Version
        CHECK(c.findById(ids[4])->at("v").asString() == "v4");
        CHECK(c.findById(ids[1]).has_value());                       // entferntes Doc wieder da
        CHECK(c.size() == 5);
        CHECK(c.count(Field("i") == 99) == 0);
        // nach Tx-Ende wurde evtl. evictet (txLocks wieder 0)
        CHECK(c.overflowInfo().inMemory <= 2);
        CHECK(c.count() == 5);

        // Commit-Variante: Aenderung an gespilltem Doc ueberlebt + Recovery per Reopen
        {
            Transaction tx = c.beginTransaction();
            CHECK(tx.updateById(spilledId, [](Document& d) { d.set("v", std::string("COMMITTED")); }));
            tx.commit();
        }
        CHECK(c.findById(spilledId)->at("v").asString() == "COMMITTED");
        {
            Database db2("cow-hybrid-reopen");
            Collection c2 = db2.createCollection("big");
            c2.setOverflow(spill, 2);
            // nur die gespillten Docs stehen in der Datei (RAM-Docs von db1 nicht)
            CHECK(c2.size() == 3);
            auto d0 = c2.findById(spilledId);
            CHECK(d0.has_value() && d0->at("v").asString() == "COMMITTED"); // neuer Record gewinnt
            c2.disableOverflow();
        }
        c.disableOverflow();
        std::remove(spill.c_str());
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
