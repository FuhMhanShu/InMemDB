#include "regex.hpp"

#include <array>
#include <cctype>
#include <stdexcept>
#include <vector>

namespace memdb::detail {

namespace {

struct Ast {
    enum class T { Empty, Char, Any, Class, Concat, Alt, Star, Plus, Opt, Start, End } t;
    char c = 0;
    std::array<bool, 256> cls{};
    std::vector<Ast> kids;
};

struct Parser {
    std::string_view p;
    std::size_t i = 0;

    explicit Parser(std::string_view pat) : p(pat) {}

    bool eof() const { return i >= p.size(); }
    char peek() const { return eof() ? '\0' : p[i]; }

    [[noreturn]] void fail(const std::string& msg) {
        throw std::invalid_argument("RegEx an Position " + std::to_string(i) + ": " + msg);
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
            case '0': return '\0';
            default: return c;
        }
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
        if (!eof() && (peek() == '*' || peek() == '+' || peek() == '?')) {
            char q = p[i++];
            Ast n;
            n.t = q == '*' ? Ast::T::Star : (q == '+' ? Ast::T::Plus : Ast::T::Opt);
            n.kids = {std::move(a)};
            return n;
        }
        return a;
    }

    Ast parseAtom() {
        if (eof())
            fail("unerwartetes Ende");
        char c = p[i];
        if (c == '(') {
            ++i;
            Ast g = parseAlt();
            if (eof() || peek() != ')')
                fail("')' erwartet");
            ++i;
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

bool regexMatchImpl(std::string_view pattern, std::string_view text) {
    Parser p(pattern);
    Ast ast = p.parseAlt();
    if (!p.eof())
        p.fail("')' ohne '('");
    Prog prog;
    Compiler c(prog);
    Frag f = c.compile(ast);
    int match = c.emit(Inst{Inst::Match});
    c.patch(f.outs, match);

    // Simulation: current = Menge von Programmzustaenden nach Epsilon-Closure
    auto closure = [&](std::vector<int>& list, int pc) {
        std::vector<int> stack{pc};
        while (!stack.empty()) {
            int s = stack.back();
            stack.pop_back();
            const Inst& in = prog.insts[s];
            if (in.op == Inst::Split) {
                stack.push_back(in.out);
                stack.push_back(in.out2);
            } else if (in.op == Inst::Jmp) {
                stack.push_back(in.out);
            } else {
                list.push_back(s);
            }
        }
    };

    // Anker expandieren: AssertStart nur an tp==0, AssertEnd nur am Textende
    auto expandAsserts = [&](std::vector<int>& list, std::size_t tp) {
        bool changed = true;
        while (changed) {
            changed = false;
            std::vector<int> add;
            for (int pc : list) {
                const Inst& in = prog.insts[pc];
                if ((in.op == Inst::AssertStart && tp == 0) ||
                    (in.op == Inst::AssertEnd && tp == text.size())) {
                    std::vector<int> tmp;
                    closure(tmp, in.out);
                    add.insert(add.end(), tmp.begin(), tmp.end());
                    changed = true;
                }
            }
            // Assert-Instruktionen selbst entfernen, gefundene Nachfolger anhaengen
            std::vector<int> keep;
            for (int pc : list) {
                Inst::Op op = prog.insts[pc].op;
                if (op != Inst::AssertStart && op != Inst::AssertEnd)
                    keep.push_back(pc);
            }
            keep.insert(keep.end(), add.begin(), add.end());
            list = std::move(keep);
        }
    };

    std::vector<int> cur;
    closure(cur, f.start);
    for (std::size_t tp = 0; tp <= text.size(); ++tp) {
        expandAsserts(cur, tp);
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
                closure(next, in.out);
        }
        cur = std::move(next);
    }
    return false;
}

} // namespace

bool regexMatch(std::string_view pattern, std::string_view text) {
    return regexMatchImpl(pattern, text); // wirft std::invalid_argument bei ungueltigem Pattern
}

} // namespace memdb::detail
