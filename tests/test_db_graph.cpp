// Tests: DbGraph (FA-22) – fromDatabase, fromSnapshotFile, toJson/toMermaid/toMarkdown/summary.
#include <memdb/memdb.hpp>

#include <cstdio>
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

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

} // namespace

int main() {
    const std::string snapPath = "inmemdb_graph_test.inmemdb.json";

    Database db("graph-test");
    Collection users = db.createCollection("users");
    users.createIndex("name", true);
    {
        Schema sch;
        sch.add("name", Value::Type::String).notNull();
        sch.add("age", Value::Type::Int);
        users.setSchema(std::move(sch));
    }
    DocumentId ada = users.insert({{"name", "Ada"}, {"age", 36}});
    users.insert({{"name", "Bob"}, {"age", 17}});
    Collection orders = db.createCollection("orders"); // schema-los
    DocumentId o1 = orders.insert({{"total", 100}, {"note", "klein"}});
    DocumentId o2 = orders.insert({{"total", 500}});
    DocumentId ghost = users.insert({{"name", "Ghost"}, {"age", 99}});

    ReferenceIndex refs(db);
    refs.link("orders", o1, "users", ada, "kunde");
    refs.link("orders", o2, "users", ghost, "kunde");
    users.removeById(ghost); // -> zweite Referenz wird dangling

    // --- fromDatabase ---
    {
        DbGraph g = DbGraph::fromDatabase(db);
        CHECK(g.nodes.size() == 2); // __references nicht als Knoten
        CHECK(g.nodes[0].collection == "orders"); // alphabetisch: orders vor users
        CHECK(g.nodes[1].collection == "users");
        CHECK(g.nodes[1].documents == 2);
        CHECK(g.nodes[1].hasSchema);
        CHECK(g.nodes[1].fields.size() == 2);
        CHECK(g.nodes[1].fields[0].name == "age" && g.nodes[1].fields[0].type == "int" &&
              g.nodes[1].fields[0].nullable && g.nodes[1].fields[0].index.empty());
        CHECK(g.nodes[1].fields[1].name == "name" && g.nodes[1].fields[1].type == "string" &&
              !g.nodes[1].fields[1].nullable && g.nodes[1].fields[1].index == "unique");
        CHECK(!g.nodes[0].hasSchema);
        CHECK(g.edges.size() == 2);
        CHECK(g.edges[0].from == "orders#" + toString(o1) || g.edges[0].from == "orders#" + toString(o2));
        CHECK(contains(g.edges[0].to, "users#"));
        CHECK(g.edges[0].relation == "kunde");
        std::size_t dang = 0;
        for (const auto& e : g.edges)
            if (e.dangling)
                ++dang;
        CHECK(dang == 1);

        // summary
        CHECK(g.summary() == "2 Collections, 4 Dokumente, 2 Referenzen (1 dangling)");

        // --- toJson ---
        std::string js = g.toJson();
        CHECK(contains(js, "\"name\":\"users\""));
        CHECK(contains(js, "\"documents\":2"));
        CHECK(contains(js, "\"type\":\"string\""));
        CHECK(contains(js, "\"nullable\":false"));
        CHECK(contains(js, "\"index\":\"unique\""));
        CHECK(contains(js, "\"schema\":null")); // orders schema-los
        CHECK(contains(js, "\"relation\":\"kunde\""));
        CHECK(contains(js, "\"dangling\":true"));
        CHECK(contains(js, "\"from\":\"orders#"));

        // --- toMermaid ---
        std::string mmd = g.toMermaid();
        CHECK(contains(mmd, "graph LR"));
        CHECK(contains(mmd, "users[\"users"));
        CHECK(contains(mmd, "name: string! [unique]"));
        CHECK(contains(mmd, "age: int?"));
        CHECK(contains(mmd, "(schema-los)"));
        CHECK(contains(mmd, "-->|kunde|"));
        CHECK(contains(mmd, "(dangling)"));
        // Dokument-Knoten mit sanitizierter ID
        CHECK(contains(mmd, "orders_" + toString(o1.value()) + "["));

        // --- toMarkdown ---
        std::string md = g.toMarkdown();
        CHECK(contains(md, "# Datenbank-Graph"));
        CHECK(contains(md, "| users | 2 | ja |"));
        CHECK(contains(md, "`name: string! [unique]`"));
        CHECK(contains(md, "| orders#"));
        CHECK(contains(md, "**ja**"));

        // --- Snapshot-Roundtrip ---
        db.saveToFile(snapPath);
        DbGraph g2 = DbGraph::fromSnapshotFile(snapPath);
        CHECK(g2.nodes.size() == 2);
        CHECK(g2.edges.size() == 2);
        std::string js2 = g2.toJson();
        CHECK(contains(js2, "\"name\":\"users\""));
        CHECK(contains(js2, "\"index\":\"unique\""));
        CHECK(contains(js2, "\"relation\":\"kunde\""));
        CHECK(g2.nodes[1].fields[1].index == "unique");
        // im Snapshot sind Dokument-IDs nicht enthalten -> dangling nur ueber Collection pruefbar
        std::size_t dang2 = 0;
        for (const auto& e : g2.edges)
            if (e.dangling)
                ++dang2;
        CHECK(dang2 == 0);
    }

    // --- CLI-artig: Snapshot-Datei lesen ---
    {
        DbGraph g = DbGraph::fromSnapshotFile(snapPath);
        CHECK(contains(g.toMermaid(), "graph LR"));
        CHECK(contains(g.summary(), "2 Collections"));
    }

    std::remove(snapPath.c_str());
    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
