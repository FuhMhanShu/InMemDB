// Beispiel 14: String-DSL fuer Queries (NFA-03) – parseQuery, Function-Operatoren, @Referenzen.
#include <memdb/memdb.hpp>

#include <iostream>

using namespace memdb;

int main() {
    Database db("dsl-demo");
    Collection users = db.createCollection("users");
    users.createIndex("name");
    users.insert({{"name", "Ada"}, {"age", 36}, {"stadt", "Berlin"}, {"tags", std::vector<Value>{Value("cpp"), Value("db")}}});
    users.insert({{"name", "Bob"}, {"age", 17}, {"stadt", "Hamburg"}, {"tags", std::vector<Value>{Value("db")}}});
    users.insert({{"name", "Cy"}, {"age", 40}, {"stadt", "Berlin"}, {"active", false}});

    std::cout << "DSL: age >= 18 && stadt == 'Berlin' -> " << users.count("age >= 18 && stadt == 'Berlin'") << "\n";
    std::cout << "DSL: tags.contains('db')            -> " << users.count("tags.contains('db')") << "\n";
    std::cout << "DSL: name.matches('^[A-C].*')       -> " << users.count("name.matches('^[A-C].*')") << "\n";
    std::cout << "DSL: age.in(17, 40)                 -> " << users.count("age.in(17, 40)") << "\n";

    // eigener Function-Operator registrieren
    QueryFunctionRegistry::instance().registerFunction(
        "even", [](Expression f, const std::vector<Value>&) {
            return f.predicate([](const Value& v) { return v.isInt() && v.asInt() % 2 == 0; });
        });
    std::cout << "DSL: age.even() (eigener Operator)  -> " << users.count("age.even()") << "\n";

    // @Referenz-Join ueber FA-18: Bestellung -> Kunde
    Collection orders = db.createCollection("orders");
    DocumentId o1 = orders.insert({{"total", 100}});
    DocumentId o2 = orders.insert({{"total", 500}});
    DocumentId ada = users.findOne("name == 'Ada'")->id();
    ReferenceIndex refs(db);
    refs.link("orders", o1, "users", ada, "kunde");
    refs.link("orders", o2, "users", ada, "kunde");
    std::cout << "DSL: @kunde.stadt == 'Berlin'       -> " << orders.count("@kunde.stadt == 'Berlin'", db) << "\n";
    std::cout << "DSL: @kunde.name == 'Ada' && total >= 200 -> "
              << orders.count("@kunde.name == 'Ada' && total >= 200", db) << "\n";

    // Aequivalenz zur C++-API
    Expression dsl = parseQuery("age >= 18 && stadt == 'Berlin'");
    Expression cpp = (Field("age") >= 18) && (Field("stadt") == "Berlin");
    std::cout << "DSL == C++-API: " << (dsl.evaluate(*users.findOne("name == 'Ada'")) == cpp.evaluate(*users.findOne("name == 'Ada'")) ? "ja" : "nein") << "\n";
    return 0;
}
