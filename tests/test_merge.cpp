// Tests: Collections zusammenführen (FA-24, v0.24) - Database::mergeCollections:
// neue IDs + _source-Feld, atomarer Rollback bei Schema/Unique-Verstoß,
// Index-Uebernahme, dropSources, Referenz-Umschreiben (rewriteKeys),
// Observer-Events, Spill-Quelle, Metriken.
#include <memdb/memdb.hpp>
#include <memdb/references.hpp>

#include <cstdio>
#include <set>
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
    // 1) Basis-Merge: zwei Quellen in bestehende Ziel-Collection
    {
        Database db("m");
        auto a = db.createCollection("a");
        auto b = db.createCollection("b");
        auto t = db.createCollection("t");
        a.insert({{"name", "A1"}, {"x", 1}});
        a.insert({{"name", "A2"}, {"x", 2}});
        b.insert({{"name", "B1"}, {"x", 3}});
        t.insert({{"name", "T1"}, {"x", 0}});

        MergeStats st = db.mergeCollections("t", {"a", "b"});
        CHECK(st.merged == 3);
        CHECK(st.perSource["a"] == 2);
        CHECK(st.perSource["b"] == 1);
        CHECK(t.size() == 4);
        CHECK(a.size() == 2 && b.size() == 1); // Quellen behalten (Default)
        // neue IDs, _source-Feld gesetzt
        auto ids = t.findIds(Field("_source") == "a");
        CHECK(ids.size() == 2);
        auto d = t.findById(ids.front()).value();
        CHECK(d.at("name").asString() == "A1" || d.at("name").asString() == "A2");
        CHECK(d.at("x").asInt() == 1 || d.at("x").asInt() == 2);
        // alte IDs der Quellen kollidieren mit Ziel-IDs -> neue IDs vergeben
        CHECK(t.findIds(Field("_source") == "b").size() == 1);
        auto all = t.findIds();
        std::set<std::int64_t> uniq;
        for (auto id : all)
            uniq.insert(id.value());
        CHECK(uniq.size() == all.size());
    }

    // 2) Ziel existiert nicht -> wird angelegt; leere Quellen -> Error; Ziel in Quellen -> Error
    {
        Database db("m");
        auto a = db.createCollection("a");
        a.insert({{"v", 1}});
        MergeStats st = db.mergeCollections("neu", {"a"});
        CHECK(st.merged == 1);
        CHECK(db.hasCollection("neu"));
        CHECK(db.collection("neu").size() == 1);
        CHECK_THROWS_AS(db.mergeCollections("neu", {}), Error);
        CHECK_THROWS_AS(db.mergeCollections("a", {"a"}), Error);
        CHECK_THROWS_AS(db.mergeCollections("x", {"gibtsnicht"}), CollectionNotFoundError);
    }

    // 3) Schema-Verstoß -> SchemaError + atomarer Rollback (Ziel unverändert)
    {
        Database db("m");
        auto a = db.createCollection("a");
        a.insert({{"name", "ok"}, {"age", 30}});
        a.insert({{"name", "bad"}}); // age fehlt
        auto t = db.createCollection("t");
        Schema sch;
        sch.add("name", Value::Type::String);
        sch.add("age", Value::Type::Int);
        t.setSchema(sch);
        t.insert({{"name", "T"}, {"age", 1}});
        const auto before = t.size();
        const auto nextBefore = t.findIds().back().value();
        CHECK_THROWS_AS(db.mergeCollections("t", {"a"}), SchemaError);
        CHECK(t.size() == before); // Rollback: nichts eingefügt
        CHECK(t.findIds(Field("_source") == "a").empty());
        CHECK(t.findIds().back().value() == nextBefore); // nextId zurückgesetzt
        // Quelle unverändert
        CHECK(a.size() == 2);
    }

    // 4) Unique-Index im Ziel -> Verstoß beim Merge -> UniqueConstraintError + Rollback
    {
        Database db("m");
        auto a = db.createCollection("a");
        a.insert({{"email", "x@y.z"}});
        a.insert({{"email", "dup@y.z"}});
        auto t = db.createCollection("t");
        t.createIndex("email", true);
        t.insert({{"email", "dup@y.z"}});
        CHECK_THROWS_AS(db.mergeCollections("t", {"a"}), UniqueConstraintError);
        CHECK(t.size() == 1); // Rollback
        CHECK(t.findIds(Field("_source") == "a").empty());
    }

    // 5) Index-Uebernahme aus Quelle + Abfrage nutzt Index danach
    {
        Database db("m");
        auto a = db.createCollection("a");
        a.createIndex("x");
        a.insert({{"x", 5}, {"name", "A"}});
        a.insert({{"x", 6}, {"name", "B"}});
        auto t = db.createCollection("t");
        MergeStats st = db.mergeCollections("t", {"a"});
        CHECK(t.hasIndex("x"));
        CHECK(st.adoptedIndexes.size() == 1 && st.adoptedIndexes[0] == "x");
        auto m0 = t.metrics();
        auto ids = t.findIds(Field("x") == 6);
        CHECK(ids.size() == 1);
        auto m1 = t.metrics();
        CHECK(m1.indexQueries > m0.indexQueries); // Index wurde genutzt
        CHECK(t.findById(ids.front()).value().at("name").asString() == "B");
    }

    // 6) dropSources: Quellen werden gelöscht
    {
        Database db("m");
        auto a = db.createCollection("a");
        auto b = db.createCollection("b");
        a.insert({{"v", 1}});
        b.insert({{"v", 2}});
        db.createCollection("t");
        MergeOptions opts;
        opts.dropSources = true;
        MergeStats st = db.mergeCollections("t", {"a", "b"}, opts);
        CHECK(st.merged == 2);
        CHECK(st.droppedSources.size() == 2);
        CHECK(!db.hasCollection("a") && !db.hasCollection("b"));
        CHECK(db.collection("t").size() == 2);
    }

    // 7) Referenzen umschreiben: Quelle löschen + ReferenceIndex -> keine Dangling-Refs
    {
        Database db("m");
        ReferenceIndex refs(db);
        auto users = db.createCollection("users");
        auto orders = db.createCollection("orders");
        auto t = db.createCollection("merged");
        DocumentId u1 = users.insert({{"name", "Ada"}});
        DocumentId o1 = orders.insert({{"total", 42}});
        refs.link("users", u1, "orders", o1, "bestellt");
        CHECK(refs.references("users", RefDirection::Outgoing, u1).size() == 1);

        MergeOptions opts;
        opts.dropSources = true;
        MergeStats st = db.mergeCollections("merged", {"users", "orders"}, opts, &refs);
        CHECK(st.merged == 2);
        CHECK(st.rewrittenReferences == 1);
        CHECK(refs.dangling().empty()); // nichts dangling
        auto merged = db.collection("merged");
        DocumentId newU = merged.findIds(Field("_source") == "users").front();
        DocumentId newO = merged.findIds(Field("_source") == "orders").front();
        auto out = refs.references("merged", RefDirection::Outgoing, newU);
        CHECK(out.size() == 1);
        if (!out.empty()) {
            CHECK(out[0].relation == "bestellt");
            CHECK(out[0].toId.has_value() && *out[0].toId == newO); // zeigt auf neues orders-Doc
        }
        auto joined = refs.joinIds("merged", "bestellt", "merged");
        CHECK(joined.size() == 1);
    }

    // 8) Observer-Events: ein Insert-Event pro gemergtem Dokument
    {
        Database db("m");
        auto a = db.createCollection("a");
        a.insert({{"v", 1}});
        a.insert({{"v", 2}});
        a.insert({{"v", 3}});
        auto t = db.createCollection("t");
        std::size_t events = 0;
        t.onChange([&](const ChangeEvent& e) {
            if (e.type == ChangeType::Insert)
                ++events;
        });
        db.mergeCollections("t", {"a"});
        CHECK(events == 3);
        CHECK(t.metrics().inserts == 3);
    }

    // 9) Spill-Quelle (Out-of-Core): gespillte Docs werden mitgemergt
    {
        Database db("m");
        auto a = db.createCollection("a");
        const std::string spill = std::string(std::tmpnam(nullptr)) + ".spill";
        a.setOverflow(spill, 2); // nur 2 Docs im RAM
        for (int i = 0; i < 10; ++i)
            a.insert({{"n", i}});
        CHECK(a.overflowInfo().spilled > 0); // wirklich ausgespillt
        auto t = db.createCollection("t");
        MergeStats st = db.mergeCollections("t", {"a"});
        CHECK(st.merged == 10);
        CHECK(t.size() == 10);
        CHECK(t.count(Field("n") >= 0) == 10);
        std::remove(spill.c_str());
    }

    // 10) sourceField leer -> kein Herkunftsfeld; Duplikate in sources ignoriert
    {
        Database db("m");
        auto a = db.createCollection("a");
        a.insert({{"v", 1}});
        db.createCollection("t");
        MergeOptions opts;
        opts.sourceField = "";
        MergeStats st = db.mergeCollections("t", {"a", "a"}, opts);
        CHECK(st.merged == 1);
        CHECK(db.collection("t").findIds(Field("_source") == "a").empty());
        CHECK(db.collection("t").findById(db.collection("t").findIds().front()).value().find("_source") ==
              nullptr);
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
