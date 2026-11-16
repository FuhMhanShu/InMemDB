#include "regex.hpp"

#include <array>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace memdb::detail {

namespace {

// Maximale Expansion von {n,m} (Schutz gegen Pattern wie a{1000000000})
constexpr int kMaxRepeat = 1000;

// Harte Limits gegen pathologische Patterns (v0.34). Ohne diese wuerde z. B. tief verschachteltes
// Pattern ("((((...))))") den rekursiven Parser-Stäck ueberlaufen (Segfault) und verschachtelte
// {n,m} bzw. lange Alternationsketten endlos kompilieren (Hang/OOM).
constexpr std::size_t kMaxPatternLen = 4096;  // Zeichen im Pattern
constexpr int kMaxDepth = 256;                // Verschachtelung (Gruppen + Quantoren)
constexpr std::size_t kMaxInsts = 50000;      // NFA-Anweisungen nach Expansion
constexpr std::size_t kMaxMatchSteps = 100000000; // Closure-Schritte pro match() (Match-Budget)

struct Ast {
    enum class T { Empty, Char, Any, Class, Concat, Alt, Star, Plus, Opt, Rep, Start, End } t;
    char c = 0;
    std::array<bool, 256> cls{};
    int repMin = 0;
    int repMax = 0; // -1 = unbegrenzt
    std::vector<Ast> kids;
};

bool isShorthand(char c) { return c == 'd' || c == 'D' || c == 's' || c == 'S' || c == 'w' || c == 'W'; }

// Fuellt cls mit den Bits einer Kurzschrift (d=Ziffern, s=Whitespace, w=Word [A-Za-z0-9_]);
// Grossbuchstaben setzen das Komplement.
void applyShorthand(char c, std::array<bool, 256>& cls) {
    bool neg = (c >= 'A' && c <= 'Z');
    char base = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    for (int k = 0; k < 256; ++k) {
        bool bit = false;
        if (base == 'd')
            bit = k >= '0' && k <= '9';
        else if (base == 's')
            bit = k == ' ' || k == '\t' || k == '\n' || k == '\r' || k == '\f' || k == '\v';
        else if (base == 'w')
            bit = (k >= '0' && k <= '9') || (k >= 'A' && k <= 'Z') || (k >= 'a' && k <= 'z') || k == '_';
        cls[k] = neg ? !bit : bit;
    }
}

struct Parser {
    std::string_view p;
    std::size_t i = 0;
    int depth = 0; // aktuelle Verschachtelung (Gruppen + Quantoren), begrenzt Rekursion/AST-Tiefe

    explicit Parser(std::string_view pat) : p(pat) {}

    bool eof() const { return i >= p.size(); }
    char peek() const { return eof() ? '\0' : p[i]; }
    char peekAt(std::size_t off) const { return i + off >= p.size() ? '\0' : p[i + off]; }

    [[noreturn]] void fail(const std::string& msg) {
        throw std::invalid_argument("RegEx an Position " + std::to_string(i) + ": " + msg);
    }

    void enter() { // eine Verschachtelungsebene tiefer (Gruppe oder Quantor)
        if (++depth > kMaxDepth)
            fail("Verschachtelung zu tief (max " + std::to_string(kMaxDepth) + ")");
    }

    char esc() { // p[i] == '\\'
        if (i + 1 >= p.size())
            fail("Flucht am Ende");
        char c = p[i + 1];
        i += 2;
        switch (c) {
            case 'n': return '\n';
            case 'r': return '\r';
            case 't': return '\t';
            case 'f': return '\f';
            case 'v': return '\v';
            case 'a': return '\a';
            case '0': return '\0';
            default: return c; // \. \\ \[ \| ... -> literales Zeichen
        }
    }

