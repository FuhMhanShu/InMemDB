// Import-Tests: JSON/JSONL, Snapshot-Reimport, SQL-Dump, BSON + Registry-Endungen.
// Keine externen Abhaengigkeiten -> immer gebaut.
#include <memdb/memdb.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
}

// --- Mini-BSON-Builder (Little-Endian) ---
void putU32(std::string& s, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        s += static_cast<char>((v >> (8 * i)) & 0xFF);
}
void putI64(std::string& s, std::int64_t v) {
    std::uint64_t u = static_cast<std::uint64_t>(v);
    for (int i = 0; i < 8; ++i)
        s += static_cast<char>((u >> (8 * i)) & 0xFF);
}
void putF64(std::string& s, double d) {
    std::uint64_t u;
    std::memcpy(&u, &d, 8);
    for (int i = 0; i < 8; ++i)
        s += static_cast<char>((u >> (8 * i)) & 0xFF);
}
void elem(std::string& doc, std::uint8_t type, const std::string& name) {
    doc += static_cast<char>(type);
    doc += name;
    doc += '\0';
}
void elemStr(std::string& doc, const std::string& name, const std::string& v) {
    elem(doc, 0x02, name);
    putU32(doc, static_cast<std::uint32_t>(v.size() + 1));
    doc += v;
    doc += '\0';
}
std::string bsonDoc(std::string body) {
    std::string d;
    putU32(d, static_cast<std::uint32_t>(body.size() + 5));
    d += body;
    d += '\0';
    return d;
}

} // namespace

