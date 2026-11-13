#include "memdb/import_csv.hpp"

#include "memdb/exceptions.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace memdb {

namespace {

bool isIntCell(const std::string& s) {
    if (s.empty())
        return false;
    std::int64_t v = 0;
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}

bool isDoubleCell(const std::string& s) {
    if (s.empty())
        return false;
    double v = 0.0;
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}

Value cellToValue(const std::string& cell, SourceType t) {
    if (cell.empty())
        return Value(nullptr);
    switch (t) {
        case SourceType::Integer: {
            std::int64_t v = 0;
            auto r = std::from_chars(cell.data(), cell.data() + cell.size(), v);
            if (r.ec == std::errc() && r.ptr == cell.data() + cell.size())
                return Value(v);
            return Value(cell);
        }
        case SourceType::Real: {
            double v = 0.0;
            auto r = std::from_chars(cell.data(), cell.data() + cell.size(), v);
            if (r.ec == std::errc() && r.ptr == cell.data() + cell.size())
                return Value(v);
            return Value(cell);
        }
        default:
            return Value(cell);
    }
}

} // namespace

struct CsvImportSource::Impl {
    std::string path;
    Options opts;
    bool parsed = false;
    std::string tableName;
    std::vector<ColumnInfo> columns;
    std::vector<std::vector<std::string>> rows;

    void parse() {
        if (parsed)
            return;
        std::ifstream in(path, std::ios::binary);
        if (!in)
            throw Error("CSV-Datei nicht lesbar: " + path);
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        std::vector<std::vector<std::string>> all;
        std::vector<std::string> row;
        std::string cell;
        bool inQuotes = false;
        bool quoted = false;
        bool lineHasContent = false;

        for (std::size_t i = 0; i < data.size(); ++i) {
            char c = data[i];
            if (inQuotes) {
                if (c == opts.quote) {
                    if (i + 1 < data.size() && data[i + 1] == opts.quote) {
                        cell += opts.quote;
                        ++i;
                    } else {
                        inQuotes = false;
                    }
                } else {
                    cell += c;
                }
                lineHasContent = true;
            } else if (c == opts.quote && cell.empty() && !quoted) {
                inQuotes = true;
                quoted = true;
                lineHasContent = true;
            } else if (c == opts.delimiter) {
                row.push_back(cell);
                cell.clear();
                quoted = false;
                lineHasContent = true;
            } else if (c == '\n') {
                if (lineHasContent || !cell.empty() || !row.empty()) {
                    row.push_back(cell);
                    all.push_back(std::move(row));
                    row.clear();
                    cell.clear();
                    quoted = false;
                }
                lineHasContent = false;
            } else if (c == '\r') {
                // CRLF: \r vor \n wird ignoriert (innerhalb Quotes oben als Inhalt behandelt)
            } else {
                cell += c;
                lineHasContent = true;
            }
        }
        if (lineHasContent || !cell.empty() || !row.empty()) {
            row.push_back(cell);
            all.push_back(std::move(row));
        }

        std::vector<std::string> header;
        std::size_t start = 0;
        std::size_t ncol = 0;
        for (const auto& r : all)
            ncol = std::max(ncol, r.size());
        if (opts.hasHeader && !all.empty()) {
            header = all.front();
            start = 1;
        }
        if (header.empty())
            for (std::size_t i = 0; i < ncol; ++i)
                header.push_back("col" + std::to_string(i + 1));

        columns.clear();
        for (std::size_t i = 0; i < header.size(); ++i) {
            ColumnInfo c;
            c.name = header[i].empty() ? "col" + std::to_string(i + 1) : header[i];
            c.type = SourceType::Text;
            columns.push_back(std::move(c));
        }
        rows.assign(all.begin() + static_cast<std::ptrdiff_t>(start), all.end());

        if (opts.inferTypes) {
            for (std::size_t col = 0; col < columns.size(); ++col) {
                bool allInt = true;
                bool allNum = true;
                bool any = false;
                for (const auto& r : rows) {
                    static const std::string empty;
                    const std::string& v = col < r.size() ? r[col] : empty;
                    if (v.empty())
                        continue;
                    any = true;
                    if (allInt && !isIntCell(v))
                        allInt = false;
                    if (allNum && !isDoubleCell(v))
                        allNum = false;
                }
                if (any && allInt)
                    columns[col].type = SourceType::Integer;
                else if (any && allNum)
                    columns[col].type = SourceType::Real;
            }
        }

        if (!opts.tableName.empty()) {
            tableName = opts.tableName;
        } else {
            tableName = std::filesystem::path(path).stem().string();
            if (tableName.empty())
                tableName = "csv";
        }
        parsed = true;
    }
};

CsvImportSource::CsvImportSource(const std::string& path, Options opts)
    : impl_(std::make_unique<Impl>()) {
    impl_->path = path;
    impl_->opts = std::move(opts);
}

CsvImportSource::~CsvImportSource() = default;

const CsvImportSource::Options& CsvImportSource::options() const { return impl_->opts; }

std::vector<TableInfo> CsvImportSource::tables() const {
    impl_->parse();
    TableInfo t;
    t.name = impl_->tableName;
    t.columns = impl_->columns;
    return {std::move(t)};
}

void CsvImportSource::readRows(
    const std::string& table,
    const std::function<void(const std::vector<Value>&)>& rowCb) const {
    impl_->parse();
    if (table != impl_->tableName)
        throw Error("CSV: unbekannte Tabelle '" + table + "' (erwartet: '" + impl_->tableName + "')");
    for (const auto& r : impl_->rows) {
        std::vector<Value> vals;
        vals.reserve(impl_->columns.size());
        for (std::size_t i = 0; i < impl_->columns.size(); ++i) {
            static const std::string empty;
            const std::string& cell = i < r.size() ? r[i] : empty;
            vals.push_back(cellToValue(cell, impl_->columns[i].type));
        }
        rowCb(vals);
    }
}

} // namespace memdb
