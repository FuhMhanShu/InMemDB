# Härtung des Frameworks – Übersicht aller Schutzmechanismen (Stand v0.38)

| Feld | Wert |
|---|---|
| Status | aktiv gepflegt; letzte Ausbaustufen v0.34 (RegEx), v0.35 (Exception-Policy), v0.36 (MessageHandler), v0.37 (JSON), v0.38 (DSL/BSON/Zahlen), v0.39 (Collection-Limits) |
| Prinzip | **nie crashen/hängen** an externer oder pathologischer Eingabe – stattdessen typisierte Exceptions (`JsonError`, `QueryError`, `Error`, …) mit Position/Offset; wo der Flow weiterlaufen soll, wird abgefangen + gezählt/gemeldet |

## 1. Harte Limits gegen pathologische Eingaben (Crash-/Hang-Schutz)

| Komponente | Limit | Reaktion |
|---|---|---|
| Mini-RegEx (`src/regex.cpp`, v0.34) | Pattern ≤ 4096 Zeichen; Verschachtelung ≤ 256; NFA ≤ 50000 Anweisungen; Match ≤ 100 Mio. Schritte | `std::invalid_argument` (Pattern, mit Position) bzw. `std::runtime_error` (Match-Budget) |
| JSON-Parser/Writer (`src/json.cpp`, v0.37) | Verschachtelung ≤ 1000 (Parser, Schema-Parser, Writer) | `JsonError „Verschachtelung zu tief“` (vorher Segfault ab ~50000 `[`) |
| DSL-Parser (`src/query_dsl.cpp`, v0.38) | Verschachtelung ≤ 256 (`(` und `!`) | `QueryError` mit Position (vorher Segfault ab ~50000 Klammern) |
| BSON-Import (`src/import_bson.cpp`, v0.38) | Verschachtelung ≤ 1000; alle Längen gegen Dateiende geprüft (`need()`) | `Error „BSON: Verschachtelung zu tief“` (vorher Segfault ab ~20000) |
| Spill-Read (Hybrid, v0.18) | Record-Länge ≤ 64 MiB, ID muss passen, JSON muss Dokument sein | `std::nullopt` (Doc wird als fehlend behandelt) |
| Verschlüsselte Snapshots (FA-19) | Magic + PBKDF2-Iterationen ≤ 10 Mio. aus Datei; HMAC via `constantTimeEqual` (kein Timing-Leak) | `CryptoError` |
| JSON-Zahlen | > 63 Zeichen → Fehler; Hex-Float-Fallback nur mit `0x`; locale-unabhängig (`std::from_chars`) | `JsonError` |
| DSL-Zahlen (v0.38) | strikte `from_chars`-Prüfung; int64-Überlauf → double | `QueryError` (vorher: „1.2.3“/„1e“ stillschweigend 0) |

## 2. Exception-Policy (v0.35, `docs/de/internals/error-handling.md`)
Exceptions bleiben für **echte Fehler** sichtbar (Unique/Schema, fehlende Collection/Doc, Query-Bau, Crypto/IO). Abgefangen wird nur dort, wo sonst mitten in einer Operation abgebrochen würde:
- **Query-Scan pro Dokument** (`safeEval`): Eval-Fehler → Dokument = Nicht-Treffer, Zähler `metrics().evalErrors`
- **Observer** (`fireEvents`): throwende Callback bricht Schreib-OP und restliche Observer nicht ab, Zähler `metrics().eventErrors`
- **Bulk-JSON-Load** (`loadJson`/`fromJson`): validiert alle Elemente **vor** der Mutation → kein Partial State, `JsonError` bleibt
- **CLI-Tools**: kompletter Lauf in try/catch → `Fehler: …` + Exit 1 statt `terminate`

## 3. MessageHandler (v0.36, `include/memdb/message_handler.hpp`)
Singleton, thread-safe; eigene Handler via `subscribe()`. Die Abfang-Stellen melden dorthin: `query-eval`, `observer`, `load`, `import`, `regex`, `cli`. Level Debug/Info/Warning/Error, Filter `setMinLevel`, Zähler `count()/totalCount()/handlerErrors()` (throwende Handler werden selbst abgefangen).

