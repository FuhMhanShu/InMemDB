#pragma once

#include "value.hpp"

#include <cstddef>
#include <optional>

namespace memdb {

/// @brief Aggregat-Operationen für `Collection::aggregate` / `groupBy` (FA-13).
enum class AggOp {
    Min,   ///< Kleinster Wert (Zahlen und Strings)
    Max,   ///< Größter Wert (Zahlen und Strings)
    Sum,   ///< Summe (Int-Erhalt, Overflow → Double)
    Avg,   ///< Durchschnitt (immer Double)
    Count, ///< Anzahl der Dokumente
    Distinct ///< v0.27: Array der distinct Werte des Feldes (sortiert; leeres Feld/Null zaehlt nicht)
};

/// @brief Eine Gruppe des `groupBy`-Ergebnisses.
struct GroupAggregate {
    Value key;                   ///< Gruppenschlüssel (Wert an keyPath; fehlend → null)
    std::size_t count = 0;       ///< Dokumente in der Gruppe
    std::optional<Value> value;  ///< Aggregat des zweiten Feldes (nur bei op != Count)
};

} // namespace memdb
