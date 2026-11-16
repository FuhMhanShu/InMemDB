# Numerische Metadaten-Analyse (FA-30) – API-Referenz

| Feld | Wert |
|---|---|
| Status | umgesetzt – **ohne externe Abhängigkeiten** (nur InMemDB + C++17) |
| Bezug | Anforderungen FA-30 (Kann); Kategorie-1-Pipeline zur Datencharakter-Kategorisierung (FA-29) |
| Modul | `include/memdb/numeric_analysis.hpp` + `src/numeric_analysis.cpp` |

Das Modul interpretiert **rein numerische Datenfelder** statistisch und leitet daraus
Metadaten ab, die im Dokument selbst nicht stehen: Skalentyp, Rundungs-Raster,
Sequenz-Charakter (ID-artig?), Benford-Konformität, Verteilungsform und lineare
Zusammenhänge zwischen Feldern. Es ist die Analyse-Pipeline für Collections der
Kategorie `DataCategory::Numeric` (FA-29).

## Funktionen

```cpp
#include <memdb/memdb.hpp>
#include <memdb/numeric_analysis.hpp>

memdb::NumericAnalysisOptions opts;
//   sampleSize/seed/maxFieldsPerCollection  – Schutz & reproduktives Sampling
//   minValuesPerField (5)      – Felder mit weniger numerischen Werten werden übersprungen
//   histogramBins (10)         – 0 = kein Histogramm
//   discreteMaxRange (100.0)  – diskret, wenn alle Werte ganzzahlig UND Spannweite <= X
//   gridMinRatio (0.95)        – Anteil der Werte exakt auf dem Rundungs-Raster
//   benfordMinSamples (30)    – Mindestwerte für die Benford-Analyse
//   benfordMadCritical (0.015)– MAD-Schwelle erste Ziffer (Nigrini)
//   withCorrelations (true)    – Pearson pro numerischem Feldpaar

// Alle Feld-Analysen einer Collection
auto fields = memdb::analyzeNumericFields(coll, opts);

// Komplett: Feld-Analysen + paarweise Pearson-Korrelationen
memdb::NumericCollectionAnalysis res = memdb::analyzeNumeric(coll, opts);
//   res.numericFieldRatio     – Anteil numerischer Felder
//   res.correlations[i]       – {fieldX, fieldY, pearson, pairs}
// Mehrere Collections (auch database-übergreifend; __references wird ignoriert)
auto all = memdb::analyzeNumeric(db, opts);

// Reports
std::string j  = memdb::toJson(fields[0]); // eine Feld-Analyse
std::string j2 = memdb::toJson(res);      // ganze Collection
std::string md = memdb::toMarkdown(res);   // Tabellen inkl. Korrelationen
```

## Was wird pro Feld analysiert (`NumericFieldAnalysis`)?

| Merkmal | Bedeutung |
|---|---|
| `count`, `distinct`, `min`, `max`, `mean`, `stddev` | Basis-Kennzahlen (Populations-Stdabw.) |
| `q1`, `median`, `q3`, `iqr` | Quantile per linearer Interpolation |
| `skewness`, `kurtosis` | Momente-Schätzer; Kurtosis als **Exzess** (Normalverteilung = 0) |
| `integralRatio`, `discrete` | Anteil ganzzahliger Werte; diskret/ordinal, wenn alle ganzzahlig und Spannweite <= `discreteMaxRange` |
| `hasGrid`, `grid`, `gridRatio` | größtes Rundungs-Raster (Kandidaten 100000 … 0.0001), auf dem >= `gridMinRatio` der Werte liegen; Preis-/Cent-Raster. Rein ganzzahlige Felder melden kein Raster <= 1 |
| `sequenceAnalyzed`, `monotonic`, `consecutive`, `stepMedian`, `gaps` | Sequenz-Charakter in **Einfügereihenfolge** (via DocumentId): surrogat-ID-artig (Schritt 1, lückenlos), monoton mit medianer Schrittweite und Lückenzählung. Nur im Vollscan (Sampling zerstört die Reihenfolge) |
| `benfordApplicable`, `benfordSuspicious`, `benfordMad`, `firstDigitFreq` | Benford-Analyse der ersten signifikanten Ziffer (MAD gegen Benford-Verteilung, Schwelle 0.015 nach Nigrini); typischer Fälschungs-/Qualitätsindikator bei Beträgen |
| `histogram`, `binMin`, `binWidth` | äquidistantes Histogramm über min..max |
| `note` | Interpretations-Hinweise auf Deutsch (z. B. "diskret …; sequenzielle ID (Schritt 1, keine Lücken); Raster 100 …") |

## Details

- Read-only; ein Scan pro Collection. Sequenz-Analysen rekonstruieren die
  Einfügereihenfolge über die DocumentId (IDs sind sequenziell vergeben), da die
  Dokument-Map unsortiert iteriert.
- Korrelationen benötigen mind. 3 gemeinsame Dokumente pro Paar; konstante Felder
  (Varianz 0) werden ausgelassen.
- Auffälligkeiten sind Hinweise, keine Beweise – Benford und Raster reagieren
  empfindlich auf kleine Wertemengen.
- Tests: `tests/test_numeric_analysis.cpp` (81 Checks).

Siehe auch: [character.md](character.md) für die Vorab-Kategorisierung und
[discovery.md](discovery.md) für Ausreißer (z-Score/IQR) und Datenprofile.
