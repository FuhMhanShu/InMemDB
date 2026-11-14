// Tests: index-beschleunigter Join bei Abfrage – ReferenceIndex::joinIds/join (v0.17).
#include <memdb/memdb.hpp>

#include <algorithm>
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
    Database db("join-test");
    Collection users = db.createCollection("users");
    users.createIndex("stadt"); // Index auf der Ziel-Seite -> Join nutzt ihn
    DocumentId ada = users.insert({{"name", "Ada"}, {"stadt", "Berlin"}});
    DocumentId bob = users.insert({{"name", "Bob"}, {"stadt", "Hamburg"}});
    DocumentId cy = users.insert({{"name", "Cy"}, {"stadt", "Berlin"}});

    Collection orders = db.createCollection("orders");
    DocumentId o1 = orders.insert({{"total", 100}});
    DocumentId o2 = orders.insert({{"total", 500}});
    DocumentId o3 = orders.insert({{"total", 50}});

    ReferenceIndex refs(db);
    refs.link("orders", o1, "users", ada, "kunde");
    refs.link("orders", o2, "users", cy, "kunde");
    refs.link("orders", o3, "users", bob, "kunde");
    refs.link("orders", o2, "users", bob, "reviewer"); // andere Relation

    // Join: alle Bestellungen, deren Kunde in Berlin wohnt -> o1, o2
    auto ids = refs.joinIds("orders", "kunde", "users", Field("stadt") == "Berlin");
    CHECK(ids.size() == 2);
    CHECK(std::find(ids.begin(), ids.end(), o1) != ids.end());
    CHECK(std::find(ids.begin(), ids.end(), o2) != ids.end());
    CHECK(users.lastQueryCandidateCount() == 2); // Index auf stadt wurde genutzt (2 Kandidaten)

    // Join ueber DSL-Query auf der Ziel-Seite
    auto ids2 = refs.joinIds("orders", "kunde", "users", parseQuery("stadt == 'Berlin' && name != 'Cy'"));
    CHECK(ids2.size() == 1 && ids2[0] == o1);

    // Join ohne Treffer
    CHECK(refs.joinIds("orders", "kunde", "users", Field("stadt") == "München").empty());
    // andere Relation filtert korrekt
    CHECK(refs.joinIds("orders", "reviewer", "users", Expression{}).size() == 1);
    // join() liefert Dokumente
    auto docs = refs.join("orders", "kunde", "users", Field("name") == "Bob");
    CHECK(docs.size() == 1 && docs[0].at("total").asInt() == 50);
    // unbekannte Collection -> Fehler
    CHECK_THROWS_AS(refs.joinIds("orders", "kunde", "gibtsnicht", Expression{}), CollectionNotFoundError);

    // dangling: Ziel-Dokument entfernen -> Join liefert nichts mehr fuer diese Kante
    users.removeById(cy);
    CHECK(refs.joinIds("orders", "kunde", "users", Field("name") == "Cy").empty());

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
