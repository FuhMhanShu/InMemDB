#pragma once

#include "value.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace memdb {

class Database;
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
/// Semantik (fehlende Felder, null, numerische Promotion) siehe `docs/de/guides/query-api.md`.
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
    /// @brief String-Feld matcht das Regex-Pattern (Full-Match; Mini-RegEx siehe
    /// `docs/de/guides/query-dsl.md`: `.` `*` `+` `?` `[...]` `^` `$` `|` `(...)` `\x`).
    /// @throws QueryError wenn die Expression kein Feldausdruck ist oder das Pattern
    ///         nicht String ist / ungueltig ist.
    Expression matches(Value pattern) const;
    /// @brief Feldwert ist in `values` enthalten (Eq-Semantik).
    /// @throws QueryError wenn die Expression kein Feldausdruck ist.
    Expression in(std::vector<Value> values) const;
    /// @brief Eigenes Prädikat auf dem Feldwert (NFA-03 Open/Closed); fehlendes
    /// Feld -> false. Basis für registrierbare DSL-Operatoren.
    /// @throws QueryError wenn die Expression kein Feldausdruck ist.
    Expression predicate(std::function<bool(const Value&)> fn) const;

    /// @brief Wertet die Expression für ein Dokument aus.
    bool evaluate(const Document& doc) const;

    /// @brief Erster index-tauglicher Vergleich im Top-Level-`&&`-Baum (oder `nullopt`).
    std::optional<IndexHint> indexHint() const;

    /// @brief true, wenn die Expression leer ist und jedes Dokument matcht (v0.27).
    bool isMatchAll() const;

private:
    explicit Expression(std::shared_ptr<ExprNode> root);
    Expression compareWith(int op, Value v) const;
    std::shared_ptr<ExprNode> root_;

    friend Expression Field(std::string path);
    friend Expression refField(Database& db, std::string fromCollection, std::string relation,
                               std::string path, std::string storage);
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

/// @brief Feldausdruck über eine Referenz (FA-18, z. B. DSL `@kunde.name`):
/// wertet `path` auf dem ersten über `relation` verknüpften Zieldokument der
/// Quell-Collection aus. Ohne Referenz/ohne Feld verhält sich ein Vergleich wie
/// ein fehlendes Feld (false außer `!=`). Kein Index-Hinweis (Traversierung pro
/// Dokument). `storage` = Speicher-Collection des ReferenceIndex.
/// @note `db` muss die Expression überdauern (wird per Zeiger gehalten).
Expression refField(Database& db, std::string fromCollection, std::string relation,
                    std::string path, std::string storage = "__references");

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
