#pragma once

#include <string>
#include <utility>

namespace memdb {

class Index {
public:
    Index() = default;
    Index(std::string fieldPath, bool unique) : fieldPath_(std::move(fieldPath)), unique_(unique) {}

    const std::string& field() const { return fieldPath_; }
    bool isUnique() const { return unique_; }

private:
    std::string fieldPath_;
    bool unique_ = false;
};

} // namespace memdb
