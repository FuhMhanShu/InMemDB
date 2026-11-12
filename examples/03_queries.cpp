#include <memdb/memdb.hpp>

#include <iostream>
#include <memory>

using namespace memdb;

namespace {

void print(const char* label, Cursor c) {
    std::cout << label << " (" << c.size() << "):";
    for (const auto& d : c)
        std::cout << " " << d.at("name").asString() << "(" << d.at("age").asInt() << ")";
    std::cout << "\n";
}

} // namespace

int main() {
    Database db;
    Collection users = db.createCollection("users");
    users.insert({{"name", "Ada"}, {"age", 36}, {"stadt", "Berlin"},
                  {"tags", Value(std::vector<Value>{Value("cpp"), Value("math")})},
                  {"adresse", Value(std::make_shared<Document>(std::initializer_list<std::pair<const std::string, Value>>{{"stadt", "Berlin"}}))}});
    users.insert({{"name", "Bob"}, {"age", 25}, {"stadt", "Berlin"}});
    users.insert({{"name", "Cy"}, {"age", 40}, {"stadt", "Hamburg"}, {"gesperrt", true}});
    users.insert({{"name", "Dee"}, {"age", 19}});

    print("alle", users.find());
    print("age >= 30", users.find(Field("age") >= 30));
    print("Berlin && >= 30", users.find((Field("stadt") == "Berlin") && (Field("age") >= 30)));
    print("Hamburg || < 20", users.find((Field("stadt") == "Hamburg") || (Field("age") < 20)));
    print("nicht gesperrt", users.find(!(Field("gesperrt") == true)));
    print("tag enthaelt 'cpp'", users.find(Field("tags").contains("cpp")));
    print(" verschachtelt adresse.stadt == Hamburg", users.find(Field("adresse.stadt") == "Hamburg"));
    print(" Feld 'gesperrt' existiert", users.find(Field("gesperrt").exists()));
    print(" fehlendes Feld != 1", users.find(Field("fehlt") != Value(1)));

    auto one = users.findOne(Field("name") == "Bob");
    if (one)
        std::cout << "findOne name=Bob -> age=" << one->at("age").asInt() << "\n";
    std::cout << "count(age < 30) = " << users.count(Field("age") < 30) << "\n";
    return 0;
}
