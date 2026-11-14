// Tests: Bulk-Update via DSL mit Projektion (v0.21) - update(queryDsl, setDsl) auf
// Collection und Transaction; Set-DSL mit Literalen, Feld-Referenzen, Arithmetik,
// String-Konkatenation, gepunkteten Pfaden (Document::setPath).
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
    // --- 1) Basis: Literal-Zuweisungen gefiltert per Query-DSL ---
    {
        Database db("dsl-update");
        Collection users = db.createCollection("users");
        users.insert({{"name", "Ada"}, {"age", 36}});
        users.insert({{"name", "Bob"}, {"age", 17}});
        users.insert({{"name", "Cy"}, {"age", 40}});

        std::size_t n = users.update("age >= 18", "adult = true, status = 'adult'");
        CHECK(n == 2);
        CHECK(users.count("adult == true") == 2);
        CHECK(users.count("status == 'adult'") == 2);
        CHECK(users.count("status") == 2); // Bob nicht angefasst
        auto bob = users.findOne("name == 'Bob'");
        CHECK(bob.has_value() && !bob->contains("adult"));
    }

    // --- 2) Arithmetik + Feld-Referenz + String-Konkatenation + Precedence ---
    {
        Database db("dsl-update-arith");
        Collection p = db.createCollection("p");
        p.insert({{"name", "Ada"}, {"vorname", "Ada"}, {"nachname", "L."}, {"age", 36}, {"price", 10}, {"qty", 3}});
        p.insert({{"name", "Bob"}, {"vorname", "Bob"}, {"nachname", "B."}, {"age", 17}, {"price", 2.5}, {"qty", 4}});

        CHECK(p.update("age >= 18", "age = age + 1, total = price * qty, full = vorname + ' ' + nachname") == 1);
        auto ada = p.findOne("vorname == 'Ada'");
        CHECK(ada->at("age").asInt() == 37);
        CHECK(ada->at("total").asInt() == 30);          // 10 * 3, int bleibt int
        CHECK(ada->at("full").asString() == "Ada L.");
        auto bob = p.findOne("vorname == 'Bob'");
        CHECK(bob->at("age").asInt() == 17);            // nicht geaendert
        CHECK(!bob->contains("total"));

        // double-Arithmetik, / und %, unariaeres Minus, Klammern
        CHECK(p.update("vorname == 'Bob'", "price = price * 2, half = price / 2, rest = qty % 3, delta = 5 - age") == 1);
        bob = p.findOne("vorname == 'Bob'");
        CHECK(bob->at("price").asDouble() == 5.0);
        CHECK(bob->at("half").asDouble() == 1.25); // Snapshot: price/2 mit ALTEM price (2.5), nicht neuem (5.0)
        CHECK(bob->at("rest").asInt() == 1);
        CHECK(bob->at("delta").asInt() == -12);
        CHECK(p.update("vorname == 'Ada'", "neg = -age, paren = (age + 1) * 2") == 1);
        ada = p.findOne("vorname == 'Ada'");
        CHECK(ada->at("neg").asInt() == -37);
        CHECK(ada->at("paren").asInt() == 76);
    }

    // --- 3) Projektion: Felder kopieren/umbenennen, fehlende Felder -> null ---
    {
        Database db("dsl-update-proj");
        Collection c = db.createCollection("c");
        c.insert({{"a", 1}, {"b", 2}});
        c.insert({{"a", 10}});
        CHECK(c.update("", "copyA = a, b2 = b, missing = nichtda") == 2); // leere Query = alle
        auto d1 = c.findOne("a == 1");
        CHECK(d1->at("copyA").asInt() == 1 && d1->at("b2").asInt() == 2);
        CHECK(d1->at("missing").isNull());
        auto d2 = c.findOne("a == 10");
        CHECK(d2->at("b2").isNull());
    }

    // --- 4) Gepunktete Ziel-Pfade (Document::setPath) ---
    {
        Database db("dsl-update-path");
        Collection c = db.createCollection("c");
        c.insert({{"name", "Ada"}, {"adresse", Value(std::make_shared<Document>(Document{{"stadt", "Berlin"}, {"plz", "10115"}}))}});
        c.insert({{"name", "Bob"}}); // kein adresse-Feld
        CHECK(c.update("name == 'Ada'", "adresse.plz = '10999', adresse.land = 'DE'") == 1);
        auto ada = c.findOne("name == 'Ada'");
        CHECK(ada->findPath("adresse.plz")->asString() == "10999");
        CHECK(ada->findPath("adresse.stadt")->asString() == "Berlin"); // bestehendes Feld bleibt
        CHECK(ada->findPath("adresse.land")->asString() == "DE");       // neues Unterfeld
        CHECK(c.update("name == 'Bob'", "adresse.stadt = 'Hamburg'") == 1); // Unterdokument wird angelegt
        auto bob = c.findOne("name == 'Bob'");
        CHECK(bob->findPath("adresse.stadt")->asString() == "Hamburg");
    }

    // --- 5) Snapshot-Semantik innerhalb eines Docs: rechte Seite sieht alte Werte ---
    {
        Database db("dsl-update-snap");
        Collection c = db.createCollection("c");
        c.insert({{"a", 1}, {"b", 2}});
        CHECK(c.update("", "a = b, b = 100") == 1);
        auto d = c.findOne("");
        CHECK(d->at("a").asInt() == 2);   // alter b-Wert
        CHECK(d->at("b").asInt() == 100); // nicht a-neu
    }

    // --- 6) Transaktion: DSL-Bulk-Update commit + rollback ---
    {
        Database db("dsl-update-tx");
        Collection c = db.createCollection("c");
        c.insert({{"age", 20}});
        c.insert({{"age", 10}});
        {
            Transaction tx = c.beginTransaction();
            CHECK(tx.update("age >= 18", "age = age + 100, flag = 'tx'") == 1);
            tx.rollback();
        }
        CHECK(c.count("flag") == 0);
        CHECK(c.findOne("age == 20").has_value());
        {
            Transaction tx = c.beginTransaction();
            CHECK(tx.update("age >= 18", "age = age + 100") == 1);
            tx.commit();
        }
        CHECK(c.findOne("age == 120").has_value());
    }

    // --- 7) Query mit @relation + Set-DSL (db-Variante) ---
    {
        Database db("dsl-update-rel");
        Collection users = db.createCollection("users");
        Collection orders = db.createCollection("orders");
        DocumentId ada = users.insert({{"name", "Ada"}, {"age", 36}});
        DocumentId bob = users.insert({{"name", "Bob"}, {"age", 17}});
        DocumentId o1 = orders.insert({{"total", 100}});
        DocumentId o2 = orders.insert({{"total", 500}});
        ReferenceIndex refs(db);
        refs.link("orders", o1, "users", ada, "kunde");
        refs.link("orders", o2, "users", bob, "kunde");

        CHECK(orders.update("@kunde.age >= 18", "vip = true, note = 'erwachsen'", db) == 1);
        CHECK(orders.findById(o1)->at("vip").asBool() == true);
        CHECK(!orders.findById(o2)->contains("vip"));
        CHECK(orders.findById(o1)->at("note").asString() == "erwachsen");
    }

    // --- 8) Fehlerfaelle ---
    {
        Database db("dsl-update-err");
        Collection c = db.createCollection("c");
        c.insert({{"a", 1}});
        CHECK_THROWS_AS(c.update("a == 1", ""), QueryError);                 // leere Set-DSL
        CHECK_THROWS_AS(c.update("a == 1", "a ="), QueryError);               // fehlender Ausdruck
        CHECK_THROWS_AS(c.update("a == 1", "a 1"), QueryError);               // fehlendes '='
        CHECK_THROWS_AS(c.update("a == 1", "a = 1,"), QueryError);            // trailing comma
        CHECK_THROWS_AS(c.update("a == 1", "= 1"), QueryError);               // fehlender Pfad
        CHECK_THROWS_AS(c.update("a == 1", "a = 'x' + 1"), Error);             // Typfehler bei Auswertung
        CHECK_THROWS_AS(c.update("a == 1", "a = 1 / 0"), Error);              // Division durch 0
        CHECK_THROWS_AS(c.update("a == 1", "a = 1 % 0"), Error);              // Modulo durch 0
        CHECK_THROWS_AS(c.update("a ===", "a = 1"), QueryError);              // Query-Syntaxfehler
        CHECK(c.findOne("a == 1")->at("a").asInt() == 1);                     // nichts geaendert
        // unariaeres Minus in Query-Werten weiterhin ok
        CHECK(c.count("a == -1") == 0);
        c.insert({{"a", -5}});
        CHECK(c.count("a == -5") == 1);
    }

    // --- 9) Unique-Constraint greift auch beim DSL-Bulk-Update ---
    {
        Database db("dsl-update-unique");
        Collection c = db.createCollection("c");
        c.createIndex("email", true);
        c.insert({{"email", "a@x.de"}, {"age", 30}});
        c.insert({{"email", "b@x.de"}, {"age", 20}});
        CHECK_THROWS_AS(c.update("age >= 18", "email = 'a@x.de'"), UniqueConstraintError);
        CHECK(c.count() == 2); // Update atomar: nichts geaendert
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
