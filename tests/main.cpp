#include "memdb/memdb.hpp"
#include "crypto.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
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

void testTransactions() {
    Database db;
    Collection c = db.createCollection("konten");
    c.createIndex("name", true);
    DocumentId a = c.insert({{"name", "a"}, {"saldo", 100}});
    c.insert({{"name", "b"}, {"saldo", 50}});

    {
        Transaction tx = c.beginTransaction();
        CHECK(tx.isActive());
        tx.insert({{"name", "tmp"}, {"saldo", 0}});
        tx.updateById(a, [](Document& d) { d.set("saldo", 999); });
        CHECK(tx.size() == 3);
        CHECK(tx.findById(a)->at("saldo").asInt() == 999);
        CHECK(tx.count(Field("saldo") >= 500) == 1);
        tx.rollback();
        CHECK(!tx.isActive());
        CHECK_THROWS_AS(tx.size(), Error);
    }
    CHECK(c.size() == 2);
    CHECK(c.findById(a)->at("saldo").asInt() == 100);

    {
        Transaction tx = c.beginTransaction();
        std::size_t n = tx.update(Field("saldo") <= 100, [](Document& d) { d.set("saldo", d.at("saldo").asInt() + 10); });
        CHECK(n == 2);
        tx.remove(Field("name") == "b");
        tx.commit();
        CHECK(!tx.isActive());
    }
    CHECK(c.size() == 1);
    CHECK(c.findById(a)->at("saldo").asInt() == 110);

    {
        Transaction tx = c.beginTransaction();
        tx.remove(Expression{});
        CHECK(tx.size() == 0);
    }
    CHECK(c.size() == 1);

    {
        Transaction tx = c.beginTransaction();
        CHECK_THROWS_AS(tx.insert({{"name", "a"}, {"saldo", 1}}), UniqueConstraintError);
        CHECK(tx.isActive());
        tx.rollback();
    }
    CHECK(c.count(Field("name") == "a") == 1);

    {
        Transaction t1 = c.beginTransaction();
        t1.insert({{"name", "c"}, {"saldo", 5}});
        t1.commit();
        Transaction t2 = c.beginTransaction();
        CHECK(t2.count(Field("name") == "c") == 1);
        t2.rollback();
    }
    CHECK(c.size() == 2);
}

void testSnapshotJson() {
    Database db("shop");
    Collection users = db.createCollection("users");
    users.createIndex("name", true);
    users.insert({{"name", "Ada"}, {"age", 36}, {"aktiv", true}});
    users.insert({{"name", "München ✓"}, {"preis", 49.99}, {"lager", 12}, {"genau", 5.0},
                  {"tags", Value(std::vector<Value>{Value("a\"b"), Value("zeile\n1"), Value(2)})},
                  {"adresse", Value(std::make_shared<Document>(std::initializer_list<std::pair<const std::string, Value>>{{"stadt", "Berlin"}, {"plz", "10115"}}))},
                  {"hash", Value(std::vector<std::uint8_t>{0xDE, 0xAD, 0xBE, 0xEF})},
                  {"leer", Value(nullptr)}});
    db.createCollection("leer");

    std::string json = db.toJson();
    CHECK(json.find("\"name\":\"shop\"") != std::string::npos);
    CHECK(json.find("\"collections\"") != std::string::npos);
    CHECK(json.find("\"indexes\"") != std::string::npos);
    CHECK(json.find("\"field\":\"name\"") != std::string::npos);
    CHECK(json.find("\"unique\":true") != std::string::npos);

    Database db2 = Database::fromJson(json, "");
    CHECK(db2.name() == "shop");
    CHECK(db2.collectionNames().size() == 2);
    Collection users2 = db2.collection("users");
    CHECK(users2.size() == 2);
    CHECK(users2.hasIndex("name"));

    auto byName = [](Collection& c, const std::string& n) {
        return c.findOne(Field("name") == n);
    };
    auto ada2 = byName(users2, "Ada");
    CHECK(ada2.has_value());
    CHECK(ada2->at("age").isInt());
    CHECK(ada2->at("age").asInt() == 36);
    auto m2 = byName(users2, "München ✓");
    CHECK(m2.has_value());
    CHECK(m2->at("preis").isDouble());
    CHECK(m2->at("preis").asDouble() == 49.99);
    CHECK(m2->at("genau").isDouble());
    CHECK(m2->at("genau").asDouble() == 5.0);
    CHECK(m2->at("lager").isInt());
    CHECK(m2->at("hash").isBinary());
    CHECK(m2->at("hash").asBinary() == std::vector<std::uint8_t>({0xDE, 0xAD, 0xBE, 0xEF}));
    CHECK(m2->at("leer").isNull());
    CHECK(m2->at("tags").asArray().size() == 3);
    CHECK(m2->at("tags").asArray()[0].asString() == "a\"b");
    CHECK(m2->at("tags").asArray()[1].asString() == "zeile\n1");
    CHECK(m2->findPath("adresse.plz")->asString() == "10115");

    CHECK(db2.collection("leer").size() == 0);

    db.fromJson(json);
    CHECK(db.collection("users").size() == 2);
    CHECK(db.collection("users").hasIndex("name"));
    CHECK(db.collection("users").count(Field("age") == 36) == 1);

    std::string collJson = users.toJson();
    Collection users3 = db2.createCollection("users3");
    users3.loadJson(collJson);
    CHECK(users3.size() == 2);
    CHECK(users3.count(Field("age") == 36) == 1);

    const std::string path = "inmemdb_snapshot_test.json";
    db.saveToFile(path);
    Database db3 = Database::loadFromFile(path);
    CHECK(db3.name() == "shop");
    CHECK(db3.collection("users").size() == 2);
    std::remove(path.c_str());

    CHECK_THROWS_AS(Database::fromJson("{invalid", ""), JsonError);
    CHECK_THROWS_AS(Database::fromJson("[1,2]", ""), JsonError);
    CHECK_THROWS_AS(users3.loadJson("{}"), JsonError);
    CHECK_THROWS_AS(Database::loadFromFile("definitiv_nicht_vorhanden_12345.json"), Error);

    users3.clear();
    CHECK(users3.size() == 0);
    users3.loadJson(collJson);
    CHECK(users3.size() == 2);
}