    int parseNum() {
        if (eof() || peek() < '0' || peek() > '9')
            fail("Zahl erwartet");
        int v = 0;
        while (!eof() && peek() >= '0' && peek() <= '9') {
            v = v * 10 + (p[i++] - '0');
            if (v > kMaxRepeat * 10)
                fail("Zahl zu gross");
        }
        return v;
    }

    // {n} {n,} {n,m} (mit optionalem Lazy-'?') -> Rep; false wenn kein '{' vorliegt
    bool parseBrace(Ast& out) {
        if (peek() != '{')
            return false;
        std::size_t save = i;
        ++i;
        if (eof() || peek() < '0' || peek() > '9') { // kein {n..}: '{' literal
            i = save;
            return false;
        }
        int n = parseNum();
        int m = n;
        if (peek() == ',') {
            ++i;
            if (peek() == '}')
                m = -1; // {n,}
            else
                m = parseNum();
        }
        if (peek() != '}') {
            i = save;
            return false; // '{' literal
        }
        ++i;
        if (m != -1 && m < n)
            fail("ungueltige Wiederholung {" + std::to_string(n) + "," + std::to_string(m) + "}");
        if (n > kMaxRepeat || (m != -1 && m > kMaxRepeat))
            fail("Wiederholung groesser als " + std::to_string(kMaxRepeat));
        if (peek() == '?')
            ++i; // Lazy-Marker: bei Full-Match ohne semantische Wirkung
        out.t = Ast::T::Rep;
        out.repMin = n;
        out.repMax = m;
        return true;
    }

    Ast parseAlt() {
        Ast left = parseConcat();
        while (peek() == '|') {
            ++i;
            Ast right = parseConcat();
            Ast alt;
            alt.t = Ast::T::Alt;
            alt.kids = {std::move(left), std::move(right)};
            left = std::move(alt);
        }
        return left;
    }

    Ast parseConcat() {
        std::vector<Ast> kids;
        while (!eof() && peek() != '|' && peek() != ')')
            kids.push_back(parseQuant());
        if (kids.empty()) {
            Ast e;
            e.t = Ast::T::Empty;
            return e;
        }
        if (kids.size() == 1)
            return std::move(kids[0]);
        Ast c;
        c.t = Ast::T::Concat;
        c.kids = std::move(kids);
        return c;
    }

    Ast parseQuant() {
        Ast a = parseAtom();
        for (;;) {
            if (!eof() && (peek() == '*' || peek() == '+' || peek() == '?')) {
                char q = p[i++];
                if (peek() == '?')
                    ++i; // Lazy-Marker (bei Full-Match ohne Wirkung)
                enter(); // Quantor wrappt das Atom -> eine AST-Ebene tiefer
                Ast n;
                n.t = q == '*' ? Ast::T::Star : (q == '+' ? Ast::T::Plus : Ast::T::Opt);
                n.kids = {std::move(a)};
                a = std::move(n);
                continue;
            }
            Ast r;
            if (parseBrace(r)) {
                enter(); // {n,m} wrappt das Atom -> eine AST-Ebene tiefer
                r.kids = {std::move(a)};
                a = std::move(r);
                continue;
            }
            return a;
        }
    }

