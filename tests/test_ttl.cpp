// Tests: TTL / Auto-Expiry (FA-25, v0.26) - opt-in (default AUS), expiresIn/expiresAt,
// lazy Sweep bei Lesezugriffen, expireNow(), Default-TTL, Tx-Guard, Spill, Schema,
// Snapshot, Metriken, Rollback.
#include <memdb/memdb.hpp>

#include <cstdio>
#include <iostream>
#include <memory>
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

// Test-Uhr: ab 1'000'000, manuell weiterschaltbar
struct TestClock {
    std::int64_t now = 1'000'000;
    std::function<std::int64_t()> fn() {
        return [this] { return now; };
    }
};

} // namespace

int main() {
    // 1) Default AUS: Docs ohne expiresAt verfallen nie
    {
        Database db("t");
        auto c = db.createCollection("c");
        TestClock clk;
        c.setClockForTesting(clk.fn());
        c.insert({{"k", "v"}});
        CHECK(c.defaultExpiresIn() == 0);
        clk.now += 10'000'000; // weit in die Zukunft
        CHECK(c.expireNow() == 0);
        CHECK(c.size() == 1);
        CHECK(c.findById(c.findIds().front()).has_value());
    }

    // 2) expiresAt in der Vergangenheit -> lazy Sweep bei findById/count/find
    {
        Database db("t");
        auto c = db.createCollection("c");
        TestClock clk;
        c.setClockForTesting(clk.fn());
        DocumentId a = c.insert(Document{{"k", "a"}}.expiresAt(clk.now - 1)); // abgelaufen
        DocumentId b = c.insert(Document{{"k", "b"}}.expiresAt(clk.now + 1000)); // gültig
        CHECK(c.size() == 1);                         // a bereits weg
        CHECK(!c.findById(a).has_value());
        CHECK(c.findById(b).has_value());
        CHECK(c.count() == 1);
        CHECK(c.findIds().size() == 1);
        auto ids = c.findIds(Field("k") == "a");
        CHECK(ids.empty());
        CHECK(c.metrics().expiredDocs == 1);
    }

    // 3) expiresIn (relativ) auf bestehendem Doc + Clock weiterschalten
    {
        Database db("t");
        auto c = db.createCollection("c");
        TestClock clk;
        c.setClockForTesting(clk.fn());
        DocumentId id = c.insert({{"k", "v"}});
        CHECK(!c.expiresAtOf(id).has_value());
        CHECK(c.expiresIn(id, 60));
        auto e = c.expiresAtOf(id);
        CHECK(e.has_value() && *e == clk.now + 60);
        CHECK(c.expiresAt(id, clk.now + 10));
        CHECK(*c.expiresAtOf(id) == clk.now + 10);
        clk.now += 11; // abgelaufen
        CHECK(c.expireNow() == 1);
        CHECK(c.size() == 0);
        CHECK(!c.expiresIn(DocumentId(999), 60)); // Doc existiert nicht
    }

    // 4) Collection-Default-TTL: setDefaultExpiresIn setzt expiresAt bei neuen Docs
    {
        Database db("t");
        auto c = db.createCollection("c");
        TestClock clk;
        c.setClockForTesting(clk.fn());
        c.setDefaultExpiresIn(100);
        CHECK(c.defaultExpiresIn() == 100);
        DocumentId a = c.insert({{"k", "auto"}});
        CHECK(c.expiresAtOf(a).has_value() && *c.expiresAtOf(a) == clk.now + 100);
        DocumentId b = c.insert(Document{{"k", "manuell"}}.expiresAt(clk.now + 5)); // manuell gewinnt
        CHECK(*c.expiresAtOf(b) == clk.now + 5);
        clk.now += 6;
        CHECK(c.expireNow() == 1); // nur b abgelaufen
        CHECK(c.size() == 1);
        CHECK(c.findById(a).has_value());
        c.setDefaultExpiresIn(0); // wieder aus
        DocumentId d = c.insert({{"k", "ohne"}});
        CHECK(!c.expiresAtOf(d).has_value());
    }

    // 5) Observer: Remove-Event beim Expiry + Metrik
    {
        Database db("t");
        auto c = db.createCollection("c");
        TestClock clk;
        c.setClockForTesting(clk.fn());
        std::size_t removeEvents = 0;
        c.onChange([&](const ChangeEvent& e) {
            if (e.type == ChangeType::Remove)
                ++removeEvents;
        });
        c.insert(Document{{"k", "x"}}.expiresAt(clk.now + 1));
        clk.now += 2;
        CHECK(c.expireNow() == 1);
        CHECK(removeEvents == 1);
        CHECK(c.metrics().expiredDocs == 1);
        CHECK(c.metrics().removes == 1);
    }

    // 6) Transaktions-Guard: mitten in Tx wird nicht gesweept; nach Commit greift lazy Sweep
    {
        Database db("t");
        auto c = db.createCollection("c");
        TestClock clk;
        c.setClockForTesting(clk.fn());
        DocumentId id = c.insert(Document{{"k", "tx"}}.expiresAt(clk.now + 10));
        clk.now += 20; // abgelaufen
        {
            auto tx = c.beginTransaction();
            // innerhalb der Tx (txLocks > 0) kein Sweep -> Dokument noch sichtbar
            CHECK(tx.findById(id).has_value());
            tx.commit();
        }
        // nach Commit: nächster Lesezugriff sweept
        CHECK(!c.findById(id).has_value());
        CHECK(c.size() == 0);
    }

    // 7) Rollback stellt expiryCount wieder her
    {
        Database db("t");
        auto c = db.createCollection("c");
        TestClock clk;
        c.setClockForTesting(clk.fn());
        DocumentId keep = c.insert(Document{{"k", "keep"}}.expiresAt(clk.now + 1000));
        {
            auto tx = c.beginTransaction();
            tx.removeById(keep); // Doc mit expiry entfernen
            // Rollback per Destruktor
        }
        CHECK(c.findById(keep).has_value()); // wieder da
        clk.now += 2000;                     // keep abgelaufen
        CHECK(c.expireNow() == 1);           // expiryCount war korrekt -> Sweep findet es
        CHECK(c.size() == 0);
    }

    // 8) Spill (Out-of-Core): abgelaufenes gespilltes Doc wird gesweept
    {
        Database db("t");
        auto c = db.createCollection("c");
        const std::string spill = std::string(std::tmpnam(nullptr)) + ".spill";
        c.setOverflow(spill, 1); // nur 1 Doc im RAM
        TestClock clk;
        c.setClockForTesting(clk.fn());
        c.insert(Document{{"n", 1}}.expiresAt(clk.now - 5));   // abgelaufen, landet evtl. im Spill
        c.insert(Document{{"n", 2}}.expiresAt(clk.now + 500)); // gültig
        CHECK(c.overflowInfo().spilled >= 1);
        CHECK(c.expireNow() == 1);
        CHECK(c.size() == 1);
        CHECK(c.count(Field("n") == 2) == 1);
        std::remove(spill.c_str());
    }

    // 9) Snapshot: abgelaufene Docs landen nicht im JSON; expiresAt wird persistiert
    {
        Database db("t");
        auto c = db.createCollection("c");
        TestClock clk;
        c.setClockForTesting(clk.fn());
        c.insert(Document{{"k", "weg"}}.expiresAt(clk.now - 1));
        // 4e9 = Jahr 2096: sowohl Test-Uhr als auch echte Uhr sehen das als Zukunft
        c.insert(Document{{"k", "bleibt"}}.expiresAt(4'000'000'000));
        std::string json = c.toJson(); // sweept vorher
        CHECK(json.find("weg") == std::string::npos);
        CHECK(json.find("bleibt") != std::string::npos);
        CHECK(json.find("expiresAt") != std::string::npos);
        Database db2 = Database::fromJson("{\"name\":\"t2\",\"collections\":{\"c\":{\"indexes\":[],\"documents\":" + json + "}}}", "");
        auto c2 = db2.collection("c");
        CHECK(c2.size() == 1);
        CHECK(c2.findIds(Field("k") == "bleibt").size() == 1);
    }

    // 10) Schema (FA-20): Default-TTL nur wenn expiresAt im Schema deklariert
    {
        Database db("t");
        auto c = db.createCollection("c");
        TestClock clk;
        c.setClockForTesting(clk.fn());
        c.setDefaultExpiresIn(50);
        Schema sch;
        sch.add("k", Value::Type::String); // expiresAt NICHT deklariert
        c.setSchema(sch);
        DocumentId a = c.insert({{"k", "v"}}); // Default-TTL greift nicht (Schema)
        CHECK(!c.expiresAtOf(a).has_value());
        Schema sch2;
        sch2.add("k", Value::Type::String);
        sch2.add("expiresAt", Value::Type::Int);
        c.setSchema(sch2);
        DocumentId b = c.insert({{"k", "v2"}});
        CHECK(c.expiresAtOf(b).has_value());
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
