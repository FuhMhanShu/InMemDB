#pragma once

#include "import.hpp"

#include <memory>

namespace memdb {

/// @brief ImportSource für InMemDB-JSON-Snapshots (FA-14, `Database::toJson` /
/// `saveToFile`), ohne externe Abhängigkeiten (interner JSON-Parser).
///
/// Übernimmt pro Collection aus den Snapshot-Metadaten:
/// - Spalten + Typen + `notNull` aus dem FA-20-Schema (falls vorhanden),
/// - Indizes (`indexes`: `{field, unique}`),
/// - Dokumente (`documents`).
/// Ohne Schema werden die Spalten aus der Vereinigung der Dokument-Keys inferiert.
/// Dokument-IDs werden nicht übernommen (beim Import neu vergeben).
///
/// Registriert als `snapshot` (Endung `.inmemdb.json`; längster Suffix gewinnt
/// vor `.json`).
class InmemdbSnapshotSource : public ImportSource {
public:
    explicit InmemdbSnapshotSource(const std::string& path);
    ~InmemdbSnapshotSource() override;
    InmemdbSnapshotSource(const InmemdbSnapshotSource&) = delete;
    InmemdbSnapshotSource& operator=(const InmemdbSnapshotSource&) = delete;

    std::vector<TableInfo> tables() const override;
    void readRows(const std::string& table,
                  const std::function<void(const std::vector<Value>&)>& rowCb) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace memdb
