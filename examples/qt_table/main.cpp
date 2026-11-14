// Qt-Modell-Adapter (v0.23): memdb::qt::MemDbTableModel zeigt eine InMemDB-Collection
// in einer QTableView - inkl. DSL-Query-Filter und schreibendem Editing.
// Headless-tauglich: QT_QPA_PLATFORM=offscreen ./qt_table (CI-Smoke-Test, beendet sich selbst).
#include <memdb/memdb.hpp>
#include <memdb/qt/memdb_qt_model.hpp>

#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

using namespace memdb;

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    Database db("qt-demo");
    Collection users = db.createCollection("users");
    users.createIndex("age");
    users.insert({{"name", "Ada"}, {"age", 36}, {"stadt", "Berlin"},
                  {"adresse", Value(std::make_shared<Document>(Document{{"plz", "10115"}}))}});
    users.insert({{"name", "Bob"}, {"age", 17}, {"stadt", "Hamburg"}});
    users.insert({{"name", "Cy"}, {"age", 40}, {"stadt", "Berlin"}});

    qt::MemDbTableModel model(users);

    QWidget window;
    window.setWindowTitle("InMemDB – MemDbTableModel (v0.23)");
    auto* layout = new QVBoxLayout(&window);
    auto* filterRow = new QHBoxLayout();
    auto* queryEdit = new QLineEdit("age >= 18");
    auto* filterBtn = new QPushButton("Filter (DSL)");
    auto* bumpBtn = new QPushButton("Alter +1 (Bulk-Update via DSL)");
    filterRow->addWidget(queryEdit);
    filterRow->addWidget(filterBtn);
    filterRow->addWidget(bumpBtn);
    layout->addLayout(filterRow);
    auto* view = new QTableView();
    view->setModel(&model);
    view->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(view);
    QObject::connect(filterBtn, &QPushButton::clicked,
                     [&] { model.setQuery(queryEdit->text()); });
    QObject::connect(bumpBtn, &QPushButton::clicked, [&] {
        // v0.21 Bulk-Update via DSL; der Observer des Models triggert refresh() automatisch
        users.update("age >= 18", "age = age + 1");
    });
    model.setQuery(queryEdit->text()); // initialer Filter aus dem LineEdit
    window.resize(640, 320);
    window.show();

    // Smoke-Test (headless moeglich): nach 300 ms Modell pruefen und beenden
    QTimer::singleShot(300, [&] {
        std::printf("rows=%d cols=%d query='%s'\n", model.rowCount(), model.columnCount(),
                    model.query().toStdString().c_str());
        std::printf("row0: %s\n",
                    model.data(model.index(0, 0)).toString().toStdString().c_str());
        auto m = users.metrics();
        std::printf("metrics: inserts=%llu queries=%llu indexQueries=%llu docs=%zu\n",
                    (unsigned long long)m.inserts, (unsigned long long)m.queries,
                    (unsigned long long)m.indexQueries, m.docs);
        if (model.rowCount() != 2 || model.columnCount() < 3) { // Filter age >= 18 -> Ada, Cy
            std::printf("SMOKE FAIL\n");
            app.exit(1);
            return;
        }
        std::printf("SMOKE OK\n");
        app.quit();
    });
    return app.exec();
}
