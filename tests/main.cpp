#include "memdb/memdb.hpp"

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

void testValue() {
    Value n;
    CHECK(n.isNull());
    CHECK(Value(true).asBool() == true);
    CHECK(Value(42).isInt());
    CHECK(Value(42).asInt() == 42);
    CHECK(Value(3.5).isDouble());
    CHECK(Value(2).asDouble() == 2.0);
    CHECK(Value("hallo").asString() == "hallo");
    CHECK(Value(std::string("x")).isString());
    CHECK(Value(std::vector<Value>{Value(1), Value(2)}).isArray());
    CHECK(Value(std::vector<Value>{Value(1)}).asArray().size() == 1);

    CHECK_THROWS_AS(Value(1).asString(), TypeError);
    CHECK_THROWS_AS(Value("x").asInt(), TypeError);
    CHECK_THROWS_AS(Value(1.5).asInt(), TypeError);

    CHECK(equal(Value(1), Value(1.0)));
    CHECK(!equal(Value(1), Value(2)));
    CHECK(equal(Value("a"), Value("a")));
    CHECK(!equal(Value("a"), Value(1)));
    CHECK(compare(Value(1), Value(2)).value() < 0);
    CHECK(compare(Value(2), Value(1)).value() > 0);
    CHECK(compare(Value(1), Value(1)).value() == 0);
    CHECK(compare(Value(2.5), Value(2)).value() > 0);
    CHECK(compare(Value("a"), Value("b")).value() < 0);
    CHECK(!compare(Value(true), Value(true)).has_value());
    CHECK(!compare(Value("a"), Value(1)).has_value());
}

void testDocument() {
    Document d = {{"name", "Ada"}, {"age", 36}};
    CHECK(d.fieldCount() == 2);
    CHECK(d.contains("name"));
    CHECK(!d.contains("stadt"));
    CHECK(d.at("age").asInt() == 36);
    CHECK_THROWS_AS(d.at("fehlt"), Error);

    d["age"] = 37;
    CHECK(d.at("age").asInt() == 37);

    d.set("beruf", "Ingenieurin");
    CHECK(d.contains("beruf"));
    CHECK(d.at("beruf").asString() == "Ingenieurin");

    d["alter_typ"] = "neu";
    CHECK(d.at("alter_typ").isString());

    CHECK(d.rename("beruf", "job"));
    CHECK(!d.contains("beruf"));
    CHECK(d.contains("job"));
    CHECK(!d.rename("fehlt", "x"));
    CHECK(!d.rename("job", "name"));

    CHECK(d.remove("alter_typ"));
    CHECK(!d.contains("alter_typ"));
    CHECK(!d.remove("alter_typ"));

    auto addr = std::make_shared<Document>(std::initializer_list<std::pair<const std::string, Value>>{
        {"stadt", "Berlin"}, {"plz", "10115"}});
    d.set("adresse", Value(addr));
    const Value* city = d.findPath("adresse.stadt");
    CHECK(city != nullptr);
    CHECK(city->asString() == "Berlin");
    CHECK(d.findPath("adresse.land") == nullptr);
    CHECK(d.findPath("fehlt.x") == nullptr);

    Document copy = d;
    CHECK(copy == d);
    copy.set("name", "Grace");
    CHECK(copy != d);
    CHECK(d.at("name").asString() == "Ada");
}

void testDatabase() {
    Database db("testdb");
    CHECK(db.name() == "testdb");
    CHECK(!db.hasCollection("users"));
    CHECK_THROWS_AS(db.collection("users"), CollectionNotFoundError);

    Collection users = db.createCollection("users");
    CHECK(db.hasCollection("users"));
    CHECK_THROWS_AS(db.createCollection("users"), CollectionExistsError);
    CHECK(db.collectionNames().size() == 1);
    CHECK(db.collection("users").name() == "users");

    CHECK(db.dropCollection("users"));
    CHECK(!db.hasCollection("users"));
    CHECK(!db.dropCollection("users"));

    db.createCollection("a");
    db.createCollection("b");
    CHECK(db.collectionNames().size() == 2);
    db.clear();
    CHECK(db.collectionNames().empty());
}

