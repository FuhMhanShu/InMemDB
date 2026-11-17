// Tests: Einfluss-Analyse (FA-31): Deutung der statistischen Werte – Hub ueber
// Pearson-Korrelationen, multiples R^2 (Erklartheit), funktionale Abhaengigkeiten,
// Rollen (driver/dependent/mediator/independent), Einfluss-Score, Reports.
#include <memdb/memdb.hpp>
#include <memdb/influence.hpp>

#include <cmath>
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

const FieldInfluence* findInfluence(const InfluenceAnalysis& a, const std::string& name) {
    for (const auto& f : a.fields)
        if (f.field == name)
            return &f;
    return nullptr;
}

const FunctionalDependency* findDependency(const InfluenceAnalysis& a, const std::string& src,
                                           const std::string& dst) {
    for (const auto& d : a.dependencies)
        if (d.source == src && d.target == dst)
            return &d;
    return nullptr;
}

bool near(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps;
}

} // namespace

int main() {
    // 1) Treiber/abhaengig/unabhaengig: menge -> preis (funktional, nicht invertierbar)
    {
        Database db("i1");
        auto c = db.createCollection("orders");
        const int menge[] = {1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6};
        const int noise[] = {7, 3, 9, 1, 5, 8, 2, 6, 0, 4, 11, 10};
        for (int i = 0; i < 12; ++i)
            c.insert({{"menge", menge[i]},
                      {"preis", menge[i] >= 4 ? 100 : 10},
                      {"noise", noise[i]}});
        InfluenceAnalysis a = analyzeInfluence(c);
        CHECK(a.collection == "orders");
        CHECK(a.docCount == 12 && a.sampleSize == 12);
        CHECK(a.numericFieldCount == 3);
        CHECK(a.fields.size() == 3);

        // Funktionale Abhaengigkeit menge -> preis (Stufenfunktion, nicht invertierbar)
        CHECK(a.dependencies.size() == 1);
        const auto* dep = findDependency(a, "menge", "preis");
        CHECK(dep != nullptr);
        CHECK(dep != nullptr && near(dep->coverage, 1.0));
        CHECK(dep != nullptr && dep->groups == 6); // jeder menge-Wert wiederholt
        CHECK(dep != nullptr && dep->pairs == 12);

        // menge: Treiber (bestimmt preis, R2 < 0.9)
        const auto* mengeI = findInfluence(a, "menge");
        CHECK(mengeI != nullptr);
        CHECK(mengeI != nullptr && mengeI->partners == 2);
        CHECK(mengeI != nullptr && mengeI->determines == 1);
        CHECK(mengeI != nullptr && mengeI->determinedBy == 0);
        CHECK(mengeI != nullptr && mengeI->strongCorrelations == 1);
        CHECK(mengeI != nullptr && near(mengeI->maxAbsPearson, 0.8783100656536799, 1e-9));
        CHECK(mengeI != nullptr && mengeI->strongestPartner == "preis");
        CHECK(mengeI != nullptr && near(mengeI->hubRatio, 0.5522355916652751, 1e-9));
        CHECK(mengeI != nullptr && mengeI->rSquaredComputed);
        CHECK(mengeI != nullptr && near(mengeI->rSquared, 0.8225774225774226, 1e-6));
        CHECK(mengeI != nullptr && mengeI->regressionRows == 12);
        CHECK(mengeI != nullptr && mengeI->role == FieldRole::Driver);
        CHECK(mengeI != nullptr && near(mengeI->influenceScore, 0.461602311317153, 1e-6));
        CHECK(mengeI != nullptr &&
              mengeI->rationale.find("bestimmt funktional") != std::string::npos);

        // preis: abhaengig (funktional von menge bestimmt)
        const auto* preisI = findInfluence(a, "preis");
        CHECK(preisI != nullptr);
        CHECK(preisI != nullptr && preisI->determinedBy == 1);
        CHECK(preisI != nullptr && preisI->determines == 0);
        CHECK(preisI != nullptr && preisI->role == FieldRole::Dependent);
        CHECK(preisI != nullptr && preisI->rationale.find("funktional abhaengig") !=
                                       std::string::npos);

        // noise: unabhaengig
        const auto* noiseI = findInfluence(a, "noise");
        CHECK(noiseI != nullptr);
        CHECK(noiseI != nullptr && noiseI->strongCorrelations == 0);
        CHECK(noiseI != nullptr && noiseI->role == FieldRole::Independent);

        // Ranking und Kurzfassung
        CHECK(a.topDriver == "menge");
        CHECK(a.fields[0].field == "menge");
        CHECK(a.fields[0].influenceScore >= a.fields[1].influenceScore);
        CHECK(a.fields[1].influenceScore >= a.fields[2].influenceScore);
        CHECK(a.note.find("menge -> preis") != std::string::npos);
        CHECK(a.note.find("Treiber: menge") != std::string::npos);

        // Reports
        const std::string js = toJson(a);
        CHECK(js.find("\"topDriver\":\"menge\"") != std::string::npos);
        CHECK(js.find("\"role\":\"driver\"") != std::string::npos);
        CHECK(js.find("\"source\":\"menge\"") != std::string::npos);
        CHECK(js.find("\"coverage\":1") != std::string::npos);
        const std::string md = toMarkdown(a);
        CHECK(md.find("Funktionale Abhaengigkeiten") != std::string::npos);
        CHECK(md.find("| menge | driver |") != std::string::npos);
    }

    // 2) Exakte Linearitaet y = 2x+1: R^2 = 1, keine FD-Evidenz (x-Werte einzigartig)
    {
        Database db("i2");
        auto c = db.createCollection("lin");
        for (int x = 1; x <= 10; ++x)
            c.insert({{"x", x}, {"y", 2 * x + 1}});
        InfluenceAnalysis a = analyzeInfluence(c);
        CHECK(a.dependencies.empty()); // keine wiederholten source-Werte
        const auto* yI = findInfluence(a, "y");
        CHECK(yI != nullptr);
        CHECK(yI != nullptr && yI->rSquaredComputed);
        CHECK(yI != nullptr && near(yI->rSquared, 1.0, 1e-6));
        CHECK(yI != nullptr && yI->role == FieldRole::Dependent);
        const auto* xI = findInfluence(a, "x");
        CHECK(xI != nullptr);
        CHECK(xI != nullptr && xI->rSquaredComputed);
        CHECK(xI != nullptr && xI->rSquared > 0.999);
        CHECK(xI != nullptr && xI->role == FieldRole::Dependent);
        CHECK(a.topDriver == "x"); // Score-Gleichstand: alphabetisch
        CHECK(a.note.find("abhaengig: x, y") != std::string::npos);
    }

    // 3) Einziges numerisches Feld: keine Partner, keine Rolle
    {
        Database db("i3");
        auto c = db.createCollection("solo");
        for (int i = 1; i <= 8; ++i)
            c.insert({{"v", i}});
        InfluenceAnalysis a = analyzeInfluence(c);
        CHECK(a.fields.size() == 1);
        CHECK(a.fields[0].field == "v");
        CHECK(a.fields[0].partners == 0);
        CHECK(a.fields[0].determines == 0 && a.fields[0].determinedBy == 0);
        CHECK(!a.fields[0].rSquaredComputed);
        CHECK(a.fields[0].role == FieldRole::Independent);
        CHECK(a.topDriver.empty());
        CHECK(a.note.find("keine starken Zusammenhaenge") != std::string::npos);
        CHECK(a.dependencies.empty());
    }

    // 4) Kette a -> b -> c: Mediatoren und vollstaendig abhaengiges Endglied
    {
        Database db("i4");
        auto c = db.createCollection("chain");
        const int a[] = {1, 1, 2, 2, 3, 3, 4, 4};
        for (int i = 0; i < 8; ++i)
            c.insert({{"a", a[i]}, {"b", a[i] * 10}, {"c", a[i] * 10 + 5}});
        InfluenceAnalysis r = analyzeInfluence(c);
        CHECK(r.dependencies.size() == 3);
        CHECK(findDependency(r, "a", "b") != nullptr);
        CHECK(findDependency(r, "a", "c") != nullptr);
        CHECK(findDependency(r, "b", "c") != nullptr);
        const auto* aI = findInfluence(r, "a");
        CHECK(aI != nullptr);
        CHECK(aI != nullptr && aI->determines == 2);
        CHECK(aI != nullptr && aI->determinedBy == 0);
        CHECK(aI != nullptr && aI->rSquared > 0.999);
        CHECK(aI != nullptr && aI->role == FieldRole::Mediator);
        const auto* bI = findInfluence(r, "b");
        CHECK(bI != nullptr);
        CHECK(bI != nullptr && bI->determines == 1);
        CHECK(bI != nullptr && bI->determinedBy == 1);
        CHECK(bI != nullptr && bI->role == FieldRole::Mediator);
        const auto* cI = findInfluence(r, "c");
        CHECK(cI != nullptr);
        CHECK(cI != nullptr && cI->determines == 0);
        CHECK(cI != nullptr && cI->determinedBy == 2);
        CHECK(cI != nullptr && cI->rSquared > 0.999);
        CHECK(cI != nullptr && cI->role == FieldRole::Dependent);
        CHECK(r.topDriver == "a");
        const std::string js = toJson(r);
        CHECK(js.find("\"role\":\"mediator\"") != std::string::npos);
    }

    // 5) Sampling: gleiche Struktur, weniger Zeilen, R^2 unter minRegressionRows
    {
        Database db("i5");
        auto c = db.createCollection("sampled");
        const int menge[] = {1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6};
        const int noise[] = {7, 3, 9, 1, 5, 8, 2, 6, 0, 4, 11, 10};
        for (int i = 0; i < 12; ++i)
            c.insert({{"menge", menge[i]},
                      {"preis", menge[i] >= 4 ? 100 : 10},
                      {"noise", noise[i]}});
        InfluenceOptions opts;
        opts.numeric.sampleSize = 6;
        InfluenceAnalysis a = analyzeInfluence(c, opts);
        CHECK(a.docCount == 12);
        CHECK(a.sampleSize == 6);
        CHECK(a.fields.size() == 3);
        for (const auto& f : a.fields) {
            CHECK(f.partners <= 2);
            CHECK(!f.rSquaredComputed); // 6 Zeilen < minRegressionRows (8)
        }
    }

    // 6) Database-/Vector-Ueberladungen, Nicht-numerische Felder, Rollennamen
    {
        CHECK(toString(FieldRole::Driver) == "driver");
        CHECK(toString(FieldRole::Dependent) == "dependent");
        CHECK(toString(FieldRole::Mediator) == "mediator");
        CHECK(toString(FieldRole::Independent) == "independent");

        Database db("i6");
        {
            auto c = db.createCollection("num");
            const int menge[] = {1, 1, 2, 2, 3, 3, 4, 4, 5, 5};
            for (int i = 0; i < 10; ++i)
                c.insert({{"menge", menge[i]}, {"preis", menge[i] * 20}});
        }
        {
            auto c = db.createCollection("text");
            for (int i = 0; i < 6; ++i)
                c.insert({{"name", std::string("k") + std::to_string(i)},
                          {"flag", i % 2 == 0},
                          {"v", i}});
        }
        auto all = analyzeInfluence(db);
        CHECK(all.size() == 2);
        CHECK(all[0].collection == "num" || all[1].collection == "num");
        bool foundDep = false;
        for (const auto& a : all)
            if (findDependency(a, "menge", "preis"))
                foundDep = true;
        CHECK(foundDep);
        // Text/Boolean-Felder bleiben unberuecksichtigt
        for (const auto& a : all)
            if (a.collection == "text")
                CHECK(a.numericFieldCount == 1);
        const std::string js = toJson(all);
        CHECK(!js.empty() && js.front() == '[');
        const std::string md = toMarkdown(all);
        CHECK(md.find("# Einfluss-Analyse (FA-31)") != std::string::npos);
        CHECK(md.find("menge") != std::string::npos);
    }

    std::cout << "influence: " << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
