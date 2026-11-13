#include <memdb/memdb.hpp>

#include <iostream>

using namespace memdb;

int main() {
    Database db;
    Collection users = db.createCollection("users");

    users.insert({{"name", "schema-los erlaubt"}, {"egal", 123}});
    std::cout << "ohne Schema: alles erlaubt, size=" << users.size() << "\n";

    Schema schema;
    schema.add("name", Value::Type::String).notNull();
    schema.add("age", Value::Type::Int);
    schema.add("aktiv", Value::Type::Bool);
    users.clear();
    users.setSchema(schema);
    std::cout << "Schema gesetzt: " << users.schema()->size() << " Felder deklariert\n";

    users.insert({{"name", "Ada"}, {"age", 36}, {"aktiv", true}});
    std::cout << "gueltiges insert ok, size=" << users.size() << "\n";

    auto tryInsert = [&](Document d, const char* label) {
        try {
            users.insert(std::move(d));
            std::cout << label << ": OK (unerwartet)\n";
        } catch (const SchemaError& e) {
            std::cout << label << ": abgelehnt -> " << e.what() << "\n";
        }
    };
    tryInsert({{"age", 20}}, "fehlendes name (notNull)");
    tryInsert({{"name", "Bob"}, {"undefiniert", 1}}, "undefiniertes Feld (strikt)");
    tryInsert({{"name", "Bob"}, {"age", 3.5}}, "age als double statt int");

    std::string json = db.toJson();
    Database db2 = Database::fromJson(json, "kopie");
    std::cout << "Snapshot enthaelt Schema: " << std::boolalpha << db2.collection("users").hasSchema()
              << ", size=" << db2.collection("users").size() << "\n";
    return 0;
}
