# InMemDB als statische Library mit qmake bauen (unabhängig von CMake):
#   qmake InMemDB.pro && make -j        -> libInMemDB.a im Repo-Root
# Optional SQLite-Import:  qmake "CONFIG+=inmemdb_sqlite" InMemDB.pro && make -j
#
# Die Library selbst braucht kein Qt (CONFIG -= qt); eingebunden wird sie über InMemDB.pri.
TEMPLATE = lib
TARGET = InMemDB
CONFIG += staticlib c++17
CONFIG -= qt

DESTDIR = $$PWD
OBJECTS_DIR = $$PWD/build-qmake/obj
MOC_DIR = $$PWD/build-qmake/moc
UI_DIR = $$PWD/build-qmake/ui
RCC_DIR = $$PWD/build-qmake/rcc

INCLUDEPATH += $$PWD/include

# alle Implementierungen außer dem optionalen SQLite-Modul
SOURCES += $$files($$PWD/src/*.cpp, false)
SOURCES -= $$PWD/src/import_sqlite.cpp
HEADERS += $$files($$PWD/include/memdb/*.hpp, false)

# Optionales SQLite-Modul (FA-21): qmake "CONFIG+=inmemdb_sqlite" InMemDB.pro
inmemdb_sqlite {
    DEFINES += INMEMDB_HAS_SQLITE
    LIBS += -lsqlite3
    SOURCES += $$PWD/src/import_sqlite.cpp
}
