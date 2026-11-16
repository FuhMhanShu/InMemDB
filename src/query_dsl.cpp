#include "memdb/query_dsl.hpp"

#include "memdb/database.hpp"
#include "memdb/exceptions.hpp"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>

namespace memdb {

namespace {

std::string toLower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct Tok {
    enum class K { Ident, Str, Num, Sym, End } k;
    std::string s;   ///< Ident-Text / String-Inhalt (unescaped) / Num-Rohtext / Sym-Text
    std::size_t pos = 0;
};

[[noreturn]] void fail(std::string_view dsl, std::size_t pos, const std::string& msg) {
    (void)dsl;
    throw QueryError("DSL an Position " + std::to_string(pos) + ": " + msg);
}

std::vector<Tok> tokenize(std::string_view dsl) {
    std::vector<Tok> out;
    std::size_t i = 0;
    const std::size_t n = dsl.size();
    while (i < n) {
        char c = dsl[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        std::size_t start = i;
        switch (c) {
            case '=':
                if (i + 1 < n && dsl[i + 1] == '=') { out.push_back({Tok::K::Sym, "==", start}); i += 2; }
                else { out.push_back({Tok::K::Sym, "=", start}); ++i; }
                continue;
            case '!':
                if (i + 1 < n && dsl[i + 1] == '=') { out.push_back({Tok::K::Sym, "!=", start}); i += 2; }
                else { out.push_back({Tok::K::Sym, "!", start}); ++i; }
                continue;
            case '<':
                if (i + 1 < n && dsl[i + 1] == '=') { out.push_back({Tok::K::Sym, "<=", start}); i += 2; }
                else { out.push_back({Tok::K::Sym, "<", start}); ++i; }
                continue;
            case '>':
                if (i + 1 < n && dsl[i + 1] == '=') { out.push_back({Tok::K::Sym, ">=", start}); i += 2; }
                else { out.push_back({Tok::K::Sym, ">", start}); ++i; }
                continue;
            case '&':
                if (i + 1 < n && dsl[i + 1] == '&') { out.push_back({Tok::K::Sym, "&&", start}); i += 2; }
                else fail(dsl, i, "'&' erwartet ('&&')");
                continue;
            case '|':
                if (i + 1 < n && dsl[i + 1] == '|') { out.push_back({Tok::K::Sym, "||", start}); i += 2; }
                else fail(dsl, i, "'|' erwartet ('||')");
                continue;
            case '(': case ')': case ',': case '.': case '@':
            case '+': case '-': case '*': case '/': case '%':
                out.push_back(Tok{Tok::K::Sym, std::string(1, c), start});
                ++i;
                continue;
            case '\'': case '"': {
                char q = c;
                ++i;
                std::string s;
                while (i < n && dsl[i] != q) {
                    if (dsl[i] == '\\' && i + 1 < n) {
                        char e = dsl[i + 1];
                        switch (e) {
                            case 'n': s += '\n'; break;
                            case 'r': s += '\r'; break;
                            case 't': s += '\t'; break;
                            case '0': s += '\0'; break;
                            default: s += e; break; // \\ \' \" ...
                        }
                        i += 2;
                    } else {
                        s += dsl[i++];
                    }
                }
                if (i >= n)
                    fail(dsl, start, "String nicht geschlossen");
                ++i; // schliessendes Quote
                out.push_back(Tok{Tok::K::Str, std::move(s), start});
                continue;
            }
            default:
                break;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(dsl[i + 1])))) {
            std::size_t s0 = i;
            if (i + 1 < n && dsl[i] == '0' && (dsl[i + 1] == 'x' || dsl[i + 1] == 'X')) {
                i += 2;
                while (i < n && std::isxdigit(static_cast<unsigned char>(dsl[i])))
                    ++i;
            } else {
                while (i < n && (std::isdigit(static_cast<unsigned char>(dsl[i])) || dsl[i] == '.'))
                    ++i;
                if (i < n && (dsl[i] == 'e' || dsl[i] == 'E')) {
                    ++i;
                    if (i < n && (dsl[i] == '-' || dsl[i] == '+'))
                        ++i;
                    while (i < n && std::isdigit(static_cast<unsigned char>(dsl[i])))
                        ++i;
                }
            }
            out.push_back(Tok{Tok::K::Num, std::string(dsl.substr(s0, i - s0)), start});
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t s0 = i;
            while (i < n && (std::isalnum(static_cast<unsigned char>(dsl[i])) || dsl[i] == '_'))
                ++i;
            out.push_back(Tok{Tok::K::Ident, std::string(dsl.substr(s0, i - s0)), s0});
            continue;
        }
        fail(dsl, i, std::string("unerwartetes Zeichen '") + c + "'");
    }
    out.push_back(Tok{Tok::K::End, "", n});
    return out;
}

Value numTokenToValue(const std::string& s) {
    bool isDouble = s.find('.') != std::string::npos || s.find('e') != std::string::npos ||
                    s.find('E') != std::string::npos;
    if (s.size() > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        std::int64_t v = 0;
        std::from_chars(s.data() + 2, s.data() + s.size(), v, 16);
        return Value(v);
    }
    if (isDouble) {
        double d = 0.0;
        std::from_chars(s.data(), s.data() + s.size(), d);
        return Value(d);
    }
    std::int64_t v = 0;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return Value(v);
}

struct Parser {
    std::string_view dsl;
    std::vector<Tok> toks;
    std::size_t i = 0;
    Database* db = nullptr;
    std::string collection;

