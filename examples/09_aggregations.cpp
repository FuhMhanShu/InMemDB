#include <memdb/memdb.hpp>

#include <iostream>

using namespace memdb;

int main() {
    Database db;
    Collection sales = db.createCollection("sales");
    sales.insert({{"city", "Berlin"}, {"betrag", 10}});
    sales.insert({{"city", "Berlin"}, {"betrag", 20}});
    sales.insert({{"city", "Hamburg"}, {"betrag", 5}});
    sales.insert({{"city", "Hamburg"}, {"betrag", 15}});
    sales.insert({{"city", "München"}, {"betrag", 7.5}});

    std::cout << "count=" << sales.aggregate(AggOp::Count, "").value().asInt()
              << " sum=" << sales.aggregate(AggOp::Sum, "betrag").value().asDouble()
              << " avg=" << sales.aggregate(AggOp::Avg, "betrag").value().asDouble()
              << " min=" << sales.aggregate(AggOp::Min, "betrag").value().asInt()
              << " max=" << sales.aggregate(AggOp::Max, "betrag").value().asInt() << "\n";

    for (const auto& g : sales.groupBy("city", Expression{}, AggOp::Sum, "betrag"))
        std::cout << "  " << toString(g.key) << ": count=" << g.count << " sum=" << toString(*g.value) << "\n";
    return 0;
}
