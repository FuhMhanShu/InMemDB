# Qt/qmake-Einbindung (FA-23) – v0.16

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.16, Kann) |
| Bezug | Anforderungen FA-23, NFA-06 (keine Pflichtabhängigkeiten) |

InMemDB selbst braucht **kein Qt** – es ist eine reine C++17-Bibliothek. Für Qt-Projekte mit
**qmake** gibt es zwei Dateien im Repo-Root:

- **`InMemDB.pro`** – baut die statische Library allein per qmake: `qmake InMemDB.pro && make` → `libInMemDB.a` im Repo-Root (`CONFIG -= qt`, also ohne Qt-Abhängigkeit; `CONFIG += staticlib c++17`).
- **`InMemDB.pri`** – wird in die eigene `.pro` inkludiert und bindet InMemDB ein.

## Variante A: vorgebaute Library linken (empfohlen für größere Projekte)

```sh
# einmalig im InMemDB-Verzeichnis:
qmake InMemDB.pro && make -j        # -> libInMemDB.a
```

```qmake
# meine-app.pro
QT += core widgets        # wie gehabt
CONFIG += c++17
INMEMDB_DIR = /pfad/zu/InMemDB
# INMEMDB_BUILD_DIR = /pfad/zum/build   # falls Shadow-Build der Lib woanders liegt
include($$INMEMDB_DIR/InMemDB.pri)
SOURCES += main.cpp
```

Der `.pri` setzt `INCLUDEPATH += $$INMEMDB_DIR/include`, `CONFIG += c++17` und
`LIBS += -L$$INMEMDB_BUILD_DIR -lInMemDB`. Unter Windows/MSVC stattdessen
`LIBS += $$INMEMDB_BUILD_DIR/InMemDB.lib` (statt `-l`).

## Variante B: Quellen direkt mitkompilieren (kein Vorab-Build)

```qmake
# meine-app.pro
DEFINES += INMEMDB_BUILD_SOURCES     # VOR dem include setzen!
INMEMDB_DIR = /pfad/zu/InMemDB
include($$INMEMDB_DIR/InMemDB.pri)
SOURCES += main.cpp
```

Der `.pri` hängt dann alle `src/*.cpp` (außer dem optionalen SQLite-Modul) an `SOURCES`.

## Optionales SQLite-Modul (FA-21)

- Library: `qmake "CONFIG+=inmemdb_sqlite" InMemDB.pro && make`
- Einbindung: `DEFINES += INMEMDB_HAS_SQLITE` und `LIBS += -lsqlite3`
  (bei Variante B zusätzlich `SOURCES += $$INMEMDB_DIR/src/import_sqlite.cpp`).

## Beispiel

`examples/qt_hello/` (Qt-Console-App, nutzt C++-API + String-DSL + DbGraph):

```sh
mkdir build && cd build
qmake ../examples/qt_hello/hello.pro && make -j && ./qt_hello
```

Die CI baut beide Varianten (A: vorgebaute Lib, B: direkt mitkompiliert) im Job
**qmake/Qt-Einbindung (FA-23)**.

## Hinweise

- CMake bleibt der primäre Build (Tests, Tools, Benchmarks); qmake ist die
  Qt-Integrationsschiene. Beide liefern dieselbe Library.
- `CONFIG += c++17` im `.pri` – bei älteren Qt-Versionen ggf. manuell
  `QMAKE_CXXFLAGS += -std=c++17` setzen.
- Multi-Projekt-Layouts (subdirs): `InMemDB.pro` als eigenes Subdirs-Target einbinden und
  im App-Target nur noch `include($$INMEMDB_DIR/InMemDB.pri)` mit passendem `INMEMDB_BUILD_DIR`.
