#pragma once

#include "document.hpp"
#include "query.hpp"

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace memdb {

class Database;

/// @brief Parst eine Query-String-DSL (NFA-03) in eine `Expression`.
///
/// Grammar (Whitespace egal, `AND`/`OR`/`NOT` als Keywords alternativ zu `&&`/`||`/`!`):
/// ```
/// expr      := orExpr
/// orExpr    := andExpr ( ('||'|'OR') andExpr )*
/// andExpr   := unary  ( ('&&'|'AND') unary )*
/// unary     := ('!'|'NOT') unary | primary
/// primary   := '(' expr ')' | predicate
/// predicate := path [ cmpOp value | '.' func '(' [value (',' value)*] ')' ]
///            | '@' relation '.' pathRest [ cmpOp value | '.' func '(' args ')' ]
/// path      := ident ('.' ident)*          // Punktnotation, auch "quoted"
/// cmpOp     := '==' | '=' | '!=' | '<' | '<=' | '>' | '>='
/// value     := number | string | true | false | null
/// ```
/// - Einzelner `path` ohne Operator = `exists()`.
/// - Function-Operatoren: eingebaute `contains(v)`, `exists()`, `matches('regex')`,
///   `in(v1, v2, ...)`; eigene über `QueryFunctionRegistry` registrierbar.
/// - `@relation.rest` wertet `rest` über eine FA-18-Referenz aus (nur mit
///   `parseQuery(dsl, db, collection)` bzw. `Collection::find(dsl, db)`).
/// @throws QueryError bei Syntaxfehler / unbekanntem Operator.
Expression parseQuery(std::string_view dsl);

/// @brief Wie `parseQuery(dsl)`, aber mit Referenz-Kontext für `@relation.path`.
/// @param db Database mit den Referenzen (FA-18); nullptr = keine `@`-Pfade erlaubt.
/// @param collection Name der Collection, auf die sich `@` bezieht.
Expression parseQuery(std::string_view dsl, Database* db, std::string_view collection);

/// @brief Wertausdruck eines Bulk-Update-Set-Feldes, ausgewertet gegen das jeweilige Dokument
/// (v0.21): Literal, Feld-Referenz (`age`), oder Arithmetik (`age + 1`, `price * 2`, `a + ' ' + b`).
using SetExpression = std::function<Value(const Document&)>;

/// @brief Parst die Set-DSL eines Bulk-Updates (`Collection::update(queryDsl, setDsl)`) in
/// eine Liste von (Pfad, Wertausdruck)-Zuweisungen. Alle Ausdruecke werden vor dem Schreiben
/// gegen das alte Dokument ausgewertet (Snapshot-Semantik innerhalb eines Dokuments).
///
/// Grammar:
/// ```
/// setDsl  := assign (',' assign)*
/// assign  := path '=' vexpr
/// vexpr   := mul (('+'|'-') mul)*
/// mul     := unary (('*'|'/'|'%') unary)*
/// unary   := '-' unary | primary
/// primary := '(' vexpr ')' | number | string | true | false | null | path
/// path    := ident ('.' ident)*        // Feld-Referenz im Dokument (findPath)
/// ```
/// - `+` bei zwei Strings = Konkatenation; `+`/`-`/`*` bei Zahlen = Arithmetik
///   (int bleibt int, sonst double); `/` = double; `%` nur int.
/// - Zuweisungs-Pfad darf gepunktet sein (`adresse.plz = '10115'`) -> `Document::setPath`.
/// @throws QueryError bei Syntaxfehler; Typfehler werfen `Error` bei der Auswertung.
std::vector<std::pair<std::string, SetExpression>> parseUpdateSet(std::string_view setDsl);

/// @brief Factory für eigene DSL-Function-Operatoren (NFA-03): bekommt den
/// Feldausdruck (Field oder @ref) und die Argumente, liefert die Expression.
using QueryFunction = std::function<Expression(Expression fieldExpr, const std::vector<Value>& args)>;

/// @brief Zentrale Registry für DSL-Function-Operatoren (Singleton, thread-sicher).
/// Eingebaute Funktionen: `contains`, `exists`, `matches`, `in`.
class QueryFunctionRegistry {
public:
    static QueryFunctionRegistry& instance();

    /// @brief Funktion anmelden (gleichnamige wird überschrieben).
    /// @throws QueryError bei leerem Namen / leerer Funktion.
    void registerFunction(std::string name, QueryFunction fn);
    void unregisterFunction(const std::string& name);
    bool hasFunction(const std::string& name) const;
    std::vector<std::string> functionNames() const;

    /// @brief Funktion aufrufen; wirft QueryError bei unbekanntem Namen.
    Expression call(const std::string& name, Expression fieldExpr,
                    const std::vector<Value>& args) const;

private:
    QueryFunctionRegistry();
    QueryFunctionRegistry(const QueryFunctionRegistry&) = delete;
    QueryFunctionRegistry& operator=(const QueryFunctionRegistry&) = delete;

    mutable std::mutex mutex_;
    std::map<std::string, QueryFunction> fns_; ///< name (lowercase) -> Funktion
};

} // namespace memdb
