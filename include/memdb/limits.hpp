#pragma once

#include <cstddef>

namespace memdb {

/// @brief Optionale Limits einer Collection (v0.39). Default: alles 0 = **unbegrenzt**
/// (InMemDB bleibt im Kern unbegrenzt; RAM-Schutz läuft über Hybrid/Spill, v0.18).
///
/// Wird ein Limit gesetzt und überschritten, werfen `insert`/`insertBulk`/`loadJson`/
/// `update`/`replace`/`upsert`/`createIndex` `LimitError` (vor der Mutation – Bulk-Operationen
/// sind dadurch atomar gegen Limit-Verstöße). Verstöße zählen zusätzlich in
/// `CollectionMetrics::limitViolations`.
///
/// Empfohlene Richtwerte (als Schranke gegen Speicherbomben, nicht als harte Produktregel):
/// - `maxDocuments`: verfügbarer RAM / Ø Dokumentgröße (z. B. 8 GB / ~1 KB ≈ 8 Mio. Docs,
///   mit Headroom eher 1–5 Mio. setzen)
/// - `maxDocumentBytes`: 1–16 MiB (ein einzelnes Riesen-Dokument ist der klassische RAM-Killer)
/// - `maxFieldsPerDocument`: ~1000 (sehr breite schema-lose Dokumente bremsen Scans/Indizes)
/// - `maxIndexes`: ~32–64 (jeder Index kostet RAM und Schreib-Amplifikation)
struct CollectionLimits {
    std::size_t maxDocuments = 0;         ///< max. Dokumentanzahl (RAM + Spill); 0 = unbegrenzt
    std::size_t maxFieldsPerDocument = 0; ///< max. Felder pro Dokument; 0 = unbegrenzt
    std::size_t maxDocumentBytes = 0;     ///< max. serialisierte JSON-Größe pro Dokument; 0 = unbegrenzt
    std::size_t maxIndexes = 0;           ///< max. Anzahl Indizes; 0 = unbegrenzt
};

} // namespace memdb
