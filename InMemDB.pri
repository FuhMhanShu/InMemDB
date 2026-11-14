# InMemDB.pri – InMemDB in ein qmake-Projekt einbinden (FA-23).
#
# Verwendung in deiner .pro-Datei:
#   INMEMDB_DIR = /pfad/zu/InMemDB        # optional; Default = Verzeichnis dieser .pri
#   include($$INMEMDB_DIR/InMemDB.pri)
#
# Zwei Varianten:
#   A) Default: vorgebaute statische Library linken.
#      Library einmalig bauen:  qmake InMemDB.pro && make   (im InMemDB-Verzeichnis)
#      -> libInMemDB.a landet im InMemDB-Root.
#      Build-Verzeichnis der Lib ggf. setzen:  INMEMDB_BUILD_DIR = /pfad/zum/build
#      (Windows/MSVC: LIBS += InMemDB.lib statt -lInMemDB, siehe Doku.)
#   B) Quellen direkt mitkompilieren (kein extra Build-Schritt):
#      DEFINES += INMEMDB_BUILD_SOURCES in deiner .pro VOR dem include() setzen.
#
# Optional SQLite-Import (beide Varianten):
#   DEFINES += INMEMDB_HAS_SQLITE ; LIBS += -lsqlite3
#   und bei Variante B zusätzlich: SOURCES += $$INMEMDB_DIR/src/import_sqlite.cpp

isEmpty(INMEMDB_DIR): INMEMDB_DIR = $$PWD
isEmpty(INMEMDB_BUILD_DIR): INMEMDB_BUILD_DIR = $$INMEMDB_DIR

INCLUDEPATH += $$INMEMDB_DIR/include
CONFIG += c++17

contains(DEFINES, INMEMDB_BUILD_SOURCES) {
    # Variante B: Quellen direkt ins Projekt aufnehmen
    SOURCES += $$files($$INMEMDB_DIR/src/*.cpp, false)
    SOURCES -= $$INMEMDB_DIR/src/import_sqlite.cpp
    contains(DEFINES, INMEMDB_HAS_SQLITE): SOURCES += $$INMEMDB_DIR/src/import_sqlite.cpp
    HEADERS += $$files($$INMEMDB_DIR/include/memdb/*.hpp, false)
} else {
    # Variante A: vorgebaute statische Lib
    LIBS += -L$$INMEMDB_BUILD_DIR -lInMemDB
    contains(DEFINES, INMEMDB_HAS_SQLITE): LIBS += -lsqlite3
}
