// Tests: Mini-RegEx (src/regex.cpp, v0.20): Basis-Features, {n,m}, \d\w\s-Klassen,
// (?:...)-Gruppen, Lazy-Quantoren, kein Hang bei verschachtelten Schleifen, Fehlerfaelle.
#include "../src/regex.hpp"

#include <chrono>
#include <iostream>
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
#define CHECK_MATCH(pat, text) CHECK(memdb::detail::regexMatch(pat, text))
#define CHECK_NOMATCH(pat, text) CHECK(!memdb::detail::regexMatch(pat, text))
#define CHECK_INVALID(pat)                                                                       \
    do {                                                                                         \
        ++g_checks;                                                                              \
        bool caught = false;                                                                     \
        try {                                                                                    \
            (void)memdb::detail::regexMatch(pat, "x");                                           \
        } catch (const std::invalid_argument&) {                                                 \
            caught = true;                                                                       \
        } catch (...) {                                                                          \
        }                                                                                        \
        if (!caught) {                                                                           \
            ++g_failures;                                                                        \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": Pattern " #pat              \
                         " wirft nicht\n";                                                       \
        }                                                                                        \
    } while (0)

using memdb::detail::regexMatch;

} // namespace

int main() {
    // --- Basis (Full-Match-Semantik) ---
    CHECK_MATCH("", "");
    CHECK_NOMATCH("", "a");
    CHECK_MATCH("abc", "abc");
    CHECK_NOMATCH("abc", "abcd");
    CHECK_NOMATCH("abc", "xbc");
    CHECK_MATCH("a.c", "abc");
    CHECK_MATCH("a.c", "a\nc"); // '.' matcht auch Newline
    CHECK_MATCH("a*", "");
    CHECK_MATCH("a*", "aaaa");
    CHECK_NOMATCH("a*", "aaab");
    CHECK_MATCH("a+", "a");
    CHECK_NOMATCH("a+", "");
    CHECK_MATCH("ab?c", "ac");
    CHECK_MATCH("ab?c", "abc");
    CHECK_NOMATCH("ab?c", "abbc");
    CHECK_MATCH("a|b", "a");
    CHECK_MATCH("a|b", "b");
    CHECK_NOMATCH("a|b", "c");
    CHECK_MATCH("^(Ada|Bob)$", "Ada");
    CHECK_MATCH("^[A-Z][a-z]+$", "Ada");
    CHECK_NOMATCH("^[A-Z][a-z]+$", "O'Brien");
    CHECK_MATCH("a(b|c)d", "abd");
    CHECK_MATCH("a(b|c)d", "acd");
    CHECK_NOMATCH("a(b|c)d", "ad");

    // --- Klassen ---
    CHECK_MATCH("[abc]", "b");
    CHECK_NOMATCH("[abc]", "d");
    CHECK_MATCH("[a-z]+", "hello");
    CHECK_NOMATCH("[a-z]+", "Hello");
    CHECK_MATCH("[^a-z]+", "ABC123");
    CHECK_NOMATCH("[^a-z]+", "abc");
    CHECK_MATCH("[]a]", "]"); // ']' als erstes Zeichen literal
    CHECK_MATCH("[-a]", "-");
    CHECK_MATCH("[a-]", "-");
    CHECK_MATCH("[\\]]", "]");
    CHECK_MATCH("[a\\-z]", "-");

    // --- Kurzschriften (v0.20) ---
    CHECK_MATCH("\\d+", "12345");
    CHECK_NOMATCH("\\d+", "12a45");
    CHECK_MATCH("\\D+", "abc");
    CHECK_NOMATCH("\\D+", "ab1");
    CHECK_MATCH("\\w+", "Hallo_Welt99");
    CHECK_NOMATCH("\\w+", "Hallo Welt");
    CHECK_MATCH("\\W+", " !?");
    CHECK_NOMATCH("\\W+", "a!");
    CHECK_MATCH("\\s+", " \t\n");
    CHECK_NOMATCH("\\s+", " a");
    CHECK_MATCH("\\S+", "abc");
    CHECK_MATCH("^\\d{3}-\\d{4}$", "123-4567"); // Tel.-Nr.
    CHECK_NOMATCH("^\\d{3}-\\d{4}$", "12-4567");
    CHECK_MATCH("[\\d]+", "42");
    CHECK_MATCH("[^\\d]+", "abc");
    CHECK_NOMATCH("[^\\d]+", "a1");
    CHECK_MATCH("[\\w.]+", "vor.name_1");
    CHECK_MATCH("^[\\w.]+@[\\w.]+$", "a.b@c.de");
    CHECK_NOMATCH("^[\\w.]+@[\\w.]+$", "a b@c.de");

    // --- {n,m} (v0.20) ---
    CHECK_MATCH("a{3}", "aaa");
    CHECK_NOMATCH("a{3}", "aa");
    CHECK_NOMATCH("a{3}", "aaaa");
    CHECK_MATCH("a{2,}", "aa");
    CHECK_MATCH("a{2,}", "aaaaa");
    CHECK_NOMATCH("a{2,}", "a");
    CHECK_MATCH("a{2,4}", "aa");
    CHECK_MATCH("a{2,4}", "aaaa");
    CHECK_NOMATCH("a{2,4}", "a");
    CHECK_NOMATCH("a{2,4}", "aaaaa");
    CHECK_MATCH("^\\d{2,4}\\.\\d{2,4}$", "12.34"); // IPv4-Oktette kurz
    CHECK_MATCH("(ab){2}", "abab");
    CHECK_NOMATCH("(ab){2}", "ab");
    CHECK_MATCH("a{0}b", "b");
    CHECK_MATCH("x{0,2}y", "y");
    CHECK_MATCH("x{0,2}y", "xxy");
    CHECK_MATCH("{2}", "{2}");   // kein gueltiges {n}: '{' literal
    CHECK_MATCH("a{", "a{");     // '{' literal
    CHECK_MATCH("a{x}", "a{x}");
    CHECK_INVALID("a{2,1}");
    CHECK_INVALID("a{1001}");    // Limit
    CHECK_INVALID("a{0,1001}");

    // --- (?:...) und Lazy-Quantoren (v0.20) ---
    CHECK_MATCH("(?:ab)+", "ababab");
    CHECK_NOMATCH("(?:ab)+", "ababa");
    CHECK_MATCH("^(?:Ada|Bob)$", "Bob");
    CHECK_MATCH("a*?", "aaa");   // lazy == greedy bei Full-Match (boolesche Semantik)
    CHECK_MATCH("a+?b", "aab");
    CHECK_MATCH("a??b", "b");
    CHECK_MATCH("a{1,3}?", "aa");
    CHECK_INVALID("(?=a)b");     // Lookahead nicht unterstuetzt

    // --- Verschachtelte Schleifen: frueher HANG, jetzt korrekt (v0.20 Fix) ---
    CHECK_MATCH("(a*)*", "aaa");
    CHECK_MATCH("(a*)*", "");
    CHECK_NOMATCH("(a*)*", "aab");
    CHECK_MATCH("(a*)+", "aa");
    CHECK_MATCH("()*", "");
    CHECK_MATCH("(a|b)*abb", "ababb");
    CHECK_MATCH("(a?)*", "aa");
    CHECK_MATCH("((a)*)*", "aaa");
    CHECK_NOMATCH("(a+)+b", "aaac");
    CHECK_MATCH("(a+)+b", "aaab");

    // --- Escapes ---
    CHECK_MATCH("a\\.b", "a.b");
    CHECK_NOMATCH("a\\.b", "axb");
    CHECK_MATCH("\\*\\+\\?", "*+?");
    CHECK_MATCH("a\\nb", "a\nb");
    CHECK_MATCH("\\\\", "\\");
    CHECK_MATCH("\\(x\\)", "(x)");

    // --- Anker ---
    CHECK_MATCH("^abc$", "abc");
    CHECK_NOMATCH("^abc$", "xabc");
    CHECK_NOMATCH("^abc$", "abcx");
    CHECK_MATCH("^$", "");
    CHECK_NOMATCH("^$", "a");

    // --- Fehlerfaelle mit Position ---
    CHECK_INVALID("(");
    CHECK_INVALID(")");
    CHECK_INVALID("a)");
    CHECK_INVALID("[abc");
    CHECK_INVALID("[z-a]");
    CHECK_INVALID("*a");
    CHECK_INVALID("a\\"); // Flucht am Ende
    CHECK_INVALID("a{2,1}");

    // Fehlermeldung enthaelt Position
    try {
        (void)regexMatch("ab[", "x");
        CHECK(false);
    } catch (const std::invalid_argument& e) {
        CHECK(std::string(e.what()).find("Position 3") != std::string::npos);
    }

    // --- Performance-Schutz: kein exponentielles Backtracking ---
    // (a+)+$ gegen langes "aaaa...b" darf nicht haengen (NFA-Simulation ist linear)
    {
        std::string longA(5000, 'a');
        longA += 'c'; // endet nicht auf b -> kein Match, darf aber nicht haengen
        auto t0 = std::chrono::steady_clock::now();
        CHECK_NOMATCH("(a+)+b$", longA); // NFA: O(n*m), schnell
        auto t1 = std::chrono::steady_clock::now();
        CHECK(std::chrono::duration<double>(t1 - t0).count() < 2.0);
    }

    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
