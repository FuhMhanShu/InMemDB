# Transaktionen (FA-12) – Umsetzung v0.3

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.3) |
| Bezug | Anforderungen FA-12 (Soll), FA-16 |

## API

```cpp
Collection konten = db.collection("konten");
{
    Transaction tx = konten.beginTransaction();   // exklusiver Lock auf der Collection
    tx.insert({{"name", "C"}, {"saldo", 5}});
    tx.updateById(id, [](Document& d) { d.set("saldo", 0); });
    tx.update(Field("saldo") > 1000, [](Document& d) { /* ... */ });
    tx.remove(Field("gesperrt") == true);
    tx.commit();        // oder tx.rollback(); Destruktor rollt ohne commit automatisch zurueck
}
```

Innerhalb der Transaktion sind alle Lese-/Schreiboperationen verfügbar
(`insert`, `update`, `updateById`, `replace`, `remove`, `removeById`, `find`,
`findById`, `count`, `size`) und sehen die eigenen, noch nicht committeten
Änderungen (Read-your-own-writes).

## Semantik
- **Isolation:** `beginTransaction()` hält ein `unique_lock` auf der
  `shared_mutex` der Collection – währenddessen blockieren alle anderen
  Zugriffe (lesen UND schreiben). Damit ist die Transaktion vollständig
  serialisiert – stärker als Snapshot-Isolation, aber ohne MVCC: **Leser
  blockieren für die gesamte Transaktionsdauer** (Befund M3, v0.44 präzisiert:
  `Transaction::Impl` hält den `unique_lock` von `beginTransaction()` bis
  `commit()`/`rollback()`, `src/transaction.cpp`). Tradeoff: einfache, korrekte
  Isolation auf Collection-Ebene zum Preis blockierender Leser bei langen Tx.
- **Atomarität/Rollback (Copy-on-Write, v0.19):** Beim Begin wird nur ein
  Undo-Log aktiviert (`CollectionState::undo`); jede schreibende Operation
  protokolliert vor ihrer ersten Änderung pro Dokument dessen alten Zustand
  (`noteUndo`). `rollback()` – oder der Destruktor ohne `commit()` – spielt
  das Undo-Log zurück und baut die Index-Einträge neu auf (alles-oder-nichts).
  Kosten: O(1) für `beginTransaction()`/`commit()`, O(angefasste Dokumente + Reindex)
  nur beim Rollback – statt O(Collection) Voll-Backup bei jedem Begin (bis v0.18).
- **Commit:** Änderungen bleiben bestehen, Lock wird freigegeben.
- Nach `commit()`/`rollback()` wirft jeder weitere Aufruf `Error`
  („Transaktion ist nicht mehr aktiv").
- Unique-Constraints (FA-10) gelten auch innerhalb der Transaktion; eine
  Verletzung wirft `UniqueConstraintError`, die Transaktion bleibt aktiv und
  kann danach gezielt zurückgerollt werden.
- Index-Nutzung (FA-10) funktioniert innerhalb der Transaktion wie außerhalb.

## Implementierungs-Hinweis (v0.19: CoW-Undo)
Rollback per Undo-Log (Copy-on-Write): `beginTransaction()` kostet O(1) + O(#Indizes)
(Index-Struktur wird als Pfad/Unique-Liste gesichert, nicht die Einträge).
Jede Mutation ruft `CollectionState::noteUndo(id)`/`noteUndoClear()` auf, bevor sie
wirkt; das Undo-Log enthält höchstens einen Eintrag pro angefasstem Dokument.
`restore()` replays das Log, stellt `nextId`/`spillIndex` wieder her und baut die
Indizes vollständig neu auf (nur beim Rollback, nie beim Commit).

Gemessen (1 Mio. Dokumente, Release, x86-64): `beginTransaction()` + 1 Änderung +
`commit()`: 835 ms (v0.18, Voll-Backup) → **0,3 ms** (v0.19); Rollback derselben Tx:
1911 ms → **558 ms** (Reindex dominiert, fällt nur bei Rollback an). Hybrid-Modus: während
der Tx ist Evict blockiert (`txLocks`); beim Rollback wird die Spill-Datei per Truncate
auf den Pre-Tx-Stand gekürzt, beim Commit nach Clear geleert.

Intern: `src/collection_state.hpp` kapselt den Collection-Zustand
(`memdb::detail::CollectionState`) mit lock-freien `do*`-Operationen; sowohl
`Collection` (mit `shared_lock`/`unique_lock`) als auch `Transaction`
(exklusiver Lock + CoW-Undo) rufen dieselben Operationen.
