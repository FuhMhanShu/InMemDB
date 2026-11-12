#include <memdb/memdb.hpp>

#include <iostream>

using namespace memdb;

const char* typeName(ChangeType t) {
    switch (t) {
        case ChangeType::Insert: return "INSERT";
        case ChangeType::Update: return "UPDATE";
        case ChangeType::Replace: return "REPLACE";
        case ChangeType::Remove: return "REMOVE";
        case ChangeType::Clear: return "CLEAR";
    }
    return "?";
}

int main() {
    Database db;
    Collection users = db.createCollection("users");

    std::uint64_t handle = users.onChange([](const ChangeEvent& e) {
        std::cout << "event: " << typeName(e.type) << " in '" << e.collection << "' id=" << toString(e.id);
        if (e.type != ChangeType::Clear && e.document.contains("name"))
            std::cout << " name=" << e.document.at("name").asString();
        std::cout << "\n";
    });

    DocumentId id = users.insert({{"name", "Ada"}});
    users.updateById(id, [](Document& d) { d.set("age", 36); });
    users.insert({{"name", "Bob"}});
    users.removeById(id);

    {
        Transaction tx = users.beginTransaction();
        tx.insert({{"name", "InTx"}});
        std::cout << "(in Transaktion, noch keine Events)\n";
        tx.commit();
    }

    users.offChange(handle);
    users.insert({{"name", "OhneObserver"}});
    std::cout << "nach offChange: keine Events mehr\n";
    return 0;
}
