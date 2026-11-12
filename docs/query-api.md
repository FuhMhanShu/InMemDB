# Query-API – verbindliche Ausprägung von FA-05

| Feld | Wert |
|---|---|
| Status | beschlossen (v0.1) |
| Entscheidung | **Variante A: typsicher Ausdruckbaum mit Operator-Overloading** |
| String-DSL | nur als Kann-Feature, in v1 **nicht** umgesetzt |
| Bezieht sich auf | Lastenheft FA-05 (Abfragen), NFA-03 (Erweiterbarkeit) |

---

## 1. Kernidee

Eine Query ist ein **Ausdruckbaum** (`Expression`). Vergleiche auf Feldern erzeugen Knoten,
logische Operatoren verknüpfen sie:

```cpp
using namespace memdb;

Expression q = (Field("age") >= 18)
            && (Field("adresse.stadt") == "Berlin")
            &&  Field("tags").contains("cpp")
            && !Field("gesperrt");
```

`Field("adresse.stadt")` nutzt **Punktnotation** für verschachtelte Dokumente.

## 2. Klassen-Skizze

```cpp
namespace memdb {

class Expression;                    // dünner Wrapper um std::shared_ptr<ExprNode>

Expression Field(std::string path);  // Feldzugriff per Punktnotation

// Vergleichsoperatoren (freie Operatoren auf Expression × Value)
Expression operator==(Expression, Value);
Expression operator!=(Expression, Value);
Expression operator< (Expression, Value);
Expression operator<=(Expression, Value);
Expression operator> (Expression, Value);
Expression operator>=(Expression, Value);

// Logische Verknüpfung
Expression operator&&(Expression, Expression);
Expression operator||(Expression, Expression);
Expression operator! (Expression);

// Member-Operatoren
Expression Expression::contains(Value v) const;  // Array: Element-Gleichheit; String: Substring
Expression Expression::exists() const;            // Feld vorhanden
}
```

## 3. Verwendung auf einer Collection

```cpp
Collection users = db.collection("users");

auto cursor = users.find(q);                 // alle Treffer, iterierbar (Cursor)
std::optional<Document> one = users.findOne(q);
std::size_t n  = users.count(q);

users.update(q, [](Document& d) {            // selektives, feldweises Update
    d["geprueft"] = true;
});

users.remove(q);                             // alle Treffer löschen

auto all = users.find({});                   // leere Query matcht alle Dokumente
```

## 4. Semantik (festgelegt)

| Aspekt | Regel |
|---|---|
| Fehlendes Feld | Vergleich (`<`, `<=`, `>`, `>=`, `==`) wertet zu `false`; `!=` wertet zu `true` |
| `null` | `== null` / `!= null` sind zulässig; Bereichsvergleiche mit `null` sind `false` |
| Numerik | `int64` und `double` werden für Vergleiche promouvoir (gemischte Vergleiche erlaubt) |
| Strings | `==`/`!=` exakt, `<` … `>=` lexikographisch; `contains` = Substring |
| Arrays | `contains(v)` = mindestens ein Element gleich `v` |
| Punktnotation | `a.b.c` navigiert verschachtelte Dokumente; nicht vorhandener Pfad = fehlendes Feld |
| Leere Query | matcht alle Dokumente (`find({})`) |
| Auswertung | pro Dokument O(Größe des Ausdruckbaums); kein globaler Scan-Index nötig |
| Typfehler | Operatoren mit unpassenden Typen werfen typisierte Exception bei der Auswertung, nicht beim Bauen |

## 5. Erweiterbarkeit (NFA-03)

Neue Operatoren werden als freie Funktionen oder Member auf `Expression` ergänzt, ohne den
Kern zu ändern, z. B.:

```cpp
Expression Expression::matches(std::string regex) const;   // String matcht Regex
Expression in(Expression, std::vector<Value>);             // Wert in Menge
```

## 6. Ausblick (Kann, nicht in v1)

String-DSL als Komfort-Feature: `users.find("age >= 18 && adresse.stadt == 'Berlin'")` –
eigener Parser, der den String in denselben `Expression`-Baum übersetzt. Keine
Pflichtabhängigkeit im Kern.
