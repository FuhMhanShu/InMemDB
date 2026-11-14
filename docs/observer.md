# Observer / Change-Callbacks (FA-15) – Umsetzung v0.7

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.7) |
| Bezug | Anforderungen FA-15 (Kann) |

## API

```cpp
std::uint64_t h = coll.onChange([](const ChangeEvent& e) { /* ... */ });
coll.offChange(h);   // true bei Erfolg
```

`ChangeEvent { ChangeType type; std::string collection; DocumentId id; Document document; }`
- `Insert`/`Update`/`Replace`: `document` = neuer Zustand
- `Remove`: `document` = entferntes Dokument (alter Zustand)
- `Clear`: ein Event, `document` leer

## Semantik
- Events feuern **nach Abschluss** der Operation und **außerhalb des Collection-Locks**
  (Callback darf gefahrlos wieder in dieselbe Collection zugreifen, z. B. `count()`).
- Reihenfolge der Events entspricht der Ausführungsreihenfolge; mehrere Observer werden
  alle benachrichtigt.
- **Transaktionen:** Events werden gepuffert und **erst bei `commit()` gefeuert** –
  Beobachter sehen also nur committete Zustände. Bei `rollback()` werden sie verworfen.
- `loadJson` erzeugt `Clear` + ein `Insert` pro Dokument.
- Index-Änderungen (`createIndex`/`dropIndex`) erzeugen keine Events (Schema, keine Daten).
