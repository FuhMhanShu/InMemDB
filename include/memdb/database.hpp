#pragma once

#include "collection.hpp"

#include <memory>
#include <string>
#include <string_view>
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

    std::string toJson() const;
    void fromJson(std::string_view json);
    static Database fromJson(std::string_view json, std::string name);
    void saveToFile(const std::string& path) const;
    static Database loadFromFile(const std::string& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace memdb
