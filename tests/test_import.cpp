// Import-Tests (nur mit -DINMEMDB_WITH_SQLITE=ON; sonst trivialer Skip).
#ifdef INMEMDB_HAS_SQLITE

#include <memdb/import.hpp>
#include <memdb/import_sqlite.hpp>
#include <memdb/memdb.hpp>

#include <sqlite3.h>

#include <cstdio>
#include <cstring>
#include <iostream>

namespace {

int g_failures = 0;
int g_checks = 0;
#define CHECK(cond)                                                                              \
    do {                                                                                         \
        ++g_checks;                                                                              \
        if (!(cond)) {                                                                           \
            ++g_failures;                                                                        \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ")\n";        \
        }                                                                                        \
    } while (0)
#define CHECK_THROWS_AS(expr, ExType)                                                            \
    do {                                                                                         \
        ++g_checks;                                                                              \
        bool caught = false;                                                                     \
        try {                                                                                    \
            (void)(expr);                                                                        \
        } catch (const ExType&) {                                                                \
            caught = true;                                                                       \
        } catch (...) {                                                                          \
        }                                                                                        \
        if (!caught) {                                                                           \
            ++g_failures;                                                                        \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": erwartete " #ExType        \
                         " von " #expr "\n";                                                     \
        }                                                                                        \
    } while (0)

using namespace memdb;

} // namespace

int main() {
    const char* path = "inmemdb_import_test.db";
    std::remove(path);

    {
        sqlite3* s = nullptr;
        if (sqlite3_open(path, &s) != SQLITE_OK)
            return 2;
        const char* ddl =
            "CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT NOT NULL, score REAL, data BLOB, active INTEGER);"
            "CREATE UNIQUE INDEX idx_users_name ON users(name);"
            "INSERT INTO users VALUES (1, 'Ada', 9.5, x'DEADBEEF', 1);"
            "INSERT INTO users VALUES (2, 'Bob', 7.25, NULL, 0);"
            "INSERT INTO users VALUES (3, 'Cy', NULL, x'00FF', NULL);";
        char* err = nullptr;
        if (sqlite3_exec(s, ddl, nullptr, nullptr, &err) != SQLITE_OK) {
            std::cerr << "sqlite setup failed: " << (err ? err : "?") << "\n";
            return 2;
        }
        sqlite3_close(s);
    }

    Database db("imported");
    SqliteImportSource src(path);
    ImportStats stats = Importer::import(db, src);

    CHECK(stats.totalRows == 3);
    CHECK(stats.rowsPerTable["users"] == 3);
    CHECK(db.hasCollection("users"));
    Collection users = db.collection("users");
    CHECK(users.size() == 3);

    // Registry: eingebaute Quellen (csv immer, sqlite mit INMEMDB_HAS_SQLITE)
    auto& reg = ImportRegistry::instance();
    CHECK(reg.hasSource("csv"));
    CHECK(reg.hasSource("sqlite"));
    CHECK(reg.createForFile(path) != nullptr);   // .db -> sqlite
    {
        Database db2("via-registry");
        auto stats2 = Importer::import(db2, "sqlite", path);
        CHECK(stats2.totalRows == 3);
        CHECK(db2.hasCollection("users"));
    }

    CHECK(users.hasIndex("name"));
    // schema-konformes Dokument (pk notNull!), verletzt aber den Unique-Index auf name
    CHECK_THROWS_AS(users.insert({{"name", "Ada"}, {"pk", 9}}), UniqueConstraintError);

    CHECK(users.hasSchema());
    auto sch = users.schema();
    CHECK(sch && sch->contains("name"));
    const FieldSchema* nameFs = sch->find("name");
    CHECK(nameFs && nameFs->type() == Value::Type::String && !nameFs->nullable());
    CHECK(sch->find("data") && sch->find("data")->type() == Value::Type::Binary);
    CHECK(sch->contains("pk"));

    auto ada = users.findOne(Field("name") == "Ada");
    CHECK(ada.has_value());
    CHECK(ada->at("id").isInt());
    CHECK(ada->at("id").asInt() == 1);
    CHECK(ada->at("pk").asInt() == 1);
    CHECK(ada->at("score").isDouble());
    CHECK(ada->at("score").asDouble() == 9.5);
    CHECK(ada->at("data").isBinary());
    CHECK(ada->at("data").asBinary() == std::vector<std::uint8_t>({0xDE, 0xAD, 0xBE, 0xEF}));
    CHECK(ada->at("active").isInt());   // SQLite INTEGER -> Int (kein Bool)
    CHECK(ada->at("active").asInt() == 1);

    auto cy = users.findOne(Field("name") == "Cy");
    CHECK(cy.has_value());
    CHECK(cy->at("score").isNull());
    CHECK(cy->at("active").isNull());

    CHECK(users.count(Field("score") >= 8.0) == 1);
    users.count(Field("name") == "Ada");
    CHECK(users.lastQueryCandidateCount() == 1);

    CHECK_THROWS_AS(users.insert({{"id", 10}, {"name", "New"}, {"score", 1.0}, {"data", nullptr}, {"active", 1}, {"pk", 10}, {"extra", 1}}),
                    SchemaError);

    std::remove(path);
    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}

#else

#include <iostream>

int main() {
    std::cout << "SKIP: ohne INMEMDB_WITH_SQLITE=ON\n";
    return 0;
}

#endif