void testUpsert() {
    Database db;
    Collection c = db.createCollection("users");
    c.createIndex("name", true);
    DocumentId a = c.insert({{"name", "Ada"}, {"age", 36}});

    DocumentId r1 = c.upsertById(a, Document{{"name", "Ada"}, {"age", 37}});
    CHECK(r1 == a);
    CHECK(c.size() == 1);
    CHECK(c.findById(a)->at("age").asInt() == 37);

    DocumentId r2 = c.upsertById(DocumentId(9999), Document{{"name", "Bob"}, {"age", 25}});
    CHECK(r2 != DocumentId(9999));
    CHECK(c.size() == 2);
    CHECK(c.findById(r2)->at("name").asString() == "Bob");

    DocumentId r3 = c.upsert(Field("name") == "Ada", Document{{"name", "Ada"}, {"age", 38}, {"neu", true}});
    CHECK(r3 == a);
    CHECK(c.size() == 2);
    CHECK(c.findById(a)->at("age").asInt() == 38);
    CHECK(c.findById(a)->at("neu").asBool() == true);

    DocumentId r4 = c.upsert(Field("name") == "Cy", Document{{"name", "Cy"}, {"age", 40}});
    CHECK(c.size() == 3);
    CHECK(c.findById(r4)->at("name").asString() == "Cy");

    CHECK_THROWS_AS(c.upsert(Field("name") == "Cy", Document{{"name", "Ada"}}), UniqueConstraintError);
    CHECK(c.count(Field("name") == "Ada") == 1);
    CHECK(c.findById(r4)->at("name").asString() == "Cy");

    {
        Transaction tx = c.beginTransaction();
        DocumentId r5 = tx.upsert(Field("name") == "Bob", Document{{"name", "Bob"}, {"age", 26}});
        CHECK(r5 == r2);
        CHECK(tx.findById(r2)->at("age").asInt() == 26);
        tx.rollback();
    }
    CHECK(c.findById(r2)->at("age").asInt() == 25);
}

