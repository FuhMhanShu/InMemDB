// Tests: allgemeine Foundations v0.27 - public ValueLess, Collection::distinctValues
// (index-aware), Collection::sample (reproduzierbar), AggOp::Distinct, Expression::isMatchAll.
#include <memdb/memdb.hpp>

#include <iostream>
#include <set>
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

} // namespace

int main() {
    // 1) ValueLess: strikte schwache Ordnung, auch gemischte Typen
    {
        std::set<Value, ValueLess> s;
        s.insert(Value(3));
        s.insert(Value(1));
        s.insert(Value(2.5));
        s.insert(Value(std::string("b")));
        s.insert(Value(std::string("a")));
        s.insert(Value(true));
        s.insert(Value(nullptr));
        s.insert(Value(1)); // Duplikat
        CHECK(s.size() == 7);
        // Rangordnung: Null < Bool < Zahl < String
        auto it = s.begin();
        CHECK(it->isNull());
        ++it;
        CHECK(it->asBool() == true);
        ++it;
        CHECK(it->asInt() == 1);
        ++it;
        CHECK(it->asDouble() == 2.5);
        ++it;
        CHECK(it->asDouble() == 3.0);
        ++it;
        CHECK(it->asString() == "a");
        ++it;
        CHECK(it->asString() == "b");
        CHECK(ValueLess{}(Value(1), Value(2)));
        CHECK(!ValueLess{}(Value(2), Value(1)));
        CHECK(!ValueLess{}(Value(nullptr), Value(nullptr)));
    }

    // 2) Expression::isMatchAll
    {
        CHECK(Expression{}.isMatchAll());
        CHECK(!(Field("age") >= 18).isMatchAll());
    }

    // 3) distinctValues: ohne Index (Scan), mit Index (Fastpath + Metrik), mit Query, DSL
    {
        Database db("f");
        auto c = db.createCollection("c");
        c.insert({{"email", "a@x.de"}, {"age", 20}});
        c.insert({{"email", "b@x.de"}, {"age", 30}});
        c.insert({{"email", "a@x.de"}, {"age", 40}}); // Duplikat
        c.insert({{"age", 50}});                       // Feld fehlt
        auto vals = c.distinctValues("email");
        CHECK(vals.size() == 2);
        CHECK(vals[0].asString() == "a@x.de" && vals[1].asString() == "b@x.de"); // sortiert
        // mit Query (Scan-Pfad)
        auto valsQ = c.distinctValues("email", Field("age") >= 30);
        CHECK(valsQ.size() == 2);
        auto valsQ2 = c.distinctValues("email", Field("age") == 20);
        CHECK(valsQ2.size() == 1 && valsQ2[0].asString() == "a@x.de");
        // DSL- overload
        auto valsDsl = c.distinctValues("email", "age == 40");
        CHECK(valsDsl.size() == 1 && valsDsl[0].asString() == "a@x.de");
        // Index-Fastpath: match-all + Index -> indexQueries steigt, Ergebnis gleich
        c.createIndex("email");
        auto m0 = c.metrics();
        auto valsIdx = c.distinctValues("email");
        auto m1 = c.metrics();
        CHECK(valsIdx.size() == 2);
        CHECK(m1.indexQueries == m0.indexQueries + 1);
        CHECK(m1.lastQueryUsedIndex);
        // mit Query trotz Index -> Scan-Pfad (kein indexQueries-Anstieg)
        auto m2 = c.metrics();
        c.distinctValues("email", Field("age") == 20);
        auto m3 = c.metrics();
        CHECK(m3.indexQueries == m2.indexQueries); // Index nur bei match-all genutzt
    }

    // 4) sample: reproduzierbar, Größe, Teilmenge; maxN >= size -> alle; maxN == 0 -> leer
    {
        Database db("f");
        auto c = db.createCollection("c");
        std::vector<DocumentId> all;
        for (int i = 0; i < 1000; ++i)
            all.push_back(c.insert({{"n", i}}));
        auto s1 = c.sample(100, 42);
        auto s2 = c.sample(100, 42);
        CHECK(s1.size() == 100);
        CHECK(s1 == s2); // gleicher seed -> gleiches Sample
        auto s3 = c.sample(100, 43);
        CHECK(s3.size() == 100);
        CHECK(s1 != s3); // anderer seed -> anderes Sample (bei 1000 choose 100 praktisch sicher)
        // Teilmenge der IDs
        std::set<std::int64_t> allIds;
        for (auto id : all)
            allIds.insert(id.value());
        for (auto id : s1)
            CHECK(allIds.count(id.value()) == 1);
        // maxN >= size -> alle; maxN == 0 -> leer
        auto sAll = c.sample(5000, 1);
        CHECK(sAll.size() == 1000);
        CHECK(c.sample(0, 1).empty());
    }

    // 5) AggOp::Distinct: aggregate + groupBy
    {
        Database db("f");
        auto c = db.createCollection("c");
        c.insert({{"plz", "10115"}, {"stadt", "Berlin"}});
        c.insert({{"plz", "10115"}, {"stadt", "Berlin"}});
        c.insert({{"plz", "80331"}, {"stadt", "Muenchen"}});
        c.insert({{"plz", "20095"}}); // stadt fehlt
        auto d = c.aggregate(AggOp::Distinct, "stadt");
        CHECK(d.has_value() && d->isArray());
        CHECK(d->asArray().size() == 2); // Berlin, Muenchen (sortiert)
        CHECK(d->asArray()[0].asString() == "Berlin");
        auto dEmpty = c.aggregate(AggOp::Distinct, "gibtsnicht");
        CHECK(dEmpty.has_value() && dEmpty->asArray().empty());
        // groupBy + Distinct: plz -> distinct stadt (FD-Erkennung: Gruppe mit count==1 und 1 distinct)
        auto groups = c.groupBy("plz", Expression{}, AggOp::Distinct, "stadt");
        CHECK(groups.size() == 3);
        for (const auto& g : groups) {
            CHECK(g.value.has_value() && g.value->isArray());
            if (g.key.asString() == "10115") {
                CHECK(g.count == 2);
                CHECK(g.value->asArray().size() == 1); // eine Stadt -> FD plz->stadt
            }
        }
        // Count bleibt nullopt-value
        auto gc = c.groupBy("plz", Expression{}, AggOp::Count, "stadt");
        CHECK(gc.size() == 3 && !gc[0].value.has_value() && gc[0].count >= 1);
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
