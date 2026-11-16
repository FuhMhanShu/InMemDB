// Tests: optionale Collection-Limits (FA-28, v0.39) – maxDocuments, maxDocumentBytes,
// maxFieldsPerDocument, maxIndexes; LimitError vor der Mutation (Bulk atomar), Metrik
// limitViolations, Default unbegrenzt, Transaktions-Rollback bei Limit-Verstoss.
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
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #expr " wirft nicht " #ExType "\n"; \
        }                                                                                        \
    } while (0)

using namespace memdb;

} // namespace

int main() {
    // 0) Default: unbegrenzt – limits() liefert Nullen
    {
        Database db("d0");
        auto c = db.createCollection("c");
        CHECK(c.limits().maxDocuments == 0);
        for (int i = 0; i < 100; ++i)
            c.insert({{"i", i}});
        CHECK(c.size() == 100);
        CHECK(c.metrics().limitViolations == 0);
    }

    // 1) maxDocuments: 4. insert wirft LimitError, Bestand bleibt 3; Metrik zählt
    {
        Database db("d1");
        auto c = db.createCollection("c");
        c.setLimits({.maxDocuments = 3});
        CHECK(c.limits().maxDocuments == 3);
        c.insert({{"a", 1}});
        c.insert({{"a", 2}});
        c.insert({{"a", 3}});
        CHECK_THROWS_AS(c.insert({{"a", 4}}), LimitError);
        CHECK(c.size() == 3);
        CHECK(c.metrics().limitViolations == 1);
        // insertBulk atomar: 2 würden passen? nein: 3+2 > 3 -> nichts wird eingefügt
        CHECK_THROWS_AS(c.insertBulk({{{"a", 4}}, {{"a", 5}}}), LimitError);
        CHECK(c.size() == 3);
        // Bulk, der genau auffüllt, geht: 0 frei -> auch 1 zu viel
        CHECK_THROWS_AS(c.insertBulk({{{"a", 4}}}), LimitError);
        CHECK(c.size() == 3);
        CHECK(c.metrics().limitViolations == 3);
        // nach remove ist wieder Platz
        c.removeById(DocumentId(1));
        c.insert({{"a", 9}});
        CHECK(c.size() == 3);
        CHECK(c.count("a == 9") == 1);
    }

    // 2) maxDocumentBytes: großes Doc wird abgelehnt, Update das Doc zu groß macht ebenso
    {
        Database db("d2");
        auto c = db.createCollection("c");
        c.setLimits({.maxDocumentBytes = 64});
        c.insert({{"s", std::string(10, 'x')}}); // klein ok
        CHECK_THROWS_AS(c.insert({{"s", std::string(200, 'x')}}), LimitError); // zu gross
        CHECK(c.size() == 1);
        auto id = DocumentId(1);
        CHECK_THROWS_AS(c.updateById(id, [](Document& d) { d.set("s", std::string(500, 'y')); }), LimitError);
        CHECK(c.findById(id)->find("s")->asString().size() == 10); // unverändert
        // replace mit zu grossem Doc ebenso
        Document big;
        big.set("s", std::string(500, 'z'));
        CHECK_THROWS_AS(c.replace(id, big), LimitError);
        // upsert (update-Pfad) ebenso
        Document big2;
        big2.set("s", std::string(500, 'z'));
        CHECK_THROWS_AS(c.upsertById(id, big2), LimitError);
    }

    // 3) maxFieldsPerDocument
    {
        Database db("d3");
        auto c = db.createCollection("c");
        c.setLimits({.maxFieldsPerDocument = 3});
        Document d;
        d.set("a", 1);
        d.set("b", 2);
        d.set("c", 3);
        c.insert(d); // 3 Felder ok
        Document d4 = d;
        d4.set("d", 4);
        CHECK_THROWS_AS(c.insert(d4), LimitError);
        // update, das ein 4. Feld addet -> LimitError, Doc bleibt bei 3 Feldern
        CHECK_THROWS_AS(c.updateById(DocumentId(1), [](Document& x) { x.set("d", 4); }), LimitError);
        CHECK(c.findById(DocumentId(1))->fieldCount() == 3);
    }

    // 4) maxIndexes
    {
        Database db("d4");
        auto c = db.createCollection("c");
        c.setLimits({.maxIndexes = 2});
        c.createIndex("a");
        c.createIndex("b");
        CHECK_THROWS_AS(c.createIndex("c"), LimitError);
        CHECK(c.metrics().limitViolations == 1);
        CHECK(c.indexes().size() == 2);
    }

    // 5) loadJson respektiert maxDocuments (atomar: alter Inhalt bleibt)
    {
        Database db("d5");
        auto c = db.createCollection("c");
        c.setLimits({.maxDocuments = 2});
        c.insert({{"a", 1}});
        CHECK_THROWS_AS(c.loadJson(R"([{"x":1},{"x":2},{"x":3}])"), LimitError);
        CHECK(c.size() == 1); // unverändert
        c.loadJson(R"([{"x":1},{"x":2}])");
        CHECK(c.size() == 2);
        CHECK(c.count("x == 2") == 1);
    }

    // 6) Transaktion: Limit-Verstoss in Tx -> Rollback stellt alles wieder her
    {
        Database db("d6");
        auto c = db.createCollection("c");
        c.setLimits({.maxDocuments = 2});
        c.insert({{"a", 1}});
        c.insert({{"a", 2}});
        {
            auto tx = c.beginTransaction();
            CHECK_THROWS_AS(tx.insert({{"a", 3}}), LimitError);
            // tx-Destruktor rollt back; Bestand bleibt 2
        }
        CHECK(c.size() == 2);
        // und innerhalb der Tx ging der erfolgreiche Insert trotzdem (read-your-own-writes) bis rollback
    }

    // 7) Database::metrics summiert limitViolations
    {
        Database db("d7");
        auto c = db.createCollection("c");
        c.setLimits({.maxDocuments = 1});
        c.insert({{"a", 1}});
        CHECK_THROWS_AS(c.insert({{"a", 2}}), LimitError);
        CHECK(db.metrics().sum.limitViolations == 1);
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