void testCollectionCrud() {
    Database db;
    Collection users = db.createCollection("users");

    DocumentId ada = users.insert({{"name", "Ada"}, {"age", 36}, {"tags", Value(std::vector<Value>{Value("cpp"), Value("math")})}});
    DocumentId bob = users.insert({{"name", "Bob"}, {"age", 25}});
    DocumentId cy  = users.insert({{"name", "Cy"}, {"age", 40}});
    CHECK(ada != bob && bob != cy);
    CHECK(users.size() == 3);

    auto doc = users.findById(ada);
    CHECK(doc.has_value());
    CHECK(doc->id() == ada);
    CHECK(doc->at("name").asString() == "Ada");
    CHECK(!users.findById(DocumentId(9999)).has_value());

    auto ids = users.insertBulk({Document{{"name", "X"}}, Document{{"name", "Y"}}});
    CHECK(ids.size() == 2);
    CHECK(users.size() == 5);

    CHECK(users.updateById(bob, [](Document& d) { d.set("age", 26); }));
    CHECK(users.findById(bob)->at("age").asInt() == 26);
    CHECK(!users.updateById(DocumentId(9999), [](Document&) {}));

    Document neu = {{"name", "Bob"}, {"age", 27}};
    CHECK(users.replace(bob, neu));
    CHECK(users.findById(bob)->at("age").asInt() == 27);
    CHECK(users.findById(bob)->fieldCount() == 2);
    CHECK(!users.replace(DocumentId(9999), neu));

    CHECK(users.removeById(cy));
    CHECK(users.size() == 4);
    CHECK(!users.removeById(cy));
}

void testQuery() {
    Database db;
    Collection users = db.createCollection("users");
    users.insert({{"name", "Ada"}, {"age", 36}, {"adresse", Value(std::make_shared<Document>(std::initializer_list<std::pair<const std::string, Value>>{{"stadt", "Berlin"}}))}, {"tags", Value(std::vector<Value>{Value("cpp")})}});
    users.insert({{"name", "Bob"}, {"age", 25}, {"adresse", Value(std::make_shared<Document>(std::initializer_list<std::pair<const std::string, Value>>{{"stadt", "Berlin"}}))}});
    users.insert({{"name", "Cy"}, {"age", 40}, {"adresse", Value(std::make_shared<Document>(std::initializer_list<std::pair<const std::string, Value>>{{"stadt", "Hamburg"}}))}, {"gesperrt", true}});
    users.insert({{"name", "Dee"}});

    CHECK(users.count(Field("age") >= 30) == 2);
    CHECK(users.count(Field("age") < 30) == 1);
    CHECK(users.count(Field("name") == "Ada") == 1);
    CHECK(users.count(Field("name") != "Ada") == 3);
    CHECK(users.count((Field("adresse.stadt") == "Berlin") && (Field("age") >= 30)) == 1);
    CHECK(users.count((Field("adresse.stadt") == "Hamburg") || (Field("age") < 30)) == 2);
    CHECK(users.count(!(Field("gesperrt") == true)) == 3);
    CHECK(users.count(Field("tags").contains("cpp")) == 1);
    CHECK(users.count(Field("adresse.stadt").contains("Ber")) == 2);
    CHECK(users.count(Field("gesperrt").exists()) == 1);
    CHECK(users.count(Field("fehlt") != Value(1)) == 4);
    CHECK(users.count(Expression{}) == 4);

    auto cursor = users.find(Field("age") > 30);
    CHECK(cursor.size() == 2);
    for (const auto& doc : cursor)
        CHECK(doc.at("age").asInt() > 30);

    auto one = users.findOne(Field("name") == "Bob");
    CHECK(one.has_value());
    CHECK(one->at("age").asInt() == 25);
    CHECK(!users.findOne(Field("name") == "Niemand").has_value());

    std::size_t changed = users.update(Field("adresse.stadt") == "Berlin",
                                       [](Document& d) { d.set("geprueft", true); });
    CHECK(changed == 2);
    CHECK(users.count(Field("geprueft") == true) == 2);

    CHECK(users.remove(Field("name") == "Dee") == 1);
    CHECK(users.size() == 3);
    CHECK(users.remove(Expression{}) == 3);
    CHECK(users.size() == 0);

    CHECK_THROWS_AS((Field("age") >= 1).contains(Value(2)), QueryError);
    CHECK_THROWS_AS(Expression{} == Value(1), QueryError);
}

