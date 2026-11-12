#pragma once

#include "cursor.hpp"
#include "document.hpp"
#include "query.hpp"

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace memdb {

class Database;

class Collection {
public:
    Collection(const Collection&) = default;
    Collection(Collection&&) noexcept = default;
    Collection& operator=(const Collection&) = default;
    Collection& operator=(Collection&&) noexcept = default;

    std::string name() const;

    DocumentId insert(Document doc);
    DocumentId insert(std::initializer_list<std::pair<const std::string, Value>> fields);
    std::vector<DocumentId> insertBulk(std::vector<Document> docs);

    std::optional<Document> findById(DocumentId id) const;
    Cursor find(const Expression& q = Expression{}) const;
    std::optional<Document> findOne(const Expression& q = Expression{}) const;
    std::size_t count(const Expression& q = Expression{}) const;

    std::size_t update(const Expression& q, const std::function<void(Document&)>& mutator);
    bool updateById(DocumentId id, const std::function<void(Document&)>& mutator);
    bool replace(DocumentId id, Document doc);

    std::size_t remove(const Expression& q = Expression{});
    bool removeById(DocumentId id);

    std::size_t size() const;

private:
    struct Impl;
    explicit Collection(std::shared_ptr<Impl> impl);
    static Collection create(std::string name);
    std::shared_ptr<Impl> impl_;

    friend class Database;
};

} // namespace memdb
