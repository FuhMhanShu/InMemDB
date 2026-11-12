#pragma once

#include "value.hpp"

#include <memory>
#include <optional>
#include <string>

namespace memdb {

class Document;
struct ExprNode;

/// @brief Vorschlag für die Index-Nutzung einer Query (intern für die Engine).
struct IndexHint {
    std::string path;
    int op = 0; ///< 0=Eq, 1=Ne, 2=Lt, 3=Le, 4=Gt, 5=Ge
    Value value;
};

/// @brief Ein Query-Ausdruck als Baum von `ExprNode`-Knoten.
///
/// Aufbau per Operator-Overloading, z. B.
/// `(Field("age") >= 18) && Field("tags").contains("cpp") && !Field("gesperrt")`.
/// Die leere Expression (`Expression{}`) matcht alle Dokumente.
/// Semantik (fehlende Felder, null, numerische Promotion) siehe `docs/query-api.md`.
class Expression {
public:
    /// @brief Leere Expression (matcht alles).
    Expression();
    Expression(const Expression&) = default;
    Expression(Expression&&) noexcept = default;
    Expression& operator=(const Expression&) = default;
    Expression& operator=(Expression&&) noexcept = default;
    ~Expression();

    /// @brief Array enthält Element / String enthält Substring.
    /// @throws QueryError wenn die Expression kein Feldausdruck ist.
    Expression contains(Value v) const;
    /// @brief Feld ist vorhanden.
    /// @throws QueryError wenn die Expression kein Feldausdruck ist.
    Expression exists() const;

    /// @brief Wertet die Expression für ein Dokument aus.
    bool evaluate(const Document& doc) const;

    /// @brief Erster index-tauglicher Vergleich im Top-Level-`&&`-Baum (oder `nullopt`).
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

/// @brief Feldausdruck per Punktnotation, z. B. `Field("adresse.stadt")`.
Expression Field(std::string path);

/// @name Vergleichsoperatoren (erzeugen einen Ausdruck auf dem Feld)
/// @throws QueryError wenn die linke Seite kein Feldausdruck ist.
/// @{
Expression operator==(Expression e, Value v);
Expression operator!=(Expression e, Value v);
Expression operator< (Expression e, Value v);
Expression operator<=(Expression e, Value v);
Expression operator> (Expression e, Value v);
Expression operator>=(Expression e, Value v);
/// @}

/// @name Logische Verknüpfung
/// @{
Expression operator&&(Expression a, Expression b);
Expression operator||(Expression a, Expression b);
/// @throws QueryError bei leerer Expression.
Expression operator! (Expression e);
/// @}

} // namespace memdb
