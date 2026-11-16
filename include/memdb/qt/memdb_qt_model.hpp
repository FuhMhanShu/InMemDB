#pragma once

// Optionaler Qt-Modell-Adapter (v0.23) - KEINE harte Abhaengigkeit von InMemDB an Qt:
// Dieses Header wird nur kompiliert, wenn INMEMDB_WITH_QT definiert ist (setzt InMemDB.pri
// automatisch, sobald das einbindende qmake-Projekt QT += core hat). Benötigt nur QtCore
// (QAbstractTableModel/QVariant) - QtWidgets erst fuer die View (QTableView etc.).
// Siehe docs/guides/qt-qmake.md, Abschnitt "Qt-Modell-Adapter (v0.23)".
#if !defined(INMEMDB_WITH_QT)
#error "memdb/qt/memdb_qt_model.hpp benoetigt Qt: INMEMDB_WITH_QT definieren (InMemDB.pri setzt es bei QT += core), siehe docs/guides/qt-qmake.md"
#endif

#include <memdb/memdb.hpp>

#include <QAbstractTableModel>
#include <QMetaObject>
#include <QMetaType>
#include <QString>
#include <QVariant>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace memdb::qt {

/// @brief QAbstractTableModel ueber einer memdb::Collection (v0.23, nur QtCore noetig).
///
/// - Zeilen = Dokumente der Collection (alle, oder per DSL-Query gefiltert via `setQuery`).
/// - Spalten = sortierte Vereinigung aller Feldnamen der sichtbaren Dokumente
///   (schema-los: neue Felder tauchen beim naechsten `refresh()` als Spalte auf).
/// - `setData()` schreibt zurueck (`Collection::updateById`).
/// - Collection-Aenderungen (Observer, auch aus Transaktionen nach Commit) loesen
///   `refresh()` ueber eine queued Qt-Connection aus (thread-sicher, GUI-Thread).
class MemDbTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit MemDbTableModel(Collection collection, QObject* parent = nullptr)
        : QAbstractTableModel(parent), collection_(std::move(collection)) {
        observer_ = collection_.onChange([this](const ChangeEvent&) {
            QMetaObject::invokeMethod(this, &MemDbTableModel::refresh, Qt::QueuedConnection);
        });
        reload();
    }
    ~MemDbTableModel() override { collection_.offChange(observer_); }

    /// @brief Zugrundeliegende Collection (Handle-Semantik: Kopie zeigt auf dieselbe Collection).
    Collection collection() const { return collection_; }

    /// @brief Aktueller DSL-Query-Filter (leer = alle Dokumente).
    QString query() const { return query_; }
    /// @brief Query-Filter setzen (String-DSL, z. B. `"age >= 18"`); leer = alle. Ruft refresh().
    void setQuery(const QString& dsl) {
        query_ = dsl;
        refresh();
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : static_cast<int>(ids_.size());
    }
    int columnCount(const QModelIndex& parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : static_cast<int>(columns_.size());
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::EditRole))
            return {};
        if (index.row() < 0 || index.row() >= static_cast<int>(ids_.size()) ||
            index.column() < 0 || index.column() >= static_cast<int>(columns_.size()))
            return {};
        auto doc = collection_.findById(ids_[static_cast<std::size_t>(index.row())]);
        if (!doc)
            return {};
        const Value* v = doc->find(columns_[static_cast<std::size_t>(index.column())]);
        return v ? toVariant(*v) : QVariant();
    }

    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override {
        if (!index.isValid() || role != Qt::EditRole || index.row() < 0 ||
            index.row() >= static_cast<int>(ids_.size()) || index.column() < 0 ||
            index.column() >= static_cast<int>(columns_.size()))
            return false;
        const DocumentId id = ids_[static_cast<std::size_t>(index.row())];
        const std::string field = columns_[static_cast<std::size_t>(index.column())];
        const Value v = fromVariant(value);
        collection_.updateById(id, [&](Document& d) { d.set(field, v); });
        emit dataChanged(index, index, {role});
        return true;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override {
        if (role != Qt::DisplayRole)
            return {};
        if (orientation == Qt::Horizontal) {
            if (section < 0 || section >= static_cast<int>(columns_.size()))
                return {};
            return QString::fromStdString(columns_[static_cast<std::size_t>(section)]);
        }
        return QVariant(section + 1); // 1-basierte Zeilennummer
    }

    Qt::ItemFlags flags(const QModelIndex& index) const override {
        Qt::ItemFlags f = QAbstractTableModel::flags(index);
        if (index.isValid())
            f |= Qt::ItemIsEditable;
        return f;
    }

public slots:
    /// @brief Zeilen/Spalten neu aus der Collection laden (nach externer Aenderung, setQuery, Tx-Commit).
    void refresh() {
        beginResetModel();
        reload();
        endResetModel();
    }

private:
    void reload() {
        Expression q = query_.isEmpty() ? Expression{} : parseQuery(query_.toStdString());
        ids_ = collection_.findIds(q);
        std::set<std::string> cols;
        for (DocumentId id : ids_)
            if (auto doc = collection_.findById(id))
                doc->forEachField([&](const std::string& k, const Value&) { cols.insert(k); });
        columns_.assign(cols.begin(), cols.end());
    }

    static QVariant toVariant(const Value& v) {
        switch (v.type()) {
            case Value::Type::Null: return {};
            case Value::Type::Bool: return QVariant(v.asBool());
            case Value::Type::Int: return QVariant::fromValue<qlonglong>(v.asInt());
            case Value::Type::Double: return QVariant(v.asDouble());
            case Value::Type::String: return QString::fromStdString(v.asString());
            default: return QString::fromStdString(memdb::toString(v)); // Binary/Array/Document als Text
        }
    }
    static Value fromVariant(const QVariant& q) {
        if (q.isNull())
            return Value(nullptr);
        switch (q.userType()) {
            case QMetaType::Bool: return Value(q.toBool());
            case QMetaType::Int:
            case QMetaType::LongLong:
            case QMetaType::UInt:
            case QMetaType::ULongLong: return Value(static_cast<std::int64_t>(q.toLongLong()));
            case QMetaType::Double:
            case QMetaType::Float: return Value(q.toDouble());
            default: return Value(q.toString().toStdString());
        }
    }

    Collection collection_;
    QString query_;
    std::vector<DocumentId> ids_;
    std::vector<std::string> columns_;
    std::uint64_t observer_ = 0;
};

} // namespace memdb::qt