void testAggregations() {
    Database db;
    Collection sales = db.createCollection("sales");
    sales.createIndex("city");
    sales.insert({{"city", "Berlin"}, {"betrag", 10}, {"kunde", "x"}});
    sales.insert({{"city", "Berlin"}, {"betrag", 20}, {"kunde", "y"}});
    sales.insert({{"city", "Hamburg"}, {"betrag", 5}, {"kunde", "x"}});
    sales.insert({{"city", "Hamburg"}, {"betrag", Value(nullptr)}});
    sales.insert({{"city", "München"}, {"betrag", 7.5}});
    sales.insert({{"betrag", 100}});

    CHECK(sales.aggregate(AggOp::Count, "").value().asInt() == 6);
    CHECK(sales.aggregate(AggOp::Count, "", Field("city") == "Berlin").value().asInt() == 2);
    CHECK(sales.aggregate(AggOp::Sum, "betrag").value().asDouble() == 142.5);
    CHECK(sales.aggregate(AggOp::Sum, "betrag", Field("city") == "Berlin").value().asInt() == 30);
    CHECK(sales.aggregate(AggOp::Avg, "betrag").value().asDouble() == 142.5 / 5.0);
    CHECK(sales.aggregate(AggOp::Min, "betrag").value().asInt() == 5);
    CHECK(sales.aggregate(AggOp::Max, "betrag").value().asInt() == 100);
    CHECK(sales.aggregate(AggOp::Min, "city").value().asString() == "Berlin");
    CHECK(sales.aggregate(AggOp::Max, "city").value().asString() == "München");
    CHECK(!sales.aggregate(AggOp::Sum, "betrag", Field("city") == "Nowhere").has_value());
    CHECK(sales.aggregate(AggOp::Count, "", Field("city") == "Nowhere").value().asInt() == 0);

    auto groups = sales.groupBy("city");
    CHECK(groups.size() == 4);
    CHECK(groups[0].key.isNull());
    CHECK(groups[0].count == 1);
    CHECK(!groups[0].value.has_value());
    CHECK(groups[1].key.asString() == "Berlin");
    CHECK(groups[1].count == 2);
    CHECK(groups[2].key.asString() == "Hamburg");
    CHECK(groups[2].count == 2);
    CHECK(groups[3].key.asString() == "München");
    CHECK(groups[3].count == 1);

    auto gsum = sales.groupBy("city", Expression{}, AggOp::Sum, "betrag");
    CHECK(gsum.size() == 4);
    CHECK(gsum[0].value.value().asInt() == 100);
    CHECK(gsum[1].value.value().asInt() == 30);
    CHECK(gsum[2].value.value().asDouble() == 5.0);
    CHECK(gsum[3].value.value().asDouble() == 7.5);

    auto gavg = sales.groupBy("city", Field("betrag") > 0, AggOp::Avg, "betrag");
    CHECK(gavg[1].value.value().asDouble() == 15.0);

    {
        Transaction tx = sales.beginTransaction();
        CHECK(tx.aggregate(AggOp::Count, "").value().asInt() == 6);
        tx.insert({{"city", "Berlin"}, {"betrag", 1000}});
        CHECK(tx.aggregate(AggOp::Sum, "betrag", Field("city") == "Berlin").value().asInt() == 1030);
        tx.rollback();
    }
    CHECK(sales.aggregate(AggOp::Sum, "betrag", Field("city") == "Berlin").value().asInt() == 30);
}

void testObserver() {
    Database db;
    Collection c = db.createCollection("users");

    std::vector<ChangeEvent> events;
    std::uint64_t h1 = c.onChange([&](const ChangeEvent& e) { events.push_back(e); });
    int calls2 = 0;
    std::uint64_t h2 = c.onChange([&](const ChangeEvent&) { ++calls2; });

    DocumentId a = c.insert({{"name", "Ada"}, {"age", 36}});
    CHECK(events.size() == 1);
    CHECK(events[0].type == ChangeType::Insert);
    CHECK(events[0].collection == "users");
    CHECK(events[0].id == a);
    CHECK(events[0].document.at("name").asString() == "Ada");
    CHECK(calls2 == 1);

    c.updateById(a, [](Document& d) { d.set("age", 37); });
    CHECK(events.size() == 2);
    CHECK(events[1].type == ChangeType::Update);
    CHECK(events[1].document.at("age").asInt() == 37);

    c.update(Field("name") == "Ada", [](Document& d) { d.set("x", 1); });
    CHECK(events.size() == 3);
    CHECK(events[2].type == ChangeType::Update);

    c.upsert(Field("name") == "Bob", {{"name", "Bob"}});
    CHECK(events.size() == 4);
    CHECK(events[3].type == ChangeType::Insert);

    c.removeById(a);
    CHECK(events.size() == 5);
    CHECK(events[4].type == ChangeType::Remove);
    CHECK(events[4].document.at("name").asString() == "Ada");

    CHECK(c.offChange(h2));
    c.insert({{"name", "Cy"}});
    CHECK(events.size() == 6);
    CHECK(calls2 == 5);
    CHECK(!c.offChange(h2));

    c.clear();
    CHECK(events.size() == 7);
    CHECK(events[6].type == ChangeType::Clear);

    c.offChange(h1);
    c.insert({{"name", "Dee"}});
    CHECK(events.size() == 7);

    {
        std::vector<ChangeEvent> txEvents;
        std::uint64_t h = c.onChange([&](const ChangeEvent& e) { txEvents.push_back(e); });
        c.insert({{"name", "Base"}});
        CHECK(txEvents.size() == 1);
        {
            Transaction tx = c.beginTransaction();
            tx.insert({{"name", "Tx1"}});
            tx.update(Field("name") == "Base", [](Document& d) { d.set("v", 2); });
            CHECK(txEvents.size() == 1);
            tx.commit();
        }
        CHECK(txEvents.size() == 3);
        CHECK(txEvents[1].type == ChangeType::Insert);
        CHECK(txEvents[2].type == ChangeType::Update);
        {
            Transaction tx = c.beginTransaction();
            tx.remove(Expression{});
            tx.rollback();
        }
        CHECK(txEvents.size() == 3);
        CHECK(c.size() == 3);
        c.offChange(h);
    }

    {
        bool reentrantOk = true;
        std::uint64_t h = c.onChange([&](const ChangeEvent&) {
            if (c.count(Expression{}) > 1000000)
                reentrantOk = false;
        });
        c.insert({{"name", "Re"}});
        CHECK(reentrantOk);
        c.offChange(h);
    }
}

