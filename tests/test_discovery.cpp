// Tests: Discovery-Modul (v0.29) – FK-Discovery (auch database-übergreifend), funktionale
// Abhängigkeiten, Korrelationen (Pearson + kategorial), Near-Duplicates, applyDiscovered,
// toJson/toMermaid, Sampling.
#include <memdb/memdb.hpp>
#include <memdb/references.hpp>

#include <cmath>
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

bool hasRel(const std::vector<DiscoveredRelation>& rels, RelationKind k, const std::string& from,
            const std::string& field, const std::string& to, const std::string& toField) {
    for (const auto& r : rels)
        if (r.kind == k && r.fromCollection == from && r.field == field && r.toCollection == to &&
            r.toField == toField)
            return true;
    return false;
}

} // namespace

int main() {
    // 1) FK-Discovery innerhalb einer Database: orders.user_id -> users.id
    {
        Database db("shop");
        auto users = db.createCollection("users");
        users.createIndex("id");
        users.insert({{"id", 1}, {"name", "Ada"}});
        users.insert({{"id", 2}, {"name", "Bob"}});
        users.insert({{"id", 3}, {"name", "Cy"}});
        auto orders = db.createCollection("orders");
        orders.insert({{"id", 10}, {"user_id", 1}, {"total", 5.0}});
        orders.insert({{"id", 11}, {"user_id", 2}, {"total", 7.5}});
        orders.insert({{"id", 12}, {"user_id", 3}, {"total", 1.0}});
        orders.insert({{"id", 13}, {"total", 9.0}}); // ohne user_id

        DiscoveryOptions opts;
        opts.minSupport = 0.0; // support-Regel hier aus, damit auch 3/4 greift
        auto rels = discoverForeignKeys({users, orders}, opts);
        CHECK(hasRel(rels, RelationKind::ForeignKey, "orders", "user_id", "users", "id"));
        // Gegenrichtung darf nicht gemeldet werden (users.id-Werte 1..3 sind nicht alle in orders.user_id)
        CHECK(!hasRel(rels, RelationKind::ForeignKey, "users", "id", "orders", "user_id"));
        // unrelated field (total) nicht als FK
        CHECK(!hasRel(rels, RelationKind::ForeignKey, "orders", "total", "users", "id"));
        for (const auto& r : rels)
            if (r.field == "user_id")
                CHECK(r.confidence == 1.0);
    }

    // 2) FK-Discovery database-übergreifend: users in DB1, orders in DB2
    {
        Database db1("a"), db2("b");
        auto users = db1.createCollection("users");
        for (int i = 1; i <= 5; ++i)
            users.insert({{"id", i}, {"name", "u" + std::to_string(i)}});
        auto orders = db2.createCollection("orders");
        orders.insert({{"oid", 100}, {"user_id", 2}});
        orders.insert({{"oid", 101}, {"user_id", 4}});
        DiscoveryOptions opts;
        opts.minSupport = 0.0;
        auto rels = discoverForeignKeys({users, orders}, opts); // verschiedene Databases!
        CHECK(hasRel(rels, RelationKind::ForeignKey, "orders", "user_id", "users", "id"));
    }

    // 3) Funktionale Abhängigkeit: plz -> stadt
    {
        Database db("fd");
        auto c = db.createCollection("addr");
        c.insert({{"plz", "10115"}, {"stadt", "Berlin"}, {"nr", 1}});
        c.insert({{"plz", "10115"}, {"stadt", "Berlin"}, {"nr", 2}});
        c.insert({{"plz", "80331"}, {"stadt", "Muenchen"}, {"nr", 3}});
        c.insert({{"plz", "20095"}, {"stadt", "Hamburg"}, {"nr", 4}});
        DiscoveryOptions opts;
        opts.minSupport = 0.0;
        auto rels = discoverFunctionalDependencies(c, opts);
        CHECK(hasRel(rels, RelationKind::FunctionalDependency, "addr", "plz", "addr", "stadt"));
        // Gegenrichtung ist keine FD (Berlin hat 1 plz, aber stadt->plz: Hamburg 20095 ok – hier je 1, also auch FD? nein: jede stadt hat genau 1 plz -> waere auch FD. nr -> nichts)
        // nr ist eindeutig -> triviale FD (mit Vermerk), plz->stadt ist die echte FD
        bool nrTrivial = false;
        for (const auto& r : rels)
            if (r.kind == RelationKind::FunctionalDependency && r.field == "nr" && r.toField == "stadt")
                nrTrivial = r.note.find("trivial") != std::string::npos;
        CHECK(nrTrivial);
    }

    // 4) Korrelation: x und y=2x (Pearson r=1); unabhängiges Feld nicht gemeldet
    {
        Database db("corr");
        auto c = db.createCollection("m");
        for (int i = 1; i <= 30; ++i)
            c.insert({{"x", i}, {"y", 2.0 * i}, {"z", (i * 7919) % 13}}); // z pseudo-zufällig
        DiscoveryOptions opts;
        opts.minCorrelation = 0.9;
        auto rels = discoverCorrelations(c, opts);
        CHECK(hasRel(rels, RelationKind::Correlation, "m", "x", "m", "y"));
        CHECK(!hasRel(rels, RelationKind::Correlation, "m", "x", "m", "z"));
    }

    // 5) kategoriale Assoziation: farbe -> kategorie (U=1)
    {
        Database db("cat");
        auto c = db.createCollection("p");
        for (int i = 0; i < 40; ++i) {
            c.insert({{"farbe", "rot"}, {"kategorie", "teuer"}, {"preis", 100 + i}});
            c.insert({{"farbe", "blau"}, {"kategorie", "billig"}, {"preis", i}});
        }
        DiscoveryOptions opts;
        opts.minCorrelation = 0.9;
        auto rels = discoverCorrelations(c, opts);
        bool found = false;
        for (const auto& r : rels)
            if (r.kind == RelationKind::Correlation && r.field == "farbe" && r.toField == "kategorie" &&
                r.note.find("kategorial") != std::string::npos)
                found = true;
        CHECK(found);
    }

    // 6) Near-Duplicates über zwei Collections hinweg
    {
        Database db1("d1"), db2("d2");
        auto a = db1.createCollection("names");
        a.insert({{"name", "Schmidt"}});
        a.insert({{"name", "Mueller"}});
        auto b = db2.createCollection("names"); // gleiche Collection in anderer DB
        b.insert({{"name", "Schmidtt"}});       // ~ Schmidt (sim 0.92)
        b.insert({{"name", "Meier"}});
        DiscoveryOptions opts;
        opts.minSimilarity = 0.85;
        auto rels = discoverNearDuplicates({a, b}, opts);
        CHECK(hasRel(rels, RelationKind::NearDuplicate, "names", "name", "names", "name"));
        bool schmidt = false;
        for (const auto& r : rels)
            if (r.kind == RelationKind::NearDuplicate && r.note.find("schmidt") != std::string::npos)
                schmidt = true;
        CHECK(schmidt);
    }

    // 7) applyDiscovered legt FA-18-Referenzen an; joinIds funktioniert danach
    {
        Database db("ap");
        auto users = db.createCollection("users");
        users.insert({{"id", 1}, {"name", "Ada"}});
        users.insert({{"id", 2}, {"name", "Bob"}});
        auto orders = db.createCollection("orders");
        orders.insert({{"user_id", 1}});
        orders.insert({{"user_id", 2}});
        DiscoveryOptions opts;
        opts.minSupport = 0.0;
        opts.minConfidence = 0.5;
        auto rels = discoverRelations(db, opts);
        ReferenceIndex refs(db);
        std::size_t n = applyDiscovered(db, rels, refs, 0.5);
        CHECK(n == 2);
        auto j = refs.joinIds("orders", "discovered_user_id", "users");
        CHECK(j.size() == 2);
        CHECK(refs.dangling().empty());
    }

    // 8) toJson / toMermaid
    {
        Database db("rep");
        auto a = db.createCollection("a");
        a.insert({{"id", 1}});
        a.insert({{"id", 2}});
        auto b = db.createCollection("b");
        b.insert({{"a_id", 1}});
        b.insert({{"a_id", 2}});
        DiscoveryOptions opts;
        opts.minSupport = 0.0;
        auto rels = discoverForeignKeys({a, b}, opts);
        std::string j = toJson(rels);
        CHECK(j.find("\"relations\"") != std::string::npos);
        CHECK(j.find("foreign_key") != std::string::npos);
        CHECK(j.find("a.id") != std::string::npos && j.find("b.a_id") != std::string::npos);
        std::string m = toMermaid(rels);
        CHECK(m.find("graph LR") != std::string::npos);
        CHECK(m.find("-->") != std::string::npos);
    }

    // 9) Sampling: reproduzierbar mit gleichem Seed, sampleSize begrenzt
    {
        Database db("s");
        auto c = db.createCollection("big");
        for (int i = 0; i < 500; ++i)
            c.insert({{"k", i % 50}, {"v", i}});
        DiscoveryOptions o1, o2;
        o1.sampleSize = 100;
        o1.seed = 7;
        o2 = o1;
        auto r1 = discoverFunctionalDependencies(c, o1);
        auto r2 = discoverFunctionalDependencies(c, o2);
        CHECK(r1.size() == r2.size()); // gleicher seed -> gleiches Ergebnis
        // k -> v ist keine FD (v variiert), aber k eindeutig pro Gruppe? nein; egal: nur Konsistenz prüfen
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
