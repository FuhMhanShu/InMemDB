# String-DSL für Queries (NFA-03) – v0.14

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.14, Kann-Feature aus FA-05/NFA-03) |
| Bezug | `docs/guides/query-api.md` (Ausdruckbaum, bleibt die Basis), FA-18 (Referenzen), Anforderungen NFA-03 |

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

// Bulk-Update per DSL mit Projektion (v0.21): Query waehlt, Set-DSL beschreibt die Aenderung
users.update("age >= 18", "adult = true, status = 'adult', age = age + 1");
users.update("", "adresse.plz = '10115'");                 // leere Query = alle Dokumente
orders.update("@kunde.age >= 18", "vip = true", db);        // mit Referenz-Kontext

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

`matches` nutzt eine eingebaute Mini-RegEx (Thompson-NFA mit Visited-Set, kein Backtracking,
kein Hang bei verschachtelten Schleifen wie `(a*)*`; Full-Match – das Pattern muss zum
**gesamten** Feldwert passen, nicht nur zu einem Teilstring):
Literale, `.`, Quantoren `*` `+` `?` `{n}` `{n,}` `{n,m}` (lazy `*?` etc. erlaubt, ohne Wirkung
bei Full-Match), Klassen `[...]`/`[^...]` mit Bereichen `a-z` und Kurzschriften
`\d \D \s \S \w \W`, `\x`-Escapes (`\n \r \t \f \v \a \0` + Literal), Anker `^` `$`,
Alternativen `|`, Gruppen `(...)` und `(?:...)` (v0.20). Beispiele: `name.matches('^[A-Z][a-z]+$')`,
`tel.matches('^\\d{3}-\\d{4}$')`.

Byte-basiert: UTF-8 wird nicht als Zeichen
behandelt (`.` und Klassen arbeiten auf Bytes; `.` matcht auch `\n`). Nicht unterstützt
(NFA-Grenze): Backreferences, Lookaround, `\b`/`\B` (`\b` ist das Literal `b`).

**Härtung v0.34** (pathologische Patterns dürfen nicht crashen/hängen – früher z. B.
Segfault ab ~20000 verschachtelten Klammern, Hang bei `((a{100}){100}){100}`):
Pattern max. 4096 Zeichen, Verschachtelung max. 256, NFA max. 50000 Anweisungen nach
{n,m}-Expansion, Match max. 100 Mio. Schritte. Ungültige/zu komplexe Patterns werfen
`std::invalid_argument` mit Position – `matches(...)` bzw. `Expression::matches(...)`
wandeln das am Query-Bau in `QueryError` um; ein überschrittenes Match-Budget wirft
`std::runtime_error` und wird bei der Auswertung als „kein Match“ behandelt (statt zu
hängen). Außerdem kompiliert `MatchesNode` das Pattern seit v0.34 **einmalig pro Query**
(`detail::Regex`), nicht mehr pro ausgewertetem Dokument.

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

## Bulk-Update: Set-DSL mit Projektion (v0.21)

`update(queryDsl, setDsl)` (Collection und Transaction, optional mit `Database&` für
`@relation` in der Query) ist das DSL-Aquivalent zu `update(q, mutator)` – ohne C++-Lambda.
Die Set-DSL beschreibt die Projektion (Zielpfad = Wertausdruck); Wertausdrücke werden vor dem
Schreiben gegen das alte Dokument ausgewertet (Snapshot-Semantik innerhalb eines Dokuments).

```
setDsl  := assign (',' assign)*
assign  := path '=' vexpr                 // path darf gepunktet sein: adresse.plz = '10115'
vexpr   := mul (('+'|'-') mul)*
mul     := unary (('*'|'/'|'%') unary)*
unary   := '-' unary | primary
primary := '(' vexpr ')' | number | string | true | false | null | feldPfad
```

Beispiele: `"adult = true"`, `"age = age + 1"`, `"total = price * qty"`, `"full = vorname + ' ' + nachname"`,
`"copy = anderesFeld"`, `"x = (a + b) * 2"`. `+` bei zwei Strings konkateniert; Zahlen bleiben int
(Overflow -> double), `/` liefert double, `%` nur int. Fehlende Felder liefern `null`.
Zuweisungen auf gepunktete Pfade legen Unterdokumente an (`Document::setPath`).
Syntaxfehler -> `QueryError` (mit Position), Typfehler/Division durch 0 -> `Error` bei Auswertung;
dadurch bricht der gesamte Update-Aufruf ab, bevor etwas geschrieben wird (atomar auf Collection-Ebene).

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
