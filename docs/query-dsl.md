# String-DSL für Queries (NFA-03) – v0.14

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.14, Kann-Feature aus FA-05/NFA-03) |
| Bezug | `docs/query-api.md` (Ausdruckbaum, bleibt die Basis), FA-18 (Referenzen), Lastenheft NFA-03 |

Die String-DSL wird per `parseQuery` in denselben `Expression`-Baum übersetzt, den die
C++-API (`Field("age") >= 18`) erzeugt – inkl. Index-Nutzung (`indexHint`) bei Vergleichen.

## Nutzung

```cpp
Collection users = db.collection("users");
users.count("age >= 18 && stadt == 'Berlin'");
users.find("tags.contains('cpp') && !active");
users.findOne("name.matches('^A.*')");
users.update("age < 18", [](Document& d) { d.set("active", true); });
users.remove("stadt == 'Hamburg'");

// mit Referenz-Kontext (FA-18): '@relation.pfad'
orders.count("@kunde.name == 'Ada' && total >= 200", db);

// direkt parsen
Expression q = parseQuery("age >= 18 || age.in(1, 2)");
```

Auch `Transaction` bietet die DSL-Überladungen (`tx.count("age >= 18")`).

## Grammatik

```
expr      := orExpr
orExpr    := andExpr ( ('||'|'OR') andExpr )*
andExpr   := unary  ( ('&&'|'AND') unary )*
unary     := ('!'|'NOT') unary | primary
primary   := '(' expr ')' | predicate
predicate := path [ cmpOp value | '.' func '(' [value (',' value)*] ')' ]
           | '@' relation '.' pathRest [ cmpOp value | '.' func '(' args ')' ]
path      := ident ('.' ident)*            // Punktnotation für verschachtelte Felder
cmpOp     := '==' | '=' | '!=' | '<' | '<=' | '>' | '>='
value     := number | 'string' / "string" | true | false | null
```

- Präzedenz: `!` > `&&` > `||`; Klammern ändern die Reihenfolge. Keywords `AND`/`OR`/`NOT` (case-insensitiv) sind Alternativen zu `&&`/`||`/`!`.
- String-Literale: `'...'` oder `"..."` mit Escapes `\' \" \\ \n \r \t \0`.
- Zahlen: dezimal (`42`, `-3`, `9.5`, `1e3`) und hex (`0xFF`) → `Int`, mit `.`/`e` → `Double`.
- **Nackter Pfad** ohne Operator bedeutet `exists()`: `"active"` matcht Dokumente mit Feld `active`.
- Fehlende Felder: Vergleich `==`/`<`/... → false, `!=` → true (wie C++-API).

## Function-Operatoren

Eingebaut: `contains(v)` (Array-Element/String-Substring), `exists()`,
`matches('regex')`, `in(v1, v2, ...)`.

`matches` nutzt eine eingebaute Mini-RegEx (Thompson-NFA, kein Backtracking; Full-Match):
Literale, `.`, `*` `+` `?`, Klassen `[...]`/`[^...]` mit Bereichen, `\x`-Escapes,
Anker `^` `$`, Alternativen `|`, Gruppen `(...)`. Beispiel: `name.matches('^[A-Z][a-z]+$')`.

**Eigene Operatoren registrieren** (NFA-03 Open/Closed, thread-sicheres Singleton):

```cpp
QueryFunctionRegistry::instance().registerFunction(
    "even", [](Expression f, const std::vector<Value>& args) {
        return f.predicate([](const Value& v) { return v.isInt() && v.asInt() % 2 == 0; });
    });
users.count("age.even()");
QueryFunctionRegistry::instance().unregisterFunction("even");
```

Die Factory bekommt den Feldausdruck (`Field` oder `@ref`) und die Argumente und liefert
eine `Expression` – beliebig kombinierbar mit `&&`/`||`/`!`/Vergleichen.

## `@relation.path` – Join über Referenzen (FA-18)

`@kunde.name == 'Ada'` wertet `name` auf dem ersten über die Relation `kunde` verknüpften
Zieldokument aus (aus der Collection, auf die die Query läuft). Voraussetzung:
`parseQuery(dsl, &db, collectionName)` bzw. die `Collection::...(dsl, db)`-Überladungen.
Ohne Referenz verhält sich der Vergleich wie ein fehlendes Feld. Kein Index-Hinweis
(Traversierung pro Dokument) – für große Joins `findIds` + `ReferenceIndex::references` erwägen.

## Fehler

Syntaxfehler, unbekannte Operatoren, `@` ohne Datenbank-Kontext und ungültige
`matches`-Patterns werfen `QueryError` mit Position.

## Implementierung

- `include/memdb/query_dsl.hpp` / `src/query_dsl.cpp`: Tokenizer + rekursiver Parser, `QueryFunctionRegistry`.
- `src/regex.cpp` (intern): Mini-RegEx (`detail::regexMatch`).
- `src/query.cpp`: feldartige Knoten (`PathNode`, neuer `RefPathNode`) über gemeinsames
  `resolve()`-Interface; neue Knoten `MatchesNode`/`InNode`/`PredicateNode`; neue
  Expression-Methoden `matches`/`in`/`predicate`; `refField(db, collection, relation, path)`.
- DSL-Überladungen (`std::string_view`) auf `Collection` und `Transaction`.
