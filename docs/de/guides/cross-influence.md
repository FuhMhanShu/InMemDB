# Cross-Collection-Einfluss-Analyse (FA-32) – API-Referenz

| Feld | Wert |
|---|---|
| Status | umgesetzt – **ohne externe Abhängigkeiten** (nur InMemDB + C++17) |
| Bezug | Anforderungen FA-32 (Kann); Fortsetzung der Einfluss-Analyse (FA-31) |
| Modul | `include/memdb/cross_influence.hpp` + `src/cross_influence.cpp` |

Das Modul beantwortet die Frage: **Wie berechnet man Abhängigkeiten über zwei
Collections hinweg?** Es verknüpft Collection-Paare per Join und überträgt die
FA-31-Maschinerie (Pearson-Hub, multiples R², funktionale Abhängigkeiten,
Rollen, Einfluss-Score) auf die verknüpfte Zeilenmenge. Read-only; die
Ergebnis-Feldnamen sind collection-qualifiziert (`collection.feld`).

## Join-Quellen (beide kombinierbar)

1. **Deklarierte Referenzen** (`origin: "declared"`): Dokument-Kanten aus
   `__references` (FA-18), die beide Enden als Feldwert tragen. Die
   (fromCollection, toCollection)-Paare werden je Relation gesammelt und
   zu deduplizierten (aId, bId)-Paaren gejoint. Selbst-Referenzen werden
   übersprungen (dafür zuständig ist FA-31).
2. **Entdeckte Fremdschlüssel** (`origin: "discovered"`):
   `discoverForeignKeys` (FA-27) über alle Collections; nur Relationen der Art
   Fremdschlüssel mit Confidence >= `minFkConfidence` (Standard 0.9) und
   unterschiedlichen Collections. Der Join läuft über eine Hash-Map auf dem
   Zielfeld in B.

## Ablauf je Collection-Paar

- **Join**: A × B über die Kanten; `rows` ist die Zahl verknüpfter Zeilen
  (bei 1:n aufgebläht), `matchedA` die Zahl der A-Dokumente mit mindestens
  einem Partner, `joinCoverage = matchedA / docsA`.
- **Kardinalität** aus A-Sicht: `n:1` (Standard) oder `1:n`, sobald ein
  A-Dokument mehrere B-Partner besitzt.
- **Guards**: `rows < minJoinPairs` (Standard 10) oder
  `joinCoverage < minJoinCoverage` (Standard 0, aus) – das Paar wird
  übersprungen und in `note` genannt.
- **Analyse** auf der verknüpften Menge: Pearson-Paare, funktionale
  Abhängigkeiten (beide Richtungen, Richtungstiebreak über distinct
  Quell-Werte) und multiples R² mit Ridge-Term, wie in FA-31.
- **Triviale Join-Abhängigkeit**: das Paar (A.joinField, B.keyField) wäre per
  Definition (fast) funktional und korreliert. Mit
  `suppressTrivialJoinDependency` (Standard an) wird es aus Pearson, FD und
  R²-Prädiktoren ausgeblendet.
- `dependencies` listet nur Abhängigkeiten mit Quelle und Ziel in
  unterschiedlichen Collections; die Rollen zählen alle Abhängigkeiten der
  verknüpften Menge.

## Optionen (CrossInfluenceOptions)

| Option | Default | Bedeutung |
|---|---|---|
| `influence` | `{}` | FA-31-Schwellwerte + numerischer Basis-Scan (`influence.numeric`) |
| `includeDeclared` | `true` | deklarierte `__references`-Kanten als Join-Quelle |
| `includeDiscovered` | `true` | entdeckte Fremdschlüssel als Join-Quelle |
| `minFkConfidence` | `0.9` | Mindest-Confidence der Discovery-FKs |
| `minJoinPairs` | `10` | Mindest-Zeilen je Paar-Analyse |
| `minJoinCoverage` | `0.0` | Mindest-Anteil verknüpfter A-Dokumente |
| `suppressTrivialJoinDependency` | `true` | Join-Feld-Paar aus Evidenz ausblenden |

## Funktionen

```cpp
#include <memdb/memdb.hpp>
#include <memdb/cross_influence.hpp>

memdb::CrossInfluenceOptions opts;
//   opts.influence            – InfluenceOptions (FA-31): Schwellwerte,
//                                Feld-Kappung via influence.numeric.maxFieldsPerCollection
//   opts.minJoinPairs (10)    – weniger Zeilen => Paar wird uebersprungen
//   opts.minFkConfidence (0.9)– Mindest-Confidence entdeckter Fremdschluessel

memdb::CrossInfluenceResult res = memdb::analyzeCrossInfluence(db, opts);
//   res.pairs[i].origin       – "declared" | "discovered"
//   res.pairs[i].relation     – Relationsname bzw. Join-Beschreibung
//   res.pairs[i].collectionA / collectionB – Collection-Paar (A-Sicht)
//   res.pairs[i].docsA / matchedA / rows    – Join-Statistik
//   res.pairs[i].joinCoverage – matchedA / docsA
//   res.pairs[i].cardinality  – "n:1" | "1:n" (aus A-Sicht)
//   res.pairs[i].fields[j]    – FieldInfluence (FA-31) auf der Join-Menge,
//                                Feldnamen "collection.feld", nach Score fallend
//   res.pairs[i].dependencies – CrossDependency { source, target, coverage, ... }
//   res.pairs[i].topDriver    – "collection.feld" mit hoechstem Einfluss-Score
//   res.pairs[i].note         – Kurzfassung (Deutsch)
//   res.note                  – Zusammenfassung: Paare, Quellen, Spruenge

// Reports
std::string json = memdb::toJson(res);      // maschinenlesbar / LLM-freundlich
std::string md   = memdb::toMarkdown(res);  // Tabellen je Collection-Paar
```

## Grenzen

Korrelation belegt auch hier keine Kausalität; die Ergebnisse sind begründete
Rangfolgen aus statistischer Dominanz und struktureller Ableitung auf der
verknüpften Menge. Entdeckte Joins hängen an der Qualität der FK-Discovery
(Werte-Überlappung); bei geringer Join-Abdeckung beschreibt das Ergebnis nur
den verknüpften Teil der Daten. Kein Sampling: die Zeilenzahl wird über
`minJoinPairs` und die Feld-Kappung begrenzt.

## Tests

`tests/test_cross_influence.cpp` (180 Checks): deklarierte und entdeckte
Joins, Rollen/Treiber auf der Join-Menge, Trivial-Suppression, Kardinalität
1:n, Guards (`minJoinPairs`, `minJoinCoverage`), Selbst-Referenz-Skip,
Konfidenz-Schwelle, Mischbetrieb beider Quellen, leere Datenbank sowie
JSON-/Markdown-Reports.