    explicit Parser(std::string_view d, Database* dbr, std::string_view coll)
        : dsl(d), toks(tokenize(d)), db(dbr), collection(coll) {}

    const Tok& peek(std::size_t off = 0) const {
        std::size_t k = i + off;
        return toks[k < toks.size() ? k : toks.size() - 1];
    }
    const Tok& next() { return toks[i < toks.size() ? i++ : i]; }

    bool peekSym(const char* s) const { return peek().k == Tok::K::Sym && peek().s == s; }
    bool peekIdentWord(const char* w) const {
        return peek().k == Tok::K::Ident && toLower(peek().s) == w;
    }
    void expectSym(const char* s) {
        if (!peekSym(s))
            fail(dsl, peek().pos, std::string("'") + s + "' erwartet, gefunden '" +
                                      (peek().k == Tok::K::End ? std::string("<ende>") : peek().s) + "'");
        next();
    }
    std::string expectIdent() {
        if (peek().k != Tok::K::Ident)
            fail(dsl, peek().pos, "Bezeichner erwartet");
        std::string s = next().s;
        return s;
    }

    Value parseValue() {
        bool neg = false;
        if (peekSym("-")) {
            neg = true;
            next();
        }
        const Tok& t = peek();
        if (t.k == Tok::K::Str) {
            if (neg)
                fail(dsl, t.pos, "unariaeres '-' vor String nicht erlaubt");
            next();
            return Value(t.s);
        }
        if (t.k == Tok::K::Num) {
            next();
            Value v = numTokenToValue(t.s);
            if (neg)
                v = v.isInt() ? Value(-v.asInt()) : Value(-v.asDouble());
            return v;
        }
        if (t.k == Tok::K::Ident) {
            std::string w = toLower(t.s);
            if (neg)
                fail(dsl, t.pos, "unariaeres '-' vor diesem Wert nicht erlaubt");
            if (w == "true") { next(); return Value(true); }
            if (w == "false") { next(); return Value(false); }
            if (w == "null") { next(); return Value(nullptr); }
            fail(dsl, t.pos, "Wert erwartet (Zahl, String, true, false, null)");
        }
        fail(dsl, t.pos, "Wert erwartet");
    }

    // ident ('.' ident)* – stoppt vor '.' func '(' (Function-Call folgt)
    std::string parsePath() {
        std::string path = expectIdent();
        while (peekSym(".") && !(peek(1).k == Tok::K::Ident && peek(2).k == Tok::K::Sym &&
                                 peek(2).s == "(")) {
            next(); // '.'
            path += "." + expectIdent();
        }
        return path;
    }

    Expression parse() {
        if (peek().k == Tok::K::End)
            return Expression{};
        Expression e = parseOr();
        if (peek().k != Tok::K::End)
            fail(dsl, peek().pos, "unerwartetes Token '" + peek().s + "'");
        return e;
    }

