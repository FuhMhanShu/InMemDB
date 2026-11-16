// Tests: Discovery v0.31 – Datenprofil (profileCollection/toJson) + Candidate-Key-Discovery.
#include <memdb/memdb.hpp>

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

const FieldProfile* findField(const CollectionProfile& p, const std::string& name) {
    for (const auto& f : p.fields)
        if (f.name == name)
            return &f;
    return nullptr;
}

} // namespace

int main() {
    // 1) Datenprofil: Typen, Nulls, distinct, min/max, Beispiele
    {
        Database db("prof");
        auto c = db.createCollection("users");
        c.insert({{"id", 1}, {"name", "Ada"}, {"score", 9.5}, {"vip", true}});
        c.insert({{"id", 2}, {"name", "Bob"}, {"score", 7.25}});
        c.insert({{"id", 3}, {"name", "Cy"}, {"score", nullptr}, {"vip", false}});
        c.insert({{"id", 2}, {"name", "Ada"}, {"score", 8.0}}); // Duplikate id/name
        auto p = profileCollection(c);
        CHECK(p.collection == "users");
        CHECK(p.docCount == 4);
        const FieldProfile* id = findField(p, "id");
        CHECK(id && id->present == 4 && id->distinct == 3); // 1,2,3
        CHECK(id->min && id->min->asInt() == 1);
        CHECK(id->max && id->max->asInt() == 3);
        const FieldProfile* score = findField(p, "score");
        CHECK(score && score->present == 4 && score->nulls == 1 && score->distinct == 3);
        CHECK(score->min && score->min->asDouble() == 7.25);
        CHECK(score->max && score->max->asDouble() == 9.5);
        const FieldProfile* vip = findField(p, "vip");
        CHECK(vip && vip->present == 2 && vip->typeCounts.count(Value::Type::Bool) == 1 && vip->typeCounts.at(Value::Type::Bool) == 2);
        const FieldProfile* name = findField(p, "name");
        CHECK(name && name->distinct == 3 && name->examples.size() <= 3);
        // toJson
        std::string j = toJson(p);
        CHECK(j.find("\"collection\":\"users\"") != std::string::npos);
        CHECK(j.find("\"distinct\":3") != std::string::npos);
        CHECK(j.find("\"docCount\":4") != std::string::npos);
    }

    // 2) Candidate Keys: Einzelfeld id ist Key
    {
        Database db("keys");
        auto c = db.createCollection("users");
        c.insert({{"id", 1}, {"name", "Ada"}});
        c.insert({{"id", 2}, {"name", "Bob"}});
        c.insert({{"id", 3}, {"name", "Cy"}});
        auto keys = discoverCandidateKeys(c);
        bool hasIdKey = false;
        for (const auto& k : keys)
            if (k == std::vector<std::string>{"id"})
                hasIdKey = true;
        CHECK(hasIdKey); // id ist Key (name hier zufällig auch eindeutig)
    }

    // 3) Candidate Keys: kein Einzelfeld-Key, aber Paar (a,b) ist Key
    {
        Database db("keys2");
        auto c = db.createCollection("events");
        c.insert({{"a", 1}, {"b", 1}, {"v", "x"}});
        c.insert({{"a", 1}, {"b", 2}, {"v", "x"}});
        c.insert({{"a", 2}, {"b", 1}, {"v", "x"}});
        c.insert({{"a", 2}, {"b", 2}, {"v", "x"}}); // v konstant -> nur (a,b) ist Key
        auto keys = discoverCandidateKeys(c);
        CHECK(keys.size() == 1);
        CHECK(keys[0].size() == 2); // Paar (a,b)
        bool hasA = false, hasB = false;
        for (auto& f : keys[0]) {
            if (f == "a") hasA = true;
            if (f == "b") hasB = true;
        }
        CHECK(hasA && hasB);
    }

    // 4) Kein Key wenn Duplikat-Kombination existiert
    {
        Database db("keys3");
        auto c = db.createCollection("t");
        c.insert({{"a", 1}, {"b", 1}});
        c.insert({{"a", 1}, {"b", 1}}); // Duplikat -> kein Key
        auto keys = discoverCandidateKeys(c);
        CHECK(keys.empty());
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
