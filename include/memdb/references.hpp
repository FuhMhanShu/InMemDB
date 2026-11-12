#pragma once

#include "collection.hpp"
#include "database.hpp"
#include "document_id.hpp"

#include <optional>
#include <string>
#include <vector>

namespace memdb {

struct Reference {
    std::string fromCollection;
    std::optional<DocumentId> fromId;
    std::string toCollection;
    std::optional<DocumentId> toId;
    std::string relation;

    bool operator==(const Reference& other) const {
        return fromCollection == other.fromCollection && fromId == other.fromId &&
               toCollection == other.toCollection && toId == other.toId && relation == other.relation;
    }
    bool operator!=(const Reference& other) const { return !(*this == other); }
};

enum class RefDirection { Outgoing, Incoming, Both };

class ReferenceIndex {
public:
    explicit ReferenceIndex(Database& db, std::string storageCollection = "__references");

    const std::string& storageCollection() const { return storage_; }

    Reference link(const std::string& fromCollection, std::optional<DocumentId> fromId,
                   const std::string& toCollection, std::optional<DocumentId> toId,
                   std::string relation = {});
    bool unlink(const std::string& fromCollection, std::optional<DocumentId> fromId,
                const std::string& toCollection, std::optional<DocumentId> toId,
                const std::string& relation = {});

    std::vector<Reference> references(const std::string& collection, RefDirection dir,
                                      std::optional<DocumentId> id = std::nullopt) const;

    std::vector<Reference> dangling() const;
    std::size_t pruneDangling();

    std::string resolve(const std::string& fromCollection, const std::string& relPath) const;

private:
    Database* db_;
    std::string storage_;
    Collection coll_;
};

} // namespace memdb
