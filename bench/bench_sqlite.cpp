/* Vergleichs-Benchmark: InMemDB vs SQLite (:memory:), N = 100.000 Datensaetze.
   Wird nur mit -DINMEMDB_WITH_SQLITE=ON gebaut (braucht libsqlite3 + Header).
   Gleiche Workloads auf beiden Seiten: insert, Index-Aufbau, indexierte Punkt-Query,
   Vollscan, Bereichs-Query, Punkt-Lookup per ID, Aggregation (AVG), Update, JSON-Export. */
#include <memdb/memdb.hpp>

#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace memdb;
using clk = std::chrono::steady_clock;

static double ms(clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

static const int N = 100000;

static void run_inmemdb() {
    std::printf("=== InMemDB ===\n");
    Database db("bench");
    Collection c = db.createCollection("bench");

    auto t0 = clk::now();
    for (int i = 0; i < N; ++i)
        c.insert({{"name", "user" + std::to_string(i)},
                  {"age", i % 100},
                  {"city", (i % 10 == 0 ? "Berlin" : "Hamburg")},
                  {"tags", Value(std::vector<Value>{Value("a"), Value("b"), Value(std::to_string(i % 7))})}});
    auto t1 = clk::now();
    std::printf("insert x%d (4 Felder, schemalos):  %10.1f ms\n", N, ms(t0, t1));

    t0 = clk::now();
    c.createIndex("age");
    t1 = clk::now();
    std::printf("createIndex(age) Aufbau:          %10.1f ms\n", ms(t0, t1));

    t0 = clk::now();
    auto n = c.count(Field("age") == 42);
    t1 = clk::now();
    std::printf("count(age==42) MIT Index:         %10.1f ms  (n=%zu)\n", ms(t0, t1), n);

    t0 = clk::now();
    n = c.count(Field("city") == "Berlin");
    t1 = clk::now();
    std::printf("count(city=Berlin) VOLLSCAN:      %10.1f ms  (n=%zu)\n", ms(t0, t1), n);

    t0 = clk::now();
    auto ids = c.findIds(Field("age") >= 90);
    t1 = clk::now();
    std::printf("findIds(age>=90) Bereich:         %10.1f ms  (n=%zu)\n", ms(t0, t1), ids.size());

    t0 = clk::now();
    for (int i = 0; i < 100000; ++i) { auto d = c.findById(DocumentId(i % N + 1)); (void)d; }
    t1 = clk::now();
    std::printf("findById x100k:                   %10.1f ms\n", ms(t0, t1));

    t0 = clk::now();
    double avg = c.aggregate(AggOp::Avg, "age").value().asDouble();
    t1 = clk::now();
    std::printf("aggregate AVG(age):               %10.1f ms  (avg=%.2f)\n", ms(t0, t1), avg);

    t0 = clk::now();
    auto ch = c.update(Field("age") >= 95, [](Document& d) { d.set("flag", true); });
    t1 = clk::now();
    std::printf("update(age>=95, +flag):           %10.1f ms  (geaendert=%zu)\n", ms(t0, t1), ch);

    t0 = clk::now();
    std::string json = db.toJson();
    t1 = clk::now();
    std::printf("toJson-Export:                    %10.1f ms  (%zu KB)\n", ms(t0, t1), json.size() / 1024);
}

static void sql(sqlite3* db, const char* z) {
    char* err = nullptr;
    if (sqlite3_exec(db, z, nullptr, nullptr, &err) != SQLITE_OK) {
        std::fprintf(stderr, "SQL-Fehler bei [%s]: %s\n", z, err ? err : "?");
        std::exit(1);
    }
}

static sqlite3_int64 q_int(sqlite3* db, const char* z) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, z, -1, &st, nullptr) != SQLITE_OK) {
        std::fprintf(stderr, "prepare failed [%s]: %s\n", z, sqlite3_errmsg(db));
        std::exit(1);
    }
    sqlite3_int64 v = 0;
    if (sqlite3_step(st) == SQLITE_ROW)
        v = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return v;
}

