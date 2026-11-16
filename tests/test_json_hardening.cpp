// Tests: JSON-Parser/Writer-Härtung v0.37 – Verschachtelungs-Limit (Parser+Writer, vorher
// Segfault bei "[[[[..." bzw. zyklischen Value-Bäumen), $binary-Landmine (nur gueltiges Hex
// wird zu Binary, sonst normales Dokument), plus Semantik-Regressionen und Fuzz-lite.
#include "../src/json.hpp"
#include <memdb/exceptions.hpp>

#include <iostream>
#include <random>
#include <stdexcept>
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
#define CHECK_JSON_INVALID(s)                                                                    \
    do {                                                                                         \
        ++g_checks;                                                                              \
        bool caught = false;                                                                     \
        try {                                                                                    \
            (void)memdb::detail::parseJson(s);                                                   \
        } catch (const memdb::JsonError&) {                                                      \
            caught = true;                                                                       \
        } catch (...) {                                                                          \
        }                                                                                        \
        if (!caught) {                                                                           \
            ++g_failures;                                                                        \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #s " wirft nicht\n";     \
        }                                                                                        \
    } while (0)

using namespace memdb;

} // namespace

int main() {
    using detail::parseJson;
    using detail::writeJson;

    // 1) Semantik-Regression: Basics + Round-Trip
    {
        auto v = parseJson(R"({"a":1,"b":[1,2.5,"x",true,null],"c":{"d":"e"}, "u":"Aé😀"})");
        CHECK(v.isDocument());
        const Document& d = *v.asDocument();
        CHECK(d.find("a")->asInt() == 1);
        CHECK(d.find("b")->asArray()[1].asDouble() == 2.5);
        CHECK(d.find("c")->asDocument()->find("d")->asString() == "e");
        CHECK(d.find("u")->asString() == "A\xC3\xA9\xF0\x9F\x98\x80"); // é + 😀 via Surrogat-Paar
        std::string out = writeJson(v);
        auto v2 = parseJson(out);
        CHECK(writeJson(v2) == out); // Round-Trip stabil
    }

    // 2) Zahlen: locale-unabhaengig, Int vs Double, ungueltige werfen
    {
        CHECK(parseJson("42").asInt() == 42);
        CHECK(parseJson("-7").asInt() == -7);
        CHECK(parseJson("2.5").asDouble() == 2.5);
        CHECK(parseJson("1e3").asDouble() == 1000.0);
        CHECK(parseJson("9223372036854775807").asInt() == 9223372036854775807LL);
        CHECK_JSON_INVALID("1.2.3");
        CHECK_JSON_INVALID("--5");
        CHECK_JSON_INVALID("1e");
        CHECK_JSON_INVALID("-");
        CHECK_JSON_INVALID("abc");
        CHECK_JSON_INVALID("[1,]");   // trailing comma nicht erlaubt
        CHECK_JSON_INVALID("{\"a\":1,}");
        CHECK_JSON_INVALID("{\"a\" 1}"); // fehlender Doppelpunkt
        CHECK_JSON_INVALID("[1 2]");
        CHECK_JSON_INVALID("\"abc");   // fehlendes schliessendes Quote
        CHECK_JSON_INVALID("\"a\\x\""); // ungueltiges Escape
        CHECK_JSON_INVALID("\"\\u12\""); // unvollstaendiges \u
        CHECK_JSON_INVALID("\"\\uD800\""); // High-Surrogate ohne Low
        CHECK_JSON_INVALID("{\"a\":1} xyz"); // Daten nach dem Wert
        CHECK(parseJson("  [ ]  ").isArray());
        CHECK(parseJson("{}").isDocument());
        CHECK(parseJson("null").isNull());
        CHECK(parseJson("true").asBool());
        // Duplikat-Keys: letzter gewinnt (dokumentiert)
        CHECK(parseJson(R"({"a":1,"a":2})").asDocument()->find("a")->asInt() == 2);
    }

    // 3) v0.37: Verschachtelung – Parser wirft statt Segfault
    {
        std::string ok(999, '['); ok += "1"; ok += std::string(999, ']');
        CHECK(parseJson(ok).isArray()); // 999 < 1000 ok
        std::string deep(5000, '['); deep += "1"; deep += std::string(5000, ']');
        CHECK_JSON_INVALID(deep); // "Verschachtelung zu tief"
        std::string objDeep;
        for (int i = 0; i < 5000; ++i) objDeep += "{\"a\":";
        objDeep += "1";
        for (int i = 0; i < 5000; ++i) objDeep += "}";
        CHECK_JSON_INVALID(objDeep);
    }

    // 4) v0.37: $binary – nur gueltiges Hex wird zu Binary, sonst normales Dokument
    {
        auto bin = parseJson(R"({"$binary":"00ff"})");
        CHECK(bin.isBinary());
        CHECK(bin.asBinary().size() == 2 && bin.asBinary()[1] == 0xFF);
        auto doc1 = parseJson(R"({"$binary":"hello"})"); // ungerade -> Dokument, kein Throw
        CHECK(doc1.isDocument());
        CHECK(doc1.asDocument()->find("$binary")->asString() == "hello");
        auto doc2 = parseJson(R"({"$binary":"zz"})"); // ungueltige Hex-Zeichen -> Dokument
        CHECK(doc2.isDocument());
        auto doc3 = parseJson(R"({"$binary":"00ff","x":1})"); // mehr als ein Feld -> Dokument
        CHECK(doc3.isDocument());
        // Round-Trip Binary
        Value b(std::vector<std::uint8_t>{0xDE, 0xAD});
        auto rt = parseJson(writeJson(b));
        CHECK(rt.isBinary() && rt.asBinary()[0] == 0xDE);
    }

    // 5) v0.37: Writer wirft bei zu tiefer/zyklischer Struktur statt Segfault
    {
        Value inner = Value(std::make_shared<Document>());
        Value cur = inner;
        for (int i = 0; i < 5000; ++i) {
            auto d = std::make_shared<Document>();
            d->set("a", cur);
            cur = Value(d);
        }
        bool threw = false;
        try {
            (void)writeJson(cur);
        } catch (const JsonError&) {
            threw = true;
        }
        CHECK(threw);
        // echter Zyklus
        Value cyc = Value(std::make_shared<Document>());
        cyc.asDocument()->set("self", cyc);
        threw = false;
        try {
            (void)writeJson(cyc);
        } catch (const JsonError&) {
            threw = true;
        }
        CHECK(threw);
        cyc.asDocument()->remove("self"); // Zyklus aufbrechen, sonst Leak (shared_ptr-Zyklus)
    }

    // 6) Fuzz-lite (deterministisch): Zufalls-Strings aus JSON-Alphabet duerfen nie crashen,
    //    nur JsonError werfen; gueltige Fragmente parsen konsistent
    {
        std::mt19937 rng(987654);
        const std::string alpha = "{}[]\",:0123456789.eE+-truefalsn\\u \t\n$binaryabz";
        int ok = 0, invalid = 0;
        for (int i = 0; i < 4000; ++i) {
            int len = 1 + static_cast<int>(rng() % 40);
            std::string s;
            for (int k = 0; k < len; ++k)
                s += alpha[rng() % alpha.size()];
            try {
                Value v = parseJson(s);
                (void)writeJson(v); // Writer darf auf geparstem Wert nie crashen
                ++ok;
            } catch (const JsonError&) {
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