void testReferences() {
    Database db("shop");
    Collection users = db.createCollection("users");
    Collection orders = db.createCollection("orders");
    DocumentId ada = users.insert({{"name", "Ada"}});
    DocumentId bob = users.insert({{"name", "Bob"}});
    DocumentId o1 = orders.insert({{"total", 42}});

    ReferenceIndex refs(db);
    CHECK(db.hasCollection("__references"));
    CHECK(db.collection("__references").hasIndex("from_key"));
    CHECK(db.collection("__references").hasIndex("to_key"));

    CHECK(refs.resolve("users", "orders") == "orders");
    CHECK(refs.resolve("users", "./orders") == "orders");
    CHECK(refs.resolve("users", "../orders") == "orders");
    CHECK(refs.resolve("users", "/orders") == "orders");
    CHECK(refs.resolve("a/b", "../c") == "c");
    CHECK(refs.resolve("a/b", "./c") == "a/c");
    CHECK(refs.resolve("a/b/c", "../../x") == "x");
    CHECK_THROWS_AS(refs.resolve("users", ""), ReferenceError);
    CHECK_THROWS_AS(refs.resolve("users", "/"), ReferenceError);

    Reference r1 = refs.link("users", ada, "orders", std::nullopt, "hat-bestellungen");
    CHECK(r1.fromCollection == "users");
    CHECK(r1.fromId == ada);
    CHECK(r1.toCollection == "orders");
    CHECK(!r1.toId.has_value());
    CHECK(r1.relation == "hat-bestellungen");

    Reference r2 = refs.link("orders", o1, "users", ada);
    Reference r3 = refs.link("users", std::nullopt, "orders", std::nullopt, "kennt");
    Reference r4 = refs.link("orders", o1, "./users", bob);
    CHECK(r4.toCollection == "users");
    CHECK(r4.toId == bob);

    CHECK_THROWS_AS(refs.link("users", ada, "orders", std::nullopt, "hat-bestellungen"), ReferenceExistsError);
    refs.link("users", ada, "orders", std::nullopt, "andere-relation");
    CHECK_THROWS_AS(refs.link("fehlt", std::nullopt, "orders", std::nullopt), CollectionNotFoundError);
    CHECK_THROWS_AS(refs.link("users", DocumentId(9999), "orders", std::nullopt), DocumentNotFoundError);
    CHECK_THROWS_AS(refs.link("users", ada, "fehlt", std::nullopt), CollectionNotFoundError);
    CHECK_THROWS_AS(refs.link("users", ada, "orders", DocumentId(9999)), DocumentNotFoundError);

    auto outAda = refs.references("users", RefDirection::Outgoing, ada);
    CHECK(outAda.size() == 2);
    auto inOrders = refs.references("orders", RefDirection::Incoming);
    CHECK(inOrders.size() == 3);
    auto outColl = refs.references("users", RefDirection::Outgoing);
    CHECK(outColl.size() == 1 && outColl[0] == r3);
    auto inAda = refs.references("users", RefDirection::Incoming, ada);
    CHECK(inAda.size() == 1 && inAda[0] == r2);
    auto outO1 = refs.references("orders", RefDirection::Outgoing, o1);
    CHECK(outO1.size() == 2);
    auto bothAda = refs.references("users", RefDirection::Both, ada);
    CHECK(bothAda.size() == 3);
    auto inBob = refs.references("users", RefDirection::Incoming, bob);
    CHECK(inBob.size() == 1 && inBob[0] == r4);

    CHECK(refs.unlink("users", ada, "orders", std::nullopt, "hat-bestellungen"));
    CHECK(!refs.unlink("users", ada, "orders", std::nullopt, "hat-bestellungen"));
    CHECK(refs.references("users", RefDirection::Outgoing, ada).size() == 1);

    orders.removeById(o1);
    CHECK(refs.dangling().size() == 2);
    CHECK(refs.pruneDangling() == 2);
    CHECK(refs.dangling().empty());
    CHECK(refs.references("orders", RefDirection::Incoming).size() == 2);

    db.dropCollection("orders");
    CHECK(refs.dangling().size() == 2);
    CHECK(refs.pruneDangling() == 2);
    CHECK(refs.dangling().empty());

    refs.link("users", ada, "users", bob, "kennt");
    std::string json = db.toJson();
    Database db2 = Database::fromJson(json, "");
    CHECK(db2.hasCollection("__references"));
    CHECK(db2.collection("__references").hasIndex("from_key"));
    ReferenceIndex refs2(db2);
    auto rt = refs2.references("users", RefDirection::Both, ada);
    CHECK(rt.size() == 1);
    CHECK(rt[0].toCollection == "users" && rt[0].toId == bob && rt[0].relation == "kennt");
}

