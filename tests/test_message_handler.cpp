// Tests: MessageHandler-Singleton (v0.36) – eigene Handler anmelden/abmelden, Level-Filter,
// thread-safe publish, throwende Handler stören nicht; Integration: die Abfang-Stellen der
// Exception-Policy (v0.35) melden dorthin (query-eval, observer, load, regex).
#include <memdb/memdb.hpp>
#include <memdb/message_handler.hpp>
#include <memdb/query_dsl.hpp>

#include <atomic>
#include <iostream>
#include <string>
#include <thread>
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
    auto& mh = MessageHandler::instance();
    mh.resetCounts();
    mh.setMinLevel(MessageLevel::Debug);

    // 1) subscribe/unsubscribe, mehrere Handler, Level + Inhalt der Message
    {
        std::vector<Message> seen1, seen2;
        auto id1 = mh.subscribe([&](const Message& m) { seen1.push_back(m); });
        auto id2 = mh.subscribe([&](const Message& m) { seen2.push_back(m); });
        CHECK(mh.handlerCount() == 2);
        mh.info("test", "hallo");
        mh.error("test", "kaputt");
        CHECK(seen1.size() == 2 && seen2.size() == 2);
        CHECK(seen1[0].level == MessageLevel::Info && seen1[0].source == "test" && seen1[0].text == "hallo");
        CHECK(seen1[1].level == MessageLevel::Error && seen1[1].text == "kaputt");
        CHECK(mh.count(MessageLevel::Info) == 1);
        CHECK(mh.count(MessageLevel::Error) == 1);
        CHECK(mh.totalCount() == 2);
        CHECK(mh.unsubscribe(id1));
        CHECK(!mh.unsubscribe(id1)); // doppelt -> false
        CHECK(mh.handlerCount() == 1);
        mh.info("test", "nur noch einer");
        CHECK(seen1.size() == 2 && seen2.size() == 3);
        mh.unsubscribe(id2);
        CHECK(mh.handlerCount() == 0);
    }

    // 2) Level-Filter: minLevel=Warning -> Info/Debug werden weder zugestellt noch gezaehlt
    {
        mh.resetCounts();
        std::vector<Message> seen;
        auto id = mh.subscribe([&](const Message& m) { seen.push_back(m); });
        mh.setMinLevel(MessageLevel::Warning);
        mh.debug("t", "d"); mh.info("t", "i"); mh.warning("t", "w"); mh.error("t", "e");
        CHECK(seen.size() == 2);
        CHECK(mh.totalCount() == 2);
        CHECK(mh.count(MessageLevel::Info) == 0);
        CHECK(mh.minLevel() == MessageLevel::Warning);
        mh.setMinLevel(MessageLevel::Debug);
        mh.unsubscribe(id);
        mh.resetCounts();
    }

    // 3) throwende Handler-Callback stoert nicht: andere Handler laufen, handlerErrors zaehlt
    {
        mh.resetCounts();
        int good = 0;
        auto idBad = mh.subscribe([](const Message&) { throw std::runtime_error("handler boom"); });
        auto idGood = mh.subscribe([&](const Message&) { ++good; });
        mh.error("t", "x");
        mh.error("t", "y");
        CHECK(good == 2);
        CHECK(mh.handlerErrors() == 2);
        mh.unsubscribe(idBad);
        mh.unsubscribe(idGood);
        mh.resetCounts();
    }

    // 4) thread-safe: 4 Threads x 1000 publishes -> totalCount == 4000
    {
        mh.resetCounts();
        std::atomic<int> received{0};
        auto id = mh.subscribe([&](const Message&) { ++received; });
        std::vector<std::thread> ts;
        for (int t = 0; t < 4; ++t)
            ts.emplace_back([&] {
                for (int i = 0; i < 1000; ++i)
                    mh.info("mt", "x");
            });
        for (auto& t : ts)
            t.join();
        CHECK(mh.totalCount() == 4000);
        CHECK(received.load() == 4000);
        mh.unsubscribe(id);
        mh.resetCounts();
    }

    // 5) Integration: throwender Observer -> Message an Handler (source "observer") + eventErrors
    {
        Database db("mh-obs");
        auto c = db.createCollection("c");
        std::vector<Message> seen;
        auto id = mh.subscribe([&](const Message& m) { seen.push_back(m); });
        mh.resetCounts();
        c.onChange([](const ChangeEvent&) { throw std::runtime_error("obs boom"); });
        c.insert({{"x", 1}});
        CHECK(c.size() == 1); // OP lief weiter
        bool got = false;
        for (auto& m : seen)
            if (m.level == MessageLevel::Error && m.source == "observer" && m.text.find("obs boom") != std::string::npos)
                got = true;
        CHECK(got);
        CHECK(mh.count(MessageLevel::Error) == 1);
        CHECK(c.metrics().eventErrors == 1);
        mh.unsubscribe(id);
        mh.resetCounts();
    }

    // 6) Integration: throwende DSL-Funktion -> source "query-eval" + evalErrors
    {
        Database db("mh-eval");
        auto c = db.createCollection("c");
        c.insert({{"v", 1}});
        c.insert({{"v", 2}});
        QueryFunctionRegistry::instance().registerFunction(
            "mhboom", [](Expression f, const std::vector<Value>&) -> Expression {
                return f.predicate([](const Value&) -> bool { throw std::runtime_error("eval boom"); });
            });
        std::vector<Message> seen;
        auto id = mh.subscribe([&](const Message& m) { seen.push_back(m); });
        mh.resetCounts();
        CHECK(c.count("v.mhboom()") == 0);
        CHECK(c.metrics().evalErrors == 2);
        bool got = false;
        for (auto& m : seen)
            if (m.source == "query-eval" && m.text.find("eval boom") != std::string::npos)
                got = true;
        CHECK(got);
        QueryFunctionRegistry::instance().unregisterFunction("mhboom");
        mh.unsubscribe(id);
        mh.resetCounts();
    }

    // 7) Integration: loadJson mit kaputtem Element -> Message (source "load") UND JsonError bleibt
    {
        Database db("mh-load");
        auto c = db.createCollection("c");
        c.insert({{"a", 1}});
        std::vector<Message> seen;
        auto id = mh.subscribe([&](const Message& m) { seen.push_back(m); });
        mh.resetCounts();
        bool threw = false;
        try {
            c.loadJson(R"([{"b":1}, 7])");
        } catch (const JsonError&) {
            threw = true;
        }
        CHECK(threw);          // wirft weiterhin (Policy)
        CHECK(c.size() == 1);  // unveraendert
        bool got = false;
        for (auto& m : seen)
            if (m.level == MessageLevel::Error && m.source == "load")
                got = true;
        CHECK(got);
        mh.unsubscribe(id);
        mh.resetCounts();
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
