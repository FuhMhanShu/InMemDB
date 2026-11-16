#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace memdb {

/// @brief Eindeutige, typsichere ID eines Dokuments innerhalb einer Collection.
class DocumentId {
public:
    DocumentId() = default;
    explicit DocumentId(std::uint64_t value) : value_(value) {}

    /// @brief Numerischer Wert der ID.
    std::uint64_t value() const { return value_; }

    friend bool operator==(DocumentId a, DocumentId b) { return a.value_ == b.value_; }
    friend bool operator!=(DocumentId a, DocumentId b) { return !(a == b); }
    friend bool operator<(DocumentId a, DocumentId b) { return a.value_ < b.value_; }

private:
    std::uint64_t value_ = 0;
};

/// @brief Dezimale String-Darstellung der ID.
std::string toString(DocumentId id);

} // namespace memdb

namespace std {
template <>
struct hash<memdb::DocumentId> {
    std::size_t operator()(memdb::DocumentId id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value());
    }
};
} // namespace std
