# Hybrid / Out-of-Core (v0.18)

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.18, opt-in, Kann) |
| Bezug | Anforderungen Ausblick Abschnitt 10 (Out-of-Core-/Hybrid-Modus) |

InMemDB bleibt ein In-Memory-System; **optional** kann eine Collection einen Teil ihrer
Dokumente in eine Datei auslagern ("Spill"), wenn das RAM knapp wird.

## API

```cpp
Collection c = db.createCollection("big");
c.setOverflow("big.spill", 100000);   // max. 100k Dokumente im RAM, Rest ins Spill-File

c.insert(...);                        // ab Ueberschreiten: aeltestes Doc (FIFO) wird gespillt
c.findById(id);                       // liest bei Bedarf aus dem Spill
c.count("i >= 0");                    // Scans sehen RAM + Spill
c.updateById(spilledId, ...);         // schreibt neue Version ans Spill (append-only)

OverflowInfo info = c.overflowInfo(); // enabled, inMemory, spilled, spillBytes, ...
std::uint64_t freed = c.compactSpill(); // Spill-Datei kompaktieren (alte Versionen weg)

c.disableOverflow();                  // alles zurueck ins RAM, Spill-Datei wird geloescht
```

## Wie es funktioniert

- Spill-Datei = Append-only-Log von Records `[u64 id][u32 len][JSON-Dokument]`; im RAM liegt
  nur der Index `id → Offset` (klein).
- **Evict:** nach `insert`/`insertBulk` werden bei Ueberschreitung von `maxInMemoryDocs` die
  Dokumente mit der niedrigsten ID (FIFO) ans Spill angehängt und aus RAM + Indizes entfernt.
- **Laden:** `findById` und alle Scans (`find`/`count`/`forEach`/`findIds`/`aggregate`/`groupBy`)
  lesen gespillte Dokumente on-demand aus der Datei. Updates/Removes/Replace von gespillten
  Dokumenten hängen eine neue Version an bzw. entfernen den Index-Eintrag.
- **Recovery:** beim `setOverflow` auf eine bestehende Datei wird der Index aus dem Log
  wieder aufgebaut (letzte Version pro ID gewinnt).
- **Transaktionen:** während einer aktiven Transaktion wird nicht evictet (`txLocks`); das
  Tx-Backup umfasst auch den Spill-Index, Rollback stellt beides wieder her. `setOverflow`/
  `disableOverflow` während aktiver Transaktion werfen `Error`.

## Hinweise / Grenzen
- Default **aus** – ohne `setOverflow` verhält sich alles wie bisher (reines RAM).
- Indizes decken nur die RAM-Dokumente ab; Unique-Constraints gelten daher im Hybrid-Modus
  nur für den „heißen“ RAM-Satz (dokumentiert; für strikte Unique + Hybrid: RAM-Grenze passend wählen).
- Scans über gespillte Dokumente lesen die Datei sequenziell (O(gesamt)); für reine
  RAM-Performance `findIds`/`forEach` auf die RAM-Teilmenge begrenzen, wenn möglich.
- `compactSpill()` räumt überschriebene/gelöschte Records aus der Datei.

## Tests

`tests/test_hybrid.cpp` (35 Checks): Evict/Load, Scans über RAM+Spill, findById/updateById/
remove auf gespillten Docs, Tx-Verhalten, compactSpill, Recovery beim Reopen, disableOverflow;
unter ASan/UBSan clean.
