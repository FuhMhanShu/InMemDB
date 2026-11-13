#pragma once

#include <stdexcept>
#include <string>

namespace memdb {

/// @brief Basisklasse aller InMemDB-Fehler.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// @brief Falscher Werttyp (z. B. `asString()` auf einer Zahl).
class TypeError : public Error {
public:
    using Error::Error;
};

/// @brief `Database::createCollection` mit bereits vorhandener Collection.
class CollectionExistsError : public Error {
public:
    using Error::Error;
};

/// @brief Zugriff auf nicht vorhandene Collection.
class CollectionNotFoundError : public Error {
public:
    using Error::Error;
};

/// @brief Zugriff auf nicht vorhandenes Dokument.
class DocumentNotFoundError : public Error {
public:
    using Error::Error;
};

/// @brief Fehler beim Bauen/Auswerten einer Query (z. B. Operator auf Nicht-Feld).
class QueryError : public Error {
public:
    using Error::Error;
};

/// @brief `createIndex` auf bereits indexiertem Feld.
class IndexExistsError : public Error {
public:
    using Error::Error;
};

/// @brief Verletzung eines Unique-Index.
class UniqueConstraintError : public Error {
public:
    using Error::Error;
};

/// @brief Fehler beim JSON-Parsen oder bei ungültiger JSON-Struktur.
class JsonError : public Error {
public:
    using Error::Error;
};

/// @brief Allgemeiner Fehler des Referenz-Moduls (FA-18).
class ReferenceError : public Error {
public:
    using Error::Error;
};

/// @brief Referenz existiert bereits (Duplikat bei `ReferenceIndex::link`).
class ReferenceExistsError : public Error {
public:
    using Error::Error;
};

/// @brief Kryptografie-/Snapshot-Fehler (FA-19): falsches Passwort, beschädigte
/// oder ungültige verschlüsselte Datei, leeres Passwort.
class CryptoError : public Error {
public:
    using Error::Error;
};

/// @brief Schema-Verletzung (FA-20): undefiniertes Feld oder Typ-Mismatch.
class SchemaError : public Error {
public:
    using Error::Error;
};

} // namespace memdb