    Expression parseOr() {
        Expression left = parseAnd();
        while (peekSym("||") || peekIdentWord("or")) {
            next();
            left = left || parseAnd();
        }
        return left;
    }

    Expression parseAnd() {
        Expression left = parseUnary();
        while (peekSym("&&") || peekIdentWord("and")) {
            next();
            left = left && parseUnary();
        }
        return left;
    }

    Expression parseUnary() {
        if (peekSym("!") || peekIdentWord("not")) {
            next();
            return !parseUnary();
        }
        return parsePrimary();
    }

    Expression parsePrimary() {
        if (peekSym("(")) {
            next();
            Expression e = parseOr();
            expectSym(")");
            return e;
        }
        return parsePredicate();
    }

    Expression applyCompare(Expression target, const std::string& op, Value v) {
        if (op == "==" || op == "=")
            return target == std::move(v);
        if (op == "!=")
            return target != std::move(v);
        if (op == "<")
            return target < std::move(v);
        if (op == "<=")
            return target <= std::move(v);
        if (op == ">")
            return target > std::move(v);
        return target >= std::move(v); // ">="
    }

    Expression parsePredicate() {
        Expression target;
        std::string path;
        if (peekSym("@")) {
            next();
            std::string rel = expectIdent();
            expectSym(".");
            path = parsePath();
            if (!db || collection.empty())
                fail(dsl, peek().pos,
                     "@" + rel + "... erfordert parseQuery(dsl, db, collection) bzw. "
                     "Collection::find(dsl, db)");
            target = refField(*db, collection, rel, path);
        } else {
            path = parsePath();
            target = Field(path);
        }

        // Vergleich?
        if (peek().k == Tok::K::Sym &&
            (peek().s == "==" || peek().s == "=" || peek().s == "!=" || peek().s == "<" ||
             peek().s == "<=" || peek().s == ">" || peek().s == ">=")) {
            std::string op = next().s;
            Value v = parseValue();
            return applyCompare(std::move(target), op, std::move(v));
        }
        // Function-Call: '.' name '(' args ')'
        if (peekSym(".")) {
            next();
            std::string fn = expectIdent();
            expectSym("(");
            std::vector<Value> args;
            if (!peekSym(")")) {
                args.push_back(parseValue());
                while (peekSym(",")) {
                    next();
                    args.push_back(parseValue());
                }
            }
            expectSym(")");
            return QueryFunctionRegistry::instance().call(fn, std::move(target), std::move(args));
        }
        // nackter Pfad = exists()
        return target.exists();
    }
};

// --- Set-DSL fuer Bulk-Update (v0.21) ---

Value applySetOp(const std::string& op, const Value& a, const Value& b) {
    if (op == "+" && a.isString() && b.isString())
        return Value(a.asString() + b.asString());
    if (a.isNumber() && b.isNumber()) {
        bool ai = a.isInt(), bi = b.isInt();
        if (op == "%") {
            if (!ai || !bi)
                throw Error("update-set: '%' nur fuer int-Werte");
            std::int64_t y = b.asInt();
            if (y == 0)
                throw Error("update-set: Modulo durch 0");
            return Value(a.asInt() % y);
        }
        if (op == "/") {
            double y = b.asDouble();
            if (y == 0.0)
                throw Error("update-set: Division durch 0");
            return Value(a.asDouble() / y);
        }
        if (ai && bi) {
            std::int64_t x = a.asInt(), y = b.asInt();
            constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
            constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
            if (op == "+") {
                if ((y > 0 && x > kMax - y) || (y < 0 && x < kMin - y))
                    return Value(static_cast<double>(x) + static_cast<double>(y));
                return Value(x + y);
            }
            if (op == "-") {
                if ((y < 0 && x > kMax + y) || (y > 0 && x < kMin + y))
                    return Value(static_cast<double>(x) - static_cast<double>(y));
                return Value(x - y);
            }
            // '*'
            if (x != 0 && (y > kMax / x || y < kMin / x))
                return Value(static_cast<double>(x) * static_cast<double>(y));
            return Value(x * y);
        }
        double x = a.asDouble(), y = b.asDouble();
        if (op == "+")
            return Value(x + y);
        if (op == "-")
            return Value(x - y);
        return Value(x * y); // '*'
    }
    throw Error("update-set: Operator '" + op + "' nicht auf diese Werttypen anwendbar");
}

struct SetParser {
    std::string_view dsl;
    std::vector<Tok> toks;
    std::size_t i = 0;

