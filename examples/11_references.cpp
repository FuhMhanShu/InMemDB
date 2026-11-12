#include <memdb/memdb.hpp>

#include <iostream>

using namespace memdb;

std::string refToString(const Reference& r) {
    std::string s = r.fromCollection + "#";
    if (r.fromId)
        s += toString(*r.fromId);
    s += " -> " + r.toCollection + "#";
    if (r.toId)
        s += toString(*r.toId);
    if (!r.relation.empty())
        s += " (" + r.relation + ")";
    return s;
}

void printRefs(const char* label, const std::vector<Reference>& refs) {
    std::cout << label << " (" << refs.size() << "):\n";
    for (const auto& r : refs)
        std::cout << "  " << refToString(r) << "\n";
}

int main() {
    Database db("shop");
    Collection users = db.createCollection("users");
    Collection orders = db.createCollection("orders");

    DocumentId ada = users.insert({{"name", "Ada"}});
    DocumentId o1 = orders.insert({{"total", 42}});

    ReferenceIndex refs(db);

    refs.link("users", ada, "orders", std::nullopt, "hat-bestellungen"); // Datensatz -> Collection
    refs.link("orders", o1, "./users", ada);                              // Datensatz -> Datensatz, relativer Pfad
    refs.link("users", std::nullopt, "orders", std::nullopt, "kennt");   // Collection <-> Collection

    printRefs("outgoing von users#ada", refs.references("users", RefDirection::Outgoing, ada));
    printRefs("incoming auf orders", refs.references("orders", RefDirection::Incoming));
    printRefs("beide Richtungen users#ada", refs.references("users", RefDirection::Both, ada));

    std::cout << "resolve('users', '../orders') = " << refs.resolve("users", "../orders") << "\n";

    orders.removeById(o1);
    printRefs("dangling nach removeById(o1)", refs.dangling());
    std::cout << "pruneDangling entfernt: " << refs.pruneDangling() << "\n";

    refs.link("orders", std::nullopt, "users", ada);
    refs.link("users", ada, "users", ada, "kennt-sich");   // Ringverweis (Selbstverweis)
    printRefs("traverse ab users#ada (outgoing)", refs.traverse("users", ada, RefDirection::Outgoing));
    std::cout << "traverse maxDepth=1: " << refs.traverse("users", ada, RefDirection::Outgoing, 1).size() << " Kanten\n";
    std::cout << std::boolalpha << "hasCycle(users#ada, outgoing) = " << refs.hasCycle("users", ada, RefDirection::Outgoing) << "\n";

    Collection products = db.createCollection("products");
    DocumentId p1 = products.insert({{"name", "Buch"}, {"preis", 10}});
    refs.link("orders", std::nullopt, "products", p1, "enthaelt");
    auto hits = refs.search("users", ada, Field("preis") > 5, RefDirection::Outgoing);
    std::cout << "traversierende Suche ab users#ada (preis > 5): " << hits.size() << " Treffer\n";
    for (const auto& h : hits)
        std::cout << "  " << h.collection << "#" << toString(h.id) << " ueber " << h.path.size() << " Kante(n)\n";

    std::string json = db.toJson();
    Database db2 = Database::fromJson(json, "");
    ReferenceIndex refs2(db2);
    printRefs("nach Snapshot-Roundtrip: incoming auf orders", refs2.references("orders", RefDirection::Incoming));

    return 0;
}
