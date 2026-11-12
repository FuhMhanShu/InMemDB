#pragma once

#include "document.hpp"
#include "exceptions.hpp"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace memdb {

class Cursor {
public:
    using iterator = std::vector<Document>::const_iterator;

    Cursor() = default;
    explicit Cursor(std::vector<Document> snapshot) : snapshot_(std::move(snapshot)) {}

    iterator begin() const { return snapshot_.begin(); }
    iterator end() const { return snapshot_.end(); }

    std::size_t size() const { return snapshot_.size(); }
    bool empty() const { return snapshot_.empty(); }

    const Document& front() const {
        if (snapshot_.empty())
            throw Error("front() auf leerem Cursor");
        return snapshot_.front();
    }

private:
    std::vector<Document> snapshot_;
};

} // namespace memdb
