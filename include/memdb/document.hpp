#pragma once

#include "document_id.hpp"
#include "value.hpp"

#include <cstddef>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace memdb {

class Collection;

class Document {
public:
    Document();
    Document(std::initializer_list<std::pair<const std::string, Value>> fields);
    Document(const Document& other);
    Document(Document&& other) noexcept;
    Document& operator=(const Document& other);
    Document& operator=(Document&& other) noexcept;
    ~Document();

    DocumentId id() const;

    std::size_t fieldCount() const;
    bool contains(std::string_view key) const;

    const Value* find(std::string_view key) const;
    Value& operator[](const std::string& key);
    const Value& at(std::string_view key) const;

    void set(std::string key, Value value);
    bool remove(std::string_view key);
    bool rename(std::string_view from, std::string_view to);

    const Value* findPath(std::string_view dotPath) const;

    std::map<std::string, Value> fields() const;

    bool operator==(const Document& other) const;
    bool operator!=(const Document& other) const { return !(*this == other); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void setId(DocumentId id);

    friend class Collection;
};

} // namespace memdb
