// Tests: Datencharakter-Kategorisierung (FA-29, assessCharacter): Numeric/Mixed/Complex,
// Feld-Charakteristik ("numeric"/"string"/"boolean"/"complex"/"mixed"/"empty"), Typ-Drift,
// Confidence/Begründung, Database-/vector-Überladungen, Reports (JSON/Markdown).
#include <memdb/memdb.hpp>
#include <memdb/character.hpp>

#include <iostream>
#include <memory>
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

using namespace memdb;

} // namespace

int main() {
    // 1) Kategorie Numeric: fast ausschließlich numerische Felder
    {
        Database db("cat1");
        auto c = db.createCollection("sensors");
        for (int i = 0; i < 10; ++i)
            c.insert({{"temp", 20 + i}, {"hum", 40.5 + i}, {"pressure", 1013 + i}});
        auto cc = assessCharacter(c);
        CHECK(cc.category == DataCategory::Numeric);
        CHECK(cc.confidence >= 0.99 && cc.confidence <= 1.0);
        CHECK(cc.docCount == 10 && cc.sampleSize == 10);
        CHECK(cc.effectiveFields == 3 && cc.numericFields == 3);
        CHECK(cc.fields.size() == 3);
        CHECK(cc.fields[0].character == "numeric");
        CHECK(cc.fields[0].dominantType == Value::Type::Int ||
              cc.fields[0].dominantType == Value::Type::Double);
        CHECK(cc.fields[0].typePurity == 1.0);
        CHECK(toString(DataCategory::Numeric) == "numeric");
    }

    // 2) Kategorie Mixed: Zahlen + Strings + Bool
    {
        Database db("cat2");
        auto c = db.createCollection("products");
        for (int i = 0; i < 10; ++i)
            c.insert({{"name", "p" + std::to_string(i)}, {"price", 9.99 + i},
                      {"active", i % 2 == 0}});
        auto cc = assessCharacter(c);
        CHECK(cc.category == DataCategory::Mixed);
        CHECK(cc.numericFields == 1 && cc.stringFields == 1 && cc.boolFields == 1);
        CHECK(cc.numericFieldRatio > 0.0 && cc.numericFieldRatio < 0.8);
        CHECK(cc.confidence > 0.0);
        CHECK(!cc.rationale.empty());
    }

    // 3) Kategorie Complex: Arrays und verschachtelte Dokumente dominieren
    {
        Database db("cat3");
        auto c = db.createCollection("events");
        for (int i = 0; i < 10; ++i) {
            c.insert({{"id", i},
                      {"tags", Value(std::vector<Value>{Value("a"), Value("b")})},
                      {"meta", Value(std::make_shared<Document>(
                                   std::initializer_list<std::pair<const std::string, Value>>{
                                       {"src", Value("web")}}))}});
        }
        auto cc = assessCharacter(c);
        CHECK(cc.category == DataCategory::Complex);
        CHECK(cc.complexFields == 2);
        CHECK(cc.complexFieldRatio >= 0.25);
        bool sawComplex = false, sawNumeric = false;
        for (const auto& f : cc.fields) {
            if (f.character == "complex")
                sawComplex = true;
            if (f.character == "numeric")
                sawNumeric = true;
            if (f.character == "complex")
                CHECK(f.complexValues);
        }
        CHECK(sawComplex && sawNumeric);
        CHECK(toString(DataCategory::Complex) == "complex");
    }

    // 4) Typ-Drift: Feld mit wechselnden Typen -> character "mixed", Kategorie Complex
    {
        Database db("cat4");
        auto c = db.createCollection("dirty");
        for (int i = 0; i < 10; ++i)
            c.insert({{"stable", 1.0 * i}, {"drift", (i % 2 == 0) ? Value(i) : Value(std::string("x"))}});
        auto cc = assessCharacter(c);
        CHECK(cc.driftFieldRatio >= 0.4);
        CHECK(cc.category == DataCategory::Complex);
        const FieldCharacter* drift = nullptr;
        for (const auto& f : cc.fields)
            if (f.name == "drift")
                drift = &f;
        CHECK(drift != nullptr);
        CHECK(drift->character == "mixed");
        CHECK(drift->typeCounts.size() == 2);
        CHECK(drift->typePurity < 0.6);
        CHECK(drift->typePurity > 0.0);
    }

    // 5) Leere Collection -> Mixed mit Confidence 0
    {
        Database db("cat5");
        auto c = db.createCollection("void");
        auto cc = assessCharacter(c);
        CHECK(cc.category == DataCategory::Mixed);
        CHECK(cc.confidence == 0.0);
        CHECK(cc.effectiveFields == 0);
        CHECK(!cc.rationale.empty());
    }

    // 6) Null-Felder: zählen als present, nicht als effektive Felder
    {
        Database db("cat6");
        auto c = db.createCollection("nulls");
        for (int i = 0; i < 10; ++i)
            c.insert({{"a", i}, {"b", nullptr}});
        auto cc = assessCharacter(c);
        CHECK(cc.effectiveFields == 1);
        CHECK(cc.numericFieldRatio == 1.0);
        const FieldCharacter* b = nullptr;
        for (const auto& f : cc.fields)
            if (f.name == "b")
                b = &f;
        CHECK(b != nullptr);
        CHECK(b->character == "empty");
        CHECK(b->nulls == 10);
        CHECK(b->present == 10);
    }

    // 7) Database-Überladung + __references wird ignoriert; vector cross-database
    {
        Database db("cat7");
        auto a = db.createCollection("nums");
        for (int i = 0; i < 5; ++i)
            a.insert({{"v", i}});
        Database db2("cat7b");
        auto b = db2.createCollection("texts");
        for (int i = 0; i < 5; ++i)
            b.insert({{"t", std::string("x") + std::to_string(i)}, {"n", 1.0 * i}});
        auto fromDb = assessCharacter(db);
        CHECK(fromDb.size() == 1); // __references zählt nicht (existiert hier ohnehin nicht)
        auto both = assessCharacter(std::vector<Collection>{a, b});
        CHECK(both.size() == 2);
        CHECK(both[0].category == DataCategory::Numeric);
        CHECK(both[1].category == DataCategory::Mixed);
    }

    // 8) Sampling: sampleSize begrenzt untersuchte Dokumente
    {
        Database db("cat8");
        auto c = db.createCollection("big");
        for (int i = 0; i < 100; ++i)
            c.insert({{"v", i}});
        DiscoveryOptions opts;
        opts.sampleSize = 10;
        auto cc = assessCharacter(c, opts);
        CHECK(cc.sampleSize == 10);
        CHECK(cc.docCount == 100);
        CHECK(cc.category == DataCategory::Numeric);
    }

    // 9) Reports: JSON enthält Kategorie/Felder, Markdown enthält Tabellenzeilen
    {
        Database db("cat9");
        auto c = db.createCollection("mixed");
        for (int i = 0; i < 6; ++i)
            c.insert({{"a", i}, {"s", std::string("v") + std::to_string(i)}});
        auto ccs = assessCharacter(db);
        std::string j = toJson(ccs);
        CHECK(j.find("\"category\":\"mixed\"") != std::string::npos);
        CHECK(j.find("\"confidence\"") != std::string::npos);
        CHECK(j.find("\"fields\":[") != std::string::npos);
        CHECK(j.find("\"character\":\"numeric\"") != std::string::npos);
        CHECK(j.find("\"character\":\"string\"") != std::string::npos);
        auto cc1 = assessCharacter(c);
        std::string j1 = toJson(cc1);
        CHECK(j1.front() == '{' && j1.back() == '}');
        std::string md = toMarkdown(ccs);
        CHECK(md.find("Datencharakter") != std::string::npos);
        CHECK(md.find("| mixed |") != std::string::npos);
        CHECK(md.find("Kategorie") != std::string::npos);
        // toString deckt alle Kategorien ab
        CHECK(toString(DataCategory::Mixed) == "mixed");
    }

    // 10) Haertung: maxFieldsPerCollection = 0 (kein Crash) und
    //     sampleSize > docCount (Vollscan statt Übersampling)
    {
        Database db("cat10");
        auto c = db.createCollection("edge");
        c.insert({{"v", 1}});
        DiscoveryOptions o;
        o.maxFieldsPerCollection = 0;
        CollectionCharacter cc = assessCharacter(c, o);
        CHECK(cc.fields.empty());
        CHECK(cc.effectiveFields == 0);
        CHECK(cc.category == DataCategory::Mixed);
        CHECK(cc.confidence == 0.0);
        o.maxFieldsPerCollection = 64;
        o.sampleSize = 100; // > docCount => Vollscan
        CollectionCharacter cc2 = assessCharacter(c, o);
        CHECK(cc2.sampleSize == 1);
        CHECK(cc2.fields.size() == 1);
        CHECK(cc2.category == DataCategory::Numeric);
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
