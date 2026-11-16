// Tests: Discovery v0.33 – automatische Gesamtanalyse (analyze) + Präsentation
// (toJson / toMarkdown / toMermaid / printSummary) + Health-Score.
#include <memdb/memdb.hpp>

#include <iostream>
#include <sstream>
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

} // namespace

int main() {
    Database db("rep");
    auto users = db.createCollection("users");
    users.insert({{"id", 1}, {"name", "Ada"}});
    users.insert({{"id", 2}, {"name", "Bob"}});
    auto orders = db.createCollection("orders");
    orders.insert({{"id", 10}, {"user_id", 1}, {"total", 5.0}});
    orders.insert({{"id", 11}, {"user_id", 2}, {"total", 7.0}});
    orders.insert({{"id", 12}, {"user_id", 999}, {"total", 1.0}}); // Orphan

    DiscoveryOptions opts;
    opts.minSupport = 0.0;
    opts.minCorrelation = 0.99; // Korrelationen in diesem Mini-Set unterdrücken
    auto rep = analyze(db, opts);

    // 1) Struktur des Reports
    CHECK(rep.totalCollections == 2);
    CHECK(rep.totalDocs == 5);
    CHECK(rep.profiles.size() == 2);
    bool fkFound = false;
    for (const auto& r : rep.relations)
        if (r.kind == RelationKind::ForeignKey && r.field == "user_id")
            fkFound = true;
    CHECK(fkFound);
    CHECK(!rep.orphans.empty());
    CHECK(rep.healthScore >= 0.0 && rep.healthScore <= 100.0);
    // Orphan senkt Integrität -> Score < 100
    CHECK(rep.healthScore < 100.0);

    // 2) Präsentationen
    std::string j = toJson(rep);
    CHECK(j.find("\"healthScore\"") != std::string::npos);
    CHECK(j.find("\"relations\"") != std::string::npos);
    CHECK(j.find("\"profiles\"") != std::string::npos);
    CHECK(j.find("\"orphans\"") != std::string::npos);

    std::string md = toMarkdown(rep);
    CHECK(md.find("# InMemDB Analyse-Report") != std::string::npos);
    CHECK(md.find("Health-Score") != std::string::npos);
    CHECK(md.find("## Entdeckte Beziehungen") != std::string::npos);
    CHECK(md.find("orders.user_id") != std::string::npos);
    CHECK(md.find("## Empfehlungen") != std::string::npos);

    std::string mm = toMermaid(rep);
    CHECK(mm.find("graph LR") != std::string::npos);
    CHECK(mm.find("-->") != std::string::npos);

    std::ostringstream ss;
    printSummary(rep, ss);
    CHECK(ss.str().find("Health") != std::string::npos);
    CHECK(ss.str().find("orders.user_id -> users.id") != std::string::npos);

    // 3) analyze() über vector<Collection> (cross-database) geht auch
    {
        Database db2("other");
        auto cust = db2.createCollection("customers");
        cust.insert({{"cid", 1}, {"user_ref", 1}});
        cust.insert({{"cid", 2}, {"user_ref", 2}});
        auto rep2 = analyze({users, orders, cust}, opts);
        CHECK(rep2.totalCollections == 3);
        bool crossFk = false;
        for (const auto& r : rep2.relations)
            if (r.kind == RelationKind::ForeignKey && r.fromCollection == "customers" && r.toCollection == "users")
                crossFk = true;
        CHECK(crossFk);
    }

    // 4) Health-Score: saubere DB (ohne Orphans) = höher
    {
        Database clean("c");
        auto u = clean.createCollection("u");
        u.insert({{"id", 1}, {"name", "a"}});
        u.insert({{"id", 2}, {"name", "b"}});
        auto repC = analyze(clean, opts);
        CHECK(repC.healthScore > rep.healthScore);
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
