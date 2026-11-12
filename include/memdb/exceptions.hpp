#pragma once

#include <stdexcept>
#include <string>

namespace memdb {

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class TypeError : public Error {
public:
    using Error::Error;
};

class CollectionExistsError : public Error {
public:
    using Error::Error;
};

class CollectionNotFoundError : public Error {
public:
    using Error::Error;
};

class DocumentNotFoundError : public Error {
public:
    using Error::Error;
};

class QueryError : public Error {
public:
    using Error::Error;
};

class IndexExistsError : public Error {
public:
    using Error::Error;
};

class UniqueConstraintError : public Error {
public:
    using Error::Error;
};

class JsonError : public Error {
public:
    using Error::Error;
};

} // namespace memdb
