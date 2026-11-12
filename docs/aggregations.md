# Aggregationen (FA-13) – Umsetzung v0.6

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.6) |
| Bezug | Lastenheft FA-13 (Kann) |

## API

```cpp
enum class AggOp { Min, Max, Sum, Avg, Count };

// skalares Aggregat über alle Dokumente, die q matchen
std::optional<Value> coll.aggregate(AggOp::Sum, "betrag", Field("city") == "Berlin");

// Gruppierung nach Feldwert; pro Gruppe count + optional Aggregat eines zweiten Feldes
std::vector<GroupAggregate> gs = coll.groupBy("city", Expression{}, AggOp::Sum, "betrag");
// GroupAggregate { Value key; size_t count; optional<Value> value; }
```

Verfügbar auf `Collection` und innerhalb von `Transaction`.

## Semantik
- `Count`: Anzahl der matchenden Dokumente (`fieldPath` wird ignoriert); leere Menge → `0`.
- `Sum`/`Avg`: nur Zahlen (Int/Double); fehlende/`null`/nicht-numerische Felder werden
  übersprungen. `Sum` liefert `Int`, solange alle Werte Int sind und kein Overflow
  auftritt, sonst `Double`; `Avg` liefert immer `Double`. Leere Menge → `nullopt`.
- `Min`/`Max`: Zahlen und Strings (über `compare()`); andere Typen werden übersprungen.
  Leere Menge → `nullopt`.
- `groupBy`: Gruppenschlüssel ist der Wert an `keyPath` (fehlend → `null`-Gruppe);
  Ergebnis ist nach Schlüssel sortiert (`ValueLess`, Int/Double äquivalent).
  `count` = Dokumente pro Gruppe; `value` nur bei `op != Count` gesetzt.
- Abfragen nutzen vorhandene Indizes (Kandidaten über `indexHint`), die volle
  Expression wird pro Kandidat geprüft.
