// Tests: Fremdschlüssel-Import als FA-18-Referenzen (v0.28) – SQL-Dump mit
// spalten-level REFERENCES, table-level FOREIGN KEY und CONSTRAINT name FOREIGN KEY;
// Importer verknüpft über pk-Feld bzw. refColumn; --no-foreign-keys-artig via Option;
// unaufloesbare FKs zaehlen in skippedForeignKeys.
#include <memdb/memdb.hpp>
#include <memdb/import_sqldump.hpp>
#include <memdb/references.hpp>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

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

using namespace memdb;

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    f << content;
}

} // namespace

int main() {
    const std::string sqlPath = std::string(std::tmpnam(nullptr)) + ".sql";
    writeFile(sqlPath,
              "CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT NOT NULL);\n"
              "CREATE TABLE orders(\n"
              "  id INTEGER PRIMARY KEY,\n"
              "  user_id INTEGER REFERENCES users(id),\n"          // spalten-level
              "  total REAL,\n"
              "  CONSTRAINT fk_ship FOREIGN KEY (id) REFERENCES users(id)\n" // table-level, benannt (selfref-artig)
              ");\n"
              "CREATE TABLE items(\n"
              "  id INTEGER PRIMARY KEY,\n"
              "  order_id INTEGER,\n"
              "  FOREIGN KEY (order_id) REFERENCES orders(id)\n"     // table-level, ohne Namen
              ");\n"
              "CREATE TABLE dangling(id INTEGER PRIMARY KEY, ghost INTEGER REFERENCES missing(id));\n"
              "INSERT INTO users VALUES (1, 'Ada'), (2, 'Bob');\n"
              "INSERT INTO orders VALUES (10, 1, 99.5), (11, 2, 10.0), (12, 999, 5.0);\n" // 999 loest nicht auf
              "INSERT INTO items VALUES (100, 10), (101, 11), (102, 12);\n"
              "INSERT INTO dangling VALUES (1, 1), (2, 77);\n");

    // 1) Import mit FKs (Default an): Referenzen werden angelegt
    {
        Database db("fk");
        SqlDumpImportSource src(sqlPath);
        ImportStats stats = Importer::import(db, src);
        CHECK(stats.totalRows == 10);
        // orders: 2 gueltige user_id (1,2) + selfref via fk_ship (id 10,11,12 -> users id 10.. nicht vorhanden -> skipped)
        // items: 3 gueltige order_id (10,11,12)
        // dangling: Zieltabelle fehlt -> FK wird uebersprungen
        CHECK(db.hasCollection("__references"));
        ReferenceIndex refs(db);
        CHECK(refs.dangling().empty()); // alle angelegten Refs loesen auf
        CHECK(stats.importedReferences == 5);  // orders.user_id: 2, items.order_id: 3
        CHECK(stats.skippedForeignKeys >= 3);  // orders fk_ship (3x kein users#10..12) + dangling ghost (2x Zieltabelle fehlt)

        // Verknüpfung stimmt: order 10 -> user 1 (ueber pk-Feld, da REFERENCES users(id) -> refColumn "id")
        Collection orders = db.collection("orders");
        auto o10 = orders.findOne(Field("id") == 10);
        CHECK(o10.has_value());
        auto out = refs.references("orders", RefDirection::Outgoing, o10->id());
        CHECK(out.size() == 1);
        if (!out.empty()) {
            CHECK(out[0].toCollection == "users");
            CHECK(out[0].relation == "fk_user_id");
            auto u = db.collection("users").findById(*out[0].toId);
            CHECK(u.has_value() && u->at("name").asString() == "Ada");
        }
        // joinIds funktioniert mit importierten Referenzen
        auto j = refs.joinIds("orders", "fk_user_id", "users", Field("name") == "Bob");
        CHECK(j.size() == 1);
        // items -> orders
        Collection items = db.collection("items");
        auto i100 = items.findOne(Field("id") == 100);
        CHECK(i100.has_value());
        auto outI = refs.references("items", RefDirection::Outgoing, i100->id());
        CHECK(outI.size() == 1 && outI[0].relation == "fk_order_id" && outI[0].toCollection == "orders");
    }

    // 2) Import ohne FKs (importForeignKeys=false): keine Referenzen
    {
        Database db("nofk");
        ImportOptions opts;
        opts.importForeignKeys = false;
        ImportStats stats = Importer::import(db, "sqldump", sqlPath, opts);
        CHECK(stats.importedReferences == 0);
        CHECK(stats.skippedForeignKeys == 0);
        CHECK(!db.hasCollection("__references"));
        CHECK(db.collection("orders").size() == 3);
    }

    // 3) REFERENCES ohne explizite Spalte -> matcht ueber pk-Feld
    {
        const std::string p2 = std::string(std::tmpnam(nullptr)) + ".sql";
        writeFile(p2,
                  "CREATE TABLE a(id INTEGER PRIMARY KEY, v TEXT);\n"
                  "CREATE TABLE b(id INTEGER PRIMARY KEY, a_id INTEGER REFERENCES a);\n" // ohne (id)
                  "INSERT INTO a VALUES (1, 'x'), (2, 'y');\n"
                  "INSERT INTO b VALUES (5, 2), (6, 1);\n");
        Database db("fk2");
        ImportStats stats = Importer::import(db, "sqldump", p2);
        CHECK(stats.importedReferences == 2);
        ReferenceIndex refs(db);
        Collection b = db.collection("b");
        auto b5 = b.findOne(Field("id") == 5);
        auto out = refs.references("b", RefDirection::Outgoing, b5->id());
        CHECK(out.size() == 1);
        if (!out.empty()) {
            auto a = db.collection("a").findById(*out[0].toId);
            CHECK(a.has_value() && a->at("v").asString() == "y"); // a_id=2 -> a.id=2 via pk
        }
        std::remove(p2.c_str());
    }

    std::remove(sqlPath.c_str());
    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
