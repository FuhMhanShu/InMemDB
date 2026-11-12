#include <memdb/memdb.hpp>

#include <iostream>
#include <memory>

using namespace memdb;

int main() {
    Database db("demo");
    Collection docs = db.createCollection("docs");

    DocumentId a = docs.insert({{"titel", "Notiz"}, {"prio", 1}});
    DocumentId b = docs.insert({{"titel", "Brief"}, {"empfaenger", "Ada"}, {"anhaenge", 3}});

    auto docA = docs.findById(a).value();
    auto docB = docs.findById(b).value();
    std::cout << "A Felder: " << docA.fieldCount() << ", B Felder: " << docB.fieldCount()
              << " (unterschiedliche Struktur – schema-los)\n";

    docA.set("prio", "hoch");
    docA.set("erledigt", false);
    docA.rename("titel", "name");
    docA.remove("name");
    std::cout << "A nach Aenderung: prio=" << docA.at("prio").asString()
              << " (Typ geaendert int->string), erledigt=" << std::boolalpha << docA.at("erledigt").asBool()
              << ", Felder=" << docA.fieldCount() << "\n";

    auto adresse = std::make_shared<Document>(
        std::initializer_list<std::pair<const std::string, Value>>{{"stadt", "Berlin"}, {"plz", "10115"}});
    docB.set("adresse", Value(adresse));
    const Value* plz = docB.findPath("adresse.plz");
    std::cout << "B verschachtelt: adresse.plz=" << (plz ? plz->asString() : std::string("?")) << "\n";

    docs.updateById(a, [](Document& d) { d.set("neu_feld", 42); });
    std::cout << "A neues Feld nachtraeglich: neu_feld=" << docs.findById(a)->at("neu_feld").asInt() << "\n";
    return 0;
}
