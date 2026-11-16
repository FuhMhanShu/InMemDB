// Tests: Discovery-Erweiterungen v0.30 – Kardinalität (1:1/n:1/1:n/n:m), Orphan-Report
// (findOrphans / findOrphansForRelations), Pfad-Suche findPaths (A->B->C), toJson mit cardinality.
#include <memdb/memdb.hpp>

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

using namespace memdb;

const DiscoveredRelation* findFk(const std::vector<DiscoveredRelation>& rels, const std::string& from,
                                 const std::string& field, const std::string& to, const std::string& toField) {
    for (const auto& r : rels)
        if (r.kind == RelationKind::ForeignKey && r.fromCollection == from && r.field == field &&
            r.toCollection == to && r.toField == toField)
            return &r;
    return nullptr;
}

} // namespace

int main() {
    // 1) Kardinalität: users.id unique -> orders.user_id n:1; orders.id unique -> 1:n-Richtung
    {
        Database db("card");
        auto users = db.createCollection("users");
        users.insert({{"id", 1}, {"name", "Ada"}});
        users.insert({{"id", 2}, {"name", "Bob"}});
        auto orders = db.createCollection("orders");
        orders.insert({{"id", 10}, {"user_id", 1}});
        orders.insert({{"id", 11}, {"user_id", 1}}); // user 1 hat 2 orders
        orders.insert({{"id", 12}, {"user_id", 2}});
        DiscoveryOptions opts;
        opts.minSupport = 0.0;
        auto rels = discoverForeignKeys({users, orders}, opts);
        const auto* r = findFk(rels, "orders", "user_id", "users", "id");
        CHECK(r != nullptr);
        if (r)
            CHECK(r->cardinality == "n:1"); // users.id eindeutig -> viele orders zu einem user
        // toJson enthält cardinality
        std::string j = toJson(rels);
        CHECK(j.find("cardinality") != std::string::npos);
        CHECK(j.find("n:1") != std::string::npos);
    }

    // 2) 1:1-Kardinalität: profils.user_id unique UND users.id unique
    {
        Database db("card2");
        auto users = db.createCollection("users");
        users.insert({{"id", 1}});
        users.insert({{"id", 2}});
        auto prof = db.createCollection("profiles");
        prof.insert({{"pid", 100}, {"user_id", 1}});
        prof.insert({{"pid", 101}, {"user_id", 2}});
        DiscoveryOptions opts;
        opts.minSupport = 0.0;
        auto rels = discoverForeignKeys({users, prof}, opts);
        const auto* r = findFk(rels, "profiles", "user_id", "users", "id");
        CHECK(r != nullptr);
        if (r)
            CHECK(r->cardinality == "1:1"); // beide Seiten eindeutig
    }

    // 3) Orphan-Report: orders.user_id=999 hat kein users.id=999
    {
        Database db("orph");
        auto users = db.createCollection("users");
        users.insert({{"id", 1}});
        users.insert({{"id", 2}});
        auto orders = db.createCollection("orders");
        orders.insert({{"id", 10}, {"user_id", 1}});
        orders.insert({{"id", 11}, {"user_id", 2}});
        orders.insert({{"id", 12}, {"user_id", 999}});
        orders.insert({{"id", 13}, {"user_id", 998}});
        DiscoveryOptions opts;
        opts.minSupport = 0.0;
        auto rels = discoverForeignKeys({users, orders}, opts);
        auto reps = findOrphansForRelations({users, orders}, rels, opts);
        CHECK(reps.size() >= 1);
        bool found = false;
        for (const auto& rep : reps) {
            if (rep.fromCollection == "orders" && rep.field == "user_id") {
                found = true;
                CHECK(rep.scanned == 4);
                CHECK(rep.orphans == 2); // 999 und 998
                CHECK(rep.orphanValues.size() == 2);
            }
        }
        CHECK(found);
        // direkt: findOrphans
        auto rep = findOrphans(orders, "user_id", users, "id", opts);
        CHECK(rep.orphans == 2);
        // Feld existiert nicht -> leerer Report
        auto rep2 = findOrphans(orders, "gibtsnicht", users, "id", opts);
        CHECK(rep2.orphans == 0 && rep2.scanned == 0);
    }

    // 4) Pfad-Suche: users <- orders <- items  (Kette orders -> users, items -> orders)
    {
        Database db("paths");
        auto users = db.createCollection("users");
        users.insert({{"id", 1}});
        users.insert({{"id", 2}});
        auto orders = db.createCollection("orders");
        orders.insert({{"id", 10}, {"user_id", 1}});
        orders.insert({{"id", 11}, {"user_id", 2}});
        auto items = db.createCollection("items");
        items.insert({{"iid", 100}, {"order_id", 10}});
        items.insert({{"iid", 101}, {"order_id", 11}});
        DiscoveryOptions opts;
        opts.minSupport = 0.0;
        auto rels = discoverForeignKeys({users, orders, items}, opts);
        // Pfad items -> users über orders
        auto paths = findPaths(rels, "items", "users", 4);
        CHECK(paths.size() >= 1);
        bool chainOk = false;
        for (const auto& p : paths) {
            if (p.size() == 2 && p[0].fromCollection == "items" && p[0].toCollection == "orders" &&
                p[1].fromCollection == "orders" && p[1].toCollection == "users")
                chainOk = true;
        }
        CHECK(chainOk);
        // kein Pfad zurück (users -> items existiert nicht)
        CHECK(findPaths(rels, "users", "items", 4).empty());
        // maxDepth=1 findet keinen 2-Hop-Pfad
        CHECK(findPaths(rels, "items", "users", 1).empty());
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