void testReferenceTraversal() {
    Database db;
    Collection a = db.createCollection("a");
    Collection b = db.createCollection("b");
    Collection c = db.createCollection("c");
    DocumentId a1 = a.insert({{"n", "a1"}});
    DocumentId b1 = b.insert({{"n", "b1"}});
    DocumentId c1 = c.insert({{"n", "c1"}});

    ReferenceIndex refs(db);
    refs.link("a", a1, "b", b1);
    refs.link("b", b1, "c", c1);
    refs.link("c", c1, "a", a1);                        // Ring: a1 -> b1 -> c1 -> a1
    refs.link("a", a1, "a", a1, "selbst");              // Selbstverweis
    refs.link("a", std::nullopt, "b", std::nullopt, "coll");

    auto all = refs.traverse("a", a1, RefDirection::Outgoing);
    CHECK(all.size() == 4);                             // terminiert trotz Ring dank visited
    auto d1 = refs.traverse("a", a1, RefDirection::Outgoing, 1);
    CHECK(d1.size() == 2);                              // a1->b1, a1->a1
    auto d2 = refs.traverse("a", a1, RefDirection::Outgoing, 2);
    CHECK(d2.size() == 3);                              // + b1->c1
    CHECK(refs.traverse("a", a1, RefDirection::Outgoing, 0).empty());

    auto inc = refs.traverse("c", c1, RefDirection::Incoming);
    CHECK(inc.size() == 4);                             // b1->c1, a1->b1, c1->a1, a1->a1

    auto incColl = refs.traverse("b", std::nullopt, RefDirection::Incoming);
    CHECK(incColl.size() == 1 && incColl[0].relation == "coll");   // a#->b# landet auf Collection-Level

    auto coll = refs.traverse("a", std::nullopt, RefDirection::Outgoing);
    CHECK(coll.size() == 1 && coll[0].relation == "coll");

    auto both = refs.traverse("b", b1, RefDirection::Both);
    CHECK(both.size() == 4);                            // a1/b1/c1-Kreis + Selbstverweis, keine Duplikate

    CHECK(refs.hasCycle("a", a1, RefDirection::Outgoing));
    CHECK(refs.hasCycle("b", b1, RefDirection::Outgoing));
    CHECK(refs.hasCycle("a", a1, RefDirection::Incoming));
    CHECK(refs.hasCycle("a", a1, RefDirection::Both));

    Database db2;
    Collection x = db2.createCollection("x");
    Collection y = db2.createCollection("y");
    DocumentId x1 = x.insert({{"n", "x1"}});
    DocumentId y1 = y.insert({{"n", "y1"}});
    ReferenceIndex refs2(db2);
    refs2.link("x", x1, "y", y1);
    CHECK(!refs2.hasCycle("x", x1, RefDirection::Outgoing));
    CHECK(!refs2.hasCycle("x", x1, RefDirection::Both));   // Parent-Skip: kein False-Positive
    CHECK(refs2.traverse("x", x1, RefDirection::Outgoing).size() == 1);
    CHECK(refs2.traverse("x", x1, RefDirection::Both).size() == 1);
}

