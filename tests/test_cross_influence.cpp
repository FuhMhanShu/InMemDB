// Tests: Cross-Collection-Einfluss-Analyse (FA-32): Abhaengigkeiten ueber zwei
// Collections – Joins ueber deklarierte FA-18-Referenzen und entdeckte
// Fremdschluessel (FA-27), FA-31-Maschinerie auf der verknuepften Menge,
// Trivial-Suppression, Guards (minJoinPairs/minJoinCoverage/minFkConfidence),
// Kardinalitaet, Reports.
#include <memdb/memdb.hpp>
#include <memdb/cross_influence.hpp>
#include <memdb/references.hpp>

#include <cmath>
#include <iomanip>
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

using namespace memdb;

const CrossPairAnalysis* findPair(const CrossInfluenceResult& r, const std::string& collA,
                                  const std::string& collB, const std::string& origin) {
    for (const auto& p : r.pairs)
        if (p.collectionA == collA && p.collectionB == collB && p.origin == origin)
            return &p;
    return nullptr;
}

const FieldInfluence* findField(const CrossPairAnalysis& p, const std::string& name) {
    for (const auto& f : p.fields)
        if (f.field == name)
            return &f;
    return nullptr;
}

const CrossDependency* findDep(const CrossPairAnalysis& p, const std::string& src,
                               const std::string& dst) {
    for (const auto& d : p.dependencies)
        if (d.source == src && d.target == dst)
            return &d;
    return nullptr;
}

bool near(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps;
}

} // namespace

