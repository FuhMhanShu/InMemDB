// Tests: Komplexe Metadaten-Analyse (FA-34, Kategorie 3): rekursives Pfad-Profil
// (verschachtelte Dokumente, Arrays, "[]"-Elementpfade, Binär, Typ-Drift),
// Schema-Varianten mit Fingerprint, Guards (Cap/Tiefe/distinct/minSightings),
// Sampling, Reports (JSON/Markdown).
#include <memdb/memdb.hpp>
#include <memdb/complex_analysis.hpp>

#include <cmath>
#include <cstdint>
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

const ComplexPathAnalysis* findPath(const std::vector<ComplexPathAnalysis>& v,
                                    const std::string& name) {
    for (const auto& p : v)
        if (p.path == name)
            return &p;
    return nullptr;
}

bool near(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps;
}

} // namespace

int main() {
    // 1) Verschachtelte Dokumente: Pfad-Notation, Tiefe, dominantKind, distinct
    {
        Database db("c1");
        auto c = db.createCollection("adressen");
        for (int i = 0; i < 2; ++i)
            c.insert({{"id", i + 1},
                      {"adresse", Value(std::make_shared<Document>(Document{
                                      {"plz", i == 0 ? "10115" : "80331"},
                                      {"stadt", i == 0 ? "Berlin" : "Muenchen"}}))}});
        ComplexCollectionAnalysis res = analyzeComplex(c);
        CHECK(res.collection == "adressen");
        CHECK(res.docCount == 2 && res.sampleSize == 2);
        CHECK(res.fieldCount == 2);
        CHECK(res.pathCount == 4);
        CHECK(res.paths.size() == 4);
        CHECK(res.maxDepth == 2);
        CHECK(res.variantCount == 1);
        CHECK(near(res.schemaConsistency, 1.0));
        CHECK(res.notes.empty());
        const auto* p0 = findPath(res.paths, "adresse");
        CHECK(p0 != nullptr);
        CHECK(p0 != nullptr && p0->depth == 1 && !p0->isElementPath);
        CHECK(p0 != nullptr && p0->dominantKind == PathKind::Document);
        CHECK(p0 != nullptr && p0->count == 2 && p0->distinct == 0);
        CHECK(p0 != nullptr && p0->missing == 0 && near(p0->missingRatio, 0.0));
        CHECK(p0 != nullptr && near(p0->scalarShare, 0.0));
        CHECK(p0 != nullptr && !p0->typeDrift);
        const auto* plz = findPath(res.paths, "adresse.plz");
        CHECK(plz != nullptr);
        CHECK(plz != nullptr && plz->depth == 2);
        CHECK(plz != nullptr && plz->dominantKind == PathKind::Text);
        CHECK(plz != nullptr && plz->count == 2 && plz->distinct == 2);
        CHECK(plz != nullptr && !plz->typeDrift);
        const auto* pid = findPath(res.paths, "id");
        CHECK(pid != nullptr);
        CHECK(pid != nullptr && pid->dominantKind == PathKind::Number);
        CHECK(pid != nullptr && pid->count == 2 && pid->distinct == 2);
        CHECK(pid != nullptr && near(pid->dominantShare, 1.0));
        CHECK(pid != nullptr && near(pid->scalarShare, 1.0));
        CHECK(res.variants.size() == 1);
        CHECK(res.variants[0].docCount == 2);
        CHECK(near(res.variants[0].share, 1.0));
        CHECK(res.variants[0].fields.size() == 4);
        CHECK(res.variants[0].fields[0] == "adresse");
        CHECK(res.variants[0].fields[3] == "id");
        // Felder-Ueberladung
        auto fieldsOnly = analyzeComplexFields(c);
        CHECK(fieldsOnly.size() == 4);
        CHECK(findPath(fieldsOnly, "adresse.stadt") != nullptr);
    }

    // 2) Arrays: Container-Pfad und "[]"-Elementpfad
    {
        Database db("c2");
        auto c = db.createCollection("posts");
        c.insert({{"n", 1}, {"tags", std::vector<Value>{"a", "b"}}});
        c.insert({{"n", 2}, {"tags", std::vector<Value>{"a", "c", "d"}}});
        auto res = analyzeComplex(c);
        CHECK(res.pathCount == 3);
        const auto* tags = findPath(res.paths, "tags");
        CHECK(tags != nullptr);
        CHECK(tags != nullptr && tags->dominantKind == PathKind::Array);
        CHECK(tags != nullptr && tags->count == 2 && tags->depth == 1);
        CHECK(tags != nullptr && tags->distinct == 0);
        CHECK(tags != nullptr && tags->note.empty());
        const auto* el = findPath(res.paths, "tags[]");
        CHECK(el != nullptr);
        CHECK(el != nullptr && el->isElementPath);
        CHECK(el != nullptr && el->depth == 2);
        CHECK(el != nullptr && el->dominantKind == PathKind::Text);
        CHECK(el != nullptr && el->count == 5 && el->distinct == 4);
        CHECK(el != nullptr && el->missing == 0 && near(el->missingRatio, 0.0));
        CHECK(el != nullptr && near(el->scalarShare, 1.0));
        const auto* n = findPath(res.paths, "n");
        CHECK(n != nullptr && n->dominantKind == PathKind::Number && n->count == 2);
    }

    // 3) Arrays aus Dokumenten: "items[]", "items[].preis" etc.
    {
        Database db("c3");
        auto c = db.createCollection("bestellungen");
        c.insert({{"items", std::vector<Value>{
                                Value(std::make_shared<Document>(Document{{"sku", "A"}, {"preis", 1.5}})),
                                Value(std::make_shared<Document>(Document{{"sku", "B"}, {"preis", 2.5}}))}}});
        c.insert({{"items", std::vector<Value>{
                                Value(std::make_shared<Document>(Document{{"sku", "C"}, {"preis", 3.5}}))}}});
        auto res = analyzeComplex(c);
        CHECK(res.pathCount == 4);
        CHECK(res.maxDepth == 3);
        const auto* items = findPath(res.paths, "items");
        CHECK(items != nullptr && items->dominantKind == PathKind::Array && items->count == 2);
        const auto* els = findPath(res.paths, "items[]");
        CHECK(els != nullptr);
        CHECK(els != nullptr && els->dominantKind == PathKind::Document);
        CHECK(els != nullptr && els->count == 3 && els->isElementPath);
        CHECK(els != nullptr && els->note.find("Array aus Dokumenten") == 0);
        const auto* preis = findPath(res.paths, "items[].preis");
        CHECK(preis != nullptr);
        CHECK(preis != nullptr && preis->depth == 3);
        CHECK(preis != nullptr && preis->dominantKind == PathKind::Number);
        CHECK(preis != nullptr && preis->count == 3 && preis->distinct == 3);
        const auto* sku = findPath(res.paths, "items[].sku");
        CHECK(sku != nullptr && sku->dominantKind == PathKind::Text && sku->count == 3);
    }

    // 4) Typ-Drift: mehrere Arten an einem Pfad, Tie-Break, scalarShare
    {
        Database db("c4");
        auto c = db.createCollection("drift");
        c.insert({{"x", 1}});
        c.insert({{"x", "abc"}});
        c.insert({{"x", 2.5}});
        c.insert({{"x", true}});
        c.insert({{"x", 5}});
        c.insert({{"y", "str"}});
        c.insert({{"y", Value(std::make_shared<Document>(Document{{"k", 1}}))}});
        auto res = analyzeComplex(c);
        const auto* x = findPath(res.paths, "x");
        CHECK(x != nullptr);
        CHECK(x != nullptr && x->typeDrift);
        CHECK(x != nullptr && x->count == 5);
        CHECK(x != nullptr && x->kinds.size() == 3);
        CHECK(x != nullptr && x->dominantKind == PathKind::Number);
        CHECK(x != nullptr && near(x->dominantShare, 0.6));
        CHECK(x != nullptr && near(x->scalarShare, 1.0));
        CHECK(x != nullptr && x->distinct == 5);
        CHECK(x != nullptr && x->note.find("Typ-Drift:") == 0);
        const auto* y = findPath(res.paths, "y");
        CHECK(y != nullptr);
        CHECK(y != nullptr && y->typeDrift);
        CHECK(y != nullptr && y->kinds.size() == 2);
        CHECK(y != nullptr && y->dominantKind == PathKind::Text); // Tie-Break: niederer Typ
        CHECK(y != nullptr && near(y->scalarShare, 0.5));
        CHECK(y != nullptr && near(y->dominantShare, 0.5));
        const auto* yk = findPath(res.paths, "y.k");
        CHECK(yk != nullptr && yk->dominantKind == PathKind::Number && yk->count == 1);
    }

    // 5) Schema-Varianten: Fingerprint-Gruppen, share, Konsistenz, Notes
    {
        Database db("c5");
        auto c = db.createCollection("mixed");
        for (int i = 0; i < 8; ++i) {
            if (i < 5)
                c.insert({{"a", i}, {"b", i * 2}});
            else
                c.insert({{"a", i}, {"c", i * 3}});
        }
        auto res = analyzeComplex(c);
        CHECK(res.variantCount == 2);
        CHECK(res.variants.size() == 2);
        CHECK(res.variants[0].docCount == 5);
        CHECK(near(res.variants[0].share, 0.625));
        CHECK(res.variants[0].fields.size() == 2);
        CHECK(res.variants[0].fields[0] == "a" && res.variants[0].fields[1] == "b");
        CHECK(res.variants[1].docCount == 3);
        CHECK(near(res.variants[1].share, 0.375));
        CHECK(res.variants[1].fields[1] == "c");
        CHECK(near(res.schemaConsistency, 0.625));
        CHECK(res.notes.size() == 1);
        CHECK(res.notes[0].find("Varianten") != std::string::npos);
        // b fehlt in 3, c fehlt in 5 Dokumenten
        const auto* b = findPath(res.paths, "b");
        CHECK(b != nullptr && b->count == 5 && b->missing == 3);
        CHECK(b != nullptr && near(b->missingRatio, 3.0 / 8.0));
        const auto* cc = findPath(res.paths, "c");
        CHECK(cc != nullptr && cc->count == 3 && cc->missing == 5);
        CHECK(cc != nullptr && near(cc->missingRatio, 5.0 / 8.0));
    }

    // 6) Binaer und null: Sichtungen, Missing-Quote, Feld-Anzahl
    {
        Database db("c6");
        auto c = db.createCollection("blobs");
        c.insert({{"b", std::vector<std::uint8_t>{1, 2}}, {"s", nullptr}});
        c.insert({{"b", std::vector<std::uint8_t>{3}}});
        c.insert({{"t", 1}});
        auto res = analyzeComplex(c);
        CHECK(res.fieldCount == 3);
        CHECK(res.variantCount == 3);
        const auto* b = findPath(res.paths, "b");
        CHECK(b != nullptr);
        CHECK(b != nullptr && b->dominantKind == PathKind::Binary);
        CHECK(b != nullptr && b->count == 2 && b->distinct == 2);
        CHECK(b != nullptr && b->missing == 1 && near(b->missingRatio, 1.0 / 3.0));
        CHECK(b != nullptr && b->note.find("Bin\303\244rdaten") == 0);
        const auto* s = findPath(res.paths, "s");
        CHECK(s != nullptr);
        CHECK(s != nullptr && s->dominantKind == PathKind::Null);
        CHECK(s != nullptr && s->count == 1 && s->missing == 2);
        CHECK(s != nullptr && near(s->missingRatio, 2.0 / 3.0));
        CHECK(s != nullptr && near(s->scalarShare, 1.0));
        const auto* t = findPath(res.paths, "t");
        CHECK(t != nullptr && t->count == 1 && t->missing == 2);
    }

    // 7) Ueberladungen: vector<Collection> und Database& (__references wird ignoriert)
    {
        Database db("c7");
        auto c1 = db.createCollection("eins");
        c1.insert({{"a", 1}});
        auto c2 = db.createCollection("zwei");
        c2.insert({{"arr", std::vector<Value>{1, 2, 3}}});
        auto refs = db.createCollection("__references");
        refs.insert({{"from", "eins"}, {"to", "zwei"}});
        auto viaDb = analyzeComplex(db);
        CHECK(viaDb.size() == 2); // __references wird ignoriert
        bool sawEins = false, sawZwei = false;
        for (const auto& r : viaDb) {
            if (r.collection == "eins")
                sawEins = r.pathCount == 1;
            if (r.collection == "zwei")
                sawZwei = r.pathCount == 2;
        }
        CHECK(sawEins);
        CHECK(sawZwei);
        std::vector<Collection> colls{c1, c2};
        auto viaVec = analyzeComplex(colls);
        CHECK(viaVec.size() == 2);
        CHECK(viaVec[0].collection == "eins" && viaVec[0].pathCount == 1);
        CHECK(viaVec[1].collection == "zwei" && viaVec[1].pathCount == 2);
        const auto* arr = findPath(viaVec[1].paths, "arr[]");
        CHECK(arr != nullptr && arr->count == 3 && arr->distinct == 3);
    }

    // 8) Sampling: reproduktiv, sampleSize kleiner als docCount, Vollscan-Default
    {
        Database db("c8");
        auto c = db.createCollection("nums");
        for (int i = 0; i < 10; ++i)
            c.insert({{"i", i}});
        ComplexAnalysisOptions opts;
        opts.sampleSize = 4;
        opts.seed = 7;
        auto r1 = analyzeComplex(c, opts);
        CHECK(r1.docCount == 10);
        CHECK(r1.sampleSize == 4);
        CHECK(r1.pathCount == 1);
        const auto* i1 = findPath(r1.paths, "i");
        CHECK(i1 != nullptr && i1->count == 4);
        CHECK(i1 != nullptr && i1->missing == 0);
        CHECK(i1 != nullptr && i1->distinct == 4);
        auto r2 = analyzeComplex(c, opts);
        const auto* i2 = findPath(r2.paths, "i");
        CHECK(i2 != nullptr && i2->count == 4 && i2->distinct == 4);
        CHECK(i1 != nullptr && i2 != nullptr && i1->distinct == i2->distinct);
        // sampleSize >= docCount => Vollscan
        ComplexAnalysisOptions full;
        full.sampleSize = 10;
        auto r3 = analyzeComplex(c, full);
        CHECK(r3.sampleSize == 10);
        CHECK(findPath(r3.paths, "i") != nullptr && findPath(r3.paths, "i")->count == 10);
    }

    // 9) Pfad-Cap: neue Pfade jenseits des Caps werden ignoriert, Note gesetzt
    {
        Database db("c9");
        auto c = db.createCollection("breit");
        c.insert({{"a", 1}, {"b", 2}, {"c", 3}});
        c.insert({{"a", 4}, {"b", 5}});
        ComplexAnalysisOptions opts;
        opts.maxPathsPerCollection = 2;
        auto res = analyzeComplex(c, opts);
        CHECK(res.pathCount == 2);
        CHECK(res.paths.size() == 2);
        CHECK(findPath(res.paths, "a") != nullptr);
        CHECK(findPath(res.paths, "b") != nullptr);
        CHECK(findPath(res.paths, "c") == nullptr);
        CHECK(res.notes.size() == 1);
        bool capNote = false;
        for (const auto& n : res.notes)
            if (n.find("Pfad-Cap") != std::string::npos)
                capNote = true;
        CHECK(capNote);
        // minSightingsPerPath: seltene Pfade werden uebersprungen
        ComplexAnalysisOptions mins;
        mins.minSightingsPerPath = 2;
        auto res2 = analyzeComplex(c, mins);
        CHECK(res2.pathCount == 2);
        CHECK(findPath(res2.paths, "c") == nullptr);
        CHECK(findPath(res2.paths, "a") != nullptr && findPath(res2.paths, "b") != nullptr);
    }

    // 10) Tiefen-Guard: Standard 8, konfigurierbar, Note gesetzt
    {
        Database db("c10");
        auto c = db.createCollection("tief");
        Value inner = Value(std::string("x"));
        for (int lvl = 9; lvl >= 1; --lvl)
            inner = Value(std::make_shared<Document>(Document{{"l" + std::to_string(lvl), inner}}));
        c.insert({{"root", inner}});
        auto res = analyzeComplex(c);
        CHECK(res.pathCount == 8); // root + l1..l7 (Tiefen-Guard stoppt bei Tiefe 8)
        CHECK(res.maxDepth == 8);
        const auto* l7 = findPath(res.paths, "root.l1.l2.l3.l4.l5.l6.l7");
        CHECK(l7 != nullptr);
        CHECK(l7 != nullptr && l7->depth == 8);
        CHECK(findPath(res.paths, "root.l1.l2.l3.l4.l5.l6.l7.l8") == nullptr);
        CHECK(res.notes.size() == 1);
        CHECK(res.notes[0].find("Tiefen-Guard") != std::string::npos);
        ComplexAnalysisOptions opts;
        opts.maxDepth = 2;
        auto res2 = analyzeComplex(c, opts);
        CHECK(res2.pathCount == 2); // root, root.l1
        CHECK(res2.maxDepth == 2);
        CHECK(findPath(res2.paths, "root.l1") != nullptr);
        CHECK(findPath(res2.paths, "root.l1.l2") == nullptr);
        CHECK(res2.notes.size() == 1);
    }

    // 11) distinct-Cap je Pfad (Speicher-Guard)
    {
        Database db("c11");
        auto c = db.createCollection("viele");
        c.insert({{"xs", std::vector<Value>{1, 2, 3, 4, 5}}});
        ComplexAnalysisOptions opts;
        opts.maxDistinctPerPath = 3;
        auto res = analyzeComplex(c, opts);
        const auto* xs = findPath(res.paths, "xs[]");
        CHECK(xs != nullptr);
        CHECK(xs != nullptr && xs->count == 5);
        CHECK(xs != nullptr && xs->distinct == 3);
    }

    // 12) Reports: JSON (maschinenlesbar, kein inf/nan) und Markdown
    {
        Database db("c12");
        auto c = db.createCollection("rep");
        c.insert({{"items", std::vector<Value>{
                                Value(std::make_shared<Document>(Document{{"sku", "A"}, {"preis", 1.5}})),
                                Value(std::make_shared<Document>(Document{{"sku", "A"}, {"preis", 2.5}}))}},
                  {"x", 3}});
        auto res = analyzeComplex(c);
        const std::string j = toJson(res);
        CHECK(j.find("\"collection\":\"rep\"") != std::string::npos);
        CHECK(j.find("\"path\":\"items[].preis\"") != std::string::npos);
        CHECK(j.find("\"dominantKind\":\"number\"") != std::string::npos);
        CHECK(j.find("\"isElementPath\":true") != std::string::npos);
        CHECK(j.find("\"variantCount\":") != std::string::npos);
        CHECK(j.find("\"schemaConsistency\":") != std::string::npos);
        CHECK(j.find(":inf") == std::string::npos);
        CHECK(j.find(":nan") == std::string::npos);
        const auto* p = findPath(res.paths, "items[].sku");
        CHECK(p != nullptr);
        const std::string jp = toJson(*p);
        CHECK(jp.find("\"path\":\"items[].sku\"") != std::string::npos);
        CHECK(jp.find("\"count\":2") != std::string::npos);
        CHECK(jp.find("\"depth\":3") != std::string::npos);
        const std::string md = toMarkdown(res);
        CHECK(md.find("# Komplexe Analyse: rep (FA-34)") == 0);
        CHECK(md.find("| Pfad | Tiefe | Typ | n | distinct | fehlt | Hinweis |") != std::string::npos);
        CHECK(md.find("items[].preis") != std::string::npos);
        CHECK(md.find("## Schema-Varianten") != std::string::npos);
    }

    // 13) Leere Collection: keine Crashes, definierte Nullwerte
    {
        Database db("c13");
        auto c = db.createCollection("leer");
        auto res = analyzeComplex(c);
        CHECK(res.docCount == 0 && res.sampleSize == 0);
        CHECK(res.fieldCount == 0 && res.pathCount == 0);
        CHECK(res.paths.empty());
        CHECK(res.variants.empty() && res.variantCount == 0);
        CHECK(near(res.schemaConsistency, 0.0));
        CHECK(res.notes.empty());
        const std::string j = toJson(res);
        CHECK(j.find("\"pathCount\":0") != std::string::npos);
        CHECK(j.find(":inf") == std::string::npos);
        CHECK(j.find(":nan") == std::string::npos);
        const std::string md = toMarkdown(res);
        CHECK(md.find("Keine Pfade gefunden") != std::string::npos);
        CHECK(pathKindName(PathKind::Text) == "text");
        CHECK(pathKindName(PathKind::Binary) == "binary");
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
