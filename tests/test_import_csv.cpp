// Import-Tests: CSV-Quelle + ImportRegistry (keine externen Abhaengigkeiten, immer gebaut).
#include <memdb/import.hpp>
#include <memdb/import_csv.hpp>
#include <memdb/memdb.hpp>

#include <algorithm>
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

} // namespace

int main() {
    const std::string csvPath = "inmemdb_import_csv_test.csv";
    writeFile(csvPath,
              "id,name,score,notes\n"
              "1,\"Doe, John\",9.5,\"multi\nline\"\n"
              "2,Jane,,plain\n"
              "3,Bob,7.25,\n");

    // --- ImportRegistry: eingebaute Quellen + Factory-Anmeldung ---
    auto& reg = ImportRegistry::instance();
    CHECK(reg.hasSource("csv"));
    CHECK(!reg.hasSource("gibts-nicht"));
    auto names = reg.sourceNames();
    CHECK(std::find(names.begin(), names.end(), "csv") != names.end());
    CHECK_THROWS_AS(reg.create("gibts-nicht", csvPath), Error);
    CHECK(reg.sourceForFile(csvPath) == "csv");
    CHECK(reg.createForFile(csvPath) != nullptr);   // .csv -> csv
    CHECK_THROWS_AS(reg.createForFile("datei.unbekannt"), Error);
    CHECK_THROWS_AS(reg.sourceForFile("datei.unbekannt"), Error);

    // eigene Quelle per Factory anmelden/verwalten
    reg.registerSource(
        "mock",
        [](const std::string&) {
            struct MockSrc : ImportSource {
                std::vector<TableInfo> tables() const override {
                    TableInfo t;
                    t.name = "mock";
                    ColumnInfo c;
                    c.name = "v";
                    c.type = SourceType::Text;
                    t.columns.push_back(c);
                    return {std::move(t)};
                }
                void readRows(const std::string&,
                              const std::function<void(const std::vector<Value>&)>& cb) const override {
                    cb({Value(std::string("hello"))});
                }
            };
            return std::unique_ptr<ImportSource>(std::make_unique<MockSrc>());
        },
        {".mock"});
    CHECK(reg.hasSource("mock"));
    CHECK(reg.createForFile("irgendwas.mock") != nullptr);
    {
        Database db("reg-test");
        auto stats = Importer::import(db, "mock", "egal.mock");   // per Name ueber Registry
        CHECK(stats.totalRows == 1);
        CHECK(db.hasCollection("mock"));
        Collection c = db.collection("mock");
        CHECK(c.size() == 1);
        auto doc = c.findOne();
        CHECK(doc.has_value());
        CHECK(doc->at("v").asString() == "hello");
    }
    reg.unregisterSource("mock");
    CHECK(!reg.hasSource("mock"));
    CHECK_THROWS_AS(reg.createForFile("irgendwas.mock"), Error);

    // --- CsvImportSource: Parsen (Quoting, CRLF/LF, leere Zellen) ---
    {
        CsvImportSource src(csvPath);
        auto ts = src.tables();
        CHECK(ts.size() == 1);
        CHECK(ts[0].name == "inmemdb_import_csv_test");   // Dateiname ohne Endung
        CHECK(ts[0].columns.size() == 4);
        CHECK(ts[0].columns[0].name == "id");
        CHECK(ts[0].columns[0].type == SourceType::Text); // ohne Inferenz alles Text
        CHECK(ts[0].indexes.empty());
        std::vector<std::vector<Value>> rows;
        src.readRows(ts[0].name, [&](const std::vector<Value>& r) { rows.push_back(r); });
        CHECK(rows.size() == 3);
        CHECK(rows[0][0].asString() == "1");
        CHECK(rows[0][1].asString() == "Doe, John");       // Quote + eingebettetes Komma
        CHECK(rows[0][2].asString() == "9.5");
        CHECK(rows[0][3].asString() == "multi\nline");     // Zeilenumbruch innerhalb Quotes
        CHECK(rows[1][2].isNull());                        // leere Zelle -> Null
        CHECK(rows[2][3].isNull());
        CHECK_THROWS_AS(src.readRows("andere", [](const std::vector<Value>&) {}), Error);
    }

    // --- Import ohne Typ-Inferenz: alles String, Schema wird generiert ---
    {
        Database db("csv-import");
        CsvImportSource src(csvPath);
        auto stats = Importer::import(db, src);
        CHECK(stats.totalRows == 3);
        CHECK(stats.rowsPerTable["inmemdb_import_csv_test"] == 3);
        Collection c = db.collection("inmemdb_import_csv_test");
        CHECK(c.size() == 3);
        CHECK(c.hasSchema());
        auto sch = c.schema();
        CHECK(sch && sch->find("name") && sch->find("name")->type() == Value::Type::String);
        CHECK(sch->find("name")->nullable());
        auto d = c.findOne(Field("name") == "Jane");
        CHECK(d.has_value());
        CHECK(d->at("id").asString() == "2");
        CHECK(d->at("score").isNull());
        // striktes Schema: undefiniertes Feld -> SchemaError
        CHECK_THROWS_AS(
            c.insert({{"id", "9"}, {"name", "X"}, {"score", "1"}, {"notes", ""}, {"extra", 1}}),
            SchemaError);
    }

    // --- Import mit Typ-Inferenz: id -> Int, score -> Real, Rest Text ---
    {
        Database db("csv-infer");
        CsvImportSource::Options o;
        o.inferTypes = true;
        CsvImportSource src(csvPath, o);
        auto ts = src.tables();
        CHECK(ts[0].columns[0].type == SourceType::Integer);
        CHECK(ts[0].columns[1].type == SourceType::Text);
        CHECK(ts[0].columns[2].type == SourceType::Real);
        CHECK(ts[0].columns[3].type == SourceType::Text);
        auto stats = Importer::import(db, src);
        CHECK(stats.totalRows == 3);
        Collection c = db.collection("inmemdb_import_csv_test");
        auto sch = c.schema();
        CHECK(sch && sch->find("id") && sch->find("id")->type() == Value::Type::Int);
        CHECK(sch->find("score")->type() == Value::Type::Double);
        auto d = c.findOne(Field("id") == 1);
        CHECK(d.has_value());
        CHECK(d->at("score").asDouble() == 9.5);
        CHECK(d->at("name").asString() == "Doe, John");
        CHECK(d->at("notes").asString() == "multi\nline");
        CHECK(c.count(Field("score") >= 8.0) == 1);
    }

    // --- Importer::import(db, "csv", path) direkt ueber die Registry ---
    {
        Database db("csv-via-registry");
        auto stats = Importer::import(db, "csv", csvPath);
        CHECK(stats.totalRows == 3);
        CHECK(db.hasCollection("inmemdb_import_csv_test"));
    }

    // --- Eigenes Trennzeichen + kein Header ---
    {
        const std::string semiPath = "inmemdb_import_csv_test_semi.csv";
        writeFile(semiPath, "1;Alpha;2.5\n2;Beta;\n");
        CsvImportSource::Options o;
        o.delimiter = ';';
        o.hasHeader = false;
        CsvImportSource src(semiPath, o);
        auto ts = src.tables();
        CHECK(ts[0].columns.size() == 3);
        CHECK(ts[0].columns[0].name == "col1");
        CHECK(ts[0].columns[2].name == "col3");
        Database db("csv-semi");
        Importer::import(db, src);
        Collection c = db.collection("inmemdb_import_csv_test_semi");
        CHECK(c.size() == 2);
        auto d = c.findOne(Field("col2") == "Alpha");
        CHECK(d.has_value());
        CHECK(d->at("col1").asString() == "1");
        CHECK(d->at("col3").asString() == "2.5");
        std::remove(semiPath.c_str());
    }

    std::remove(csvPath.c_str());
    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
