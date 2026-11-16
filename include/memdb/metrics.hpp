#pragma once

#include <cstddef>
#include <cstdint>

namespace memdb {

/// @brief Metrik-/Diagnose-Snapshot einer Collection (v0.22).
///
/// Zaehler werden in `detail::CollectionState` gefuehrt (zentral fuer `Collection`- und
/// `Transaction`-Zugriffe) und ueber `Collection::metrics()` als Kopie gelesen.
/// `docs`/`indexCount`/`last*` sind Ist- bzw. Letztwerte, keine akkumulierten Zaehler.
struct CollectionMetrics {
    std::uint64_t inserts = 0;        ///< eingefügte Dokumente (inkl. insertBulk, Upsert-Neuanlage)
    std::uint64_t updates = 0;        ///< geänderte Dokumente (update/updateById/replace/upsert)
    std::uint64_t removes = 0;        ///< entfernte Dokumente
    std::uint64_t queries = 0;        ///< Lesezugriffe (find/findOne/findIds/forEach/count/aggregate/groupBy)
    std::uint64_t indexQueries = 0;   ///< davon Queries, die einen Index benutzt haben
    std::uint64_t scannedDocs = 0;    ///< Summe der Kandidaten aller Queries (Vollscan-Anteil)
    std::uint64_t changeEvents = 0;   ///< emittierte Change-Events (Observer / Transaktions-Events)
    std::uint64_t spillReads = 0;     ///< Hybrid-Modus: aus Spill-Datei gelesene Dokumente
    std::uint64_t spillWrites = 0;    ///< Hybrid-Modus: in Spill-Datei geschriebene Records
    std::uint64_t txCommits = 0;      ///< abgeschlossene Transaktionen (Commit)
    std::uint64_t txRollbacks = 0;    ///< zurückgerollte Transaktionen (explizit oder per Destruktor)
    std::uint64_t expiredDocs = 0;    ///< TTL/Auto-Expiry (FA-25): abgelaufene und entfernte Dokumente
    std::uint64_t evalErrors = 0;     ///< v0.35: abgefangene Query-Eval-Fehler im Scan (Dokument zaehlte als Nicht-Treffer)
    std::uint64_t eventErrors = 0;    ///< v0.35: abgefangene Observer-Exceptions (Schreib-OP lief weiter)
    std::size_t docs = 0;             ///< aktuelle Dokumentanzahl (RAM + Spill)
    std::size_t indexCount = 0;       ///< Anzahl aktiver Indizes
    std::size_t lastCandidateCount = 0; ///< Kandidaten der letzten Query
    bool lastQueryUsedIndex = false;  ///< hat die letzte Query einen Index benutzt?
    double lastQueryMs = 0.0;         ///< Dauer der letzten Query in Millisekunden
    double totalQueryMs = 0.0;        ///< akkumulierte Query-Dauer in Millisekunden
};

/// @brief Aggregierte Metriken einer Database (v0.22): Summe ueber alle Collections.
struct DatabaseMetrics {
    std::size_t collectionCount = 0;
    std::uint64_t totalDocs = 0;
    CollectionMetrics sum; ///< Aggregation der Zaehler ueber alle Collections
};

} // namespace memdb
