# Qt/qmake integration (FA-23) – v0.16

| Field | Value |
|---|---|
| Status | implemented (v0.16, optional) |
| Reference | requirements FA-23, NFA-06 (no mandatory dependencies) |

InMemDB itself needs **no Qt** – it is a pure C++17 library. For Qt projects with
**qmake** there are two files in the repo root:

- **`InMemDB.pro`** – builds the static library with qmake alone: `qmake InMemDB.pro && make` →
  `libInMemDB.a` in the repo root (`CONFIG -= qt`, i.e. without a Qt dependency;
  `CONFIG += staticlib c++17`).
- **`InMemDB.pri`** – included into your own `.pro` and links InMemDB in.

## Variant A: link the prebuilt library (recommended for larger projects)

```sh
# once in the InMemDB directory:
qmake InMemDB.pro && make -j        # -> libInMemDB.a
```

```qmake
# my-app.pro
QT += core widgets        # as usual
CONFIG += c++17
INMEMDB_DIR = /path/to/InMemDB
# INMEMDB_BUILD_DIR = /path/to/build   # if a shadow build of the lib lives elsewhere
include($$INMEMDB_DIR/InMemDB.pri)
SOURCES += main.cpp
```

The `.pri` sets `INCLUDEPATH += $$INMEMDB_DIR/include`, `CONFIG += c++17` and
`LIBS += -L$$INMEMDB_BUILD_DIR -lInMemDB`. On Windows/MSVC use
`LIBS += $$INMEMDB_BUILD_DIR/InMemDB.lib` instead of `-l`.

## Variant B: compile the sources directly (no pre-build)

```qmake
# my-app.pro
DEFINES += INMEMDB_BUILD_SOURCES     # set BEFORE the include!
INMEMDB_DIR = /path/to/InMemDB
include($$INMEMDB_DIR/InMemDB.pri)
SOURCES += main.cpp
```

The `.pri` then appends all `src/*.cpp` (except the optional SQLite module) to `SOURCES`.

## Optional SQLite module (FA-21)

- Library: `qmake "CONFIG+=inmemdb_sqlite" InMemDB.pro && make`
- Integration: `DEFINES += INMEMDB_HAS_SQLITE` and `LIBS += -lsqlite3`
  (with variant B additionally `SOURCES += $$INMEMDB_DIR/src/import_sqlite.cpp`).

## Qt model adapter (v0.23) – without hard dependencies

`include/memdb/qt/memdb_qt_model.hpp` provides `memdb::qt::MemDbTableModel` – a
`QAbstractTableModel` over a `memdb::Collection` (**QtCore only**, no widgets needed):

- Rows = documents (all, or filtered via DSL query `setQuery("age >= 18")`),
  columns = sorted union of all field names (schema-less: new fields appear as a column
  on the next `refresh()`).
- `setData()` writes back (`Collection::updateById`); collection changes (observer,
  also after tx commit) trigger `refresh()` via a queued Qt connection (thread-safe).

**No hard dependency:** the adapter is header-only and is only compiled when
`INMEMDB_WITH_QT` is defined. `InMemDB.pri` sets this define automatically as soon as the
including qmake project has `QT += core` (and adds the header to `HEADERS` for moc).
Without Qt (`CONFIG -= qt`, e.g. `InMemDB.pro`) InMemDB stays fully Qt-free; the header
protects itself with `#error`.

```qmake
# my-app.pro
QT += core gui widgets
INMEMDB_DIR = /path/to/InMemDB
include($$INMEMDB_DIR/InMemDB.pri)   # sets INMEMDB_WITH_QT + moc header automatically
SOURCES += main.cpp
```

```cpp
#include <memdb/qt/memdb_qt_model.hpp>
memdb::qt::MemDbTableModel model(users);   // collection handle
model.setQuery("age >= 18");               // optional DSL filter
tableView.setModel(&model);
// users.update("age >= 18", "age = age + 1");  // v0.21 – model refreshes automatically
```

Example: `examples/qt_table/` (QTableView + DSL filter + bulk-update button), headless
runnable: `QT_QPA_PLATFORM=offscreen ./qt_table`. CI builds and smoke-tests it in the
**qmake/Qt integration** job.

## Example

`examples/qt_hello/` (Qt console app, uses C++ API + string DSL + DbGraph):

```sh
mkdir build && cd build
qmake ../examples/qt_hello/hello.pro && make -j && ./qt_hello
```

CI builds both variants (A: prebuilt lib, B: compile sources directly) in the
**qmake/Qt integration (FA-23)** job.

## Notes

- CMake remains the primary build (tests, tools, benchmarks); qmake is the Qt integration
  track. Both produce the same library.
- `CONFIG += c++17` in the `.pri` – with older Qt versions set
  `QMAKE_CXXFLAGS += -std=c++17` manually if needed.
- Multi-project layouts (subdirs): include `InMemDB.pro` as its own subdirs target and in
  the app target only `include($$INMEMDB_DIR/InMemDB.pri)` with a matching `INMEMDB_BUILD_DIR`.