    Ast parseAtom() {
        if (eof())
            fail("unerwartetes Ende");
        char c = p[i];
        if (c == '(') {
            ++i;
            if (peek() == '?') { // (?:...) Non-Capturing; andere (?...) nicht unterstuetzt
                ++i;
                if (peek() != ':')
                    fail("nur (?:...) wird unterstuetzt");
                ++i;
            }
            enter(); // rekursive Gruppe
            Ast g = parseAlt();
            if (eof() || peek() != ')')
                fail("')' erwartet");
            ++i;
            --depth;
            return g;
        }
        if (c == '[') {
            ++i;
            Ast cl;
            cl.t = Ast::T::Class;
            bool neg = false;
            if (peek() == '^') {
                neg = true;
                ++i;
            }
            if (peek() == ']') { // ']' als erstes Zeichen ist literal
                cl.cls[static_cast<unsigned char>(']')] = true;
                ++i;
            }
            while (!eof() && peek() != ']') {
                if (p[i] == '\\' && i + 1 < p.size() && isShorthand(p[i + 1])) {
                    applyShorthand(p[i + 1], cl.cls);
                    i += 2;
                    continue;
                }
                char lo;
                if (p[i] == '\\')
                    lo = esc();
                else
                    lo = p[i++];
                if (peek() == '-' && i + 1 < p.size() && p[i + 1] != ']') {
                    ++i; // '-'
                    char hi;
                    if (p[i] == '\\')
                        hi = esc();
                    else
                        hi = p[i++];
                    if (static_cast<unsigned char>(hi) < static_cast<unsigned char>(lo))
                        fail("ungueltiger Bereich");
                    for (int k = lo; k <= hi; ++k)
                        cl.cls[static_cast<unsigned char>(k)] = true;
                } else {
                    cl.cls[static_cast<unsigned char>(lo)] = true;
                }
            }
            if (eof())
                fail("']' erwartet");
            ++i; // ']'
            if (neg)
                for (int k = 0; k < 256; ++k)
                    cl.cls[k] = !cl.cls[k];
            return cl;
        }
        if (c == '.') {
            ++i;
            Ast a;
            a.t = Ast::T::Any;
            return a;
        }
        if (c == '^') {
            ++i;
            Ast a;
            a.t = Ast::T::Start;
            return a;
        }
        if (c == '$') {
            ++i;
            Ast a;
            a.t = Ast::T::End;
            return a;
        }
        if (c == '\\') {
            if (i + 1 < p.size() && isShorthand(p[i + 1])) {
                Ast cl;
                cl.t = Ast::T::Class;
                applyShorthand(p[i + 1], cl.cls);
                i += 2;
                return cl;
            }
            Ast a;
            a.t = Ast::T::Char;
            a.c = esc();
            return a;
        }
        if (c == '*' || c == '+' || c == '?')
            fail("Quantor ohne Atom");
        Ast a;
        a.t = Ast::T::Char;
        a.c = c;
        ++i;
        return a;
    }
};

// Thompson-NFA
struct Inst {
    enum Op { Char, Any, Class, Split, Jmp, Match, AssertStart, AssertEnd } op;
    char c = 0;
    std::array<bool, 256> cls{};
    int out = -1;
    int out2 = -1;
};

struct Prog {
    std::vector<Inst> insts;
};

struct Frag {
    int start;
    std::vector<std::pair<int, int>> outs; // (instIndex, 0=out, 1=out2) noch offen
};

struct Compiler {
    Prog& prog;
    explicit Compiler(Prog& p) : prog(p) {}

    int emit(Inst in) {
        if (prog.insts.size() >= kMaxInsts)
            throw std::invalid_argument("RegEx: Pattern zu komplex (max " + std::to_string(kMaxInsts) +
                                        " NFA-Anweisungen nach Expansion)");
        prog.insts.push_back(std::move(in));
        return static_cast<int>(prog.insts.size()) - 1;
    }

