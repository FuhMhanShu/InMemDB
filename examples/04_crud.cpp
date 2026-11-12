#include <memdb/memdb.hpp>

#include <iostream>

using namespace memdb;

int main() {
    Database db;
    Collection tasks = db.createCollection("tasks");

    auto ids = tasks.insertBulk({Document{{"titel", "A"}, {"offen", true}},
                                 Document{{"titel", "B"}, {"offen", true}},
                                 Document{{"titel", "C"}, {"offen", false}, {"prio", 5}}});
    std::cout << "insertBulk: " << ids.size() << " Dokumente, size=" << tasks.size() << "\n";

    std::size_t n = tasks.update(Field("offen") == true, [](Document& d) { d.set("offen", false); });
    std::cout << "update(offen==true): " << n << " geaendert, noch offen: " << tasks.count(Field("offen") == true) << "\n";

    tasks.updateById(ids[2], [](Document& d) { d.set("prio", 1); });
    std::cout << "updateById prio: " << tasks.findById(ids[2])->at("prio").asInt() << "\n";

    tasks.replace(ids[0], Document{{"titel", "A2"}, {"offen", false}, {"fertig", true}});
    auto d0 = tasks.findById(ids[0]).value();
    std::cout << "replace: titel=" << d0.at("titel").asString() << " felder=" << d0.fieldCount() << "\n";

    std::cout << "remove(offen==true): " << tasks.remove(Field("offen") == true) << "\n";
    std::cout << "removeById: " << tasks.removeById(ids[1]) << ", size=" << tasks.size() << "\n";
    return 0;
}
