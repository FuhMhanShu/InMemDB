// Qt/qmake-Beispiel (FA-23): InMemDB in einer Qt-Console-App über InMemDB.pri.
#include <QCoreApplication>
#include <QDebug>

#include <memdb/memdb.hpp>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    memdb::Database db("qt-demo");
    memdb::Collection users = db.createCollection("users");
    users.insert({{"name", "Ada"}, {"age", 36}, {"stadt", "Berlin"}});
    users.insert({{"name", "Bob"}, {"age", 17}, {"stadt", "Hamburg"}});
    users.insert({{"name", "Cy"}, {"age", 40}, {"stadt", "Berlin"}});

    qInfo() << "C++-API, age >= 18:" << users.count(memdb::Field("age") >= 18);
    qInfo() << "String-DSL, stadt == 'Berlin':" << users.count("stadt == 'Berlin'");
    qInfo() << "DbGraph:" << QString::fromStdString(memdb::DbGraph::fromDatabase(db).summary());

    db.saveToFile("qt_demo.inmemdb.json");
    qInfo() << "Snapshot geschrieben: qt_demo.inmemdb.json";
    return 0;
}
