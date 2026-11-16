# Fehlerbehandlung / Exception-Policy (v0.35)

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.35) |
| Bezug | NFA-05 (Zuverlässigkeit), FA-12/FA-15/FA-20, ADR-01/ADR-02 |

Grundsatz: **Exceptions sind für echte Fehler da** (API-Missbrauch, Constraint-Verletzungen,
I/O-Fehler, kaputte Eingabedateien) und werden an diesen Stellen **bewusst nicht** abgefangen –
der Aufrufer soll sie sehen und behandeln. Abgefangen wird nur dort, wo eine Exception sonst
** mitten in einer laufenden Operation** den Programmfluss hart abbrechen würde (Daten bereits
teilweise geschrieben, Rest der Arbeit liegengeblieben).

## Wo NICHT abgefangen wird (Fehler bleiben sichtbar)

| Situation | Exception |
|---|---|
| Unique-Index verletzt (`insert`/`update`/Merge) | `UniqueConstraintError` (atomarer Rollback) |
| Schema-Verstoß (FA-20) | `SchemaError` |
| Collection existiert schon / fehlt | `CollectionExistsError` / `CollectionNotFoundError` |
| Referenz auf fehlendes Dokument (`ReferenceIndex::link`) | `DocumentNotFoundError` |
| Query-Bau mit ungültigem DSL/Pattern (`find("name.matches('(')")`) | `QueryError` (mit Position) |
| JSON-Syntaxfehler, kaputte/fehlende Datei, falsches Passwort | `JsonError` / `Error` / `CryptoError` |
| `Value::asString()` auf einer Zahl etc. | `TypeError` |
| Explizite Einzel-Auswertung `Expression::evaluate(doc)` | wirft durch (nur die **Scan-Schleifen** fangen ab, s. u.) |

`findById` liefert dagegen bewusst `std::optional<Document>` (kein Throw bei „nicht gefunden“).

## Wo abgefangen wird (Programmfluss läuft weiter)

### 1. Query-Scan pro Dokument (`metrics().evalErrors`)
In allen Scan/Update/Remove-Schleifen (`find`, `findOne`, `findIds`, `forEach`, `count`,
`update`, `remove`, Aggregationen) wird die Auswertung **pro Dokument** in `safeEval` gekapselt:
Wirft die Auswertung (z. B. eine eigene DSL-Funktion aus der `QueryFunctionRegistry`, die
`throw` macht, oder ein Regex-Match-Budget, v0.34), zählt das Dokument als **Nicht-Treffer**
und der Fehler wird in `CollectionMetrics::evalErrors` gezählt – der Scan läuft zu Ende.
```cpp
c.find("v.meineFunktion()");   // throwende Funktion -> 0 Treffer, kein Abbruch
c.metrics().evalErrors;        // Anzahl der abgefangenen Eval-Fehler
```
Am **Query-Bau** (`parseQuery`/`Expression::matches`) wird dagegen weiterhin sofort `QueryError`
geworfen – Syntaxfehler sind Programmierfehler und sollen früh auffallen.

### 2. Observer-Callbacks (`metrics().eventErrors`)
Change-Events werden **nach** der Schreiboperation ausgeliefert (Daten sind zu dem Zeitpunkt
bereits geschrieben). Eine throwende Callback darf deshalb weder die Schreib-OP abbrechen noch
die restlichen Observer auslassen: `fireEvents` fängt pro Callback, alle Observer laufen, und
der Fehler landet in `CollectionMetrics::eventErrors`.
```cpp
c.onChange([](const ChangeEvent&) { throw std::runtime_error("Log kaputt"); });
c.insert({{"x", 1}});          // gelingt trotzdem
c.metrics().eventErrors == 1;
```
In Transaktionen werden Events erst beim Commit ausgeliefert (FA-15) – dort greift derselbe Schutz.

### 3. Bulk-Load: validieren vor mutieren (atomar, kein Partial State)
`Collection::loadJson` / `Database::fromJson` prüfen **zuerst alle Elemente** und werfen
`JsonError` (mit Collection-Name), **bevor** irgendetwas geleert oder angelegt wird. Bei einem
kaputten Element bleibt die Collection/Database also vollständig unverändert – der Aufrufer
kann den Fehler behandeln (z. B. Element überspringen, wie der Importer mit `skippedTables`)
und den Load erneut versuchen, statt einen halb geleerten Zustand zu retten.
```cpp
try {
    c.loadJson(json);
} catch (const JsonError& e) {
    // c ist unveraendert – Flow fortsetzen, z. B. fehlerhaftes Element filtern und retry
}
```

### 4. CLI-Tools: sauberer Abbruch statt `terminate`
Alle Tools (`inmemdb_import`, `inmemdb_graph`, `inmemdb_discover`) kapseln den kompletten Lauf
inkl. Options-Parsing: Auch Nicht-`memdb::Error`-Exceptions (z. B. `std::stod`/`std::stoull` auf
`--batch xyz`) werden als `Fehler: …` auf stderr mit **Exit-Code 1** gemeldet – kein
`terminate`/Stacktrace. Ungültige Optionen ohne Wert → Exit-Code 2 mit Aufruf-Hinweis.

## Metriken
Die beiden neuen Zähler sind in `CollectionMetrics` / `DatabaseMetrics` sichtbar
(`docs/metrics.md`): `evalErrors`, `eventErrors` (per `resetMetrics()` zurücksetzbar).

## Tests
`tests/test_error_handling.cpp` (32 Checks): throwender Observer (OP gelingt, `eventErrors`,
alle Observer laufen), throwende DSL-Funktion im Scan (kein Abbruch, `evalErrors`,
`update` ohne Wirkung), ungültiges `matches`-Pattern wirft am Query-Bau, `loadJson`/`fromJson`
bleiben bei kaputtem Element unverändert, echte Fehler (Unique/Schema/fehlende Collection/
falsches Passwort) werfen weiterhin, `Expression::evaluate` bleibt throwend.
