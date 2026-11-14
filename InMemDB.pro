# InMemDB als statische Library mit qmake bauen (unabhängig von CMake):
#   qmake InMemDB.pro && make -j        -> libInMemDB.a im Repo-Root
# Optional SQLite-Import:  qmake "CONFIG+=inmemdb_sqlite" InMemDB.pro && make -j
#
# Die Library selbst braucht kein Qt (CONFIG -= qt); eingebunden wird sie über InMemDB.pri.
TEMPLATE = lib
TARGET = InMemDB
CONFIG += staticlib c++17
CONFIG -= qt

DESTDIR = $$_PRO_FILE_PWD_
OBJECTS_DIR = $$_PRO_FILE_PWD_/build-qmake/obj
MOC_DIR = $$_PRO_FILE_PWD_/build-qmake/moc
UI_DIR = $$_PRO_FILE_PWD_/build-qmake/ui
RCC_DIR = $$_PRO_FILE_PWD_/build-qmake/rcc

INCLUDEPATH += $$_PRO_FILE_PWD_/include

# alle Implementierungen außer dem optionalen SQLite-Modul
SOURCES += $$files($$_PRO_FILE_PWD_/src/*.cpp, false)
SOURCES -= $$_PRO_FILE_PWD_/src/import_sqlite.cpp
HEADERS += $$files($$_PRO_FILE_PWD_/include/memdb/*.hpp, false)

# Optionales SQLite-Modul (FA-21): qmake "CONFIG+=inmemdb_sqlite" InMemDB.pro
inmemdb_sqlite {
    DEFINES += INMEMDB_HAS_SQLITE
    LIBS += -lsqlite3
    SOURCES += $$_PRO_FILE_PWD_/src/import_sqlite.cpp
}
