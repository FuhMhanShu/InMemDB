# String DSL for queries (NFA-03) – v0.14

| Field | Value |
|---|---|
| Status | implemented (v0.14, optional feature from FA-05/NFA-03) |
| Reference | `docs/en/guides/query-api.md` (expression tree, stays the basis), FA-18 (references), requirements NFA-03 |

The string DSL is translated by `parseQuery` into the same `Expression` tree that the
C++ API (`Field("age") >= 18`) produces – incl. index usage (`indexHint`) on comparisons.

## Usage

```cpp
Collection users = db.collection("users");
users.count("age >= 18 && city == 'Berlin'");
users.find("tags.contains('cpp') && !active");
users.findOne("name.matches('^A.*')");
users.update("age < 18", [](Document& d) { d.set("active", true); });
users.remove("city == 'Hamburg'");

// bulk update via DSL with projection (v0.21): query selects, set-DSL describes the change
users.update("age >= 18", "adult = true, status = 'adult', age = age + 1");
users.update("", "address.zip = '10115'");                 // empty query = all documents
orders.update("@customer.age >= 18", "vip = true", db);    // with reference context

// with reference context (FA-18): '@relation.path'
orders.count("@customer.name == 'Ada' && total >= 200", db);

// parse directly
Expression q = parseQuery("age >= 18 || age.in(1, 2)");
```

`Transaction` also offers the DSL overloads (`tx.count("age >= 18")`).

## Grammar

```
expr      := orExpr
orExpr    := andExpr ( ('||'|'OR') andExpr )*
andExpr   := unary  ( ('&&'|'AND') unary )*
unary     := ('!'|'NOT') unary | primary
primary   := '(' expr ')' | predicate
predicate := path [ cmpOp value | '.' func '(' [value (',' value)*] ')' ]
           | '@' relation '.' pathRest [ cmpOp value | '.' func '(' args ')' ]
path      := ident ('.' ident)*            // dot notation for nested fields
cmpOp     := '==' | '=' | '!=' | '<' | '<=' | '>' | '>='
value     := number | 'string' / "string" | true | false | null
```

- Precedence: `!` > `&&` > `||`; parentheses change the order. Keywords `AND`/`OR`/`NOT` (case-insensitive) are alternatives to `&&`/`||`/`!`.
- String literals: `'...'` or `"..."` with escapes `\\' \\" \\\\ \\n \\r \\t \\0`.
- Numbers: decimal (`42`, `-3`, `9.5`, `1e3`) and hex (`0xFF`) → `Int`, with `.`/`e` → `Double`.
- **Bare path** without an operator means `exists()`: `"active"` matches documents that have the field `active`.
- Missing fields: comparison `==`/`<`/... → false, `!=` → true (like the C++ API).

## Function operators

Built-in: `contains(v)` (array element / string substring), `exists()`,
`matches('regex')`, `in(v1, v2, ...)`.

`matches` uses a built-in mini regex (Thompson NFA with visited set, no backtracking,
no hang on nested loops like `(a*)*`; full match – the pattern must match the
**entire** field value, not just a substring):
literals, `.`, quantifiers `*` `+` `?` `{n}` `{n,}` `{n,m}` (lazy `*?` etc. allowed, no effect
with full match), classes `[...]`/`[^...]` with ranges `a-z` and shorthands
`\d \D \s \S \w \W`, `\x` escapes (`\n \r \t \f \v \a \0` + literal), anchors `^` `$`,
alternatives `|`, groups `(...)` and `(?:...)` (v0.20). Examples: `name.matches('^[A-Z][a-z]+$')`,
`tel.matches('^\\\\d{3}-\\\\d{4}$')`.

Byte-based: UTF-8 is not treated as characters
(`.` and classes operate on bytes; `.` also matches `\n`). Not supported
(NFA limit): backreferences, lookaround, `\b`/`\B` (`\b` is the literal `b`).

**Hardening v0.34** (pathological patterns must not crash/hang – previously e.g.
segfault at ~20000 nested parens, hang on `((a{100}){100}){100}`):
pattern max 4096 chars, nesting max 256, NFA max 50000 instructions after
{n,m} expansion, match max 100M steps. Invalid/over-complex patterns throw
`std::invalid_argument` with position – `matches(...)` / `Expression::matches(...)`
convert that to `QueryError` at query construction; an exceeded match budget throws
`std::runtime_error` and is treated as "no match" during evaluation (instead of
hanging). Additionally, since v0.34 `MatchesNode` compiles the pattern **once per query**
(`detail::Regex`), no longer per evaluated document.

**Register your own operators** (NFA-03 open/closed, thread-safe singleton):

```cpp
QueryFunctionRegistry::instance().registerFunction(
    "even", [](Expression f, const std::vector<Value>& args) {
        return f.predicate([](const Value& v) { return v.isInt() && v.asInt() % 2 == 0; });
    });
users.count("age.even()");
QueryFunctionRegistry::instance().unregisterFunction("even");
```

The factory gets the field expression (`Field` or `@ref`) and the arguments and returns
an `Expression` – freely combinable with `&&`/`||`/`!`/comparisons.

## Bulk update: set DSL with projection (v0.21)

`update(queryDsl, setDsl)` (Collection and Transaction, optionally with `Database&` for
`@relation` in the query) is the DSL equivalent of `update(q, mutator)` – without a C++ lambda.
The set DSL describes the projection (target path = value expression); value expressions are
evaluated against the old document before writing (snapshot semantics within a document).

```
setDsl  := assign (',' assign)*
assign  := path '=' vexpr                 // path may be dotted: address.zip = '10115'
vexpr   := mul (('+'|'-') mul)*
mul     := unary (('*'|'/'|'%') unary)*
unary   := '-' unary | primary
primary := '(' vexpr ')' | number | string | true | false | null | fieldPath
```

Examples: `"adult = true"`, `"age = age + 1"`, `"total = price * qty"`, `"full = first + ' ' + last"`,
`"copy = otherField"`, `"x = (a + b) * 2"`. `+` on two strings concatenates; numbers stay int
(overflow → double), `/` yields double, `%` int only. Missing fields yield `null`.
Assignments to dotted paths create sub-documents (`Document::setPath`).
Syntax errors → `QueryError` (with position), type errors/division by zero → `Error` at evaluation;
the whole update call then aborts before anything is written (atomic at collection level).

## `@relation.path` – join via references (FA-18)

`@customer.name == 'Ada'` evaluates `name` on the first target document linked via the relation
`customer` (from the collection the query runs on). Prerequisite:
`parseQuery(dsl, &db, collectionName)` or the `Collection::...(dsl, db)` overloads.
Without a reference the comparison behaves like a missing field. No index hint
(traversal per document) – for large joins consider `findIds` + `ReferenceIndex::references`.

## Errors

Syntax errors, unknown operators, `@` without a database context and invalid
`matches` patterns throw `QueryError` with position.

## Implementation

- `include/memdb/query_dsl.hpp` / `src/query_dsl.cpp`: tokenizer + recursive parser, `QueryFunctionRegistry`.
- `src/regex.cpp` (internal): mini regex (`detail::regexMatch`).
- `src/query.cpp`: field-like nodes (`PathNode`, new `RefPathNode`) via a common
  `resolve()` interface; new nodes `MatchesNode`/`InNode`/`PredicateNode`; new
  Expression methods `matches`/`in`/`predicate`; `refField(db, collection, relation, path)`.
- DSL overloads (`std::string_view`) on `Collection` and `Transaction`.
