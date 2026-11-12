#include "memdb/document_id.hpp"

namespace memdb {

std::string toString(DocumentId id) { return std::to_string(id.value()); }

} // namespace memdb
