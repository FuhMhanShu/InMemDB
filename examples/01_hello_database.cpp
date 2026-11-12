#include <memdb/memdb.hpp>

#include <iostream>

using namespace memdb;

int main() {
    Database db("shop");
    Collection products = db.createCollection("products");

    DocumentId id = products.insert({{"name", "Tastatur"}, {"preis", 49.99}, {"lager", 12}});
    std::cout << "eingefuegt mit ID " << toString(id) << "\n";

    auto doc = products.findById(id);
    if (doc) {
        std::cout << "name=" << doc->at("name").asString()
                  << " preis=" << doc->at("preis").asDouble()
                  << " lager=" << doc->at("lager").asInt() << "\n";
    }

    std::cout << "Collections:";
    for (const auto& n : db.collectionNames())
        std::cout << " " << n;
    std::cout << "\n";
    return 0;
}