    explicit SetParser(std::string_view d) : dsl(d), toks(tokenize(d)) {}

    const Tok& peek(std::size_t off = 0) const {
        std::size_t k = i + off;
        return toks[k < toks.size() ? k : toks.size() - 1];
    }
    const Tok& next() { return toks[i < toks.size() ? i++ : i]; }
    bool peekSym(const char* s) const { return peek().k == Tok::K::Sym && peek().s == s; }
    void expectSym(const char* s) {
        if (!peekSym(s))
            fail(dsl, peek().pos, std::string("'") + s + "' erwartet, gefunden '" +
                                      (peek().k == Tok::K::End ? std::string("<ende>") : peek().s) + "'");
        next();
    }
    std::string expectIdent() {
        if (peek().k != Tok::K::Ident)
            fail(dsl, peek().pos, "Bezeichner erwartet");
        return next().s;
    }
    std::string parsePath() {
        std::string path = expectIdent();
        while (peekSym(".") && peek(1).k == Tok::K::Ident) {
            next();
            path += "." + expectIdent();
        }
        return path;
    }

    SetExpression parseVExpr() {
        SetExpression left = parseMul();
        while (peekSym("+") || peekSym("-")) {
            std::string op = next().s;
            SetExpression right = parseMul();
            left = [left = std::move(left), right = std::move(right), op](const Document& d) {
                return applySetOp(op, left(d), right(d));
            };
        }
        return left;
    }
    SetExpression parseMul() {
        SetExpression left = parseUnary();
        while (peekSym("*") || peekSym("/") || peekSym("%")) {
            std::string op = next().s;
            SetExpression right = parseUnary();
            left = [left = std::move(left), right = std::move(right), op](const Document& d) {
                return applySetOp(op, left(d), right(d));
            };
        }
        return left;
    }
    SetExpression parseUnary() {
        if (peekSym("-")) {
            next();
            SetExpression e = parseUnary();
            return [e = std::move(e)](const Document& d) {
                Value v = e(d);
                if (!v.isNumber())
                    throw Error("update-set: unariaeres '-' nur auf Zahlen anwendbar");
                return v.isInt() ? Value(-v.asInt()) : Value(-v.asDouble());
            };
        }
        return parsePrimary();
    }
    SetExpression parsePrimary() {
        if (peekSym("(")) {
            next();
            SetExpression e = parseVExpr();
            expectSym(")");
            return e;
        }
        const Tok& t = peek();
        if (t.k == Tok::K::Str) {
            next();
            std::string s = t.s;
            return [s = std::move(s)](const Document&) { return Value(s); };
        }
        if (t.k == Tok::K::Num) {
            next();
            Value v = numTokenToValue(t.s);
            return [v = std::move(v)](const Document&) { return v; };
        }
        if (t.k == Tok::K::Ident) {
            std::string w = toLower(t.s);
            if (w == "true") { next(); return [](const Document&) { return Value(true); }; }
            if (w == "false") { next(); return [](const Document&) { return Value(false); }; }
            if (w == "null") { next(); return [](const Document&) { return Value(nullptr); }; }
            std::string path = parsePath();
            return [path = std::move(path)](const Document& d) {
                const Value* v = d.findPath(path);
                return v ? *v : Value(nullptr);
            };
        }
        fail(dsl, t.pos, "Wertausdruck erwartet (Zahl, String, true/false/null, Feldpfad, '(...)')");
    }

    std::vector<std::pair<std::string, SetExpression>> parse() {
        std::vector<std::pair<std::string, SetExpression>> out;
        if (peek().k == Tok::K::End)
            fail(dsl, 0, "mindestens eine Zuweisung 'pfad = ausdruck' erwartet");
        while (true) {
            std::string path = parsePath();
            expectSym("=");
            out.emplace_back(std::move(path), parseVExpr());
            if (peekSym(",")) {
                next();
                continue;
            }
            break;
        }
        if (peek().k != Tok::K::End)
            fail(dsl, peek().pos, "unerwartetes Token '" + peek().s + "'");
        return out;
    }
};

} // namespace

