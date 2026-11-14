#include "memdb/query_dsl.hpp"

#include "memdb/database.hpp"
#include "memdb/exceptions.hpp"

#include <cctype>
#include <charconv>

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
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '-' && i + 1 < n && (std::isdigit(static_cast<unsigned char>(dsl[i + 1])) || dsl[i + 1] == '.'))) {
            std::size_t s0 = i;
            if (c == '-')
                ++i;
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
        const Tok& t = peek();
        if (t.k == Tok::K::Str) {
            next();
            return Value(t.s);
        }
        if (t.k == Tok::K::Num) {
            next();
            const std::string& s = t.s;
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
        if (t.k == Tok::K::Ident) {
            std::string w = toLower(t.s);
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

} // namespace

Expression parseQuery(std::string_view dsl) {
    Parser p(dsl, nullptr, {});
    return p.parse();
}

Expression parseQuery(std::string_view dsl, Database* db, std::string_view collection) {
    Parser p(dsl, db, collection);
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
}

void QueryFunctionRegistry::unregisterFunction(const std::string& name) {
    std::lock_guard<std::mutex> lk(mutex_);
    fns_.erase(toLower(name));
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
