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
  Zugriffe (lesen und schreiben). Damit ist die Transaktion vollständig
  serialisiert (Snapshot-Isolation, FA-16).
- **Atomarität/Rollback:** Beim Begin wird eine vollständige Kopie des
  Collection-Zustands (Dokumente, ID-Zähler, Indizes) als Backup gesichert.
  `rollback()` – oder der Destruktor ohne `commit()` – stellt das Backup
  wieder her (alles-oder-nichts).
- **Commit:** Änderungen bleiben bestehen, Lock wird freigegeben.
- Nach `commit()`/`rollback()` wirft jeder weitere Aufruf `Error`
  („Transaktion ist nicht mehr aktiv").
- Unique-Constraints (FA-10) gelten auch innerhalb der Transaktion; eine
  Verletzung wirft `UniqueConstraintError`, die Transaktion bleibt aktiv und
  kann danach gezielt zurückgerollt werden.
- Index-Nutzung (FA-10) funktioniert innerhalb der Transaktion wie außerhalb.

## Implementierungs-Hinweis (v1)
Rollback per vollständiger Kopie – einfach und korrekt, aber O(Collection) pro
`beginTransaction()`. Für große Collections ist als Ausblick ein Undo-Log /
Copy-on-Write vorgesehen (siehe Ausblick in den Anforderungen).

Intern: `src/collection_state.hpp` kapselt den Collection-Zustand
(`memdb::detail::CollectionState`) mit lock-freien `do*`-Operationen; sowohl
`Collection` (mit `shared_lock`/`unique_lock`) als auch `Transaction`
(exklusiver Lock + Backup) rufen dieselben Operationen.
