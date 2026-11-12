#include <memdb/memdb.hpp>

#include <iostream>

using namespace memdb;

int main() {
    Database db("shop");
    Collection products = db.createCollection("products");
    products.createIndex("name", true);
    products.insert({{"name", "Tastatur"}, {"preis", 49.99}, {"lager", 12},
                     {"tags", Value(std::vector<Value>{Value("elektronik"), Value("input")})}});
    products.insert({{"name", "Maus"}, {"preis", 19.95}, {"lager", 30}});

    std::string json = db.toJson();
    std::cout << "JSON ( gekuerzt ): " << json.substr(0, 120) << "...\n";

    db.saveToFile("/tmp/inmemdb_snapshot.json");
    std::cout << "gespeichert nach /tmp/inmemdb_snapshot.json\n";

    Database geladen = Database::loadFromFile("/tmp/inmemdb_snapshot.json");
    Collection p2 = geladen.collection("products");
    std::cout << "geladen: name=" << geladen.name() << ", products=" << p2.size()
              << ", index name vorhanden: " << std::boolalpha << p2.hasIndex("name") << "\n";
    auto maus = p2.findOne(Field("name") == "Maus");
    if (maus)
        std::cout << "Maus: preis=" << maus->at("preis").asDouble() << "\n";
    auto tastatur = p2.findOne(Field("name") == "Tastatur");
    if (tastatur && tastatur->contains("tags"))
        std::cout << "Tastatur: tags=" << toString(tastatur->at("tags")) << "\n";

    Database db2 = Database::fromJson(json, "kopie");
    std::cout << "aus JSON-String: db2.name=" << db2.name()
              << ", products=" << db2.collection("products").size() << "\n";
    return 0;
}
