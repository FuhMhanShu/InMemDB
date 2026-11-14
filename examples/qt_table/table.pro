# Qt-Widgets-Beispiel: MemDbTableModel (v0.23) zeigt eine InMemDB-Collection in einer QTableView.
# Build (Shadow-Build empfohlen):
#   mkdir build && cd build && qmake ../examples/qt_table/table.pro && make -j
# Run (GUI):            ./qt_table
# Run (headless/CI):    QT_QPA_PLATFORM=offscreen ./qt_table
QT += core gui widgets
CONFIG += c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = qt_table

# Pfad zum InMemDB-Repo ($$_PRO_FILE_PWD_ = Verzeichnis dieser .pro, auch im Shadow-Build korrekt)
INMEMDB_DIR = $$clean_path($$_PRO_FILE_PWD_/../..)

# Variante B: InMemDB-Quellen direkt mitkompilieren (kein Vorab-Build noetig).
# Variante A stattdessen: Zeile auskommentieren und vorher "qmake InMemDB.pro && make" im InMemDB-Root.
DEFINES += INMEMDB_BUILD_SOURCES

include($$INMEMDB_DIR/InMemDB.pri)

SOURCES += $$_PRO_FILE_PWD_/main.cpp
