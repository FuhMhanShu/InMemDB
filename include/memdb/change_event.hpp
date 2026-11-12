#pragma once

#include "document.hpp"
#include "document_id.hpp"

#include <functional>
#include <string>

namespace memdb {

/// @brief Art einer Änderung (FA-15, Observer).
enum class ChangeType {
    Insert,  ///< Dokument eingefügt (document = neuer Zustand)
    Update,  ///< Dokument geändert (document = neuer Zustand)
    Replace, ///< Dokument ersetzt (document = neuer Zustand)
    Remove,  ///< Dokument entfernt (document = alter Zustand)
    Clear    ///< Collection geleert (document leer)
};

/// @brief Ein Änderungs-Event, das Observer bei `Collection::onChange` erhalten.
struct ChangeEvent {
    ChangeType type;       ///< Art der Änderung
    std::string collection; ///< Name der betroffenen Collection
    DocumentId id;         ///< Betroffene Dokument-ID (0 bei Clear)
    Document document;     ///< Betroffener Zustand (siehe ChangeType)
};

/// @brief Callback-Typ für `Collection::onChange`.
using ChangeCallback = std::function<void(const ChangeEvent&)>;

} // namespace memdb
