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

/// @brief Atomare Transaktion auf einer Collection (FA-12).
///
/// `Collection::beginTransaction()` hält während der gesamten Transaktion einen
/// Exklusiv-Lock (volle Isolation); Lesezugriffe sehen die eigenen Änderungen
/// (Read-your-own-writes). `commit()` macht die Änderungen dauerhaft und feuert
/// gefangene Observer-Events; `rollback()` – oder der Destruktor ohne commit –
/// stellt den Zustand beim Begin wieder her (alles-oder-nichts).
/// Nach commit/rollback wirft jeder Aufruf `Error`.
class Transaction {
public:
    Transaction(Transaction&&) noexcept;
    Transaction& operator=(Transaction&&) noexcept;
    /// @brief Rollback, falls noch aktiv und nicht committet.
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    /// @brief Wie `Collection::insert` (innerhalb der Transaktion).
    DocumentId insert(Document doc);
    /// @brief Wie `Collection::insert` aus Feldliste.
    DocumentId insert(std::initializer_list<std::pair<const std::string, Value>> fields);
    /// @brief Wie `Collection::update`.
    std::size_t update(const Expression& q, const std::function<void(Document&)>& mutator);
    /// @brief Wie `Collection::updateById`.
    bool updateById(DocumentId id, const std::function<void(Document&)>& mutator);
    /// @brief Wie `Collection::replace`.
    bool replace(DocumentId id, Document doc);
    /// @brief Wie `Collection::upsertById`.
    DocumentId upsertById(DocumentId id, Document doc);
    /// @brief Wie `Collection::upsert`.
    DocumentId upsert(const Expression& q, Document doc);
    /// @brief Wie `Collection::remove`.
    std::size_t remove(const Expression& q = Expression{});
    /// @brief Wie `Collection::removeById`.
    bool removeById(DocumentId id);

    /// @brief Wie `Collection::find` (sieht eigene Änderungen).
    Cursor find(const Expression& q = Expression{}) const;
    /// @brief Wie `Collection::findIds` (ohne Dokument-Kopien).
    std::vector<DocumentId> findIds(const Expression& q = Expression{}) const;
    /// @brief Wie `Collection::forEach` (ohne Dokument-Kopien; Callback read-only).
    std::size_t forEach(const Expression& q, const std::function<void(const Document&)>& cb) const;
    /// @brief Wie `Collection::findById`.
    std::optional<Document> findById(DocumentId id) const;
    /// @brief Wie `Collection::count`.
    std::size_t count(const Expression& q = Expression{}) const;
    /// @brief Wie `Collection::size`.
    std::size_t size() const;

    /// @brief Wie `Collection::aggregate`.
    std::optional<Value> aggregate(AggOp op, const std::string& fieldPath,
                                    const Expression& q = Expression{}) const;
    /// @brief Wie `Collection::groupBy`.
    std::vector<GroupAggregate> groupBy(const std::string& keyPath, const Expression& q = Expression{},
                                         AggOp op = AggOp::Count, const std::string& aggPath = "") const;

    /// @brief Änderungen dauerhaft machen; feuert Observer-Events.
    /// @throws Error wenn die Transaktion nicht mehr aktiv ist.
    void commit();
    /// @brief Änderungen verwerfen (Backup wiederherstellen); Events werden verworfen.
    void rollback();
    /// @brief true zwischen beginTransaction() und commit()/rollback().
    bool isActive() const;

private:
    struct Impl;
    explicit Transaction(std::shared_ptr<detail::CollectionState> state);
    std::unique_ptr<Impl> impl_;

    friend class Collection;
};

} // namespace memdb