void testIndexes() {
    Database db;
    Collection c = db.createCollection("items");
    DocumentId a = c.insert({{"name", "a"}, {"age", 10}});
    c.insert({{"name", "b"}, {"age", 20}});
    c.insert({{"name", "c"}, {"age", 30}});
    c.insert({{"name", "d"}, {"age", 40}});

    CHECK(!c.hasIndex("age"));
    CHECK(c.indexes().empty());
    Index idx = c.createIndex("age");
    CHECK(c.hasIndex("age"));
    CHECK(c.indexes().size() == 1);
    CHECK(idx.field() == "age");
    CHECK(!idx.isUnique());
    CHECK_THROWS_AS(c.createIndex("age"), IndexExistsError);

    auto r = c.find(Field("age") == 30);
    CHECK(r.size() == 1);
    CHECK(r.front().at("name").asString() == "c");
    CHECK(c.lastQueryCandidateCount() == 1);

    CHECK(c.find(Field("age") == 30.0).size() == 1);
    CHECK(c.count(Field("age") >= 30) == 2);
    CHECK(c.count(Field("age") > 10) == 3);
    CHECK(c.count(Field("age") < 30) == 2);
    CHECK(c.count(Field("age") <= 20) == 2);
    CHECK(c.count(Field("age") >= 30) == 2);
    CHECK(c.lastQueryCandidateCount() == 2);

    CHECK(c.count(Field("name") == "a") == 1);
    CHECK(c.lastQueryCandidateCount() == 4);

    CHECK(c.count((Field("age") >= 20) && (Field("name") == "b")) == 1);
    CHECK(c.lastQueryCandidateCount() == 3);

    CHECK(c.updateById(a, [](Document& d) { d.set("age", 100); }));
    CHECK(c.count(Field("age") == 10) == 0);
    CHECK(c.count(Field("age") == 100) == 1);
    CHECK(c.count(Field("age") > 50) == 1);

    c.remove(Field("age") == 100);
    CHECK(c.count(Field("age") == 100) == 0);
    CHECK(c.size() == 3);

    Collection u = db.createCollection("users");
    u.insert({{"mail", "x@y.de"}});
    Index uidx = u.createIndex("mail", true);
    CHECK(uidx.isUnique());
    CHECK_THROWS_AS(u.insert({{"mail", "x@y.de"}}), UniqueConstraintError);
    u.insert({{"mail", "a@b.de"}});
    CHECK(u.size() == 2);
    DocumentId second = u.findOne(Field("mail") == "a@b.de")->id();
    CHECK_THROWS_AS(u.updateById(second, [](Document& d) { d.set("mail", "x@y.de"); }), UniqueConstraintError);
    CHECK(u.findById(second)->at("mail").asString() == "a@b.de");
    CHECK_THROWS_AS(u.createIndex("mail", true), IndexExistsError);
    Collection u2 = db.createCollection("u2");
    u2.insert({{"mail", "d@e.de"}});
    u2.insert({{"mail", "d@e.de"}});
    CHECK_THROWS_AS(u2.createIndex("mail", true), UniqueConstraintError);

    CHECK(c.dropIndex("age"));
    CHECK(!c.hasIndex("age"));
    CHECK(!c.dropIndex("age"));
    CHECK(c.count(Field("age") == 20) == 1);
    CHECK(c.lastQueryCandidateCount() == 3);
}

} // namespace

int main() {
    testValue();
    testDocument();
    testDatabase();
    testCollectionCrud();
    testQuery();
    testIndexes();

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    if (g_failures == 0)
        std::cout << "ALLE TESTS BESTANDEN\n";
    return g_failures == 0 ? 0 : 1;
}
