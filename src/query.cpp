#include "memdb/document.hpp"
#include "memdb/exceptions.hpp"
#include "memdb/query.hpp"

#include <vector>

namespace memdb {

struct ExprNode {
    virtual ~ExprNode() = default;
    virtual bool eval(const Document& doc) const = 0;
};

namespace {

struct PathNode : ExprNode {
    std::string path;
    explicit PathNode(std::string p) : path(std::move(p)) {}
    bool eval(const Document& doc) const override { return doc.findPath(path) != nullptr; }
};

struct CompareNode : ExprNode {
    enum class Op { Eq, Ne, Lt, Le, Gt, Ge };
    std::string path;
    Op op;
    Value rhs;
    CompareNode(std::string p, Op o, Value v) : path(std::move(p)), op(o), rhs(std::move(v)) {}
    bool eval(const Document& doc) const override {
        const Value* lhs = doc.findPath(path);
        if (!lhs)
            return op == Op::Ne;
        if (op == Op::Eq)
            return equal(*lhs, rhs);
        if (op == Op::Ne)
            return !equal(*lhs, rhs);
        auto c = compare(*lhs, rhs);
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
    std::string path;
    Value needle;
    ContainsNode(std::string p, Value v) : path(std::move(p)), needle(std::move(v)) {}
    bool eval(const Document& doc) const override {
        const Value* lhs = doc.findPath(path);
        if (!lhs)
            return false;
        if (lhs->isArray()) {
            for (const auto& item : lhs->asArray())
                if (equal(item, needle))
                    return true;
            return false;
        }
        if (lhs->isString() && needle.isString())
            return lhs->asString().find(needle.asString()) != std::string::npos;
        return false;
    }
};

struct ExistsNode : ExprNode {
    std::string path;
    explicit ExistsNode(std::string p) : path(std::move(p)) {}
    bool eval(const Document& doc) const override { return doc.findPath(path) != nullptr; }
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

} // namespace

Expression::Expression() = default;
Expression::Expression(std::shared_ptr<ExprNode> root) : root_(std::move(root)) {}
Expression::~Expression() = default;

Expression Expression::compareWith(int op, Value v) const {
    auto* path = dynamic_cast<const PathNode*>(root_.get());
    if (!path)
        throw QueryError("Vergleichsoperator erfordert Feldausdruck (Field(...))");
    return Expression(std::make_shared<CompareNode>(path->path, static_cast<CompareNode::Op>(op), std::move(v)));
}

Expression Expression::contains(Value v) const {
    auto* path = dynamic_cast<const PathNode*>(root_.get());
    if (!path)
        throw QueryError("contains() erfordert Feldausdruck (Field(...))");
    return Expression(std::make_shared<ContainsNode>(path->path, std::move(v)));
}

Expression Expression::exists() const {
    auto* path = dynamic_cast<const PathNode*>(root_.get());
    if (!path)
        throw QueryError("exists() erfordert Feldausdruck (Field(...))");
    return Expression(std::make_shared<ExistsNode>(path->path));
}

bool Expression::evaluate(const Document& doc) const {
    return root_ ? root_->eval(doc) : true;
}

Expression Field(std::string path) { return Expression(std::make_shared<PathNode>(std::move(path))); }

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