    Frag compile(const Ast& a) {
        switch (a.t) {
            case Ast::T::Empty: {
                Frag f{emit(Inst{Inst::Jmp}), {}};
                f.outs.push_back({f.start, 0});
                return f;
            }
            case Ast::T::Char: {
                Inst in{Inst::Char};
                in.c = a.c;
                Frag f{emit(in), {}};
                f.outs.push_back({f.start, 0});
                return f;
            }
            case Ast::T::Any: {
                Frag f{emit(Inst{Inst::Any}), {}};
                f.outs.push_back({f.start, 0});
                return f;
            }
            case Ast::T::Class: {
                Inst in{Inst::Class};
                in.cls = a.cls;
                Frag f{emit(in), {}};
                f.outs.push_back({f.start, 0});
                return f;
            }
            case Ast::T::Start: {
                Frag f{emit(Inst{Inst::AssertStart}), {}};
                f.outs.push_back({f.start, 0});
                return f;
            }
            case Ast::T::End: {
                Frag f{emit(Inst{Inst::AssertEnd}), {}};
                f.outs.push_back({f.start, 0});
                return f;
            }
            case Ast::T::Concat: {
                Frag f = compile(a.kids[0]);
                for (std::size_t k = 1; k < a.kids.size(); ++k) {
                    Frag g = compile(a.kids[k]);
                    patch(f.outs, g.start);
                    f.outs = std::move(g.outs);
                }
                return f;
            }
            case Ast::T::Alt: {
                Inst s{Inst::Split};
                int sp = emit(s);
                Frag l = compile(a.kids[0]);
                Inst j{Inst::Jmp};
                int jp = emit(j);
                patch({{sp, 0}}, l.start);
                patch({{jp, 0}}, -1); // wird spaeter gepatcht
                Frag r = compile(a.kids[1]);
                patch({{sp, 1}}, r.start);
                Frag f{sp, {}};
                f.outs = l.outs;
                f.outs.push_back({jp, 0});
                append(f.outs, r.outs);
                return f;
            }
            case Ast::T::Star: {
                Inst s{Inst::Split};
                int sp = emit(s);
                Frag b = compile(a.kids[0]);
                patch({{sp, 0}}, b.start);
                patch(b.outs, sp);
                Frag f{sp, {}};
                f.outs.push_back({sp, 1});
                return f;
            }
            case Ast::T::Plus: {
                Frag b = compile(a.kids[0]);
                Inst s{Inst::Split};
                int sp = emit(s);
                patch({{sp, 0}}, b.start);
                patch(b.outs, sp);
                Frag f{b.start, {{sp, 1}}};
                return f;
            }
            case Ast::T::Opt: {
                Inst s{Inst::Split};
                int sp = emit(s);
                Frag b = compile(a.kids[0]);
                patch({{sp, 0}}, b.start);
                Frag f{sp, {}};
                f.outs = b.outs;
                f.outs.push_back({sp, 1});
                return f;
            }
            case Ast::T::Rep: { // {n} {n,} {n,m}: n Pflicht-Kopien + Star bzw. (m-n) Opt-Kopien
                std::vector<Frag> parts;
                for (int k = 0; k < a.repMin; ++k)
                    parts.push_back(compile(a.kids[0]));
                if (a.repMax == -1) {
                    Ast s;
                    s.t = Ast::T::Star;
                    s.kids.push_back(a.kids[0]);
                    parts.push_back(compile(s));
                } else {
                    for (int k = a.repMin; k < a.repMax; ++k) {
                        Ast o;
                        o.t = Ast::T::Opt;
                        o.kids.push_back(a.kids[0]);
                        parts.push_back(compile(o));
                    }
                }
                if (parts.empty()) {
                    Ast e;
                    e.t = Ast::T::Empty;
                    return compile(e);
                }
                Frag f = std::move(parts[0]);
                for (std::size_t k = 1; k < parts.size(); ++k) {
                    patch(f.outs, parts[k].start);
                    f.outs = std::move(parts[k].outs);
                }
                return f;
            }
        }
        throw std::invalid_argument("RegEx: ungueltiger AST");
    }

