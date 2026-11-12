#pragma once

#include "aggregate.hpp"
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

namespace detail {
struct CollectionState;
}

class Transaction {
public:
    Transaction(Transaction&&) noexcept;
    Transaction& operator=(Transaction&&) noexcept;
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    DocumentId insert(Document doc);
    DocumentId insert(std::initializer_list<std::pair<const std::string, Value>> fields);
    std::size_t update(const Expression& q, const std::function<void(Document&)>& mutator);
    bool updateById(DocumentId id, const std::function<void(Document&)>& mutator);
    bool replace(DocumentId id, Document doc);
    DocumentId upsertById(DocumentId id, Document doc);
    DocumentId upsert(const Expression& q, Document doc);
    std::size_t remove(const Expression& q = Expression{});
    bool removeById(DocumentId id);

    Cursor find(const Expression& q = Expression{}) const;
    std::optional<Document> findById(DocumentId id) const;
    std::size_t count(const Expression& q = Expression{}) const;
    std::size_t size() const;

    std::optional<Value> aggregate(AggOp op, const std::string& fieldPath,
                                    const Expression& q = Expression{}) const;
    std::vector<GroupAggregate> groupBy(const std::string& keyPath, const Expression& q = Expression{},
                                         AggOp op = AggOp::Count, const std::string& aggPath = "") const;

    void commit();
    void rollback();
    bool isActive() const;

private:
    struct Impl;
    explicit Transaction(std::shared_ptr<detail::CollectionState> state);
    std::unique_ptr<Impl> impl_;

    friend class Collection;
};

} // namespace memdb