static void run_sqlite(bool in_tx) {
    std::printf("=== SQLite %s (:memory:) %s ===\n", sqlite3_libversion(),
                in_tx ? "(Bulk-Insert in 1 Transaktion)" : "(Autocommit je Insert)");
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_MEMORY,
                        nullptr) != SQLITE_OK) {
        std::fprintf(stderr, "open failed\n");
        std::exit(1);
    }
    sql(db, "CREATE TABLE bench(id INTEGER PRIMARY KEY, name TEXT, age INTEGER, city TEXT, tags TEXT, flag INTEGER DEFAULT 0)");

    sqlite3_stmt* ins = nullptr;
    sqlite3_prepare_v2(db, "INSERT INTO bench(id,name,age,city,tags) VALUES(?,?,?,?,?)", -1, &ins, nullptr);

    auto t0 = clk::now();
    if (in_tx)
        sql(db, "BEGIN");
    for (int i = 1; i <= N; ++i) {
        std::string name = "user" + std::to_string(i - 1);
        std::string tags = "a,b," + std::to_string(i % 7);
        sqlite3_bind_int64(ins, 1, i);
        sqlite3_bind_text(ins, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(ins, 3, i % 100);
        sqlite3_bind_text(ins, 4, (i % 10 == 0 ? "Berlin" : "Hamburg"), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 5, tags.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(ins) != SQLITE_DONE) {
            std::fprintf(stderr, "insert failed: %s\n", sqlite3_errmsg(db));
            std::exit(1);
        }
        sqlite3_reset(ins);
    }
    if (in_tx)
        sql(db, "COMMIT");
    auto t1 = clk::now();
    sqlite3_finalize(ins);
    std::printf("insert x%d (5 Spalten, relational): %9.1f ms\n", N, ms(t0, t1));

    t0 = clk::now();
    sql(db, "CREATE INDEX idx_age ON bench(age)");
    t1 = clk::now();
    std::printf("CREATE INDEX age Aufbau:          %10.1f ms\n", ms(t0, t1));

    t0 = clk::now();
    auto n = q_int(db, "SELECT COUNT(*) FROM bench WHERE age=42");
    t1 = clk::now();
    std::printf("COUNT WHERE age=42 (Index):       %10.1f ms  (n=%lld)\n", ms(t0, t1), (long long)n);

    t0 = clk::now();
    n = q_int(db, "SELECT COUNT(*) FROM bench WHERE city='Berlin'");
    t1 = clk::now();
    std::printf("COUNT WHERE city='Berlin' (SCAN): %9.1f ms  (n=%lld)\n", ms(t0, t1), (long long)n);

    t0 = clk::now();
    n = q_int(db, "SELECT COUNT(*) FROM bench WHERE age>=90");
    t1 = clk::now();
    std::printf("COUNT WHERE age>=90 (Bereich):    %10.1f ms  (n=%lld)\n", ms(t0, t1), (long long)n);

    t0 = clk::now();
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db, "SELECT name FROM bench WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, N / 2);
    sqlite3_step(st);
    sqlite3_finalize(st);
    t1 = clk::now();
    std::printf("Punkt-Lookup id=%d:               %10.1f ms\n", N / 2, ms(t0, t1));

    t0 = clk::now();
    sqlite3_prepare_v2(db, "SELECT AVG(age) FROM bench", -1, &st, nullptr);
    sqlite3_step(st);
    double avg = sqlite3_column_double(st, 0);
    sqlite3_finalize(st);
    t1 = clk::now();
    std::printf("AVG(age) (Vollscan):              %10.1f ms  (avg=%.2f)\n", ms(t0, t1), avg);

    t0 = clk::now();
    sql(db, "UPDATE bench SET flag=1 WHERE age>=95");
    t1 = clk::now();
    std::printf("UPDATE flag=1 WHERE age>=95:      %10.1f ms  (changes=%d)\n", ms(t0, t1), sqlite3_changes(db));

    t0 = clk::now();
    auto len = q_int(db,
                     "SELECT LENGTH(json_group_array(json_object('id',id,'name',name,'age',age,'city',city,'tags',tags))) FROM bench");
    t1 = clk::now();
    std::printf("JSON-Export (json_group_array):   %10.1f ms  (%lld KB)\n", ms(t0, t1), (long long)(len / 1024));

    sqlite3_close_v2(db);
}

int main() {
    run_inmemdb();
    std::printf("\n");
    run_sqlite(false);
    std::printf("\n");
    run_sqlite(true);
    return 0;
}
