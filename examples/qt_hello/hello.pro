# Qt-Console-Beispiel: InMemDB per qmake einbinden (FA-23).
# Build (Shadow-Build empfohlen):
#   mkdir build && cd build && qmake ../examples/qt_hello/hello.pro && make -j && ./qt_hello
QT += core
QT -= gui
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = qt_hello

# Pfad zum InMemDB-Repo (hier: zwei Ebenen hoch)
INMEMDB_DIR = $$clean_path($$PWD/../..)

# Variante B: InMemDB-Quellen direkt mitkompilieren (kein Vorab-Build der Lib nötig).
# Variante A stattdessen: Zeile auskommentieren und vorher "qmake InMemDB.pro && make"
# im InMemDB-Root ausführen (liefert libInMemDB.a), dann lib per InMemDB.pri linken.
DEFINES += INMEMDB_BUILD_SOURCES

include($$INMEMDB_DIR/InMemDB.pri)

SOURCES += main.cpp
