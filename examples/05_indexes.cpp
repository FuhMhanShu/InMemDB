#include <memdb/memdb.hpp>

#include <iostream>

using namespace memdb;

int main() {
    Database db;
    Collection users = db.createCollection("users");
    for (int i = 1; i <= 1000; ++i)
        users.insert({{"name", "user" + std::to_string(i)}, {"age", i % 100}});

    std::cout << "ohne Index: count(age==42) Kandidaten = ";
    std::cout << users.count(Field("age") == 42) << " Treffer, Kandidaten = "
              << users.lastQueryCandidateCount() << "\n";

    users.createIndex("age");
    std::cout << "mit Index:  count(age==42) Treffer = " << users.count(Field("age") == 42)
              << ", Kandidaten = " << users.lastQueryCandidateCount() << "\n";
    users.count(Field("age") >= 90);
    std::cout << "mit Index:  count(age>=90) Kandidaten = " << users.lastQueryCandidateCount() << "\n";

    users.createIndex("name", true);
    try {
        users.insert({{"name", "user1"}, {"age", 1}});
        std::cout << "FEHLER: Duplikat nicht erkannt\n";
    } catch (const UniqueConstraintError& e) {
        std::cout << "Unique-Constraint greift: " << e.what() << "\n";
    }

    std::cout << "Indizes:";
    for (const auto& ix : users.indexes())
        std::cout << " " << ix.field() << (ix.isUnique() ? "(unique)" : "");
    std::cout << "\n";
    users.dropIndex("age");
    std::cout << "nach dropIndex(age): hasIndex = " << std::boolalpha << users.hasIndex("age") << "\n";
    return 0;
}
