#pragma once

#include "aggregate.hpp"
#include "change_event.hpp"
#include "cursor.hpp"
#include "document.hpp"
#include "index.hpp"
#include "query.hpp"
#include "transaction.hpp"

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace memdb {

class Database;

namespace detail {
struct CollectionState;
}

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
    DocumentId upsertById(DocumentId id, Document doc);
    DocumentId upsert(const Expression& q, Document doc);

    std::size_t remove(const Expression& q = Expression{});
    bool removeById(DocumentId id);

    std::size_t size() const;

    Index createIndex(const std::string& fieldPath, bool unique = false);
    bool dropIndex(const std::string& fieldPath);
    bool hasIndex(const std::string& fieldPath) const;
    std::vector<Index> indexes() const;

    std::size_t lastQueryCandidateCount() const;

    Transaction beginTransaction();

    std::string toJson() const;
    void loadJson(std::string_view json);
    void clear();

    std::optional<Value> aggregate(AggOp op, const std::string& fieldPath,
                                    const Expression& q = Expression{}) const;
    std::vector<GroupAggregate> groupBy(const std::string& keyPath, const Expression& q = Expression{},
                                         AggOp op = AggOp::Count, const std::string& aggPath = "") const;

    std::uint64_t onChange(ChangeCallback cb);
    bool offChange(std::uint64_t handle);

private:
    explicit Collection(std::shared_ptr<detail::CollectionState> impl);
    static Collection create(std::string name);
    std::shared_ptr<detail::CollectionState> impl_;

    friend class Database;
    friend class Transaction;
};

} // namespace memdb
