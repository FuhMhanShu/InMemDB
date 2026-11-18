// Regressionstests v0.43 (M1): NaN verletzt die strikte schwache Ordnung der Indizes nicht mehr.
// Postgres-Stil: NaN ist groesser als jede Zahl (auch +Inf), NaN == NaN; equal(): NaN==NaN true,
// NaN != Zahl. compare()/equal() in src/value.cpp behandeln NaN explizit.
#include <memdb/memdb.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <set>
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

const double NAN_ = std::numeric_limits<double>::quiet_NaN();
const double INF_ = std::numeric_limits<double>::infinity();

Value v(double d) { return Value(d); }

} // namespace

int main() {
    // 1) compare(): totale Ordnung ueber Zahlen inkl. NaN/Inf – NaN groesster Wert
    {
        CHECK(compare(Value(1), v(NAN_)).value() < 0);
        CHECK(compare(v(NAN_), Value(1)).value() > 0);
        CHECK(compare(v(NAN_), v(NAN_)).value() == 0);
        CHECK(compare(v(NAN_), v(INF_)).value() > 0);   // NaN > +Inf
        CHECK(compare(v(-INF_), v(NAN_)).value() < 0);  // -Inf < NaN
        CHECK(compare(Value(5), v(INF_)).value() < 0);
        CHECK(compare(v(NAN_), Value(std::numeric_limits<int64_t>::max())).value() > 0);
        // Int-vs-Double-Pfad (gemischte Typen landen im Double-Branch)
        CHECK(compare(Value(3), v(NAN_)).value() < 0);
    }

    // 2) equal(): NaN == NaN, NaN != Zahl (vorher: NaN == NaN war false – inkonsistent zu compare)
    {
        CHECK(equal(v(NAN_), v(NAN_)));
        CHECK(!equal(v(NAN_), Value(5)));
        CHECK(!equal(Value(5), v(NAN_)));
        CHECK(equal(Value(5), v(5.0))); // Int/Double-Promotion unveraendert
    }

    // 3) ValueLess: strikte schwache Ordnung bleibt transitiv (Spot-Checks ueber Wertemenge)
    {
        std::vector<Value> vals = {Value(1), Value(2), v(0.5), v(NAN_), v(INF_), v(-INF_), Value(0)};
        ValueLess less;
        for (const auto& a : vals)
            for (const auto& b : vals)
                if (less(a, b))
                    CHECK(!less(b, a)); // Antisymmetrie
        // Transitivitaet komplett ueber alle Tripel
        for (const auto& a : vals)
            for (const auto& b : vals)
                for (const auto& c : vals)
                    if (less(a, b) && less(b, c))
                        CHECK(less(a, c));
        // Aequivalenzklassen: nur gleiche Werte (bzw. NaN/NaN) sind aequivalent
        std::set<Value, ValueLess> uniq(vals.begin(), vals.end());
        CHECK(uniq.size() == vals.size()); // 1,2,0.5,NaN,+Inf,-Inf,0 alle verschieden
    }

    // 4) Index + Queries: NaN im Index – keine Korruption, keine Endlosschleife, korrekte Ergebnisse
    {
        Database db("nan");
        auto c = db.createCollection("t");
        c.createIndex("v"); // nicht-unique
        std::vector<double> vals = {-INF_, 1, 2, 3, 5, 10, INF_, NAN_};
        for (double d : vals)
            c.insert({{"v", v(d)}});
        c.insert({{"v", Value(7)}});
        CHECK(c.size() == 9);
        CHECK(c.count("v == 5") == 1);            // NaN matcht nicht mehr fälschlich
        CHECK(c.count(Field("v") == Value(5)) == 1);
        CHECK(c.count(Field("v") == v(NAN_)) == 1); // NaN == NaN
        CHECK(c.count("v >= 10") == 3);           // 10, +Inf, NaN
        CHECK(c.count("v > 1000") == 2);          // +Inf, NaN
        CHECK(c.count("v < 0") == 1);             // -Inf
        CHECK(c.count("v <= 2") == 3);            // -Inf, 1, 2
        CHECK(c.count() == 9);                    // Vollscan ok
        // Nachtraeglicher Index-Aufbau ueber Daten mit NaN
        auto c2 = db.createCollection("t2");
        for (double d : vals)
            c2.insert({{"v", v(d)}});
        c2.createIndex("v");
        CHECK(c2.count("v >= 10") == 3);
        CHECK(c2.count("v == 5") == 1);
        // Entfernen eines NaN-Dokuments aus dem Index
        auto nanDoc = c.findOne(Field("v") == v(NAN_));
        CHECK(nanDoc.has_value());
        CHECK(c.removeById(nanDoc->id()) == 1);
        CHECK(c.count("v >= 10") == 2);           // nur noch 10, +Inf
        CHECK(c.size() == 8);
    }

    // 5) Unique-Index: NaN == NaN -> zwei NaN-Dokumente verletzen unique; NaN + Zahl nicht
    {
        Database db("uniq");
        auto c = db.createCollection("u");
        c.createIndex("v", true);
        c.insert({{"v", Value(5)}});
        c.insert({{"v", v(NAN_)}}); // ok: NaN != 5
        bool threw = false;
        try {
            c.insert({{"v", v(NAN_)}}); // zweites NaN -> UniqueConstraintError
        } catch (const UniqueConstraintError&) {
            threw = true;
        }
        CHECK(threw);
        CHECK(c.size() == 2); // fehlgeschlagener Insert hat nichts geaendert
        threw = false;
        try {
            c.insert({{"v", Value(5)}});
        } catch (const UniqueConstraintError&) {
            threw = true;
        }
        CHECK(threw);
    }

    // 6) JSON-Parser (tolerant, akzeptiert nan/inf): Index bleibt konsistent
    {
        Database db("j");
        auto c = db.createCollection("j");
        c.createIndex("v");
        c.loadJson(R"([{"v": nan}, {"v": 5}, {"v": inf}, {"v": -inf}, {"v": 1}])");
        CHECK(c.size() == 5);
        CHECK(c.count("v == 5") == 1);
        CHECK(c.count("v >= 5") == 3);   // 5, +Inf, NaN
        CHECK(c.count("v < 0") == 1);    // -Inf
        // Snapshot-Roundtrip: Writer schreibt NaN/Inf als null (bestehendes Verhalten)
        std::string j = c.toJson();
        CHECK(j.find("null") != std::string::npos);
        auto c2 = db.createCollection("j2");
        c2.loadJson(j);
        CHECK(c2.size() == 5);
    }

    // 7) Aggregation/Distinct mit NaN: Max = NaN (groesster Wert), Min = -Inf; eine NaN-Gruppe
    {
        Database db("agg");
        auto c = db.createCollection("a");
        for (double d : {1.0, NAN_, -INF_, 3.0})
            c.insert({{"v", v(d)}});
        CHECK(c.aggregate(AggOp::Max, "v").value().asDouble() == NAN_ || std::isnan(c.aggregate(AggOp::Max, "v").value().asDouble()));
        CHECK(c.aggregate(AggOp::Min, "v").value().asDouble() == -INF_);
        auto dv = c.aggregate(AggOp::Distinct, "v").value().asArray();
        CHECK(dv.size() == 4);
        auto gs = c.groupBy("v", Expression{}, AggOp::Count, "");
        CHECK(gs.size() == 4); // NaN eigene Gruppe, kein Kollaps mit Zahlen
    }

    // 8) Groesserer Stresstest: 2000 gemischte Werte inkl. vieler NaN im Index – keine Haenger/Korruption
    {
        Database db("stress");
        auto c = db.createCollection("s");
        c.createIndex("v");
        std::vector<double> vals;
        for (int i = 0; i < 2000; ++i)
            vals.push_back(i % 7 == 0 ? NAN_ : (i % 11 == 0 ? INF_ : i * 0.5 - 500));
        for (double d : vals)
            c.insert({{"v", v(d)}});
        CHECK(c.size() == 2000);
        std::size_t nanCount = 0;
        for (int i = 0; i < 2000; ++i)
            if (i % 7 == 0)
                ++nanCount;
        CHECK(c.count(Field("v") == v(NAN_)) == nanCount);
        // robust: Vollscan-Vergleich mit Index-Query – Referenz nutzt compare(), damit
        // NaN konsistent als groesster Wert zaehlt (Postgres-Stil, wie die Query selbst)
        std::size_t full = 0;
        for (const auto& d : c.find(Expression{})) {
            auto cmp = compare(d.at("v"), Value(400));
            if (cmp && *cmp > 0)
                ++full;
        }
        CHECK(c.count("v > 400") == full);
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