namespace {
// Cache fuer geparste DSL-Queries (ohne DB-Kontext): parseQuery(dsl) in Schleifen
// parst sonst jedes Mal neu. Begrenzt (clear bei voll), thread-safe; Registry-Version
// im Key, damit (de)registrierte eigene Operatoren gecachte Queries nicht ueberleben.
std::mutex g_parseCacheMtx;
std::unordered_map<std::string, Expression> g_parseCache;
constexpr std::size_t kParseCacheMax = 128;
} // namespace

Expression parseQuery(std::string_view dsl) {
    const std::string key = std::string(dsl) + '\x01' + std::to_string(QueryFunctionRegistry::instance().version());
    {
        std::lock_guard<std::mutex> lock(g_parseCacheMtx);
        if (auto it = g_parseCache.find(key); it != g_parseCache.end())
            return it->second;
    }
    Parser p(dsl, nullptr, {});
    Expression parsed = p.parse();
    {
        std::lock_guard<std::mutex> lock(g_parseCacheMtx);
        if (g_parseCache.size() >= kParseCacheMax)
            g_parseCache.clear();
        g_parseCache.emplace(key, parsed);
    }
    return parsed;
}

Expression parseQuery(std::string_view dsl, Database* db, std::string_view collection) {
    Parser p(dsl, db, collection);
    return p.parse();
}

std::vector<std::pair<std::string, SetExpression>> parseUpdateSet(std::string_view setDsl) {
    SetParser p(setDsl);
    return p.parse();
}

QueryFunctionRegistry& QueryFunctionRegistry::instance() {
    static QueryFunctionRegistry reg;
    return reg;
}

QueryFunctionRegistry::QueryFunctionRegistry() {
    registerFunction("contains", [](Expression f, const std::vector<Value>& args) {
        if (args.size() != 1)
            throw QueryError("contains() erwartet genau 1 Argument");
        return f.contains(args[0]);
    });
    registerFunction("exists", [](Expression f, const std::vector<Value>& args) {
        if (!args.empty())
            throw QueryError("exists() erwartet keine Argumente");
        return f.exists();
    });
    registerFunction("matches", [](Expression f, const std::vector<Value>& args) {
        if (args.size() != 1)
            throw QueryError("matches() erwartet genau 1 Argument (Pattern-String)");
        return f.matches(args[0]);
    });
    registerFunction("in", [](Expression f, const std::vector<Value>& args) {
        if (args.empty())
            throw QueryError("in() erwartet mindestens 1 Argument");
        return f.in(args);
    });
}

void QueryFunctionRegistry::registerFunction(std::string name, QueryFunction fn) {
    name = toLower(std::move(name));
    if (name.empty() || !fn)
        throw QueryError("QueryFunctionRegistry::registerFunction: Name/Funktion leer");
    std::lock_guard<std::mutex> lk(mutex_);
    fns_[name] = std::move(fn);
    ++version_;
}

void QueryFunctionRegistry::unregisterFunction(const std::string& name) {
    std::lock_guard<std::mutex> lk(mutex_);
    fns_.erase(toLower(name));
    ++version_;
}

std::uint64_t QueryFunctionRegistry::version() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return version_;
}

bool QueryFunctionRegistry::hasFunction(const std::string& name) const {
    std::lock_guard<std::mutex> lk(mutex_);
    return fns_.count(toLower(name)) != 0;
}

std::vector<std::string> QueryFunctionRegistry::functionNames() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<std::string> out;
    for (const auto& [n, f] : fns_)
        out.push_back(n);
    return out;
}

Expression QueryFunctionRegistry::call(const std::string& name, Expression fieldExpr,
                                       const std::vector<Value>& args) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = fns_.find(toLower(name));
    if (it == fns_.end())
        throw QueryError("DSL: unbekannter Function-Operator '" + name +
                         "' (registriert: " +
                         [&] {
                             std::string s;
                             for (const auto& [n, f] : fns_) {
                                 if (!s.empty())
                                     s += ", ";
                                 s += n;
                             }
                             return s;
                         }() +
                         ")");
    return it->second(std::move(fieldExpr), args);
}

} // namespace memdb
