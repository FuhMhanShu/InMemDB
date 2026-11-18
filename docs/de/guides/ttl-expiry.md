# TTL / Auto-Expiry – FA-25, v0.26

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.26) |
| Bezug | Anforderungen FA-25 (Kann) |

Dokumente können eine **Verfallszeit** bekommen (`expiresAt`, Unix-Sekunden als `Int`).
Abgelaufene Dokumente (`expiresAt <= now`) werden automatisch entfernt.
**Opt-in: defaultmäßig AUS** – Dokumente ohne `expiresAt` verfallen nie, bestehende
Datenbanken verhalten sich unverändert.

## API

```cpp
// Verfallszeit beim Einfügen (chainable, Document-Methoden):
coll.insert(Document{{"session", "abc"}}.expiresIn(3600));   // now + 3600 s
coll.insert(Document{{"token", "xyz"}}.expiresAt(1893456000)); // absolut (Unix-Sekunden)

// Verfallszeit auf bestehendem Dokument setzen (Update-Event, Schema/Unique-Prüfung):
coll.expiresIn(id, 60);        // now + 60 s
coll.expiresAt(id, 1893456000); // absolut
std::optional<std::int64_t> e = coll.expiresAtOf(id); // aktuell gesetzter Wert

// Collection-Default-TTL für neue Docs (insert/upsert; 0 = aus, Default):
coll.setDefaultExpiresIn(3600);  // jedes neue Doc ohne expiresAt bekommt now+3600

// Manueller Sweep (z. B. vom eigenen Timer des Anwenders):
std::size_t n = coll.expireNow(); // Anzahl entfernter Dokumente

// Uhr für Tests injizieren (Default: std::time(nullptr)):
coll.setClockForTesting([] { return myFakeNow; });
```

## Semantik

- **Lazy Sweep**: Bei Lesezugriffen (`findById`, `find`, `findIds`, `forEach`, `findOne`,
  `count`, `aggregate`, `groupBy`, `size`, `toJson`) werden abgelaufene Dokumente vorher
  entfernt (einmalig, mit `ChangeType::Remove`-Event und Metrik `expiredDocs`). Billig,
  solange kein Dokument `expiresAt` trägt (nur ein atomarer Check).
- **Kein Sweep mitten in Transaktionen/Merges** (`txLocks > 0`; während einer Tx hält die Collection den Exklusiv-Lock – volle Serialisierung, kein MVCC, s. M3/`transactions.md`);
  nach Commit greift der Sweep beim nächsten Lesezugriff.
- **Kein Hintergrund-Thread**: die Bibliothek startet keine Threads; `expireNow()` ist
  der manuelle Sweep für eigene Timer.
- **Schema (FA-20)**: mit striktem Schema muss `expiresAt` deklariert sein, sonst wird
  es (auch per Default-TTL) nicht automatisch gesetzt; manuelles Setzen ohne Deklaration
  → `SchemaError` wie jedes andere undefinierte Feld.
- **Hybrid/Spill (v0.18)**: gespillte Dokumente werden beim Sweep mitgeprüft und aus dem
  Spill-Index entfernt (Datei räumt `compactSpill()`).
- **Snapshot (FA-14)**: `expiresAt` ist ein normales Feld und wird persistiert;
  abgelaufene Dokumente landen nach dem Laden nicht im Snapshot (Sweep vor `toJson`).
- **Transaktions-Rollback (CoW)**: `expiryCount` wird beim Rollback korrekt neu aufgebaut.
- **Metriken**: neuer Zähler `CollectionMetrics::expiredDocs` (auch in `DatabaseMetrics::sum`).

## Tests

`tests/test_ttl.cpp` (47 Checks): Default aus, `expiresIn`/`expiresAt` (relativ/absolut),
lazy Sweep in allen Lesepfaden, `expireNow`, Default-TTL (manuelles `expiresAt` gewinnt,
Abschalten mit 0), Observer-Events + Metriken, Tx-Guard + Sweep nach Commit, Rollback,
Spill-Fall, Snapshot-Roundtrip, Schema-Interaktion.