void testReferenceSearch() {
    Database db;
    Collection users = db.createCollection("users");
    Collection orders = db.createCollection("orders");
    Collection products = db.createCollection("products");
    DocumentId ada = users.insert({{"name", "Ada"}, {"age", 36}});
    DocumentId bob = users.insert({{"name", "Bob"}, {"age", 25}});
    DocumentId o1 = orders.insert({{"total", 42}, {"status", "offen"}});
    DocumentId o2 = orders.insert({{"total", 7}, {"status", "bezahlt"}});
    DocumentId p1 = products.insert({{"name", "Buch"}, {"preis", 10}});

    ReferenceIndex refs(db);
    refs.link("users", ada, "orders", o1);
    refs.link("orders", o1, "products", p1, "enthaelt");
    refs.link("users", bob, "orders", o2);
    refs.link("orders", o2, "users", ada);   // Ring zurueck zu ada

    auto all = refs.search("users", ada);
    CHECK(all.size() == 3);                  // ada, o1, p1
    CHECK(all[0].collection == "users" && all[0].id == ada && all[0].path.empty());

    auto hit = refs.search("users", ada, Field("status") == "offen");
    CHECK(hit.size() == 1);
    CHECK(hit[0].collection == "orders" && hit[0].id == o1);
    CHECK(hit[0].path.size() == 1);
    CHECK(hit[0].path[0].fromId == ada && hit[0].path[0].toId == o1);
    CHECK(hit[0].document.at("total").asInt() == 42);

    auto deep = refs.search("users", ada, Field("preis") > 5);
    CHECK(deep.size() == 1 && deep[0].id == p1 && deep[0].path.size() == 2);

    auto byAge = refs.search("users", ada, Field("age") >= 30);
    CHECK(byAge.size() == 1 && byAge[0].id == ada);

    CHECK(refs.search("users", ada, Expression{}, RefDirection::Outgoing, 0).size() == 1);
    CHECK(refs.search("users", ada, Expression{}, RefDirection::Outgoing, 1).size() == 2);

    auto in = refs.search("products", p1, Expression{}, RefDirection::Incoming);
    CHECK(in.size() == 5);                   // p1, o1, ada, o2, bob — terminiert trotz Ring
    auto inName = refs.search("products", p1, Field("name").exists(), RefDirection::Incoming);
    CHECK(inName.size() == 3);               // ada, bob, p1 (o1/o2 haben kein name)

    auto fromColl = refs.search("users", std::nullopt);
    CHECK(fromColl.size() == 5);             // alle users-Doks + o1, o2, p1
    CHECK(refs.search("users", std::nullopt, Expression{}, RefDirection::Outgoing, 0).size() == 2);

    auto both = refs.search("users", ada, Expression{}, RefDirection::Both);
    CHECK(both.size() == 5);                 // kompletter Graph

    products.removeById(p1);
    CHECK(refs.search("users", ada).size() == 2);   // ada, o1 — dangling p1 wird uebersprungen
}

void testCrypto() {
    using namespace memdb::detail::crypto;

    const char* abc = "abc";
    auto h = sha256(reinterpret_cast<const std::uint8_t*>(abc), 3);
    std::string hex;
    for (auto b : h) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", b);
        hex += buf;
    }
    CHECK(hex == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    std::uint8_t key20[20];
    std::memset(key20, 0x0b, 20);
    const char* hi = "Hi There";
    auto mac = hmacSha256(key20, 20, reinterpret_cast<const std::uint8_t*>(hi), 8);
    std::string macHex;
    for (auto b : mac) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", b);
        macHex += buf;
    }
    CHECK(macHex == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");

    std::uint8_t ccKey[32];
    for (int i = 0; i < 32; ++i)
        ccKey[i] = static_cast<std::uint8_t>(i);
    std::uint8_t ccNonce[12] = {0, 0, 0, 0, 0, 0, 0, 0x4a, 0, 0, 0, 0};
    const char* pt =
        "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, "
        "sunscreen would be it.";
    std::vector<std::uint8_t> ct(std::strlen(pt));
    chacha20Xor(ct.data(), reinterpret_cast<const std::uint8_t*>(pt), ct.size(), ccKey, 1, ccNonce);
    std::string ctHex;
    for (auto b : ct) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", b);
        ctHex += buf;
    }
    CHECK(ctHex.substr(0, 32) == "6e2e359a2568f98041ba0728dd0d6981");

    std::vector<std::uint8_t> back(ct.size());
    chacha20Xor(back.data(), ct.data(), ct.size(), ccKey, 1, ccNonce);
    CHECK(std::string(back.begin(), back.end()) == pt);

    auto dk = pbkdf2HmacSha256("password", reinterpret_cast<const std::uint8_t*>("salt"), 4, 1, 32);
    std::string dkHex;
    for (auto b : dk) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", b);
        dkHex += buf;
    }
    CHECK(dkHex == "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    auto dk2 = pbkdf2HmacSha256("password", reinterpret_cast<const std::uint8_t*>("salt"), 4, 2, 32);
    std::string dk2Hex;
    for (auto b : dk2) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", b);
        dk2Hex += buf;
    }
    CHECK(dk2Hex == "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    auto dk3 = pbkdf2HmacSha256("password2", reinterpret_cast<const std::uint8_t*>("salt"), 4, 1, 32);
    CHECK(dk != dk3);
}