int main() {
    auto& reg = ImportRegistry::instance();

    // --- Registry: Endungen (laengster Suffix gewinnt) ---
    CHECK(reg.hasSource("csv"));
    CHECK(reg.hasSource("json"));
    CHECK(reg.hasSource("jsonl"));
    CHECK(reg.hasSource("snapshot"));
    CHECK(reg.hasSource("sqldump"));
    CHECK(reg.hasSource("bson"));
    CHECK(reg.sourceForFile("a.json") == "json");
    CHECK(reg.sourceForFile("a.JSON") == "json");
    CHECK(reg.sourceForFile("a.jsonl") == "jsonl");
    CHECK(reg.sourceForFile("a.ndjson") == "jsonl");
    CHECK(reg.sourceForFile("shop.inmemdb.json") == "snapshot"); // laengerer Suffix schlaegt .json
    CHECK(reg.sourceForFile("a.sql") == "sqldump");
    CHECK(reg.sourceForFile("a.dump") == "sqldump");
    CHECK(reg.sourceForFile("a.bson") == "bson");

    // --- JSON-Array (Auto-Modus) ---
    {
        const std::string p = "inmemdb_fmt_data.json";
        writeFile(p, "[{\"id\":1,\"name\":\"Ada\",\"score\":9.5,\"active\":true,"
                     "\"tags\":[\"a\",\"b\"],\"addr\":{\"city\":\"Berlin\"},\"note\":null},"
                     "{\"id\":2,\"name\":\"Bob\",\"score\":7,\"active\":false,\"note\":\"x\"}]");
        Database db("json-test");
        auto stats = Importer::import(db, "json", p);
        CHECK(stats.totalRows == 2);
        Collection c = db.collection("inmemdb_fmt_data");
        CHECK(c.size() == 2);
        CHECK(c.hasSchema());
        auto sch = c.schema();
        CHECK(sch->find("id")->type() == Value::Type::Int);
        CHECK(sch->find("score")->type() == Value::Type::Double); // 9.5 + 7(int) -> Double
        CHECK(sch->find("active")->type() == Value::Type::Bool);
        CHECK(sch->find("tags")->type() == Value::Type::Array);
        CHECK(sch->find("addr")->type() == Value::Type::Document);
        auto d = c.findOne(Field("id") == 1);
        CHECK(d.has_value());
        CHECK(d->at("name").asString() == "Ada");
        CHECK(d->at("score").asDouble() == 9.5);
        CHECK(d->at("active").asBool() == true);
        CHECK(d->at("tags").asArray().size() == 2);
        CHECK(d->at("tags").asArray()[0].asString() == "a");
        CHECK(d->at("addr").asDocument()->find("city")->asString() == "Berlin");
        CHECK(d->at("note").isNull());
        auto d2 = c.findOne(Field("id") == 2);
        CHECK(d2.has_value());
        CHECK(d2->at("score").asInt() == 7); // int bleibt int im Dokument
        std::remove(p.c_str());
    }

    // --- JSONL (pro Zeile ein Objekt; Auto-Fallback und .jsonl-Endung) ---
    {
        const std::string p = "inmemdb_fmt_lines.jsonl";
        writeFile(p, "{\"id\":1,\"v\":\"a\"}\n{\"id\":2,\"v\":\"b\"}\r\n\n{\"id\":3,\"v\":\"c\"}\n");
        CHECK(reg.sourceForFile(p) == "jsonl");
        Database db("jsonl-test");
        auto stats = Importer::import(db, "jsonl", p);
        CHECK(stats.totalRows == 3);
        Collection c = db.collection("inmemdb_fmt_lines");
        CHECK(c.size() == 3);
        CHECK(c.count(Field("v") == "b") == 1);

        // Auto-Modus auf .json-Datei mit mehreren Zeilen -> JSONL-Fallback
        const std::string p2 = "inmemdb_fmt_auto.json";
        writeFile(p2, "{\"id\":10}\n{\"id\":20}\n");
        Database db2("json-auto");
        auto stats2 = Importer::import(db2, reg.sourceForFile(p2), p2);
        CHECK(stats2.totalRows == 2);
        CHECK(db2.collection("inmemdb_fmt_auto").size() == 2);

        // einzelnes Objekt
        const std::string p3 = "inmemdb_fmt_single.json";
        writeFile(p3, "{\"a\": 1, \"b\": \"x\"}\n");
        Database db3("json-single");
        auto stats3 = Importer::import(db3, "json", p3);
        CHECK(stats3.totalRows == 1);
        auto one = db3.collection("inmemdb_fmt_single").findOne();
        CHECK(one.has_value() && one->at("a").asInt() == 1);
        std::remove(p.c_str());
        std::remove(p2.c_str());
        std::remove(p3.c_str());
    }

    // --- Snapshot-Reimport (.inmemdb.json, Schema + Unique-Index aus Metadaten) ---
    {
        const std::string snap = "inmemdb_fmt_snap.inmemdb.json";
        {
            Database src("orig");
            Collection c = src.createCollection("users");
            c.createIndex("name", true);
            Schema sch;
            sch.add("id", Value::Type::Int).notNull();
            sch.add("name", Value::Type::String).notNull();
            sch.add("score", Value::Type::Double);
            c.setSchema(std::move(sch));
            c.insert({{"id", 1}, {"name", "Ada"}, {"score", 9.5}});
            c.insert({{"id", 2}, {"name", "Bob"}, {"score", 7.25}});
            src.saveToFile(snap);
        }
        CHECK(reg.sourceForFile(snap) == "snapshot");
        Database db("reimport");
        auto stats = Importer::import(db, reg.sourceForFile(snap), snap);
        CHECK(stats.totalRows == 2);
        CHECK(stats.rowsPerTable["users"] == 2);
        Collection c = db.collection("users");
        CHECK(c.size() == 2);
        CHECK(c.hasSchema());
        auto sch = c.schema();
        CHECK(sch->find("id") && !sch->find("id")->nullable()); // notNull aus Snapshot uebernommen
        CHECK(sch->find("name")->type() == Value::Type::String);
        CHECK(sch->find("score")->nullable());
        CHECK(c.hasIndex("name"));
        auto d = c.findOne(Field("name") == "Ada");
        CHECK(d.has_value() && d->at("id").asInt() == 1 && d->at("score").asDouble() == 9.5);
        // Unique-Index aus dem Snapshot greift
        CHECK_THROWS_AS(c.insert({{"id", 9}, {"name", "Ada"}, {"score", 1.0}}), UniqueConstraintError);
        std::remove(snap.c_str());
    }

    // --- SQL-Dump (CREATE TABLE + INSERT, Escapes, Blobs, Kommentare) ---
    {
        const std::string p = "inmemdb_fmt_dump.sql";
        writeFile(p,
                  "-- Kommentar\n"
                  "SET NAMES utf8mb4;\n"
                  "/*!40101 SET @OLD=@@OLD */;\n"
                  "CREATE TABLE `users` (\n"
                  "  `id` INT NOT NULL PRIMARY KEY,\n"
                  "  `name` VARCHAR(100) NOT NULL,\n"
                  "  `score` DOUBLE,\n"
                  "  `active` BOOLEAN,\n"
                  "  `data` BLOB,\n"
                  "  UNIQUE KEY `uniq_name` (`name`)\n"
                  ") ENGINE=InnoDB;\n"
                  "INSERT INTO `users` (`id`,`name`,`score`,`active`,`data`) VALUES\n"
                  "(1,'O''Brien',9.5,TRUE,X'DEADBEEF'),\n"
                  "(2,'Ada \"A\"',7.25,false,NULL),\n"
                  "(3,'Cy\\nLine',NULL,TRUE,X'00FF');\n"
                  "INSERT INTO logs (msg) VALUES ('hello'),('world');\n");
        CHECK(reg.sourceForFile(p) == "sqldump");
        Database db("sqldump-test");
        auto stats = Importer::import(db, "sqldump", p);
        CHECK(stats.totalRows == 5); // 3 users + 2 logs
        CHECK(stats.rowsPerTable["users"] == 3);
        CHECK(stats.rowsPerTable["logs"] == 2);

        Collection users = db.collection("users");
        CHECK(users.hasSchema());
        auto sch = users.schema();
        CHECK(sch->find("id")->type() == Value::Type::Int && !sch->find("id")->nullable());
        CHECK(sch->find("name")->type() == Value::Type::String && !sch->find("name")->nullable());
        CHECK(sch->find("score")->type() == Value::Type::Double);
        CHECK(sch->find("active")->type() == Value::Type::Bool);
        CHECK(sch->find("data")->type() == Value::Type::Binary);
        CHECK(sch->contains("pk")); // PK als Feld mitgefuehrt
        CHECK(users.hasIndex("name")); // UNIQUE KEY aus CREATE TABLE

        auto o = users.findOne(Field("name") == "O'Brien"); // ''-Escape
        CHECK(o.has_value());
        CHECK(o->at("id").asInt() == 1);
        CHECK(o->at("pk").asInt() == 1);
        CHECK(o->at("score").asDouble() == 9.5);
        CHECK(o->at("active").asBool() == true);
        CHECK(o->at("data").asBinary() == std::vector<std::uint8_t>({0xDE, 0xAD, 0xBE, 0xEF}));
        auto cy = users.findOne(Field("id") == 3);
        CHECK(cy.has_value());
        CHECK(cy->at("name").asString() == "Cy\nLine"); // \n-Escape
        CHECK(cy->at("score").isNull());
        CHECK(cy->at("data").asBinary() == std::vector<std::uint8_t>({0x00, 0xFF}));
        auto ada = users.findOne(Field("id") == 2);
        CHECK(ada.has_value());
        CHECK(ada->at("name").asString() == "Ada \"A\"");
        CHECK(ada->at("active").asBool() == false);
        CHECK(ada->at("data").isNull());

        // Unique-Index greift (schema-konformes Doc mit Doppelgaenger-Name)
        CHECK_THROWS_AS(
            users.insert({{"id", 9}, {"name", "O'Brien"}, {"score", 1.0}, {"active", true},
                          {"data", nullptr}, {"pk", 9}}),
            UniqueConstraintError);

        // Tabelle ohne CREATE TABLE: Spalten inferiert
        Collection logs = db.collection("logs");
        CHECK(logs.size() == 2);
        CHECK(logs.hasSchema());
        CHECK(logs.schema()->find("msg")->type() == Value::Type::String);
        CHECK(logs.count(Field("msg") == "hello") == 1);
        std::remove(p.c_str());
    }

    // --- BSON (mongodump-aehnlich, von Hand gebaut) ---
    {
        const std::string p = "inmemdb_fmt_test.bson";
        std::string file;
        { // doc1: a int32, b string, c double, d bool, e null, f binary, g subdoc, h array
            std::string b;
            elem(b, 0x10, "a");
            putU32(b, 1);
            elemStr(b, "b", "hi");
            elem(b, 0x01, "c");
            putF64(b, 1.5);
            elem(b, 0x08, "d");
            b += '\x01';
            elem(b, 0x0A, "e");
            elem(b, 0x05, "f");
            putU32(b, 2);
            b += '\x00'; // subtype
            b += '\xDE';
            b += '\xAD';
            elem(b, 0x03, "g");
            {
                std::string gb;
                elem(gb, 0x10, "x");
                putU32(gb, 42);
                b += bsonDoc(gb);
            }
            elem(b, 0x04, "h");
            {
                std::string hb;
                elem(hb, 0x10, "0");
                putU32(hb, 7);
                elemStr(hb, "1", "two");
                b += bsonDoc(hb);
            }
            file += bsonDoc(b);
        }
        { // doc2: a int32 2, b string, c double, d bool false, e int32 7
            std::string b;
            elem(b, 0x10, "a");
            putU32(b, 2);
            elemStr(b, "b", "yo");
            elem(b, 0x01, "c");
            putF64(b, 2.5);
            elem(b, 0x08, "d");
            b += '\x00';
            elem(b, 0x10, "e");
            putU32(b, 7);
            file += bsonDoc(b);
        }
        {
            std::ofstream out(p, std::ios::binary);
            out.write(file.data(), static_cast<std::streamsize>(file.size()));
        }
        CHECK(reg.sourceForFile(p) == "bson");
        Database db("bson-test");
        auto stats = Importer::import(db, "bson", p);
        CHECK(stats.totalRows == 2);
        Collection c = db.collection("inmemdb_fmt_test");
        CHECK(c.size() == 2);
        CHECK(c.hasSchema());
        auto sch = c.schema();
        CHECK(sch->find("a")->type() == Value::Type::Int);
        CHECK(sch->find("b")->type() == Value::Type::String);
        CHECK(sch->find("c")->type() == Value::Type::Double);
        CHECK(sch->find("d")->type() == Value::Type::Bool);
        CHECK(sch->find("e")->type() == Value::Type::Int); // null + int -> Int
        CHECK(sch->find("f")->type() == Value::Type::Binary);
        CHECK(sch->find("g")->type() == Value::Type::Document);
        CHECK(sch->find("h")->type() == Value::Type::Array);
        auto d = c.findOne(Field("a") == 1);
        CHECK(d.has_value());
        CHECK(d->at("b").asString() == "hi");
        CHECK(d->at("c").asDouble() == 1.5);
        CHECK(d->at("d").asBool() == true);
        CHECK(d->at("e").isNull());
        CHECK(d->at("f").asBinary() == std::vector<std::uint8_t>({0xDE, 0xAD}));
        CHECK(d->at("g").asDocument()->find("x")->asInt() == 42);
        CHECK(d->at("h").asArray().size() == 2);
        CHECK(d->at("h").asArray()[0].asInt() == 7);
        CHECK(d->at("h").asArray()[1].asString() == "two");
        auto d2 = c.findOne(Field("a") == 2);
        CHECK(d2.has_value() && d2->at("e").asInt() == 7 && d2->at("d").asBool() == false);
        std::remove(p.c_str());
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
