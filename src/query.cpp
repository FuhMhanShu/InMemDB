#include "memdb/database.hpp"
#include "memdb/document.hpp"
#include "memdb/exceptions.hpp"
#include "memdb/query.hpp"
#include "memdb/references.hpp"

#include "regex.hpp"

#include <vector>

namespace memdb {

struct ExprNode {
    virtual ~ExprNode() = default;
    virtual bool eval(const Document& doc) const = 0;
    /// Feldartigen Knoten: Wert zum Dokument auflösen (by value, damit auch
    /// Referenz-Auflösung (FA-18) kein Dangling-Zeiger in ein temporäres Dokument
    /// zurückliefert); nullopt = Feld fehlt.
    virtual std::optional<Value> resolve(const Document& doc) const {
        (void)doc;
        return std::nullopt;
    }
    /// Index-Pfad feldartiger Knoten (für Index-Hinweis).
    virtual std::optional<std::string> indexPath() const { return std::nullopt; }
    /// Ist der Knoten ein Feldausdruck (Vergleiche/Operatoren erlaubt)?
    virtual bool isFieldLike() const { return false; }
};

namespace {

struct PathNode : ExprNode {
    std::string path;
    explicit PathNode(std::string p) : path(std::move(p)) {}
    bool eval(const Document& doc) const override { return doc.findPath(path) != nullptr; }
    std::optional<Value> resolve(const Document& doc) const override {
        if (const Value* v = doc.findPath(path))
            return *v;
        return std::nullopt;
    }
    std::optional<std::string> indexPath() const override { return path; }
    bool isFieldLike() const override { return true; }
};

/// Feldausdruck über eine Referenz (FA-18): '@relation.path' – Auflösung pro Dokument.
struct RefPathNode : ExprNode {
    Database* db; // Database ist nicht kopierbar; Lebenszeit: db muss den Knoten überdauern
    std::string storage;
    std::string fromCollection;
    std::string relation;
    std::string path;
    RefPathNode(Database& d, std::string s, std::string fc, std::string rel, std::string p)
        : db(&d), storage(std::move(s)), fromCollection(std::move(fc)), relation(std::move(rel)),
          path(std::move(p)) {}
    bool eval(const Document& doc) const override { return resolve(doc).has_value(); }
    std::optional<Value> resolve(const Document& doc) const override {
        ReferenceIndex refs(*db, storage);
        for (const auto& r : refs.references(fromCollection, RefDirection::Outgoing, doc.id())) {
            if (r.relation != relation || !r.toId)
                continue;
            auto target = db->collection(r.toCollection).findById(*r.toId);
            if (target)
                if (const Value* v = target->findPath(path))
                    return *v; // Kopie: target ist temporär
        }
        return std::nullopt;
    }
    bool isFieldLike() const override { return true; }
};

struct CompareNode : ExprNode {
    enum class Op { Eq, Ne, Lt, Le, Gt, Ge };
    std::shared_ptr<ExprNode> lhs;
    Op op;
    Value rhs;
    CompareNode(std::shared_ptr<ExprNode> l, Op o, Value v)
        : lhs(std::move(l)), op(o), rhs(std::move(v)) {}
    bool eval(const Document& doc) const override {
        auto l = lhs->resolve(doc);
        if (!l)
            return op == Op::Ne;
        if (op == Op::Eq)
            return equal(*l, rhs);
        if (op == Op::Ne)
            return !equal(*l, rhs);
        auto c = compare(*l, rhs);
        if (!c)
            return false;
        switch (op) {
            case Op::Lt: return *c < 0;
            case Op::Le: return *c <= 0;
            case Op::Gt: return *c > 0;
            case Op::Ge: return *c >= 0;
            default: return false;
        }
    }
};

struct ContainsNode : ExprNode {
    std::shared_ptr<ExprNode> lhs;
    Value needle;
    ContainsNode(std::shared_ptr<ExprNode> l, Value v) : lhs(std::move(l)), needle(std::move(v)) {}
    bool eval(const Document& doc) const override {
        auto l = lhs->resolve(doc);
        if (!l)
            return false;
        if (l->isArray()) {
            for (const auto& item : l->asArray())
                if (equal(item, needle))
                    return true;
            return false;
        }
        if (l->isString() && needle.isString())
            return l->asString().find(needle.asString()) != std::string::npos;
        return false;
    }
};

struct ExistsNode : ExprNode {
    std::shared_ptr<ExprNode> lhs;
    explicit ExistsNode(std::shared_ptr<ExprNode> l) : lhs(std::move(l)) {}
    bool eval(const Document& doc) const override { return lhs->resolve(doc).has_value(); }
};

struct MatchesNode : ExprNode {
    std::shared_ptr<ExprNode> lhs;
    std::string pattern;
    MatchesNode(std::shared_ptr<ExprNode> l, std::string p) : lhs(std::move(l)), pattern(std::move(p)) {}
    bool eval(const Document& doc) const override {
        auto l = lhs->resolve(doc);
        if (!l || !l->isString())
            return false;
        try {
            return detail::regexMatch(pattern, l->asString());
        } catch (const std::invalid_argument&) {
            return false;
        }
    }
};

struct InNode : ExprNode {
    std::shared_ptr<ExprNode> lhs;
    std::vector<Value> values;
    InNode(std::shared_ptr<ExprNode> l, std::vector<Value> vs)
        : lhs(std::move(l)), values(std::move(vs)) {}
    bool eval(const Document& doc) const override {
        auto l = lhs->resolve(doc);
        if (!l)
            return false;
        for (const auto& v : values)
            if (equal(*l, v))
                return true;
        return false;
    }
};

struct PredicateNode : ExprNode {
    std::shared_ptr<ExprNode> lhs;
    std::function<bool(const Value&)> fn;
    PredicateNode(std::shared_ptr<ExprNode> l, std::function<bool(const Value&)> f)
        : lhs(std::move(l)), fn(std::move(f)) {}
    bool eval(const Document& doc) const override {
        auto l = lhs->resolve(doc);
        return l && fn(*l);
    }
};

struct AndNode : ExprNode {
    std::shared_ptr<ExprNode> left;
    std::shared_ptr<ExprNode> right;
    AndNode(std::shared_ptr<ExprNode> l, std::shared_ptr<ExprNode> r)
        : left(std::move(l)), right(std::move(r)) {}
    bool eval(const Document& doc) const override { return left->eval(doc) && right->eval(doc); }
};

struct OrNode : ExprNode {
    std::shared_ptr<ExprNode> left;
    std::shared_ptr<ExprNode> right;
    OrNode(std::shared_ptr<ExprNode> l, std::shared_ptr<ExprNode> r)
        : left(std::move(l)), right(std::move(r)) {}
    bool eval(const Document& doc) const override { return left->eval(doc) || right->eval(doc); }
};

struct NotNode : ExprNode {
    std::shared_ptr<ExprNode> child;
    explicit NotNode(std::shared_ptr<ExprNode> c) : child(std::move(c)) {}
    bool eval(const Document& doc) const override { return !child->eval(doc); }
};

std::optional<IndexHint> hintFrom(const std::shared_ptr<ExprNode>& n) {
    if (auto* c = dynamic_cast<const CompareNode*>(n.get())) {
        if (c->op == CompareNode::Op::Ne)
            return std::nullopt;
        if (auto p = c->lhs->indexPath())
            return IndexHint{*p, static_cast<int>(c->op), c->rhs};
        return std::nullopt;
    }
    if (auto* a = dynamic_cast<const AndNode*>(n.get())) {
        if (auto h = hintFrom(a->left))
            return h;
        return hintFrom(a->right);
    }
    return std::nullopt;
}

} // namespace

Expression::Expression() = default;
Expression::Expression(std::shared_ptr<ExprNode> root) : root_(std::move(root)) {}
Expression::~Expression() = default;

Expression Expression::compareWith(int op, Value v) const {
    if (!root_ || !root_->isFieldLike())
        throw QueryError("Vergleichsoperator erfordert Feldausdruck (Field(...))");
    return Expression(
        std::make_shared<CompareNode>(root_, static_cast<CompareNode::Op>(op), std::move(v)));
}

Expression Expression::contains(Value v) const {
    if (!root_ || !root_->isFieldLike())
        throw QueryError("contains() erfordert Feldausdruck (Field(...))");
    return Expression(std::make_shared<ContainsNode>(root_, std::move(v)));
}

Expression Expression::exists() const {
    if (!root_ || !root_->isFieldLike())
        throw QueryError("exists() erfordert Feldausdruck (Field(...))");
    return Expression(std::make_shared<ExistsNode>(root_));
}

Expression Expression::matches(Value pattern) const {
    if (!root_ || !root_->isFieldLike())
        throw QueryError("matches() erfordert Feldausdruck (Field(...))");
    if (!pattern.isString())
        throw QueryError("matches(): Pattern muss ein String sein");
    try {
        detail::regexMatch(pattern.asString(), ""); // Pattern validieren
    } catch (const std::invalid_argument& e) {
        throw QueryError(std::string("matches(): ungueltiges Pattern: ") + e.what());
    }
    return Expression(std::make_shared<MatchesNode>(root_, pattern.asString()));
}

Expression Expression::in(std::vector<Value> values) const {
    if (!root_ || !root_->isFieldLike())
        throw QueryError("in() erfordert Feldausdruck (Field(...))");
    return Expression(std::make_shared<InNode>(root_, std::move(values)));
}

Expression Expression::predicate(std::function<bool(const Value&)> fn) const {
    if (!root_ || !root_->isFieldLike())
        throw QueryError("predicate() erfordert Feldausdruck (Field(...))");
    if (!fn)
        throw QueryError("predicate(): Funktion leer");
    return Expression(std::make_shared<PredicateNode>(root_, std::move(fn)));
}

bool Expression::evaluate(const Document& doc) const {
    return root_ ? root_->eval(doc) : true;
}

std::optional<IndexHint> Expression::indexHint() const {
    if (!root_)
        return std::nullopt;
    return hintFrom(root_);
}

Expression Field(std::string path) { return Expression(std::make_shared<PathNode>(std::move(path))); }

Expression refField(Database& db, std::string fromCollection, std::string relation,
                    std::string path, std::string storage) {
    return Expression(std::make_shared<RefPathNode>(db, std::move(storage),
                                                     std::move(fromCollection), std::move(relation),
                                                     std::move(path)));
}

Expression operator==(Expression e, Value v) { return e.compareWith(0, std::move(v)); }
Expression operator!=(Expression e, Value v) { return e.compareWith(1, std::move(v)); }
Expression operator< (Expression e, Value v) { return e.compareWith(2, std::move(v)); }
Expression operator<=(Expression e, Value v) { return e.compareWith(3, std::move(v)); }
Expression operator> (Expression e, Value v) { return e.compareWith(4, std::move(v)); }
Expression operator>=(Expression e, Value v) { return e.compareWith(5, std::move(v)); }

Expression operator&&(Expression a, Expression b) {
    if (!a.root_)
        return b;
    if (!b.root_)
        return a;
    return Expression(std::make_shared<AndNode>(a.root_, b.root_));
}

Expression operator||(Expression a, Expression b) {
    if (!a.root_ || !b.root_)
        return Expression();
    return Expression(std::make_shared<OrNode>(a.root_, b.root_));
}

Expression operator!(Expression e) {
    if (!e.root_)
        throw QueryError("! auf leerer Expression nicht definiert");
    return Expression(std::make_shared<NotNode>(e.root_));
}

} // namespace memdb