void testEncryptedSnapshot() {
    Database db("geheim");
    Collection c = db.createCollection("users");
    c.createIndex("name", true);
    c.insert({{"name", "Ada"}, {"geheimnis", "42"}, {"tags", Value(std::vector<Value>{Value("x")})}});
    c.insert({{"name", "Bob"}, {"geheimnis", "München ✓"}});

    const std::string path = "inmemdb_encrypted_test.bin";
    db.saveToFileEncrypted(path, "s3cret-Passwort!");

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    CHECK(f.good());
    CHECK(static_cast<std::size_t>(f.tellg()) > 72);
    f.seekg(0);
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(raw.find("Ada") == std::string::npos);
    CHECK(raw.find("geheimnis") == std::string::npos);

    Database db2 = Database::loadFromFileEncrypted(path, "s3cret-Passwort!");
    CHECK(db2.name() == "geheim");
    CHECK(db2.collection("users").size() == 2);
    CHECK(db2.collection("users").hasIndex("name"));
    CHECK(db2.collection("users").findOne(Field("name") == "Ada")->at("geheimnis").asString() == "42");
    CHECK(db2.toJson() == db.toJson());

    CHECK_THROWS_AS(Database::loadFromFileEncrypted(path, "falsches-passwort"), CryptoError);
    CHECK_THROWS_AS(Database::loadFromFileEncrypted(path, ""), CryptoError);

    {
        std::vector<char> corrupt(raw.begin(), raw.end());
        corrupt[corrupt.size() / 2] ^= 0xFF;
        std::ofstream cf("inmemdb_encrypted_corrupt.bin", std::ios::binary | std::ios::trunc);
        cf.write(corrupt.data(), static_cast<std::streamsize>(corrupt.size()));
        cf.close();
        CHECK_THROWS_AS(Database::loadFromFileEncrypted("inmemdb_encrypted_corrupt.bin", "s3cret-Passwort!"),
                        CryptoError);
        std::remove("inmemdb_encrypted_corrupt.bin");
    }
    {
        std::ofstream mf("inmemdb_encrypted_magic.bin", std::ios::binary | std::ios::trunc);
        std::string bad = "XXXXXXXX" + raw.substr(8);
        mf.write(bad.data(), static_cast<std::streamsize>(bad.size()));
        mf.close();
        CHECK_THROWS_AS(Database::loadFromFileEncrypted("inmemdb_encrypted_magic.bin", "s3cret-Passwort!"),
                        CryptoError);
        std::remove("inmemdb_encrypted_magic.bin");
    }
    CHECK_THROWS_AS(Database::loadFromFileEncrypted("nicht_vorhanden_98765.bin", "x"), Error);

    std::remove(path.c_str());
}

