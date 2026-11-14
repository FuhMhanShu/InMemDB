// Beispiel 15: Datenbank-Graph visualisieren (FA-22) – JSON / Mermaid / Markdown, LLM-freundlich.
#include <memdb/memdb.hpp>

#include <iostream>

using namespace memdb;

int main() {
    Database db("shop");
    Collection users = db.createCollection("users");
    users.createIndex("name", true);
    Schema sch;
    sch.add("name", Value::Type::String).notNull();
    sch.add("age", Value::Type::Int);
    users.setSchema(std::move(sch));
    DocumentId ada = users.insert({{"name", "Ada"}, {"age", 36}});
    users.insert({{"name", "Bob"}, {"age", 17}});

    Collection orders = db.createCollection("orders"); // schema-los
    DocumentId o1 = orders.insert({{"total", 100}});
    orders.insert({{"total", 500}});

    ReferenceIndex refs(db);
    refs.link("orders", o1, "users", ada, "kunde");

    DbGraph g = DbGraph::fromDatabase(db);
    std::cout << g.summary() << "\n\n--- Mermaid ---\n" << g.toMermaid() << "\n--- JSON ---\n"
              << g.toJson() << "\n\n--- Markdown ---\n" << g.toMarkdown() << "\n";

    // aus Snapshot-Datei (z. B. fuer ein LLM: eine Datei genuegt)
    db.saveToFile("shop_graph_demo.inmemdb.json");
    DbGraph g2 = DbGraph::fromSnapshotFile("shop_graph_demo.inmemdb.json");
    std::cout << "aus Snapshot: " << g2.summary() << "\n";
    std::remove("shop_graph_demo.inmemdb.json");
    return 0;
}
