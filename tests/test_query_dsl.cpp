// Tests: String-DSL (NFA-03) – parseQuery, Function-Registry, @Referenzen (FA-18), Mini-RegEx.
#include <memdb/memdb.hpp>

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
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": erwartete " #ExType        \
                         " von " #expr "\n";                                                     \
        }                                                                                        \
    } while (0)

using namespace memdb;

} // namespace

int main() {
    Database db("dsl-test");
    Collection users = db.createCollection("users");
    users.createIndex("name");
    users.insert({{"name", "Ada"}, {"age", 36}, {"stadt", "Berlin"}, {"active", true},
                  {"tags", std::vector<Value>{Value("cpp"), Value("db")}},
                  {"adresse", Value(std::make_shared<Document>(Document{{"stadt", "Berlin"}}))},
                  {"score", 9.5}});
    users.insert({{"name", "Bob"}, {"age", 17}, {"stadt", "Hamburg"},
                  {"tags", std::vector<Value>{Value("db")}}, {"score", 7}});
    users.insert({{"name", "Cy"}, {"age", 40}, {"stadt", "Berlin"}, {"active", false},
                  {"score", 8.0}});
    users.insert({{"name", "O'Brien"}, {"age", 22}, {"stadt", "Köln"}});

    // --- Grundvergleiche + Logik + Präzedenz ---
    CHECK(users.count("age >= 18 && stadt == 'Berlin'") == 2);          // Ada, Cy
    CHECK(users.count("age < 18 || age >= 40 && active == false") == 2); // Bob || (Cy) -> Bob, Cy
    CHECK(users.count("!(age < 18)") == 3);                              // Ada, Cy, O'Brien
    CHECK(users.count("(age < 18 || age >= 40) && stadt == 'Berlin'") == 1); // nur Cy
    CHECK(users.count("age != 36 && stadt != 'Berlin'") == 2);           // Bob, O'Brien
    CHECK(users.count("age = 36") == 1);                                 // '=' als Alias für '=='
    CHECK(users.count("score > 9.0") == 1);                              // Ada (9.5)
    CHECK(users.count("score >= 7") == 3);                               // 9.5, 7, 8.0
    CHECK(users.count("name == 'O\\'Brien'") == 1);                      // Escape in DSL-String
    CHECK(users.count("active") == 2);                                   // nackter Pfad = exists()
    CHECK(users.count("missing == null") == 0);                          // fehlendes Feld: Eq -> false
    CHECK(users.count("missing != null") == 4);                          // fehlendes Feld: Ne -> true
    CHECK(users.count("adresse.stadt == 'Berlin'") == 1);                // Punktnotation
    CHECK(users.count("age >= 18 AND stadt == 'Köln'") == 1);             // Keyword-AND

    // --- eingebaute Function-Operatoren ---
    CHECK(users.count("tags.contains('db')") == 2);                      // Ada, Bob
    CHECK(users.count("name.matches('A.*')") == 1);                      // Ada
    CHECK(users.count("name.matches('[BC].*')") == 2);                   // Bob, Cy
    CHECK(users.count("name.matches('^(Ada|Bob)$')") == 2);              // Gruppe + Anker + Alt
    CHECK(users.count("name.matches('^[A-Z][a-z]+$')") == 3);             // Ada, Bob, Cy (nicht O'Brien)
    CHECK(users.count("stadt.matches('^[BK].*')") == 3);                 // Berlin(Ada,Cy), Koeln(O'Brien); Full-Match!
    CHECK(users.count("age.in(36, 40, 99)") == 2);                       // Ada, Cy
    CHECK(users.count("name.contains('a')") == 1);                       // nur Ada (case-sensitive)

    // --- Index-Hinweis aus DSL ---
    users.count("name == 'Ada'");
    CHECK(users.lastQueryCandidateCount() == 1);                         // Index auf name genutzt

    // --- parseQuery == C++-API ---
    {
        Document d{{"age", 36}};
        CHECK(parseQuery("age >= 18").evaluate(d) == (Field("age") >= 18).evaluate(d));
        CHECK(parseQuery("age >= 18 && age < 40").evaluate(d));
    }

    // --- eigene Function-Operatoren registrieren (NFA-03) ---
    {
        auto& reg = QueryFunctionRegistry::instance();
        CHECK(reg.hasFunction("matches"));
        CHECK(reg.hasFunction("in"));
        reg.registerFunction("even", [](Expression f, const std::vector<Value>& args) {
            if (!args.empty())
                throw QueryError("even() erwartet keine Argumente");
            return f.predicate([](const Value& v) { return v.isInt() && v.asInt() % 2 == 0; });
        });
        CHECK(reg.hasFunction("even"));
        CHECK(users.count("age.even()") == 3);                           // Ada(36), Cy(40), O'Brien(22)
        reg.unregisterFunction("even");
        CHECK(!reg.hasFunction("even"));
        CHECK_THROWS_AS(users.count("age.even()"), QueryError);          // unbekannt nach unregister
    }

    // --- findOne / findIds / forEach / update / remove per DSL ---
    {
        auto one = users.findOne("name == 'Bob'");
        CHECK(one.has_value() && one->at("age").asInt() == 17);
        CHECK(users.findIds("age >= 36").size() == 2);
        std::size_t n = 0;
        users.forEach("stadt == 'Berlin'", [&](const Document&) { ++n; });
        CHECK(n == 2);
        CHECK(users.update("age < 18", [](Document& d) { d.set("active", true); }) == 1);
        CHECK(users.findOne("name == 'Bob'")->at("active").asBool() == true);
        CHECK(users.remove("stadt == 'Hamburg'") == 1);
        CHECK(users.size() == 3);
    }

    // --- Syntaxfehler ---
    CHECK_THROWS_AS(parseQuery("age >> 3"), QueryError);
    CHECK_THROWS_AS(parseQuery("age == "), QueryError);
    CHECK_THROWS_AS(parseQuery("(age == 3"), QueryError);
    CHECK_THROWS_AS(parseQuery("x.unknown()"), QueryError);
    CHECK_THROWS_AS(parseQuery("@kunde.name == 'Ada'"), QueryError);     // ohne db-Kontext
    CHECK_THROWS_AS(users.find("name == 'Ada") , QueryError);            // ungeschlossener String

    // --- Transaktion mit DSL ---
    {
        // zu diesem Zeitpunkt: Ada(36), Cy(40), O'Brien(22) – Bob wurde oben entfernt
        auto tx = users.beginTransaction();
        CHECK(tx.count("age >= 18") == 3);
        tx.insert({{"name", "Tx"}, {"age", 50}});
        CHECK(tx.count("age >= 18") == 4);                               // read-your-own-writes
        tx.remove("name == 'Tx'");
        tx.commit();
        CHECK(users.count("age >= 18") == 3);
    }

    // --- @Referenz-Join über FA-18 ---
    {
        Collection orders = db.createCollection("orders");
        DocumentId o1 = orders.insert({{"total", 100}, {"note", "klein"}});
        DocumentId o2 = orders.insert({{"total", 500}, {"note", "gross"}});
        DocumentId ada = users.findOne("name == 'Ada'")->id();
        ReferenceIndex refs(db);
        refs.link("orders", o1, "users", ada, "kunde");
        refs.link("orders", o2, "users", ada, "kunde");
        DocumentId zoe = users.insert({{"name", "Zoe"}, {"stadt", "Berlin"}, {"age", 30}});
        refs.unlink("orders", o2, "users", ada, "kunde");
        refs.link("orders", o2, "users", zoe, "kunde");

        CHECK(orders.count("@kunde.name == 'Ada'", db) == 1);            // nur o1
        CHECK(orders.count("@kunde.stadt == 'Berlin'", db) == 2);        // Ada + Zoe
        CHECK(orders.count("@kunde.name == 'Ada' && total >= 200", db) == 0);
        CHECK(orders.count("@kunde.name == 'Zoe' && total > 100", db) == 1); // o2
        auto hit = orders.findOne("@kunde.name == 'Zoe'", db);
        CHECK(hit.has_value() && hit->at("total").asInt() == 500);
        // ohne Referenz: verhält sich wie fehlendes Feld
        Collection lonely = db.createCollection("lonely");
        lonely.insert({{"x", 1}});
        CHECK(lonely.count("@kunde.name == 'Ada'", db) == 0);
        CHECK(lonely.count("@kunde.name != 'Ada'", db) == 1);            // fehlendes Feld: != -> true
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
