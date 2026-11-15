// Regressionstest Locale-Unabhaengigkeit (Bugfix v0.23.1): Bei aktiver
// de_DE-Locale (Komma als Dezimaltrennzeichen) duerfen Fliesskommawerte
// weder im JSON-Snapshot (toJson/saveToFile/verschluesselt) mit Komma
// geschrieben noch beim Parsen falsch gelesen werden - immer Punkt.
#include <memdb/memdb.hpp>

#include <clocale>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <locale>
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

// Simuliert de_DE: Komma als Dezimaltrennzeichen, Punkt als Gruppierung.
struct CommaPunct : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
    char do_thousands_sep() const override { return '.'; }
    std::string do_grouping() const override { return "\3"; }
};

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

} // namespace

int main() {
    // 1) echte C-Locale setzen, falls im System vorhanden (CI-Runner haben
    //    de_DE.UTF-8 oft installiert); egal ob oder nicht: C++-Facet setzen.
    bool cLocaleSet = false;
    for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "de_DE.ISO-8859-1", "de_DE@euro"})
        if (std::setlocale(LC_ALL, name)) {
            cLocaleSet = true;
            break;
        }
    std::locale::global(std::locale(std::locale::classic(), new CommaPunct));
    std::cout << "C-Locale de_DE aktiv: " << (cLocaleSet ? "ja" : "nein (nur C++-Facet)") << "\n";

    using namespace memdb;

    // 2) Schreiben: toJson muss Punkt liefern, niemals Komma
    Database db("locale");
    auto coll = db.createCollection("preise");
    coll.insert({{"name", "A"}, {"preis", 1.5}, {"genau", 5.0}, {"kl", 0.1}, {"neg", -2.25}});
    coll.insert({{"name", "B"}, {"preis", 49.99}});
    std::string json = db.toJson();
    CHECK(contains(json, "\"preis\":1.5"));
    CHECK(contains(json, "\"genau\":5.0"));
    CHECK(contains(json, "\"kl\":0.1"));
    CHECK(contains(json, "\"neg\":-2.25"));
    CHECK(contains(json, "\"preis\":49.99"));
    CHECK(!contains(json, "1,5"));
    CHECK(!contains(json, "5,0"));
    CHECK(!contains(json, "49,99"));
    CHECK(!contains(json, "0,1"));

    std::string collJson = coll.toJson();
    CHECK(contains(collJson, "1.5") && !contains(collJson, "1,5"));

    // 3) toString(Value) ebenfalls mit Punkt
    CHECK(toString(Value(2.5)) == "2.5");
    CHECK(toString(Value(0.1)) == "0.1");
    CHECK(toString(Value(-2.25)) == "-2.25");
    CHECK(toString(coll.findById(coll.findIds(Field("name") == "A").front()).value().at("preis")) == "1.5");

    // 4) Lesen/Round-Trip: Punkt wird korrekt geparst (exakt, Round-Trip)
    Database db2 = Database::fromJson(json, "");
    auto c2 = db2.collection("preise");
    auto ids = c2.findIds(Field("name") == "A");
    CHECK(ids.size() == 1);
    Document d = c2.findById(ids.front()).value();
    CHECK(d.at("preis").asDouble() == 1.5);
    CHECK(d.at("genau").asDouble() == 5.0);
    CHECK(d.at("kl").asDouble() == 0.1);   // exakter Round-Trip von 0.1
    CHECK(d.at("neg").asDouble() == -2.25);

    // 5) direktes Parsen von JSON-Zahlen mit Punkt (Snapshot-Format)
    Database db3 = Database::fromJson(
        "{\"name\":\"t\",\"collections\":{\"c\":{\"indexes\":[],\"documents\":["
        "{\"x\":2.5,\"y\":1e3,\"z\":-0.5,\"w\":1.0e-7}]}}}",
        "");
    auto d3 = db3.collection("c").findById(db3.collection("c").findIds(Field("x") == 2.5).front()).value();
    CHECK(d3.at("x").asDouble() == 2.5);
    CHECK(d3.at("y").asDouble() == 1000.0);
    CHECK(d3.at("z").asDouble() == -0.5);
    CHECK(d3.at("w").asDouble() == 1.0e-7);

    // 6) Datei-Snapshot (plain + verschluesselt) unter aktiver de_DE-Locale
    const std::string path = std::string(std::tmpnam(nullptr)) + ".inmemdb.json";
    const std::string encPath = path + ".enc";
    db.saveToFile(path);
    Database db4 = Database::loadFromFile(path);
    auto d4 = db4.collection("preise").findById(db4.collection("preise").findIds(Field("name") == "B").front()).value();
    CHECK(d4.at("preis").asDouble() == 49.99);

    db.saveToFileEncrypted(encPath, "passwort");
    Database db5 = Database::loadFromFileEncrypted(encPath, "passwort");
    auto d5 = db5.collection("preise").findById(db5.collection("preise").findIds(Field("name") == "A").front()).value();
    CHECK(d5.at("preis").asDouble() == 1.5);
    CHECK(d5.at("kl").asDouble() == 0.1);
    std::remove(path.c_str());
    std::remove(encPath.c_str());

    // 7) DSL-Parsing von Fliesskomma-Literalen (from_chars, locale-unabhaengig)
    auto hit = coll.findIds(parseQuery("preis == 49.99", &db, "preise"));
    CHECK(hit.size() == 1);
    auto hit2 = coll.findIds(parseQuery("preis > 1.4 && preis < 2.0", &db, "preise"));
    CHECK(hit2.size() == 1);

    // Locale zuruecksetzen ( Prozess endet ohnehin, aber sauber)
    std::locale::global(std::locale::classic());
    std::setlocale(LC_ALL, "C");

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
