// Tests: Discovery v0.45 – Klartext-Beschreibungen (Narrative) der Collection-Zusammenhänge:
// describeRelations / toNarrative / describeDatabase, de+en, Filter (minConfidence),
// Deckel (maxRelations), Sortierung nach Konfidenz, leere Eingabe, Konfidenz-Wortstufen.
#include <memdb/memdb.hpp>
#include <memdb/discovery.hpp>

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

bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

DiscoveredRelation fk(const std::string& a, const std::string& fa, const std::string& b,
                      const std::string& fb, double conf, double sup = 1.0, std::size_t n = 100,
                      const std::string& card = "n:1", const std::string& note = "") {
    DiscoveredRelation r;
    r.kind = RelationKind::ForeignKey;
    r.fromCollection = a;
    r.field = fa;
    r.toCollection = b;
    r.toField = fb;
    r.confidence = conf;
    r.support = sup;
    r.sampleSize = n;
    r.cardinality = card;
    r.note = note;
    return r;
}

} // namespace

int main() {
    // 1) describeRelations – Deutsch: FK-Satz mit allen Bestandteilen
    {
        std::vector<DiscoveredRelation> rels;
        rels.push_back(fk("orders", "customer_id", "users", "id", 0.95, 0.98, 1234, "n:1", "name-heuristik:_id"));
        std::string de = describeRelations(rels);
        CHECK(has(de, "Zusammenhang ohne deklarierte Relation"));
        CHECK(has(de, "`orders.customer_id`"));
        CHECK(has(de, "`users.id`"));
        CHECK(has(de, "98 %"));
        CHECK(has(de, "1234"));
        CHECK(has(de, "n:1"));
        CHECK(has(de, "sehr hoch")); // conf 0.95
        CHECK(has(de, "Hinweis: name-heuristik:_id"));
    }
    // 2) describeRelations – Englisch
    {
        std::vector<DiscoveredRelation> rels;
        rels.push_back(fk("orders", "customer_id", "users", "id", 0.95, 0.98, 1234));
        NarrativeOptions o;
        o.language = NarrativeLanguage::English;
        std::string en = describeRelations(rels, o);
        CHECK(has(en, "Relationship without a declared relation"));
        CHECK(has(en, "`orders.customer_id` points to `users.id`"));
        CHECK(has(en, "98 % of 1234 examined values"));
        CHECK(has(en, "cardinality n:1"));
        CHECK(has(en, "confidence very high"));
        CHECK(!has(en, "Zusammenhang"));
    }
    // 3) Alle vier Befundarten – de+en
    {
        std::vector<DiscoveredRelation> rels;
        DiscoveredRelation f;
        f.kind = RelationKind::FunctionalDependency;
        f.fromCollection = "users"; f.field = "id"; f.toCollection = "users"; f.toField = "email";
        f.confidence = 1.0; f.support = 1.0; f.sampleSize = 500;
        rels.push_back(f);
        DiscoveredRelation c = f;
        c.kind = RelationKind::Correlation;
        c.fromCollection = "metrics"; c.field = "cpu"; c.toField = "load"; c.confidence = 0.93;
        rels.push_back(c);
        DiscoveredRelation nd = f;
        nd.kind = RelationKind::NearDuplicate;
        nd.fromCollection = "users"; nd.field = "name"; nd.toCollection = "customers"; nd.toField = "name";
        nd.confidence = 0.91;
        rels.push_back(nd);
        std::string de = describeRelations(rels);
        CHECK(has(de, "Innerhalb von `users` bestimmt `id` eindeutig `email`"));
        CHECK(has(de, "In `metrics` hängen `cpu` und `load` zusammen (Korrelation r = 0.93, 500 Werte)"));
        CHECK(has(de, "mögliche Dubletten über Collections hinweg"));
        NarrativeOptions o;
        o.language = NarrativeLanguage::English;
        std::string en = describeRelations(rels, o);
        CHECK(has(en, "`id` uniquely determines `email`"));
        CHECK(has(en, "`cpu` and `load` are associated (correlation r = 0.93, 500 values)"));
        CHECK(has(en, "possible duplicates across collections"));
    }
    // 4) Konfidenz-Wortstufen
    {
        std::vector<DiscoveredRelation> rels;
        rels.push_back(fk("a", "x", "b", "y", 0.95));
        rels.push_back(fk("a", "x2", "b", "y", 0.75));
        rels.push_back(fk("a", "x3", "b", "y", 0.55));
        rels.push_back(fk("a", "x4", "b", "y", 0.2));
        std::string de = describeRelations(rels);
        CHECK(has(de, "sehr hoch"));
        CHECK(has(de, "Konfidenz hoch"));
        CHECK(has(de, "Konfidenz mittel"));
        CHECK(has(de, "Konfidenz niedrig"));
        // Sortierung: stärkster Befund zuerst (x vor x2 vor x3 vor x4)
        auto p1 = de.find("`a.x`"), p2 = de.find("`a.x2`"), p3 = de.find("`a.x3`"), p4 = de.find("`a.x4`");
        CHECK(p1 < p2 && p2 < p3 && p3 < p4);
    }
    // 5) minConfidence filtert, maxRelations deckelt, 0 = alle
    {
        std::vector<DiscoveredRelation> rels;
        for (int i = 0; i < 30; ++i)
            rels.push_back(fk("a", "f" + std::to_string(i), "b", "y", 0.5 + 0.015 * i)); // 0.50 .. 0.935
        NarrativeOptions o;
        o.minConfidence = 0.7;
        std::string de = describeRelations(rels, o);
        CHECK(!has(de, "`a.f13`")); // conf 0.695 < 0.7 -> weg
        CHECK(has(de, "`a.f14`"));  // conf 0.71 >= 0.7 -> drin
        // 16 Befunde >= 0.7, Deckel 5 -> 11 weitere
        o.maxRelations = 5;
        de = describeRelations(rels, o);
        CHECK(has(de, "11 weitere Zusammenhänge"));
        CHECK(has(de, "maxRelations=5"));
        o.maxRelations = 0;
        de = describeRelations(rels, o);
        CHECK(!has(de, "weitere Zusammenhänge"));
        NarrativeOptions oe;
        oe.language = NarrativeLanguage::English;
        oe.minConfidence = 0.7;
        oe.maxRelations = 5;
        std::string en = describeRelations(rels, oe);
        CHECK(has(en, "11 more relationships"));
    }
    // 6) Leere Eingabe – freundlicher Satz
    {
        std::vector<DiscoveredRelation> rels;
        CHECK(has(describeRelations(rels), "Keine Zusammenhänge zwischen den Collections gefunden"));
        NarrativeOptions o;
        o.language = NarrativeLanguage::English;
        CHECK(has(describeRelations(rels, o), "No relationships between collections found"));
    }
    // 7) Determinismus
    {
        std::vector<DiscoveredRelation> rels;
        rels.push_back(fk("orders", "customer_id", "users", "id", 0.95, 0.98, 1234));
        rels.push_back(fk("logs", "user_id", "users", "id", 0.9, 1.0, 300));
        CHECK(describeRelations(rels) == describeRelations(rels));
    }
    // 8) describeDatabase auf echter kleiner DB – de+en, inkl. Übersicht
    {
        Database db("shop");
        auto users = db.createCollection("users");
        auto orders = db.createCollection("orders");
        for (int i = 1; i <= 5; ++i)
            users.insert({{"id", i}, {"name", "u" + std::to_string(i)}});
        for (int i = 1; i <= 8; ++i)
            orders.insert({{"id", 100 + i}, {"user_id", (i % 5) + 1}, {"total", i * 10}});
        std::string de = describeDatabase(db);
        CHECK(has(de, "Übersicht: 2 Collections, 13 Dokumente, Health-Score"));
        CHECK(has(de, "`orders.user_id`"));
        CHECK(has(de, "`users.id`"));
        NarrativeOptions o;
        o.language = NarrativeLanguage::English;
        std::string en = describeDatabase(db, {}, o);
        CHECK(has(en, "Overview: 2 collections, 13 documents, health score"));
        CHECK(has(en, "`orders.user_id` points to `users.id`"));
        // ohne __references -> keine Confusion-Zeile
        CHECK(!has(de, "TP="));
    }
    // 9) toNarrative – Pfade + Confusion-Zeile
    {
        AnalysisReport r;
        r.totalCollections = 3;
        r.totalDocs = 42;
        r.healthScore = 88.5;
        r.relations.push_back(fk("orders", "customer_id", "users", "id", 0.95));
        DiscoveredRelation hop2 = fk("users", "dept_id", "departments", "id", 0.9);
        r.paths.push_back({r.relations[0], hop2});
        ConfusionMatrix cm;
        cm.tp = 1; cm.fp = 3; cm.fn = 0; cm.tn = 2;
        cm.precision = 0.25; cm.recall = 1.0; cm.f1 = 0.4;
        r.confusion = cm;
        std::string de = toNarrative(r);
        CHECK(has(de, "Übersicht: 3 Collections, 42 Dokumente, Health-Score 88.5/100."));
        CHECK(has(de, "TP=1 FP=3 FN=0 TN=2, F1=0.4"));
        CHECK(has(de, "Wichtigste Ketten (Multi-Hop):"));
        CHECK(has(de, "orders.customer_id → users.id"));
        CHECK(has(de, "users.dept_id → departments.id"));
        NarrativeOptions o;
        o.language = NarrativeLanguage::English;
        std::string en = toNarrative(r, o);
        CHECK(has(en, "Overview: 3 collections, 42 documents, health score 88.5/100."));
        CHECK(has(en, "TP=1 FP=3 FN=0 TN=2, F1=0.4"));
        CHECK(has(en, "Main chains (multi-hop):"));
        CHECK(has(en, "orders.customer_id → users.id"));
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
