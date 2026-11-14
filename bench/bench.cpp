#include <memdb/memdb.hpp>

#include <chrono>
#include <iostream>
#include <string>

using namespace memdb;
using clk = std::chrono::steady_clock;

static double ms(clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

int main() {
    const int N = 100000;
    Database db("bench");
    Collection c = db.createCollection("bench");

    auto t0 = clk::now();
    for (int i = 0; i < N; ++i)
        c.insert({{"name", "user" + std::to_string(i)},
                  {"age", i % 100},
                  {"city", (i % 10 == 0 ? "Berlin" : "Hamburg")},
                  {"tags", Value(std::vector<Value>{Value("a"), Value("b"), Value(std::to_string(i % 7))})}});
    auto t1 = clk::now();
    std::cout << "insert x" << N << " (schema-los, 4 Felder):        " << ms(t0, t1) << " ms\n";

    t0 = clk::now();
    c.createIndex("age");
    t1 = clk::now();
    std::cout << "createIndex(age) auf " << N << " Docs (Aufbau):     " << ms(t0, t1) << " ms\n";

    t0 = clk::now();
    auto n = c.count(Field("age") == 42);
    t1 = clk::now();
    std::cout << "count(age==42) MIT Index:  " << n << " in " << ms(t0, t1)
              << " ms (Kandidaten=" << c.lastQueryCandidateCount() << ")\n";

    t0 = clk::now();
    n = c.count(Field("city") == "Berlin");
    t1 = clk::now();
    std::cout << "count(city=Berlin) VOLLSCAN: " << n << " in " << ms(t0, t1)
              << " ms (Kandidaten=" << c.lastQueryCandidateCount() << ")\n";

    t0 = clk::now();
    auto cur = c.find(Field("age") >= 90);
    t1 = clk::now();
    std::cout << "find(age>=90): " << cur.size() << " Treffer, ALLE Dokumente kopiert: " << ms(t0, t1) << " ms\n";

    t0 = clk::now();
    auto ids = c.findIds(Field("age") >= 90);
    t1 = clk::now();
    std::cout << "findIds(age>=90): " << ids.size() << " IDs OHNE Dokument-Kopien:      " << ms(t0, t1) << " ms\n";

    t0 = clk::now();
    std::size_t fe = c.forEach(Field("age") >= 90, [](const Document&) {});
    t1 = clk::now();
    std::cout << "forEach(age>=90): " << fe << " Treffer ohne Kopien:               " << ms(t0, t1) << " ms\n";

    t0 = clk::now();
    auto one = c.findById(DocumentId(N / 2));
    t1 = clk::now();
    std::cout << "findById (1 Kopie):                              " << ms(t0, t1) << " ms\n";

    t0 = clk::now();
    auto ch = c.update(Field("age") >= 95, [](Document& d) { d.set("flag", true); });
    t1 = clk::now();
    std::cout << "update(age>=95): " << ch << " geaendert (Kopie+Move je Doc): " << ms(t0, t1) << " ms\n";

    t0 = clk::now();
    {
        Transaction tx = c.beginTransaction();
        tx.insert({{"name", "tx"}});
        tx.commit();
    }
    t1 = clk::now();
    std::cout << "Transaction begin+commit auf " << N << " Docs (CoW-Undo v0.19, O(1)): " << ms(t0, t1) << " ms\n";

    t0 = clk::now();
    {
        Transaction tx = c.beginTransaction();
        tx.insert({{"name", "tx-rb"}});
        tx.rollback();
    }
    t1 = clk::now();
    std::cout << "Transaction begin+rollback auf " << N << " Docs (Undo-Replay+Reindex): " << ms(t0, t1) << " ms\n";

    t0 = clk::now();
    std::string json = db.toJson();
    t1 = clk::now();
    std::cout << "toJson (" << json.size() / 1024 << " KB):                          " << ms(t0, t1) << " ms\n";

    t0 = clk::now();
    Database dbLoad = Database::fromJson(json, "copy");
    t1 = clk::now();
    std::cout << "fromJson (parse+bulk-load " << N << "):  " << ms(t0, t1) << " ms, size="
              << dbLoad.collection("bench").size() << "\n";

    auto doc = c.findById(DocumentId(1)).value();
    t0 = clk::now();
    for (int i = 0; i < 100000; ++i) {
        Document copy = doc;
        (void)copy;
    }
    t1 = clk::now();
    std::cout << "Document-Kopie (1 kleines Doc) x100k:            " << ms(t0, t1) << " ms\n";
    return 0;
}
