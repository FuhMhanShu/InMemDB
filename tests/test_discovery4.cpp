// Tests: Discovery v0.32 – diffCollections/diffDatabases/toJson(DatabaseDiff) +
// suggestMerges (Schema-Jaccard + Werte-Overlap, cross-database).
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

using namespace memdb;

} // namespace

int main() {
    // 1) diffCollections: gleiches Feld + Zusatzfeld, Werte-Overlap
    {
        Database db("d");
        auto a = db.createCollection("users");
        a.insert({{"id", 1}, {"name", "Ada"}});
        a.insert({{"id", 2}, {"name", "Bob"}});
        auto b = db.createCollection("users_v2");
        b.insert({{"id", 1}, {"name", "Ada"}, {"email", "a@x.de"}});
        b.insert({{"id", 3}, {"name", "Cy"}, {"email", "c@x.de"}});
        auto d = diffCollections(a, b);
        CHECK(d.docCountA == 2 && d.docCountB == 2);
        CHECK(d.fieldsOnlyInB.size() == 1 && d.fieldsOnlyInB[0] == "email");
        CHECK(d.fieldsOnlyInA.empty());
        CHECK(d.commonFields.size() == 2);
        CHECK(d.fieldSimilarity > 0.6); // 2/3
        CHECK(d.valueOverlap["id"] == 0.5); // {1,2} ∩ {1,3} = {1}
        CHECK(d.valueOverlap["name"] == 0.5);
    }

    // 2) diffDatabases: nur in A / nur in B / gemeinsame
    {
        Database db1("a"), db2("b");
        db1.createCollection("users").insert({{"id", 1}});
        db1.createCollection("orders").insert({{"id", 10}});
        db2.createCollection("users").insert({{"id", 1}, {"vip", true}});
        db2.createCollection("customers").insert({{"cid", 1}});
        auto d = diffDatabases(db1, db2);
        CHECK(d.collectionsOnlyInA.size() == 1 && d.collectionsOnlyInA[0] == "orders");
        CHECK(d.collectionsOnlyInB.size() == 1 && d.collectionsOnlyInB[0] == "customers");
        CHECK(d.common.size() == 1 && d.common[0].name == "users");
        CHECK(d.common[0].fieldsOnlyInB.size() == 1 && d.common[0].fieldsOnlyInB[0] == "vip");
        std::string j = toJson(d);
        CHECK(j.find("\"onlyInA\":[\"orders\"]") != std::string::npos);
        CHECK(j.find("\"fieldSimilarity\"") != std::string::npos);
    }

    // 3) suggestMerges: users vs users_old (gleiche Felder, id-Overlap) -> Vorschlag;
    //    unrelated Collection nicht vorgeschlagen; auch cross-database
    {
        Database db1("m1"), db2("m2");
        auto u1 = db1.createCollection("users");
        u1.insert({{"id", 1}, {"name", "Ada"}});
        u1.insert({{"id", 2}, {"name", "Bob"}});
        auto u2 = db2.createCollection("users_old"); // andere DB!
        u2.insert({{"id", 1}, {"name", "Ada L."}});
        u2.insert({{"id", 2}, {"name", "Bob L."}});
        auto unrelated = db1.createCollection("logs");
        unrelated.insert({{"ts", 1}, {"msg", "x"}});
        DiscoveryOptions opts;
        opts.minSchemaSimilarity = 0.6;
        auto sug = suggestMerges({u1, u2, unrelated}, opts);
        CHECK(sug.size() == 1);
        if (!sug.empty()) {
            CHECK(sug[0].collectionA == "users" && sug[0].collectionB == "users_old");
            CHECK(sug[0].schemaSimilarity == 1.0);
            CHECK(sug[0].valueOverlap >= 1.0); // id voll überlappend + Boost
            CHECK(sug[0].commonField == "id");
            CHECK(sug[0].note.find("mergeCollections") != std::string::npos);
        }
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
