/// @file
/// @brief Umbrella-Header: inkludiert die gesamte öffentliche API von InMemDB.
///
/// ```cpp
/// #include <memdb/memdb.hpp>
/// ```
#pragma once

#include "aggregate.hpp"
#include "change_event.hpp"
#include "collection.hpp"
#include "cursor.hpp"
#include "database.hpp"
#include "document.hpp"
#include "document_id.hpp"
#include "exceptions.hpp"
#include "import.hpp"
#include "import_csv.hpp"
#include "index.hpp"
#include "query.hpp"
#include "references.hpp"
#include "schema.hpp"
#include "transaction.hpp"
#include "value.hpp"

#ifdef INMEMDB_HAS_SQLITE
#include "import_sqlite.hpp"
#endif
