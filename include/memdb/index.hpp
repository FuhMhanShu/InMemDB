#pragma once

#include <string>
#include <utility>

namespace memdb {

/// @brief Handle/Beschreibung eines Sekundärindex einer Collection (FA-10).
///
/// Anlage/Löschung über `Collection::createIndex` / `Collection::dropIndex`;
/// Details siehe `docs/indexes.md`.
class Index {
public:
    Index() = default;
    Index(std::string fieldPath, bool unique) : fieldPath_(std::move(fieldPath)), unique_(unique) {}

    /// @brief Indexierter Feldpfad (Punktnotation möglich).
    const std::string& field() const { return fieldPath_; }
    /// @brief true bei Unique-Index.
    bool isUnique() const { return unique_; }

private:
    std::string fieldPath_;
    bool unique_ = false;
};

} // namespace memdb
