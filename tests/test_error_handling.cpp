// Tests: Exception-Policy v0.35 – Exceptions werden nur dort abgefangen, wo der Programmfluss
// sonst hart abbrechen wuerde (Query-Scan pro Dokument, Observer-Callbacks, Bulk-Load-Validierung,
// CLI). Echte Fehler (Constraints, kaputte Dateien, API-Missbrauch) werfen weiterhin.
#include <memdb/memdb.hpp>
#include <memdb/query_dsl.hpp>
#include <memdb/references.hpp>

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
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #expr " wirft nicht " #ExType "\n"; \
        }                                                                                        \
    } while (0)

using namespace memdb;

} // namespace

int main() {
    // 1) Observer-Callback wirft -> Schreib-OP gelingt trotzdem, alle Observer laufen,
    //    Fehler landet in metrics().eventErrors
    {
        Database db("obs");
        auto c = db.createCollection("c");
        int callsGood = 0;
        c.onChange([&](const ChangeEvent&) { throw std::runtime_error("boom"); });
        c.onChange([&](const ChangeEvent&) { ++callsGood; });
        auto id = c.insert({{"x", 1}}); // darf nicht werfen
        CHECK(c.findById(id).has_value());
        CHECK(c.size() == 1);
        CHECK(callsGood == 1); // zweiter Observer wurde trotz throwendem ersten Angerufen
        CHECK(c.metrics().eventErrors == 1);
        c.resetMetrics();
        CHECK(c.metrics().eventErrors == 0);
        CHECK(db.metrics().sum.eventErrors == 0); // nach reset
    }

    // 2) Eigene DSL-Funktion wirft zur Eval-Zeit -> Scan bricht nicht ab, Dokument zaehlt als
    //    Nicht-Treffer, metrics().evalErrors zaehlt; count/find/update liefern saubere Ergebnisse
    {
        Database db("eval");
        auto c = db.createCollection("c");
        c.insert({{"v", 1}});
        c.insert({{"v", 2}});
        c.insert({{"v", 3}});
        QueryFunctionRegistry::instance().registerFunction(
            "boom", [](Expression f, const std::vector<Value>&) -> Expression {
                return f.predicate([](const Value&) -> bool { throw std::runtime_error("eval boom"); });
            });
        // Scan mit throwender Funktion: kein Throw, 0 Treffer, evalErrors == 3
        auto cur = c.find("v.boom()");
        CHECK(cur.size() == 0);
        CHECK(c.metrics().evalErrors == 3);
        CHECK(c.count("v.boom()") == 0);
        // normales Query danach weiterhin ok
        CHECK(c.count("v >= 2") == 2);
        // update mit throwender Funktion: aendert nichts, wirft nicht
        CHECK(c.update("v.boom()", "v = 99") == 0);
        CHECK(c.count("v == 99") == 0);
        QueryFunctionRegistry::instance().unregisterFunction("boom");
    }

    // 3) Ungueltiges matches-Pattern wirft am Query-BAU (QueryError) – nicht mitten im Scan
    {
        Database db("q");
        auto c = db.createCollection("c");
        c.insert({{"name", "Ada"}});
        CHECK_THROWS_AS(c.find("name.matches('(')"), QueryError);
        CHECK(c.find("name.matches('^A.*')").size() == 1); // Full-Match: ^A allein matcht "Ada" nicht
    }

    // 4) loadJson mit kaputtem Element -> JsonError, aber Collection bleibt UNVERAENDERT
    //    (validate-first, kein geleerter/halbgeladener Zustand)
    {
        Database db("lj");
        auto c = db.createCollection("c");
        c.insert({{"a", 1}});
        c.insert({{"a", 2}});
        CHECK_THROWS_AS(c.loadJson(R"([{"b":1}, 42, {"b":2}])"), JsonError);
        CHECK(c.size() == 2); // alter Stand intact
        CHECK(c.findById(DocumentId(1)).has_value());
        // gueltiges JSON geht danach normal
        c.loadJson(R"([{"b": 9}])");
        CHECK(c.size() == 1);
        CHECK(c.count("b == 9") == 1);
    }

    // 5) Database::fromJson mit kaputtem Dokument in einer Collection -> JsonError,
    //    Database bleibt unveraendert (keine neue/halbgeladene Collection)
    {
        Database db("fj");
        db.createCollection("alt").insert({{"x", 1}});
        std::string bad = R"({"collections": {"neu": [{"ok":1}, "kaputt"], "alt": [{"x":1}]}})";
        CHECK_THROWS_AS(db.fromJson(bad), JsonError);
        CHECK(!db.hasCollection("neu")); // wurde nicht halb angelegt
        CHECK(db.collection("alt").size() == 1); // alter Stand intact
        // gueltiges fromJson danach ok
        db.fromJson(R"({"collections": {"neu": [{"ok": 5}]}})");
        CHECK(db.collection("neu").count("ok == 5") == 1);
    }

    // 6) Echte Fehler werfen weiterhin (Policy: nur Flow-Schutz, keine Fehler-Verheimlichung)
    {
        Database db("err");
        auto c = db.createCollection("c");
        c.createIndex("email", /*unique=*/true);
        c.insert({{"email", "a@b.c"}});
        CHECK_THROWS_AS(c.insert({{"email", "a@b.c"}}), UniqueConstraintError); // Unique-Verstoss
        CHECK_THROWS_AS(db.createCollection("c"), CollectionExistsError);        // Doppelanlage
        CHECK_THROWS_AS(db.collection("gibtsnicht"), CollectionNotFoundError);     // fehlende Collection
        CHECK(!c.findById(DocumentId(999)).has_value());                          // findById -> optional, wirft nicht
        { ReferenceIndex refs(db); CHECK_THROWS_AS(refs.link("c", DocumentId(999), "c", DocumentId(1), "r"), DocumentNotFoundError); } // fehlendes Doc
        CHECK_THROWS_AS(c.loadJson("{kein array}"), JsonError);                    // kaputtes JSON
        Database db2("enc");
        db2.createCollection("c").insert({{"a", 1}});
        db2.saveToFileEncrypted("/tmp/eh_test.enc", "pw");
        CHECK_THROWS_AS(Database::loadFromFileEncrypted("/tmp/eh_test.enc", "falsch"), CryptoError);
        CHECK_THROWS_AS(Database::loadFromFile("/tmp/eh_test_das_gibt_es_nicht.json"), Error);
    }

    // 7) Expression::evaluate (explizite Einzelstelle) bleibt throwend – nur Scans fangen ab
    {
        Database db("ev");
        auto c = db.createCollection("c");
        c.insert({{"v", 1}});
        QueryFunctionRegistry::instance().registerFunction(
            "boom2", [](Expression f, const std::vector<Value>&) -> Expression {
                return f.predicate([](const Value&) -> bool { throw std::runtime_error("x"); });
            });
        auto expr = parseQuery("v.boom2()");
        auto doc = *c.findById(DocumentId(1));
        CHECK_THROWS_AS(expr.evaluate(doc), std::runtime_error); // explizit: throw
        QueryFunctionRegistry::instance().unregisterFunction("boom2");
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
