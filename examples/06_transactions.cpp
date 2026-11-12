#include <memdb/memdb.hpp>

#include <iostream>

using namespace memdb;

int main() {
    Database db;
    Collection konten = db.createCollection("konten");
    DocumentId alice = konten.insert({{"name", "Alice"}, {"saldo", 100}});
    DocumentId bob = konten.insert({{"name", "Bob"}, {"saldo", 20}});

    auto saldo = [&](DocumentId id) { return konten.findById(id)->at("saldo").asInt(); };

    {
        Transaction tx = konten.beginTransaction();
        tx.updateById(alice, [](Document& d) { d.set("saldo", d.at("saldo").asInt() - 30); });
        tx.updateById(bob, [](Document& d) { d.set("saldo", d.at("saldo").asInt() + 30); });
        std::cout << "in Transaktion: Alice=" << tx.findById(alice)->at("saldo").asInt()
                  << " Bob=" << tx.findById(bob)->at("saldo").asInt() << "\n";
        tx.commit();
        std::cout << "commit -> Alice=" << saldo(alice) << " Bob=" << saldo(bob) << "\n";
    }

    {
        Transaction tx = konten.beginTransaction();
        tx.updateById(alice, [](Document& d) { d.set("saldo", -999); });
        std::cout << "in Transaktion (vor rollback): Alice=" << tx.findById(alice)->at("saldo").asInt() << "\n";
        tx.rollback();
    }
    std::cout << "nach rollback: Alice=" << saldo(alice) << " (unveraendert)\n";

    {
        Transaction tx = konten.beginTransaction();
        tx.remove(Expression{});
    }
    std::cout << "RAII ohne commit (Destruktor rollt zurueck): size=" << konten.size() << "\n";
    return 0;
}
