#pragma once

#include <cstdint>
#include <string>

namespace memdb {

class DocumentId {
public:
    DocumentId() = default;
    explicit DocumentId(std::uint64_t value) : value_(value) {}

    std::uint64_t value() const { return value_; }

    friend bool operator==(DocumentId a, DocumentId b) { return a.value_ == b.value_; }
    friend bool operator!=(DocumentId a, DocumentId b) { return !(a == b); }
    friend bool operator<(DocumentId a, DocumentId b) { return a.value_ < b.value_; }

private:
    std::uint64_t value_ = 0;
};

std::string toString(DocumentId id);

} // namespace memdb