    void patch(const std::vector<std::pair<int, int>>& outs, int target) {
        for (auto& [idx, which] : outs) {
            if (which == 0)
                prog.insts[idx].out = target;
            else
                prog.insts[idx].out2 = target;
        }
    }
    static void append(std::vector<std::pair<int, int>>& dst,
                       const std::vector<std::pair<int, int>>& src) {
        dst.insert(dst.end(), src.begin(), src.end());
    }
};

// Pattern -> NFA-Programm kompilieren.
// Wirft std::invalid_argument bei ungueltigem oder zu komplexem Pattern (Limits s. o.).
Prog compilePattern(std::string_view pattern, int& startOut) {
    if (pattern.size() > kMaxPatternLen)
        throw std::invalid_argument("RegEx: Pattern zu lang (max " + std::to_string(kMaxPatternLen) +
                                    " Zeichen)");
    Parser p(pattern);
    Ast ast = p.parseAlt();
    if (!p.eof())
        p.fail("')' ohne '('");
    Prog prog;
    Compiler c(prog);
    Frag f = c.compile(ast);
    int match = c.emit(Inst{Inst::Match});
    c.patch(f.outs, match);
    startOut = f.start;
    return prog;
}

// NFA ausfuehren (Full-Match). Wirft std::runtime_error bei ueberschrittenem Match-Budget.
bool matchProg(const Prog& prog, int start, std::string_view text) {
    // Epsilon-Closure inkl. Anker-Auswertung an Position tp.
    // Visited-Set pro Aufruf (stamp): verhindert Endlosschleifen bei Rueckkanten
    // (z. B. (a*)* oder ()*).
    std::vector<int> stamp(prog.insts.size(), 0);
    int stampCounter = 0;
    std::size_t steps = 0;
    auto closure = [&](std::vector<int>& list, int pc, std::size_t tp) {
        ++stampCounter;
        std::vector<int> stack{pc};
        while (!stack.empty()) {
            int s = stack.back();
            stack.pop_back();
            if (++steps > kMaxMatchSteps)
                throw std::runtime_error("RegEx: Match zu aufwaendig (Budget von " +
                                         std::to_string(kMaxMatchSteps) + " Schritten ueberschritten)");
            if (s < 0 || stamp[s] == stampCounter)
                continue;
            stamp[s] = stampCounter;
            const Inst& in = prog.insts[s];
            switch (in.op) {
                case Inst::Split:
                    stack.push_back(in.out);
                    stack.push_back(in.out2);
                    break;
                case Inst::Jmp:
                    stack.push_back(in.out);
                    break;
                case Inst::AssertStart:
                    if (tp == 0)
                        stack.push_back(in.out);
                    break; // sonst Pfad stirbt
                case Inst::AssertEnd:
                    if (tp == text.size())
                        stack.push_back(in.out);
                    break;
                default:
                    list.push_back(s);
                    break;
            }
        }
    };

    std::vector<int> cur;
    closure(cur, start, 0);
    for (std::size_t tp = 0; tp <= text.size(); ++tp) {
        if (tp == text.size()) {
            for (int pc : cur)
                if (prog.insts[pc].op == Inst::Match)
                    return true; // Full-Match: am Textende muss MATCH aktiv sein
            return false;
        }
        unsigned char ch = static_cast<unsigned char>(text[tp]);
        std::vector<int> next;
        for (int pc : cur) {
            const Inst& in = prog.insts[pc];
            bool ok = false;
            switch (in.op) {
                case Inst::Char: ok = in.c == static_cast<char>(ch); break;
                case Inst::Any: ok = true; break;
                case Inst::Class: ok = in.cls[ch]; break;
                default: ok = false; break;
            }
            if (ok)
                closure(next, in.out, tp + 1);
        }
        cur = std::move(next);
    }
    return false;
}

} // namespace

struct Regex::Impl {
    Prog prog;
    int start = 0;
    std::string pattern;
};

Regex::Regex(std::string_view pattern) : impl_(std::make_shared<Impl>()) {
    impl_->prog = compilePattern(pattern, impl_->start);
    impl_->pattern = std::string(pattern);
}
Regex::~Regex() = default;
bool Regex::match(std::string_view text) const { return matchProg(impl_->prog, impl_->start, text); }
const std::string& Regex::pattern() const { return impl_->pattern; }

bool regexMatch(std::string_view pattern, std::string_view text) {
    return Regex(pattern).match(text); // wirft std::invalid_argument bei ungueltigem Pattern
}

} // namespace memdb::detail
