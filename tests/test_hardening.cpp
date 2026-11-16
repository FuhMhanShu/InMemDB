// Tests: Framework-Härtung v0.38 – DSL-Parser (Tiefenlimit bei '(' / '!', strikte Zahlen),
// BSON-Parser (Tiefenlimit), Fuzz-lite DSL. Siehe docs/de/internals/hardening.md für die Übersicht aller Limits.
#include <memdb/memdb.hpp>
#include <memdb/query_dsl.hpp>
#include <memdb/import_bson.hpp>

#include <cstdio>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
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
#define CHECK_NOTHROW(expr)                                                                      \
    do {                                                                                         \
        ++g_checks;                                                                              \
        try {                                                                                    \
            (void)(expr);                                                                        \
        } catch (...) {                                                                          \
            ++g_failures;                                                                        \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #expr " wirft\n";        \
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
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #expr " wirft nicht " #ExType "\n"; \
        }                                                                                        \
    } while (0)

using namespace memdb;

void putI32le(std::string& s, std::int32_t v) {
    for (int i = 0; i < 4; ++i)
        s += static_cast<char>((v >> (8 * i)) & 0xFF);
}

// BSON-Dokument {"a": {"a": ... {leeres Doc} ...}} mit Tiefe `depth`
std::string nestedBson(int depth) {
    std::string cur;
    putI32le(cur, 5);
    cur += '\0';
    for (int i = 0; i < depth; ++i) {
        std::string el;
        el += '\x03';
        el += 'a';
        el += '\0';
        el += cur;
        std::string doc;
        putI32le(doc, 4 + static_cast<std::int32_t>(el.size()) + 1);
        doc += el;
        doc += '\0';
        cur = std::move(doc);
    }
    return cur;
}

} // namespace

int main() {
    // 1) DSL: Tiefenlimit bei Klammern und '!' (vorher Segfault ab ~50000 Klammern)
    {
        std::string ok(200, '('); ok += "x == 1"; ok += std::string(200, ')');
        CHECK_NOTHROW(parseQuery(ok));
        std::string deep(5000, '('); deep += "x == 1"; deep += std::string(5000, ')');
        CHECK_THROWS_AS(parseQuery(deep), QueryError);
        std::string nots(100000, '!'); nots += "x == 1";
        CHECK_THROWS_AS(parseQuery(nots), QueryError);
        // normale Queries weiter ok
        CHECK_NOTHROW(parseQuery("a == 1 && (b == 2 || !(c == 3))"));
    }

    // 2) DSL: Zahlen strikt – vorher wurde z. B. "1.2.3" stillschweigend zu 0
    {
        CHECK_THROWS_AS(parseQuery("x == 1.2.3"), QueryError);
        CHECK_THROWS_AS(parseQuery("x == 1e"), QueryError);
        CHECK_THROWS_AS(parseQuery("x == 0xZZ"), QueryError);
        // int64-Ueberlauf -> double (nicht 0, nicht Throw): 1e23 als 24-stellige Zahl
        auto e = parseQuery("x == 100000000000000000000000");
        (void)e; // parst ohne Throw
        Database db("n");
        auto c = db.createCollection("c");
        c.insert({{"x", 1e23}});
        CHECK(c.count("x == 100000000000000000000000") == 1); // 1e23 als double erkannt
        CHECK(c.count("x == 42") == 0);
        c.insert({{"x", 42}});
        CHECK(c.count("x == 42") == 1);
    }

    // 3) BSON: Tiefenlimit (vorher Segfault ab ~20000 Verschachtelung)
    {
        std::string path = "/tmp/inmemdb_hard_" + std::to_string(static_cast<long>(::getpid())) + ".bson";
        {
            std::ofstream f(path, std::ios::binary);
            auto small = nestedBson(100);
            f.write(small.data(), static_cast<std::streamsize>(small.size()));
        }
        {
            BsonImportSource src(path);
            auto t = src.tables();
            std::size_t rows = 0;
            src.readRows(t[0].name, [&](const std::vector<Value>&) { ++rows; });
            CHECK(rows == 1); // Tiefe 100 ok
        }
        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            auto deep = nestedBson(5000);
            f.write(deep.data(), static_cast<std::streamsize>(deep.size()));
        }
        {
            bool threw = false;
            try {
                BsonImportSource src(path);
                auto t = src.tables();
                src.readRows(t[0].name, [](const std::vector<Value>&) {});
            } catch (const Error& e) {
                threw = std::string(e.what()).find("Verschachtelung zu tief") != std::string::npos;
            }
            CHECK(threw);
        }
        std::remove(path.c_str());
    }

    // 4) Fuzz-lite DSL (deterministisch): nie Crash, nur QueryError oder Erfolg
    {
        std::mt19937 rng(24680);
        const std::string alpha = "abx()!&|=<>.,'\"0123456789eE+-truefalsn@_";
        int ok = 0, invalid = 0;
        for (int i = 0; i < 3000; ++i) {
            int len = 1 + static_cast<int>(rng() % 30);
            std::string s;
            for (int k = 0; k < len; ++k)
                s += alpha[rng() % alpha.size()];
            try {
                (void)parseQuery(s);
                ++ok;
            } catch (const QueryError&) {
                ++invalid;
            } catch (...) {
                CHECK(false); // keine andere Exception erlaubt
            }
        }
        CHECK(ok > 0 && invalid > 0);
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
