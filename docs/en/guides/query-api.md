# Query API – the binding form of FA-05

| Field | Value |
|---|---|
| Status | decided (v0.1) |
| Decision | **variant A: type-safe expression tree with operator overloading** |
| String DSL | optional feature, **implemented in v0.14**: see [`query-dsl.md`](query-dsl.md) |
| Relates to | requirements FA-05 (queries), NFA-03 (extensibility) |

---

## 1. Core idea

A query is an **expression tree** (`Expression`). Comparisons on fields create nodes,
logical operators link them:

```cpp
using namespace memdb;

Expression q = (Field("age") >= 18)
            && (Field("address.city") == "Berlin")
            &&  Field("tags").contains("cpp")
            && !Field("blocked");
```

`Field("address.city")` uses **dot notation** for nested documents.

## 2. Class sketch

```cpp
namespace memdb {

class Expression;                    // thin wrapper around std::shared_ptr<ExprNode>

Expression Field(std::string path);  // field access via dot notation

// comparison operators (free operators on Expression × Value)
Expression operator==(Expression, Value);
Expression operator!=(Expression, Value);
Expression operator< (Expression, Value);
Expression operator<=(Expression, Value);
Expression operator> (Expression, Value);
Expression operator>=(Expression, Value);

// logical combination
Expression operator&&(Expression, Expression);
Expression operator||(Expression, Expression);
Expression operator! (Expression);

// member operators
Expression Expression::contains(Value v) const;  // array: element equality; string: substring
Expression Expression::exists() const;            // field present
}
```

## 3. Usage on a collection

```cpp
Collection users = db.collection("users");

auto cursor = users.find(q);                 // all matches, iterable (Cursor) – copies documents
auto ids    = users.findIds(q);              // only IDs, NO document copies (fast for large sets)
std::size_t n0 = users.forEach(q, [](const Document& d) { /* read-only */ }); // no copies, under read lock
std::optional<Document> one = users.findOne(q);
std::size_t n  = users.count(q);

users.update(q, [](Document& d) {            // selective, field-wise update
    d["checked"] = true;
});

users.remove(q);                             // delete all matches

auto all = users.find({});                   // empty query matches all documents
```

## 4. Semantics (fixed)

| Aspect | Rule |
|---|---|
| Missing field | comparison (`<`, `<=`, `>`, `>=`, `==`) evaluates to `false`; `!=` evaluates to `true` |
| `null` | `== null` / `!= null` are allowed; range comparisons with `null` are `false` |
| Numerics | `int64` and `double` are promoted for comparisons (mixed comparisons allowed) |
| Strings | `==`/`!=` exact, `<` … `>=` lexicographic; `contains` = substring |
| Arrays | `contains(v)` = at least one element equal to `v` |
| Dot notation | `a.b.c` navigates nested documents; non-existent path = missing field |
| Empty query | matches all documents (`find({})`) |
| Evaluation | per document O(size of the expression tree); no global scan index needed |
| Type errors | operators with mismatching types throw a typed exception at evaluation time, not at construction time |

## 5. Extensibility (NFA-03)

New operators are added as free functions or members on `Expression` without changing the
core, e.g.:

```cpp
Expression Expression::matches(std::string regex) const;   // string matches regex
Expression in(Expression, std::vector<Value>);             // value in set
```

## 6. String DSL (optional, implemented in v0.14)

`users.find("age >= 18 && address.city == 'Berlin'")` – own parser translating the string
into the same `Expression` tree (incl. index usage). Details, grammar, registrable
operators and `@relation` joins see [`query-dsl.md`](query-dsl.md).
No mandatory dependency in the core.