void testSchema() {
    Database db;
    Collection c = db.createCollection("users");

    c.insert({{"name", "ohne-schema-ok"}, {"egal", 1}});
    CHECK(!c.hasSchema());

    Schema schema;
    schema.add("name", Value::Type::String).notNull();
    schema.add("age", Value::Type::Int);
    schema.add("score", Value::Type::Double);
    Schema adresse;
    adresse.add("stadt", Value::Type::String).notNull();
    schema.add("adresse", Value::Type::Document).subSchema(adresse);
    schema.add("tags", Value::Type::Array).elements(Value::Type::String);
    CHECK_THROWS_AS(c.setSchema(schema), SchemaError);

    c.clear();
    c.setSchema(schema);
    CHECK(c.hasSchema());
    CHECK(c.schema()->size() == 5);

    DocumentId ok = c.insert({{"name", "Ada"}, {"age", 36}, {"score", 9},
                              {"adresse", Value(std::make_shared<Document>(std::initializer_list<std::pair<const std::string, Value>>{{"stadt", "Berlin"}}))},
                              {"tags", Value(std::vector<Value>{Value("a"), Value("b")})}});
    CHECK(c.size() == 1);

    CHECK_THROWS_AS(c.insert({{"age", 1}}), SchemaError);
    CHECK_THROWS_AS(c.insert({{"name", "X"}, {"undefiniert", 1}}), SchemaError);
    CHECK_THROWS_AS(c.insert({{"name", 5}}), SchemaError);
    CHECK_THROWS_AS(c.insert({{"name", "X"}, {"age", 3.5}}), SchemaError);
    CHECK_THROWS_AS(c.insert({{"name", "X"}, {"score", "hoch"}}), SchemaError);
    CHECK_THROWS_AS(c.insert({{"name", "X"}, {"tags", Value(std::vector<Value>{Value(1)})}}), SchemaError);
    CHECK_THROWS_AS(c.insert({{"name", "X"}, {"adresse", Value(std::make_shared<Document>(std::initializer_list<std::pair<const std::string, Value>>{{"plz", "1"}}))}}), SchemaError);
    CHECK(c.size() == 1);

    c.insert({{"name", "Bob"}, {"score", 5}});
    CHECK(c.size() == 2);
    CHECK(c.findById(c.findOne(Field("name") == "Bob")->id())->at("score").isInt());

    CHECK_THROWS_AS(c.updateById(ok, [](Document& d) { d.set("age", "alt"); }), SchemaError);
    CHECK(c.findById(ok)->at("age").asInt() == 36);
    CHECK_THROWS_AS(c.upsert(Field("name") == "Bob", Document{{"name", "Bob"}, {"score", true}}), SchemaError);

    std::string json = db.toJson();
    CHECK(json.find("\"schema\"") != std::string::npos);
    Database db2 = Database::fromJson(json, "");
    Collection c2 = db2.collection("users");
    CHECK(c2.hasSchema());
    CHECK(c2.schema()->size() == 5);
    CHECK(c2.size() == 2);
    CHECK_THROWS_AS(c2.insert({{"name", "Neu"}, {"falsch", 1}}), SchemaError);
    CHECK_THROWS_AS(c2.loadJson("[{\"name\":\"X\",\"age\":\"viel\"}]"), SchemaError);

    c.clearSchema();
    CHECK(!c.hasSchema());
    c.insert({{"alles", "erlaubt"}});
    CHECK(c.size() == 3);
}

void testFindIdsForEach() {
    Database db;
    Collection c = db.createCollection("items");
    c.createIndex("age");
    for (int i = 0; i < 1000; ++i)
        c.insert({{"name", "n" + std::to_string(i)}, {"age", i}});

    auto ids = c.findIds(Field("age") >= 990);
    CHECK(ids.size() == 10);
    CHECK(c.lastQueryCandidateCount() == 10);
    for (auto id : ids)
        CHECK(c.findById(id)->at("age").asInt() >= 90);

    std::size_t seen = 0;
    std::int64_t sumAge = 0;
    auto n = c.forEach(Field("age") < 10, [&](const Document& d) {
        ++seen;
        sumAge += d.at("age").asInt();
    });
    CHECK(n == 10);
    CHECK(seen == 10);
    CHECK(sumAge == 45);

    auto allIds = c.findIds();
    CHECK(allIds.size() == 1000);
    std::size_t all = c.forEach(Expression{}, [](const Document&) {});
    CHECK(all == 1000);

    {
        Transaction tx = c.beginTransaction();
        tx.insert({{"name", "tx"}, {"age", 5000}});
        CHECK(tx.findIds(Field("age") == 5000).size() == 1);
        std::size_t cnt = tx.forEach(Field("age") == 5000, [](const Document&) {});
        CHECK(cnt == 1);
        tx.rollback();
    }
    CHECK(c.findIds(Field("age") == 5000).empty());
}

} // namespace

int main() {
    testValue();
    testDocument();
    testDatabase();
    testCollectionCrud();
    testQuery();
    testIndexes();
    testTransactions();
    testSnapshotJson();
    testUpsert();
    testAggregations();
    testObserver();
    testCrypto();
    testEncryptedSnapshot();
    testSchema();
    testFindIdsForEach();
    testReferences();
    testReferenceTraversal();
    testReferenceSearch();

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    if (g_failures == 0)
        std::cout << "ALLE TESTS BESTANDEN\n";
    return g_failures == 0 ? 0 : 1;
}