## 4. Wie testen wir das?
- **Attack-Probes** (vor jedem Fix reproduziert): tiefe Verschachtelung, verschachtelte `{n,m}`, Alternationsketten, Zyklus-Values, kaputte `$binary`-Marker, Zufalls-Strings
- **Fuzz-lite** in den Testsuiten (deterministischer Seed): `test_regex.cpp` (2000 Pattern), `test_json_hardening.cpp` (4000 Strings), `test_hardening.cpp` (3000 DSL-Strings) — nie Crash, nur typisierte Exceptions
- **ASan/UBSan** auf den Härtungs-Testsuiten clean
- **Fault-Injection** (Empfehlung für Persistenz/WAL, s. Ausblick): Datei an jedem Byte-Offset truncaten/korruptieren → Recovery muss sauber fehlschlagen

## 5. Tests
`tests/test_regex.cpp` (5921 Checks) · `test_json_hardening.cpp` (43) · `test_hardening.cpp` (13) · `test_error_handling.cpp` (32) · `test_message_handler.cpp` (30) – plus die Regressionssuiten (insg. 40 CTest-Einträge).

## 1b. Ressourcen-Limits pro Collection (v0.39, FA-28, `docs/de/guides/limits.md`)

Opt-in via `Collection::setLimits(CollectionLimits)` – Default unbegrenzt (0):
`maxDocuments`, `maxDocumentBytes` (JSON-Größe), `maxFieldsPerDocument`, `maxIndexes`.
Verstoß → `LimitError` **vor der Mutation** (Bulk atomar), Zähler `metrics().limitViolations`.
Faustregeln: `maxDocuments` ≈ RAM/ØDocgröße, `maxDocumentBytes` 1–16 MiB, Felder ~1000, Indizes ~32–64.
RAM-Schutz für „größer als RAM“ bleibt der Spill-Modus (v0.18).

## 6. Analyse-Pipeline (FA-29 bis FA-32)

Die Analyse-Module sind bewusst **rechnerisch gehärtet** statt mit Exceptions – Analyseauf
pathologischen Daten muss ein Ergebnis liefern, kein Crash und kein `inf`/`nan` im Report:

| Baustein | Härtung |
|---|---|
| Pearson-Korrelation (FA-30/31/32) | zweistufig **zentriert** und in `long double`; die Summenformel (`n·Sxy − Sx·Sy`) verliert bei großen Werten mit kleinem Variationsbereich durch Auslöschung massiv an Genauigkeit; Wert-Slots konsistent zum sortierten Feldpaar (wechselnde Feldreihenfolge je Dokument); Ergebnis auf [−1, 1] geklemmt |
| Momente (Mittel/Stdabw./Schiefe/Kurtosis, FA-30) | Akkumulation in `long double` (kein Überlauf bei Extremwerten), Finite-Check beim Zurückschreiben – Reports bleiben gültiges JSON |
| Histogramm (FA-30) | nicht endliche Spannweite (±DBL_MAX) → alle Werte in Bin 0 statt `static_cast` von NaN |
| R²-Regression (FA-31/32) | Gauss-Solver mit Teilpivotisierung + **Finite-Check** der Lösung; **mittelwertzentrierte** Normalgleichungen ohne Intercept-Spalte – große Offsets (z. B. 1e12) würden die kleine Restvarianz sonst im Rundungsrauschen ertrinken lassen; `SST`-Checks; `R²` nur endlich oder `rSquaredComputed = false` |
| Einfluss-Score (FA-31/32) | `influenceScore` per Finite-Guard auf 0 – NaN würde sonst die `std::sort`-Ordnung (strict weak ordering) brechen |
| Join-Quellen (FA-32) | hängende Kanten (gelöschte Dokumente) zählen nicht in `rows`/`matchedA`/Join-Abdeckung; `__references`-Einträge mit negativen, nicht ganzzahligen oder NaN-IDs werden ignoriert |
| Feld-Filter (FA-30) | Felder ohne numerische Werte werden auch bei `minValuesPerField = 0` übersprungen (kein Zugriff auf leere Vektoren) |

Regressionstests: `test_numeric_analysis.cpp` (112 Checks), `test_influence.cpp` (130 Checks),
`test_cross_influence.cpp` (193 Checks), `test_character.cpp` (62 Checks) – jeweils mit
Extremwert-, Offset- und Guardszenarien.
