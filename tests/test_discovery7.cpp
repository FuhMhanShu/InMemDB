// Tests: Discovery v0.42 – Confusion-Matrix (evaluateConfusion: entdeckte FKs vs. deklarierte
// __references), Integration in analyze(Database&) + alle Präsentationen, CLI-relevant.
#include <memdb/memdb.hpp>
#include <memdb/discovery.hpp>
#include <memdb/references.hpp>

#include <cmath>
#include <iostream>
#include <sstream>
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

bool closeEnough(double a, double b) { return std::fabs(a - b) < 1e-9; }

} // namespace

int main() {
    // 1) Setup: users/orders/logs – orders.user_id -> users.id ist entdeckt UND deklariert (TP),
    //    logs.user_id -> users.id ist entdeckt aber NICHT deklariert (FP)
    Database db("shop");
    auto users = db.createCollection("users");
    auto orders = db.createCollection("orders");
    auto logs = db.createCollection("logs");
    DocumentId u1 = users.insert({{"id", 1}, {"name", "Ada"}});
    users.insert({{"id", 2}, {"name", "Bob"}});
    users.insert({{"id", 3}, {"name", "Cy"}});
    DocumentId o1 = orders.insert({{"id", 10}, {"user_id", 1}, {"total", 50}});
    orders.insert({{"id", 11}, {"user_id", 2}, {"total", 70}});
    orders.insert({{"id", 12}, {"user_id", 3}, {"total", 90}});
    logs.insert({{"id", 100}, {"user_id", 1}});
    logs.insert({{"id", 101}, {"user_id", 2}});
    logs.insert({{"id", 102}, {"user_id", 3}});
    ReferenceIndex refs(db);
    refs.link("orders", o1, "users", u1, "kunde"); // deklarierte Referenz orders->users

    DiscoveryOptions opts;
    auto rels = discoverRelations(db, opts);
    // direkte evaluateConfusion
    auto cm = evaluateConfusion(rels, db, opts);
    CHECK(cm.tp >= 1);                       // orders->users: entdeckt + deklariert
    CHECK(cm.fp >= 1);                       // logs->users (und ggf. orders<->logs): entdeckt, nicht deklariert
    CHECK(cm.fn == 0);                       // keine deklarierte Referenz fehlt
    CHECK(cm.tp + cm.fp + cm.fn + cm.tn == 6); // 3 Collections -> 3*2 gerichtete Paare
    CHECK(closeEnough(cm.precision, double(cm.tp) / (cm.tp + cm.fp)));
    CHECK(closeEnough(cm.recall, 1.0));
    CHECK(closeEnough(cm.f1, 2.0 * cm.precision * cm.recall / (cm.precision + cm.recall)));
    // JSON der Matrix
    std::string cj = toJson(cm);
    CHECK(cj.find("\"tp\"") != std::string::npos && cj.find("\"f1\"") != std::string::npos);

    // 2) FN-Fall: products + orders.product_id mit nur 1 von 3 gemeinsamen Werten -> conf < 0.9
    //    -> nicht vorhergesagt, aber Referenz deklariert -> FN
    auto products = db.createCollection("products");
    DocumentId p1 = products.insert({{"id", 100}, {"sku", "A"}});
    products.insert({{"id", 101}, {"sku", "B"}});
    products.insert({{"id", 102}, {"sku", "C"}});
    orders.updateById(o1, [](Document& d) { d.set("product_id", 100); });
    orders.updateById(DocumentId(11), [](Document& d) { d.set("product_id", 999); }); // Orphan-Wert
    orders.updateById(DocumentId(12), [](Document& d) { d.set("product_id", 998); });
    refs.link("orders", o1, "products", p1, "fk_product_id"); // deklariert, aber conf 1/3 < 0.9
    auto cm2 = evaluateConfusion(discoverRelations(db, opts), db, opts);
    CHECK(cm2.fn >= 1);                      // orders->products deklariert, nicht entdeckt
    CHECK(cm2.tp >= 1);                      // orders->users weiterhin TP
    CHECK(cm2.tp + cm2.fp + cm2.fn + cm2.tn == 12); // 4 Collections -> 4*3 Paare

    // 3) analyze(Database&): confusion im Report + alle Präsentationen
    auto rep = analyze(db, opts);
    CHECK(rep.confusion.has_value());
    CHECK(rep.confusion->fn >= 1 && rep.confusion->tp >= 1);
    CHECK(toJson(rep).find("\"confusion\"") != std::string::npos);
    CHECK(toJson(rep).find("\"precision\"") != std::string::npos);
    std::string md = toMarkdown(rep);
    CHECK(md.find("Confusion-Matrix") != std::string::npos);
    CHECK(md.find("TP = ") != std::string::npos);
    std::ostringstream ss;
    printSummary(rep, ss);
    CHECK(ss.str().find("Confusion (FK vs. Referenzen):") != std::string::npos);
    CHECK(ss.str().find("F1=") != std::string::npos);

    // 4) analyze(vector<Collection>): keine __references -> kein confusion
    std::vector<Collection> colls;
    for (const auto& n : db.collectionNames())
        if (n != "__references")
            colls.push_back(db.collection(n));
    auto repV = analyze(colls, opts);
    CHECK(!repV.confusion.has_value());
    CHECK(toJson(repV).find("\"confusion\"") == std::string::npos);
    CHECK(toMarkdown(repV).find("Confusion-Matrix") == std::string::npos);

    // 5) Schwelle konfigurierbar: confusionMinConfidence = 0 -> mehr predicted positives
    DiscoveryOptions optsLoose;
    optsLoose.confusionMinConfidence = 0.0;
    auto cmLoose = evaluateConfusion(rels, db, optsLoose);
    CHECK(cmLoose.tp + cmLoose.fp >= cm.tp + cm.fp);

    // 6) Fehlerfall: Database ohne __references -> evaluateConfusion wirft Error
    {
        Database db2("norefs");
        db2.createCollection("a").insert({{"id", 1}});
        bool threw = false;
        try {
            evaluateConfusion({}, db2, opts);
        } catch (const Error&) {
            threw = true;
        }
        CHECK(threw);
        auto rep2 = analyze(db2, opts); // analyze() selbst bleibt robust: confusion = nullopt
        CHECK(!rep2.confusion.has_value());
    }

    // 7) leere __references-Collection: Matrix vorhanden, fn = 0, recall = 0 (keine tatsächlichen)
    {
        Database db3("emptyrefs");
        auto a = db3.createCollection("a");
        auto b = db3.createCollection("b");
        a.insert({{"id", 1}, {"ref", 1}});
        a.insert({{"id", 2}, {"ref", 2}});
        b.insert({{"id", 1}});
        b.insert({{"id", 2}});
        ReferenceIndex refs3(db3); // legt __references an, ohne Links
        auto cm3 = evaluateConfusion(discoverRelations(db3, opts), db3, opts);
        CHECK(cm3.fn == 0);
        CHECK(cm3.tp == 0);
        CHECK(closeEnough(cm3.recall, 0.0));
        auto rep3 = analyze(db3, opts);
        CHECK(rep3.confusion.has_value() && rep3.confusion->tp == 0);
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
