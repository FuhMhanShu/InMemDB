#pragma once

#include "value.hpp"

#include <memory>
#include <optional>
#include <string>

namespace memdb {

class Document;
struct ExprNode;

struct IndexHint {
    std::string path;
    int op = 0; // 0=Eq, 1=Ne, 2=Lt, 3=Le, 4=Gt, 5=Ge
    Value value;
};

class Expression {
public:
    Expression();
    Expression(const Expression&) = default;
    Expression(Expression&&) noexcept = default;
    Expression& operator=(const Expression&) = default;
    Expression& operator=(Expression&&) noexcept = default;
    ~Expression();

    Expression contains(Value v) const;
    Expression exists() const;

    bool evaluate(const Document& doc) const;

    std::optional<IndexHint> indexHint() const;

private:
    explicit Expression(std::shared_ptr<ExprNode> root);
    Expression compareWith(int op, Value v) const;
    std::shared_ptr<ExprNode> root_;

    friend Expression Field(std::string path);
    friend Expression operator==(Expression, Value);
    friend Expression operator!=(Expression, Value);
    friend Expression operator< (Expression, Value);
    friend Expression operator<=(Expression, Value);
    friend Expression operator> (Expression, Value);
    friend Expression operator>=(Expression, Value);
    friend Expression operator&&(Expression, Expression);
    friend Expression operator||(Expression, Expression);
    friend Expression operator! (Expression);
};

Expression Field(std::string path);

Expression operator==(Expression e, Value v);
Expression operator!=(Expression e, Value v);
Expression operator< (Expression e, Value v);
Expression operator<=(Expression e, Value v);
Expression operator> (Expression e, Value v);
Expression operator>=(Expression e, Value v);
Expression operator&&(Expression a, Expression b);
Expression operator||(Expression a, Expression b);
Expression operator! (Expression e);

} // namespace memdb
