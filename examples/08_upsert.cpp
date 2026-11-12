#include <memdb/memdb.hpp>

#include <iostream>

using namespace memdb;

int main() {
    Database db;
    Collection users = db.createCollection("users");
    users.createIndex("name", true);

    DocumentId id = users.upsert(Field("name") == "Ada", {{"name", "Ada"}, {"age", 36}});
    std::cout << "upsert (neu):   id=" << toString(id) << " size=" << users.size() << "\n";

    DocumentId id2 = users.upsert(Field("name") == "Ada", {{"name", "Ada"}, {"age", 37}, {"aktiv", true}});
    std::cout << "upsert (match): id=" << toString(id2) << " (gleich: " << std::boolalpha << (id2 == id)
              << ") age=" << users.findById(id)->at("age").asInt()
              << " size=" << users.size() << "\n";

    DocumentId id3 = users.upsertById(id, {{"name", "Ada"}, {"age", 38}});
    std::cout << "upsertById:     id=" << toString(id3) << " age=" << users.findById(id)->at("age").asInt() << "\n";
    return 0;
}
