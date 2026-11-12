#pragma once

#include "collection.hpp"

#include <memory>
#include <string>
#include <vector>

namespace memdb {

class Database {
public:
    explicit Database(std::string name = {});
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;

    const std::string& name() const;

    Collection createCollection(const std::string& name);
    Collection collection(const std::string& name) const;
    bool hasCollection(const std::string& name) const;
    std::vector<std::string> collectionNames() const;
    bool dropCollection(const std::string& name);
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace memdb
