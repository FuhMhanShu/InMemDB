#pragma once

#include "document.hpp"
#include "document_id.hpp"

#include <functional>
#include <string>

namespace memdb {

enum class ChangeType { Insert, Update, Replace, Remove, Clear };

struct ChangeEvent {
    ChangeType type;
    std::string collection;
    DocumentId id;
    Document document;
};

using ChangeCallback = std::function<void(const ChangeEvent&)>;

} // namespace memdb
