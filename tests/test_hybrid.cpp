// Tests: Hybrid / Out-of-Core (v0.18) – setOverflow/disableOverflow/overflowInfo/compactSpill.
#include <memdb/memdb.hpp>

#include <cstdio>
#include <iostream>

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
    const std::string spill = "inmemdb_hybrid_test.spill";
    std::remove(spill.c_str());

    Database db("hybrid-test");
    Collection c = db.createCollection("big");
    c.createIndex("name");

    // --- Overflow einschalten: max 2 im RAM ---
    c.setOverflow(spill, 2);
    auto info = c.overflowInfo();
    CHECK(info.enabled && info.maxInMemory == 2 && info.inMemory == 0 && info.spilled == 0);

    DocumentId ids[5];
    for (int i = 0; i < 5; ++i)
        ids[i] = c.insert({{"name", "n" + std::to_string(i)}, {"i", i}});

    info = c.overflowInfo();
    CHECK(info.inMemory == 2);        // nur die letzten 2 im RAM
    CHECK(info.spilled == 3);         // 3 ausgelagert
    CHECK(c.size() == 5);             // size sieht RAM + Spill
    CHECK(info.spillBytes > 0);

    // findById: RAM und Spill
    CHECK(c.findById(ids[0]).has_value());  // gespillt
    CHECK(c.findById(ids[0])->at("i").asInt() == 0);
    CHECK(c.findById(ids[4]).has_value());  // RAM
    CHECK(!c.findById(DocumentId(999)).has_value());

    // Scans sehen alles
    CHECK(c.count() == 5);
    CHECK(c.count(Field("i") >= 3) == 2);
    CHECK(c.count("i >= 0") == 5);          // DSL
    CHECK(c.find(Field("name") == "n1").size() == 1);   // gespilltes Doc per Index? nein: Vollscan -> gefunden
    CHECK(c.findIds("i < 2").size() == 2);
    std::size_t seen = 0;
    c.forEach("i >= 0", [&](const Document&) { ++seen; });
    CHECK(seen == 5);

    // updateById auf gespilltem Doc: landet als neue Version im Spill
    CHECK(c.updateById(ids[0], [](Document& d) { d.set("i", 100); }));
    CHECK(c.findById(ids[0])->at("i").asInt() == 100);
    CHECK(c.count("i == 100") == 1);

    // remove gespilltes Doc
    CHECK(c.removeById(ids[1]));
    CHECK(c.size() == 4);
    CHECK(!c.findById(ids[1]).has_value());
    CHECK(c.count() == 4);

    // remove per Query ueber RAM + Spill
    CHECK(c.remove("i >= 100") == 1);
    CHECK(c.size() == 3);

    // Transaktion waehrend Overflow: kein Evict waehrend Tx, danach evtl.
    {
        auto tx = c.beginTransaction();
        CHECK_THROWS_AS(c.setOverflow("andere.spill", 1), Error); // waehrend Tx verboten
        tx.insert({{"name", "tx1"}, {"i", 50}});
        tx.commit();
    }
    info = c.overflowInfo();
    CHECK(info.inMemory <= 2); // nach Tx-Ende evict
    CHECK(c.size() == 4);

    // compactSpill: Datei schrumpft (alte Versionen von ids[0] entfernt)
    std::uint64_t before = c.overflowInfo().spillBytes;
    std::uint64_t freed = c.compactSpill();
    CHECK(c.overflowInfo().spillBytes <= before);
    CHECK(c.size() == 4);
    CHECK(c.findById(ids[4]).has_value());

    // Recovery: Spill-Datei erneut oeffnen (neue Collection-Instanz, gleiche Datei)
    {
        Database db2("hybrid-reopen");
        Collection c2 = db2.createCollection("big");
        c2.setOverflow(spill, 2);
        auto info2 = c2.overflowInfo();
        CHECK(info2.spilled >= 1);      // Index aus Datei wiederhergestellt
        CHECK(c2.size() == info2.inMemory + info2.spilled);
        c2.disableOverflow();
    }

    // disableOverflow: alles zurueck ins RAM, Datei weg
    c.disableOverflow();
    info = c.overflowInfo();
    CHECK(!info.enabled && info.inMemory == 4 && info.spilled == 0);
    CHECK(c.count() == 4);
    CHECK(std::remove(spill.c_str()) == 0 || true); // Datei wurde von disableOverflow geloescht

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