int main() {
    // =====================================================================
    // 1) Deklarierte Referenz (FA-18): orders -> kunden, Dokument-Kanten
    // =====================================================================
    {
        Database db("ci1");
        auto kunden = db.createCollection("kunden");
        auto orders = db.createCollection("orders");
        const int kNr[] = {1, 2, 3, 4, 5, 6};
        const int bran[] = {10, 10, 20, 20, 30, 30};
        const int preisK[] = {100, 100, 200, 200, 300, 300};
        std::vector<DocumentId> kid;
        for (int i = 0; i < 6; ++i)
            kid.push_back(kunden.insert({{"kundenNr", kNr[i]}, {"branche", bran[i]}}));
        std::vector<DocumentId> oid;
        for (int i = 0; i < 12; ++i) {
            const int k = i / 2; // Kunde 0..5
            oid.push_back(orders.insert({{"kundenId", kNr[k]},
                                         {"menge", (i % 2) + 1},
                                         {"preis", preisK[k]}}));
        }
        ReferenceIndex ri(db); // legt __references an
        for (int i = 0; i < 12; ++i)
            ri.link("orders", oid[i], "kunden", kid[i / 2], "kunde");

        CrossInfluenceOptions opts;
        opts.includeDiscovered = false; // deklarierte Quelle isolieren
        CrossInfluenceResult r = analyzeCrossInfluence(db, opts);

        CHECK(r.pairs.size() == 1);
        CHECK(r.note.find("1 deklariert") != std::string::npos);
        const CrossPairAnalysis* p = r.pairs.empty() ? nullptr : &r.pairs[0];
        CHECK(p != nullptr);
        if (p) {
            CHECK(p->origin == "declared");
            CHECK(p->relation == "kunde");
            CHECK(p->collectionA == "orders");
            CHECK(p->collectionB == "kunden");
            CHECK(p->joinFieldA.empty());
            CHECK(p->keyFieldB.empty());
            CHECK(p->docsA == 12);
            CHECK(p->matchedA == 12);
            CHECK(p->rows == 12);
            CHECK(near(p->joinCoverage, 1.0));
            CHECK(p->cardinality == "n:1");
            CHECK(p->fields.size() == 5);

            // Cross-Abhaengigkeiten: Quelle und Ziel in verschiedenen Collections
            CHECK(p->dependencies.size() == 4);
            const auto* d1 = findDep(*p, "orders.kundenId", "kunden.branche");
            CHECK(d1 != nullptr);
            CHECK(d1 != nullptr && near(d1->coverage, 1.0));
            CHECK(d1 != nullptr && d1->groups == 6);
            CHECK(d1 != nullptr && d1->pairs == 12);
            CHECK(findDep(*p, "kunden.branche", "orders.preis") != nullptr);
            CHECK(findDep(*p, "kunden.kundenNr", "orders.preis") != nullptr);
            CHECK(findDep(*p, "kunden.kundenNr", "orders.kundenId") != nullptr);
            CHECK(findDep(*p, "orders.kundenId", "orders.preis") ==
                  nullptr); // innerhalb A: nicht in dependencies
            const auto* dbb = findDep(*p, "kunden.branche", "orders.preis");
            CHECK(dbb != nullptr && dbb->groups == 3);

            // orders.kundenId: Mediator (bestimmt 2 Felder, selbst referenziert)
            const auto* kidI = findField(*p, "orders.kundenId");
            CHECK(kidI != nullptr);
            CHECK(kidI != nullptr && kidI->partners == 4);
            CHECK(kidI != nullptr && kidI->determines == 2);
            CHECK(kidI != nullptr && kidI->determinedBy == 1);
            CHECK(kidI != nullptr && kidI->strongCorrelations == 3);
            CHECK(kidI != nullptr && near(kidI->maxAbsPearson, 1.0));
            CHECK(kidI != nullptr && kidI->strongestPartner == "kunden.kundenNr");
            CHECK(kidI != nullptr && kidI->rSquaredComputed);
            CHECK(kidI != nullptr && near(kidI->rSquared, 0.999999999878047, 1e-6));
            CHECK(kidI != nullptr && kidI->regressionRows == 12);
            CHECK(kidI != nullptr && kidI->role == FieldRole::Mediator);
            CHECK(kidI != nullptr && near(kidI->influenceScore, 0.5140457218912693, 1e-6));
            CHECK(kidI != nullptr &&
                  kidI->rationale.find("bestimmt funktional") != std::string::npos);
            CHECK(kidI != nullptr &&
                  kidI->rationale.find("-> kunden.branche") != std::string::npos);

            // kunden.kundenNr: staerkster Treiber (bestimmt 3 Felder)
            const auto* knrI = findField(*p, "kunden.kundenNr");
            CHECK(knrI != nullptr);
            CHECK(knrI != nullptr && knrI->determines == 3);
            CHECK(knrI != nullptr && knrI->determinedBy == 0);
            CHECK(knrI != nullptr && knrI->role == FieldRole::Mediator);
            CHECK(knrI != nullptr && near(knrI->hubRatio, 0.7280914437337574, 1e-9));
            CHECK(knrI != nullptr && near(knrI->influenceScore, 0.5890457218912694, 1e-6));

            // kunden.branche: Mediator (bestimmt orders.preis, selbst abhaengig)
            const auto* branI = findField(*p, "kunden.branche");
            CHECK(branI != nullptr);
            CHECK(branI != nullptr && branI->determines == 1);
            CHECK(branI != nullptr && branI->determinedBy == 2);
            CHECK(branI != nullptr && branI->role == FieldRole::Mediator);

            // orders.preis: abhaengig (von 3 Feldern funktional bestimmt)
            const auto* preisI = findField(*p, "orders.preis");
            CHECK(preisI != nullptr);
            CHECK(preisI != nullptr && preisI->determinedBy == 3);
            CHECK(preisI != nullptr && preisI->determines == 0);
            CHECK(preisI != nullptr && preisI->role == FieldRole::Dependent);
            CHECK(preisI != nullptr && near(preisI->rSquared, 1.0, 1e-6));
            CHECK(preisI != nullptr &&
                  preisI->rationale.find("funktional abhaengig") != std::string::npos);

            // orders.menge: unabhaengig (r = 0 gegen alle anderen Felder)
            const auto* mengeI = findField(*p, "orders.menge");
            CHECK(mengeI != nullptr);
            CHECK(mengeI != nullptr && mengeI->strongCorrelations == 0);
            CHECK(mengeI != nullptr && mengeI->determines == 0);
            CHECK(mengeI != nullptr && mengeI->determinedBy == 0);
            CHECK(mengeI != nullptr && mengeI->role == FieldRole::Independent);
            CHECK(mengeI != nullptr && near(mengeI->maxAbsPearson, 0.0));
            CHECK(mengeI != nullptr && near(mengeI->influenceScore, 0.2, 1e-9));

            // Ranking und Top-Treiber (qualifizierter Name)
            CHECK(p->topDriver == "kunden.kundenNr");
            CHECK(p->fields[0].field == "kunden.kundenNr");
            CHECK(p->fields[0].influenceScore >= p->fields[1].influenceScore);
            CHECK(p->fields[1].influenceScore >= p->fields[2].influenceScore);
            CHECK(p->note.find("orders.kundenId -> kunden.branche") != std::string::npos);
            CHECK(p->note.find("Treiber: kunden.kundenNr, orders.kundenId, kunden.branche") !=
                  std::string::npos);
            CHECK(p->note.find("abhaengig: orders.preis") != std::string::npos);
        }

        // 1b) Beide Quellen zusammen (Default): deklariert UND entdeckt vorhanden
        CrossInfluenceResult mixed = analyzeCrossInfluence(db);
        CHECK(mixed.pairs.size() >= 2);
        CHECK(findPair(mixed, "orders", "kunden", "declared") != nullptr);
        bool hasDiscovered = false;
        for (const auto& mp : mixed.pairs)
            if (mp.origin == "discovered")
                hasDiscovered = true;
        CHECK(hasDiscovered);
        CHECK(mixed.note.find("entdeckt") != std::string::npos);

        // 1c) minFkConfidence ueber Maximum: entdeckte Quellen fallen weg
        CrossInfluenceOptions high;
        high.includeDeclared = false;
        high.minFkConfidence = 1.01;
        CrossInfluenceResult none = analyzeCrossInfluence(db, high);
        CHECK(none.pairs.empty());

        // 1d) beide Quellen abgeschaltet
        CrossInfluenceOptions off;
        off.includeDeclared = false;
        off.includeDiscovered = false;
        CrossInfluenceResult offR = analyzeCrossInfluence(db, off);
        CHECK(offR.pairs.empty());
        CHECK(offR.note.find("keine verknuepfbaren") != std::string::npos);

        // 1e) Reports
        if (!r.pairs.empty()) {
            const std::string js = toJson(r.pairs[0]);
            CHECK(js.find("\"origin\":\"declared\"") != std::string::npos);
            CHECK(js.find("\"collectionA\":\"orders\"") != std::string::npos);
            CHECK(js.find("\"source\":\"orders.kundenId\"") != std::string::npos);
            CHECK(js.find("\"target\":\"kunden.branche\"") != std::string::npos);
            CHECK(js.find("\"topDriver\":\"kunden.kundenNr\"") != std::string::npos);
            CHECK(js.find("\"role\":\"mediator\"") != std::string::npos);
            CHECK(js.find("\"coverage\":1") != std::string::npos);
            CHECK(js.find("\"cardinality\":\"n:1\"") != std::string::npos);
            const std::string md = toMarkdown(r.pairs[0]);
            CHECK(md.find("# Cross-Einfluss: orders -> kunden") != std::string::npos);
            CHECK(md.find("## Cross-Abhaengigkeiten") != std::string::npos);
            CHECK(md.find("| kunden.kundenNr | mediator |") != std::string::npos);
            const std::string rjs = toJson(r);
            CHECK(rjs.find("\"pairs\":[") != std::string::npos);
            CHECK(rjs.find("\"note\":") != std::string::npos);
            const std::string rmd = toMarkdown(r);
            CHECK(rmd.find("# Cross-Collection-Einfluss-Analyse (FA-32)") != std::string::npos);
            CHECK(rmd.find("| orders | declared | kunden |") != std::string::npos);
        }
    }

    // =====================================================================
    // 2) Entdeckte Fremdschluessel (FA-27): orders2.kat -> kategorien.kat,
    //    triviale Join-Abhaengigkeit unterdrueckt, echte Querverlinkung sichtbar
    // =====================================================================
    {
        Database db("ci2");
        auto kategorien = db.createCollection("kategorien");
        auto orders2 = db.createCollection("orders2");
        const int katB[] = {1, 2, 3, 4};
        const double rabB[] = {0.9, 0.9, 0.8, 0.8};
        for (int i = 0; i < 4; ++i)
            kategorien.insert({{"kat", katB[i]}, {"rabatt", rabB[i]}});
        for (int i = 0; i < 12; ++i) {
            const int k = i / 3; // Kategorie 0..3, je 3 Bestellungen
            const int menge = 101 + i;
            orders2.insert({{"kat", katB[k]}, {"menge", menge}, {"wert", 10 * menge}});
        }

        CrossInfluenceResult r = analyzeCrossInfluence(db);
        CHECK(r.pairs.size() == 2); // FK in beide Richtungen entdeckt
        bool anyOrders2 = false, anyKat = false;
        for (const auto& pp : r.pairs) {
            CHECK(pp.origin == "discovered");
            if (pp.collectionA == "orders2")
                anyOrders2 = true;
            if (pp.collectionA == "kategorien")
                anyKat = true;
        }
        CHECK(anyOrders2);
        CHECK(anyKat);
        CHECK(r.note.find("2 entdeckt") != std::string::npos);

        const CrossPairAnalysis* p = findPair(r, "orders2", "kategorien", "discovered");
        CHECK(p != nullptr);
        if (p) {
            CHECK(p->joinFieldA == "kat");
            CHECK(p->keyFieldB == "kat");
            CHECK(p->relation.find("FK orders2.kat -> kategorien.kat") != std::string::npos);
            CHECK(p->docsA == 12);
            CHECK(p->matchedA == 12);
            CHECK(p->rows == 12);
            CHECK(near(p->joinCoverage, 1.0));
            CHECK(p->cardinality == "n:1");
            CHECK(p->fields.size() == 5);

            // Genau eine Cross-Abhaengigkeit: orders2.kat -> kategorien.rabatt
            CHECK(p->dependencies.size() == 1);
            const auto* d = findDep(*p, "orders2.kat", "kategorien.rabatt");
            CHECK(d != nullptr);
            CHECK(d != nullptr && near(d->coverage, 1.0));
            CHECK(d != nullptr && d->groups == 4);
            CHECK(d != nullptr && d->pairs == 12);

            // Triviale Join-Abhaengigkeit unterdrueckt: orders2.kat <-> kategorien.kat
            CHECK(findDep(*p, "orders2.kat", "kategorien.kat") == nullptr);
            CHECK(findDep(*p, "kategorien.kat", "orders2.kat") == nullptr);
            const auto* katI = findField(*p, "orders2.kat");
            CHECK(katI != nullptr);
            CHECK(katI != nullptr && katI->partners == 3); // kategorien.kat fehlt
            // Tie: wert = 10*menge ist exakt linear, beide |r| identisch;
            // strongestPartner haengt dann von der Feld-Iterationsreihenfolge ab.
            CHECK(katI != nullptr && (katI->strongestPartner == "orders2.menge" ||
                                      katI->strongestPartner == "orders2.wert"));
            CHECK(katI != nullptr && near(katI->maxAbsPearson, 0.9716254134469435, 1e-9));
            CHECK(katI != nullptr && katI->determines == 1);
            CHECK(katI != nullptr && katI->determinedBy == 0);
            CHECK(katI != nullptr && near(katI->rSquared, 0.9393900944595107, 1e-6));
            CHECK(katI != nullptr && katI->role == FieldRole::Mediator);
            const auto* bkatI = findField(*p, "kategorien.kat");
            CHECK(bkatI != nullptr);
            CHECK(bkatI != nullptr && bkatI->partners == 3);
            CHECK(bkatI != nullptr &&
                  bkatI->rationale.find("kategorien.kat") == std::string::npos);

            // kategorien.rabatt: von orders2.kat funktional bestimmt (Cross), starker |r|
            const auto* rabI = findField(*p, "kategorien.rabatt");
            CHECK(rabI != nullptr);
            CHECK(rabI != nullptr && rabI->determinedBy == 2);
            CHECK(rabI != nullptr && rabI->determines == 0);
            CHECK(rabI != nullptr && rabI->strongCorrelations == 4);
            CHECK(rabI != nullptr && near(rabI->rSquared, 0.7854252199104226, 1e-6));
            CHECK(rabI != nullptr && rabI->role == FieldRole::Mediator);
            CHECK(rabI != nullptr && rabI->rationale.find("orders2.kat -> kategorien.rabatt") !=
                                          std::string::npos);

            // menge/wert: perfekt korreliert (r = 1), gegenseitig erklaert
            const auto* mengeI = findField(*p, "orders2.menge");
            CHECK(mengeI != nullptr);
            CHECK(mengeI != nullptr && mengeI->partners == 4);
            CHECK(mengeI != nullptr && near(mengeI->maxAbsPearson, 1.0));
            CHECK(mengeI != nullptr && mengeI->strongestPartner == "orders2.wert");
            CHECK(mengeI != nullptr && mengeI->role == FieldRole::Dependent);
            const auto* wertI = findField(*p, "orders2.wert");
            CHECK(wertI != nullptr);
            CHECK(wertI != nullptr && wertI->role == FieldRole::Dependent);

            // Top-Treiber: Gleichstand -> alphabetisch kleinerer Name
            std::cout << "DIAG topDriver=" << p->topDriver << "\n";
            for (const auto& df : p->fields)
                std::cout << "DIAG f=" << df.field << " score=" << std::setprecision(17)
                          << df.influenceScore << " hub=" << df.hubRatio
                          << " det=" << df.determines << " dep=" << df.determinedBy
                          << " r2=" << df.rSquared << " strong=" << df.strongCorrelations
                          << " role=" << toString(df.role) << "\n";
            CHECK(p->topDriver == "kategorien.kat");
            CHECK(p->note.find("orders2.kat -> kategorien.rabatt") != std::string::npos);
        }

        // 2b) Ohne Trivial-Suppression ist die Join-Abhaengigkeit wieder sichtbar
        CrossInfluenceOptions opts;
        opts.includeDeclared = false;
        opts.suppressTrivialJoinDependency = false;
        CrossInfluenceResult r2 = analyzeCrossInfluence(db, opts);
        const CrossPairAnalysis* p2 = findPair(r2, "orders2", "kategorien", "discovered");
        CHECK(p2 != nullptr);
        if (p2) {
            CHECK(findDep(*p2, "kategorien.kat", "orders2.kat") != nullptr);
            const auto* katI2 = findField(*p2, "orders2.kat");
            CHECK(katI2 != nullptr);
            CHECK(katI2 != nullptr && katI2->partners == 4);
            CHECK(katI2 != nullptr && near(katI2->maxAbsPearson, 1.0));
            CHECK(katI2 != nullptr && katI2->strongestPartner == "kategorien.kat");
        }

        // 2c) Reports
        if (p) {
            const std::string js = toJson(*p);
            CHECK(js.find("\"origin\":\"discovered\"") != std::string::npos);
            CHECK(js.find("\"joinFieldA\":\"kat\"") != std::string::npos);
            CHECK(js.find("\"source\":\"orders2.kat\"") != std::string::npos);
            const std::string md = toMarkdown(*p);
            CHECK(md.find("orders2.kat") != std::string::npos);
            CHECK(md.find("Kardinalitaet (A-Sicht): n:1") != std::string::npos);
        }
    }

    // =====================================================================
    // 3) Guards: minJoinPairs, minJoinCoverage, Kardinalitaet 1:n,
    //    Selbst-Referenz, leere Database
    // =====================================================================
    {
        // 3a) minJoinPairs
        Database db("ci3");
        auto a = db.createCollection("a");
        auto b = db.createCollection("b");
        std::vector<DocumentId> aid, bid;
        for (int i = 0; i < 4; ++i)
            aid.push_back(a.insert({{"x", i + 1}, {"y", 10 * (i + 1)}}));
        for (int i = 0; i < 2; ++i)
            bid.push_back(b.insert({{"bId", i + 1}, {"z", 7 + i}}));
        ReferenceIndex ri(db);
        for (int i = 0; i < 4; ++i)
            ri.link("a", aid[i], "b", bid[i % 2], "gehoert zu");

        CrossInfluenceOptions opts;
        opts.includeDiscovered = false;
        // 4 verknuepfte Zeilen < minValuesPerField (5): Feldfilter extra senken,
        // damit der zweite Lauf wirklich analysiert (Feldanzahl pruefbar).
        opts.influence.numeric.minValuesPerField = 3;
        opts.minJoinPairs = 5;
        CrossInfluenceResult r = analyzeCrossInfluence(db, opts);
        CHECK(r.pairs.size() == 1);
        if (!r.pairs.empty()) {
            const CrossPairAnalysis& p = r.pairs[0];
            CHECK(p.rows == 4);
            CHECK(p.note.find("uebersprungen") != std::string::npos);
            CHECK(p.note.find("minJoinPairs") != std::string::npos);
            CHECK(p.fields.empty());
            CHECK(p.dependencies.empty());
            CHECK(p.topDriver.empty());
        }

        opts.minJoinPairs = 3; // jetzt analysiert
        CrossInfluenceResult r2 = analyzeCrossInfluence(db, opts);
        CHECK(r2.pairs.size() == 1);
        if (!r2.pairs.empty()) {
            const CrossPairAnalysis& p = r2.pairs[0];
            CHECK(p.fields.size() == 4);
            CHECK(p.note.find("uebersprungen") == std::string::npos);
            CHECK(p.cardinality == "n:1");
        }

        // 3b) minJoinCoverage: nur 5 von 10 A-Dokumenten verknuepft
        auto a4 = db.createCollection("big");
        auto b4 = db.createCollection("target");
        std::vector<DocumentId> a4id, b4id;
        for (int i = 0; i < 10; ++i)
            a4id.push_back(a4.insert({{"x", i + 1}, {"y", 2 * (i + 1)}}));
        for (int i = 0; i < 2; ++i)
            b4id.push_back(b4.insert({{"bId", i + 1}, {"z", i}}));
        for (int i = 0; i < 5; ++i)
            ri.link("big", a4id[i], "target", b4id[i % 2], "ref");

        CrossInfluenceOptions cov;
        cov.includeDiscovered = false;
        cov.minJoinPairs = 3;
        cov.minJoinCoverage = 0.6;
        CrossInfluenceResult rCov = analyzeCrossInfluence(db, cov);
        const CrossPairAnalysis* pBig =
            findPair(rCov, "big", "target", "declared");
        CHECK(pBig != nullptr);
        if (pBig) {
            CHECK(pBig->note.find("Join-Abdeckung") != std::string::npos);
            CHECK(pBig->fields.empty());
        }
        cov.minJoinCoverage = 0.5; // Abdeckung 0.5 erfuellt Schwelle
        CrossInfluenceResult rCov2 = analyzeCrossInfluence(db, cov);
        const CrossPairAnalysis* pBig2 = findPair(rCov2, "big", "target", "declared");
        CHECK(pBig2 != nullptr);
        if (pBig2) {
            CHECK(near(pBig2->joinCoverage, 0.5));
            CHECK(!pBig2->fields.empty());
        }

        // 3c) Kardinalitaet 1:n: ein A-Dokument mit mehreren B-Partnern
        auto a5 = db.createCollection("one");
        auto b5 = db.createCollection("many");
        std::vector<DocumentId> a5id, b5id;
        for (int i = 0; i < 3; ++i)
            a5id.push_back(a5.insert({{"p", i + 1}, {"q", 10 * (i + 1)}}));
        for (int i = 0; i < 6; ++i)
            b5id.push_back(b5.insert({{"bId", i + 1}, {"r", 3 * (i + 1)}}));
        for (int i = 0; i < 3; ++i) {
            ri.link("one", a5id[i], "many", b5id[2 * i], "hat");
            ri.link("one", a5id[i], "many", b5id[2 * i + 1], "hat");
        }
        CrossInfluenceOptions card;
        card.includeDiscovered = false;
        card.minJoinPairs = 5;
        CrossInfluenceResult rCard = analyzeCrossInfluence(db, card);
        const CrossPairAnalysis* pCard = findPair(rCard, "one", "many", "declared");
        CHECK(pCard != nullptr);
        if (pCard) {
            CHECK(pCard->rows == 6);
            CHECK(pCard->docsA == 3);
            CHECK(pCard->matchedA == 3);
            CHECK(near(pCard->joinCoverage, 1.0));
            CHECK(pCard->cardinality == "1:n");
        }
    }

    // 3d) Selbst-Referenzen (gleiche Collection) werden uebersprungen
    {
        Database db("ci6");
        auto s = db.createCollection("s");
        std::vector<DocumentId> sid;
        for (int i = 0; i < 6; ++i)
            sid.push_back(s.insert({{"v", i + 1}, {"w", 2 * (i + 1)}}));
        ReferenceIndex ri(db);
        for (int i = 0; i < 5; ++i)
            ri.link("s", sid[i], "s", sid[i + 1], "folge");
        CrossInfluenceOptions opts;
        opts.includeDiscovered = false;
        CrossInfluenceResult r = analyzeCrossInfluence(db, opts);
        CHECK(r.pairs.empty());
        CHECK(r.note.find("Selbst-Referenz") != std::string::npos);
    }

    // 3e) Leere Database
    {
        Database db("ci0");
        CrossInfluenceResult r = analyzeCrossInfluence(db);
        CHECK(r.pairs.empty());
        CHECK(r.note.find("keine verknuepfbaren Collection-Paare") != std::string::npos);
        const std::string js = toJson(r);
        CHECK(js == "{\"note\":\"keine verknuepfbaren Collection-Paare gefunden\",\"pairs\":[]}");
    }

    if (g_failures == 0)
        std::cout << "Alle " << g_checks << " Checks erfolgreich (FA-32 Cross-Influence)\n";
    return g_failures == 0 ? 0 : 1;
}
