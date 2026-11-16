#include "memdb/import_sqldump.hpp"

#include "import_util.hpp"
#include "memdb/exceptions.hpp"

#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>

namespace memdb {

namespace {

std::string upper(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

SourceType sqlTypeToSourceType(const std::string& decl) {
    std::string up = upper(decl);
    if (up.find("BOOL") != std::string::npos)
        return SourceType::Bool;
    if (up.find("INT") != std::string::npos)
        return SourceType::Integer;
    if (up.find("CHAR") != std::string::npos || up.find("CLOB") != std::string::npos ||
        up.find("TEXT") != std::string::npos)
        return SourceType::Text;
    if (up.find("BLOB") != std::string::npos)
        return SourceType::Blob;
    if (up.find("REAL") != std::string::npos || up.find("FLOA") != std::string::npos ||
        up.find("DOUB") != std::string::npos)
        return SourceType::Real;
    return SourceType::Unknown;
}

bool isWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

struct Parser {
    const std::string& s;
    std::size_t i = 0;

    explicit Parser(const std::string& str) : s(str) {}

    bool eof() const { return i >= s.size(); }
    char peek() const { return i < s.size() ? s[i] : '\0'; }

    void skipWsComments() {
        while (i < s.size()) {
            char c = s[i];
            if (std::isspace(static_cast<unsigned char>(c))) {
                ++i;
                continue;
            }
            if (c == '-' && i + 1 < s.size() && s[i + 1] == '-') {
                while (i < s.size() && s[i] != '\n')
                    ++i;
                continue;
            }
            if (c == '#') {
                while (i < s.size() && s[i] != '\n')
                    ++i;
                continue;
            }
            if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
                i += 2;
                while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/'))
                    ++i;
                i = (i + 1 < s.size()) ? i + 2 : s.size();
                continue;
            }
            break;
        }
    }

    std::string peekWord() const {
        std::size_t j = i;
        while (j < s.size() && isWordChar(s[j]))
            ++j;
        return upper(s.substr(i, j - i));
    }

    std::string readWord() {
        std::size_t start = i;
        while (i < s.size() && isWordChar(s[i]))
            ++i;
        return s.substr(start, i - start);
    }

    std::string readIdent() {
        skipWsComments();
        if (eof())
            throw Error("SQL-Dump: Identifier erwartet, Dateiende");
        char c = s[i];
        if (c == '`' || c == '"' || c == '\'') {
            char q = c;
            ++i;
            std::string out;
            while (i < s.size()) {
                if (s[i] == q) {
                    if (q != '\'' && i + 1 < s.size() && s[i + 1] == q) {
                        out += q;
                        i += 2;
                        continue;
                    }
                    ++i;
                    return out;
                }
                out += s[i++];
            }
            throw Error("SQL-Dump: unfertiger quotierter Identifier");
        }
        if (c == '[') {
            ++i;
            std::string out;
            while (i < s.size()) {
                if (s[i] == ']') {
                    if (i + 1 < s.size() && s[i + 1] == ']') {
                        out += ']';
                        i += 2;
                        continue;
                    }
                    ++i;
                    return out;
                }
                out += s[i++];
            }
            throw Error("SQL-Dump: unfertiges [..]");
        }
        return readWord();
    }

    std::string readString() {
        if (peek() != '\'')
            throw Error("SQL-Dump: String erwartet an Position " + std::to_string(i));
        ++i;
        std::string out;
        while (i < s.size()) {
            char c = s[i];
            if (c == '\\' && i + 1 < s.size()) {
                char e = s[i + 1];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case '0': out += '\0'; break;
                    case 'b': out += '\b'; break;
                    case 'Z': out += '\x1a'; break;
                    default: out += e; break; // \\ \' \" ...
                }
                i += 2;
                continue;
            }
            if (c == '\'') {
                if (i + 1 < s.size() && s[i + 1] == '\'') {
                    out += '\'';
                    i += 2;
                    continue;
                }
                ++i;
                return out;
            }
            out += c;
            ++i;
        }
        throw Error("SQL-Dump: unfertiger String");
    }

    void expect(char c) {
        skipWsComments();
        if (eof() || s[i] != c)
            throw Error(std::string("SQL-Dump: '") + c + "' erwartet an Position " +
                        std::to_string(i));
        ++i;
    }

    void skipToParenClose() { // wir stehen IN Klammern (nach '('); konsumiert bis ')'
        int depth = 1;
        while (i < s.size() && depth > 0) {
            char c = s[i];
            if (c == '\'') {
                try {
                    readString();
                } catch (...) {
                    i = s.size();
                    break;
                }
                continue;
            }
            if (c == '(')
                ++depth;
            else if (c == ')')
                --depth;
            ++i;
        }
    }

    void skipParens() {
        skipWsComments();
        if (peek() != '(')
            return;
        ++i;
        skipToParenClose();
    }

    void skipStatement() { // bis ';' (quotes/comments respektieren)
        while (i < s.size()) {
            char c = s[i];
            if (c == '\'') {
                try {
                    readString();
                } catch (...) {
                    i = s.size();
                }
                continue;
            }
            if (c == '"' || c == '`') {
                char q = c;
                ++i;
                while (i < s.size() && s[i] != q)
                    ++i;
                if (i < s.size())
                    ++i;
                continue;
            }
            if (c == '-' && i + 1 < s.size() && s[i + 1] == '-') {
                while (i < s.size() && s[i] != '\n')
                    ++i;
                continue;
            }
            if (c == '#') {
                while (i < s.size() && s[i] != '\n')
                    ++i;
                continue;
            }
            if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
                i += 2;
                while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/'))
                    ++i;
                i = (i + 1 < s.size()) ? i + 2 : s.size();
                continue;
            }
            if (c == ';') {
                ++i;
                return;
            }
            ++i;
        }
    }

    void skipConstraint() { // bis ',' oder ')' auf Tiefe 0 (Delimiter nicht konsumieren)
        int depth = 0;
        while (i < s.size()) {
            char c = s[i];
            if (c == '(') {
                ++depth;
                ++i;
                continue;
            }
            if (c == ')') {
                if (depth == 0)
                    return;
                --depth;
                ++i;
                continue;
            }
            if (c == ',' && depth == 0)
                return;
            if (c == '\'') {
                try {
                    readString();
                } catch (...) {
                    i = s.size();
                }
                continue;
            }
            ++i;
        }
    }

    Value readValue() {
        skipWsComments();
        if (eof())
            throw Error("SQL-Dump: Wert erwartet, Dateiende");
        char c = s[i];
        if (c == '\'')
            return Value(readString());
        if ((c == 'X' || c == 'x') && i + 1 < s.size() && s[i + 1] == '\'') {
            i += 2;
            std::vector<std::uint8_t> bytes;
            auto hexv = [](char h) -> int {
                if (h >= '0' && h <= '9')
                    return h - '0';
                if (h >= 'a' && h <= 'f')
                    return h - 'a' + 10;
                if (h >= 'A' && h <= 'F')
                    return h - 'A' + 10;
                return -1;
            };
            while (i < s.size() && s[i] != '\'') {
                int hi = hexv(s[i]);
                int lo = (i + 1 < s.size()) ? hexv(s[i + 1]) : -1;
                if (hi < 0 || lo < 0)
                    throw Error("SQL-Dump: ungueltiges X'..'-Blob an Position " + std::to_string(i));
                bytes.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
                i += 2;
            }
            expect('\'');
            return Value(std::move(bytes));
        }
        std::string w = peekWord();
        if (w == "NULL") {
            i += 4;
            return Value(nullptr);
        }
        if (w == "TRUE") {
            i += 4;
            return Value(true);
        }
        if (w == "FALSE") {
            i += 5;
            return Value(false);
        }
        if (c == '-' || c == '+' || c == '.' || std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t start = i;
            if (c == '-' || c == '+')
                ++i;
            if (i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
                i += 2;
                std::size_t hs = i;
                while (i < s.size() && std::isxdigit(static_cast<unsigned char>(s[i])))
                    ++i;
                std::int64_t v = 0;
                std::from_chars(s.data() + hs, s.data() + i, v, 16);
                return Value(v);
            }
            bool isDouble = false;
            while (i < s.size() &&
                   (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) {
                if (s[i] == '.')
                    isDouble = true;
                ++i;
            }
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                isDouble = true;
                ++i;
                if (i < s.size() && (s[i] == '-' || s[i] == '+'))
                    ++i;
                while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
                    ++i;
            }
            std::string num = s.substr(start, i - start);
            if (isDouble) {
                double d = 0.0;
                std::from_chars(num.data(), num.data() + num.size(), d);
                return Value(d);
            }
            std::int64_t v = 0;
            std::from_chars(num.data(), num.data() + num.size(), v);
            return Value(v);
        }
        throw Error("SQL-Dump: ungueltiger Wert an Position " + std::to_string(i));
    }
};

} // namespace

struct SqlDumpImportSource::Impl {
    std::string path;
    bool parsed = false;
    struct Table {
        TableInfo info;
        bool columnsFixed = false; ///< Spalten kamen aus CREATE TABLE
        std::vector<Document> rows;
    };
    std::vector<Table> tables;
    std::map<std::string, std::size_t> byName;

    void parseCreateTableBody(Parser& p) {
        std::string name = p.readIdent();
        p.skipWsComments();
        if (p.peek() != '(') {
            p.skipStatement();
            return;
        }
        ++p.i;
        Table t;
        t.info.name = name;
        t.columnsFixed = true;
        // v0.28: parst ab nach "FOREIGN": KEY ( col ) REFERENCES table [ ( col ) ]
        auto parseFk = [&](const std::string& fkName) {
            p.skipWsComments();
            if (p.peekWord() == "KEY")
                p.readWord();
            p.skipWsComments();
            p.expect('(');
            std::string col = p.readIdent();
            p.skipToParenClose();
            p.skipWsComments();
            if (p.peekWord() == "REFERENCES")
                p.readWord();
            p.skipWsComments();
            std::string rt = p.readIdent();
            std::string rc;
            p.skipWsComments();
            if (p.peek() == '(') {
                p.expect('(');
                rc = p.readIdent();
                p.skipToParenClose();
            }
            t.info.foreignKeys.push_back(ForeignKeyInfo{col, rt, rc, fkName});
        };
        while (true) {
            p.skipWsComments();
            if (p.eof())
                break;
            if (p.peek() == ')') {
                ++p.i;
                break;
            }
            std::string w = p.peekWord();
            if (w == "PRIMARY" || w == "UNIQUE" || w == "KEY" || w == "INDEX" || w == "CONSTRAINT" ||
                w == "FOREIGN" || w == "CHECK") {
                p.readWord();
                if (w == "PRIMARY") {
                    p.skipWsComments();
                    if (p.peekWord() == "KEY")
                        p.i += 3;
                    p.expect('(');
                    std::string col = p.readIdent();
                    for (auto& c : t.info.columns)
                        if (c.name == col)
                            c.primaryKey = true;
                    p.skipToParenClose();
                } else if (w == "UNIQUE") {
                    p.skipWsComments();
                    if (p.peekWord() == "KEY" || p.peekWord() == "INDEX")
                        p.readWord();
                    p.skipWsComments();
                    if (p.peek() != '(')
                        (void)p.readIdent(); // optionaler Indexname
                    p.expect('(');
                    std::string col = p.readIdent();
                    t.info.indexes.push_back(IndexInfo{col, true});
                    p.skipToParenClose();
                } else if (w == "KEY" || w == "INDEX") {
                    p.skipWsComments();
                    if (p.peek() != '(')
                        (void)p.readIdent();
                    p.expect('(');
                    std::string col = p.readIdent(); // nur erste Spalte
                    t.info.indexes.push_back(IndexInfo{col, false});
                    p.skipToParenClose();
                } else if (w == "FOREIGN") {
                    parseFk(""); // FOREIGN KEY (col) REFERENCES t [(c)]
                } else if (w == "CONSTRAINT") {
                    p.skipWsComments();
                    std::string cname = p.readIdent(); // Constraint-Name
                    p.skipWsComments();
                    if (p.peekWord() == "FOREIGN") {
                        p.readWord();
                        parseFk(cname); // CONSTRAINT name FOREIGN KEY ...
                    } else {
                        p.skipConstraint();
                    }
                } else {
                    p.skipConstraint();
                }
                p.skipWsComments();
                if (p.peek() == ',')
                    ++p.i;
                continue;
            }
            // Spaltendefinition
            ColumnInfo c;
            c.name = p.readIdent();
            p.skipWsComments();
            std::string typeWord = p.peekWord();
            if (!typeWord.empty()) {
                p.readWord();
                p.skipWsComments();
                if (p.peek() == '(')
                    p.skipParens();
                c.type = sqlTypeToSourceType(typeWord);
                p.skipWsComments();
                if (p.peekWord() == "PRECISION" || p.peekWord() == "UNSIGNED") {
                    std::string extra = p.readWord();
                    if (extra == "PRECISION")
                        c.type = sqlTypeToSourceType(typeWord + " " + extra);
                }
            }
            // Rest der Spaltendefinition: PRIMARY KEY / NOT NULL / UNIQUE
            int depth = 0;
            bool consumedDelim = false;
            while (p.i < p.s.size()) {
                p.skipWsComments();
                char ch = p.peek();
                if (ch == '(') {
                    ++depth;
                    ++p.i;
                    continue;
                }
                if (ch == ')') {
                    if (depth == 0)
                        break;
                    --depth;
                    ++p.i;
                    continue;
                }
                if (ch == ',' && depth == 0) {
                    ++p.i;
                    consumedDelim = true;
                    break;
                }
                std::string kw = p.peekWord();
                if (kw == "PRIMARY") {
                    p.readWord();
                    p.skipWsComments();
                    if (p.peekWord() == "KEY")
                        p.i += 3;
                    c.primaryKey = true;
                    continue;
                }
                if (kw == "NOT") {
                    p.readWord();
                    p.skipWsComments();
                    if (p.peekWord() == "NULL")
                        p.i += 4;
                    c.notNull = true;
                    continue;
                }
                if (kw == "UNIQUE") {
                    p.readWord();
                    t.info.indexes.push_back(IndexInfo{c.name, true});
                    continue;
                }
                if (kw == "REFERENCES") { // v0.28: col ... REFERENCES t [(c)]
                    p.readWord();
                    p.skipWsComments();
                    std::string rt = p.readIdent();
                    std::string rc;
                    p.skipWsComments();
                    if (p.peek() == '(') {
                        p.expect('(');
                        rc = p.readIdent();
                        p.skipToParenClose();
                    }
                    t.info.foreignKeys.push_back(ForeignKeyInfo{c.name, rt, rc, ""});
                    continue;
                }
                if (!kw.empty()) {
                    p.readWord();
                    continue;
                }
                ++p.i;
            }
            (void)consumedDelim;
            t.info.columns.push_back(std::move(c));
        }
        auto it = byName.find(name);
        if (it == byName.end()) {
            byName[name] = tables.size();
            tables.push_back(std::move(t));
        } else {
            tables[it->second].info.columns = t.info.columns;
            tables[it->second].info.indexes = t.info.indexes;
        }
        p.skipStatement(); // Tabellenoptionen (ENGINE=..., ...) bis ';'
    }

    void parseInsert(Parser& p) {
        p.i += 6; // INSERT
        while (true) {
            p.skipWsComments();
            std::string m = p.peekWord();
            if (m == "INTO") {
                p.i += 4;
                break;
            }
            if (m == "LOW_PRIORITY" || m == "DELAYED" || m == "HIGH_PRIORITY" || m == "IGNORE" ||
                m == "OR" || m == "REPLACE") {
                p.readWord();
                continue;
            }
            p.skipStatement();
            return;
        }
        std::string name = p.readIdent();
        p.skipWsComments();
        std::vector<std::string> cols;
        if (p.peek() == '(') {
            ++p.i;
            while (true) {
                p.skipWsComments();
                if (p.peek() == ')') {
                    ++p.i;
                    break;
                }
                cols.push_back(p.readIdent());
                p.skipWsComments();
                if (p.peek() == ',') {
                    ++p.i;
                    continue;
                }
                if (p.peek() == ')') {
                    ++p.i;
                    break;
                }
                throw Error("SQL-Dump: Spaltenliste ungueltig in INSERT " + name);
            }
        }
        p.skipWsComments();
        std::string vw = p.peekWord();
        if (vw != "VALUES" && vw != "VALUE") {
            p.skipStatement();
            return;
        }
        p.readWord();

        std::size_t tid;
        auto it = byName.find(name);
        if (it == byName.end()) {
            Table t;
            t.info.name = name;
            byName[name] = tables.size();
            tables.push_back(std::move(t));
            tid = tables.size() - 1;
        } else {
            tid = it->second;
        }

        while (true) {
            p.skipWsComments();
            if (p.peek() != '(')
                break;
            ++p.i;
            Document row;
            std::size_t idx = 0;
            while (true) {
                p.skipWsComments();
                Value v = p.readValue();
                std::string key;
                if (!cols.empty()) {
                    if (idx >= cols.size())
                        throw Error("SQL-Dump: mehr Werte als Spalten in INSERT " + name);
                    key = cols[idx];
                } else {
                    const Table& t = tables[tid];
                    if (t.columnsFixed && idx < t.info.columns.size())
                        key = t.info.columns[idx].name;
                    else
                        key = "col" + std::to_string(idx + 1);
                }
                row.set(key, std::move(v));
                ++idx;
                p.skipWsComments();
                if (p.peek() == ',') {
                    ++p.i;
                    continue;
                }
                if (p.peek() == ')') {
                    ++p.i;
                    break;
                }
                throw Error("SQL-Dump: VALUES-Liste ungueltig in INSERT " + name);
            }
            tables[tid].rows.push_back(std::move(row));
            p.skipWsComments();
            if (p.peek() == ',') {
                ++p.i;
                continue;
            }
            break;
        }
        p.skipStatement();
    }

    void parse() {
        if (parsed)
            return;
        std::ifstream in(path, std::ios::binary);
        if (!in)
            throw Error("SQL-Dump nicht lesbar: " + path);
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        Parser p(data);
        while (true) {
            p.skipWsComments();
            if (p.eof())
                break;
            std::string w = p.peekWord();
            if (w == "CREATE") {
                p.i += 6;
                bool isTable = false;
                while (true) {
                    p.skipWsComments();
                    std::string m = p.peekWord();
                    if (m == "TABLE") {
                        p.i += 5;
                        isTable = true;
                        break;
                    }
                    if (m == "IF") {
                        p.i += 2;
                        p.skipWsComments();
                        if (p.peekWord() == "NOT") {
                            p.i += 3;
                            p.skipWsComments();
                            if (p.peekWord() == "EXISTS")
                                p.i += 6;
                        }
                        continue;
                    }
                    if (m == "TEMP" || m == "TEMPORARY" || m == "OR" || m == "REPLACE" ||
                        m == "UNIQUE" || m == "GLOBAL" || m == "LOCAL") {
                        p.readWord();
                        continue;
                    }
                    break;
                }
                if (isTable)
                    parseCreateTableBody(p);
                else
                    p.skipStatement();
            } else if (w == "INSERT") {
                parseInsert(p);
            } else {
                p.skipStatement();
            }
        }
        // Typ-Inferenz
        for (auto& t : tables) {
            if (!t.columnsFixed) {
                detail::collectColumns(t.rows, t.info.columns);
            } else {
                std::map<std::string, SourceType> types;
                for (const auto& r : t.rows)
                    r.forEachField([&](const std::string& k, const Value& v) {
                        SourceType cur = types.count(k) ? types[k] : SourceType::Null;
                        types[k] = detail::widenSourceType(cur, sourceTypeFromValueType(v.type()));
                    });
                for (auto& c : t.info.columns)
                    if (c.type == SourceType::Unknown && types.count(c.name) &&
                        types[c.name] != SourceType::Null)
                        c.type = types[c.name];
            }
        }
        parsed = true;
    }
};

SqlDumpImportSource::SqlDumpImportSource(const std::string& path) : impl_(std::make_unique<Impl>()) {
    impl_->path = path;
}

SqlDumpImportSource::~SqlDumpImportSource() = default;

std::vector<TableInfo> SqlDumpImportSource::tables() const {
    impl_->parse();
    std::vector<TableInfo> out;
    out.reserve(impl_->tables.size());
    for (const auto& t : impl_->tables)
        out.push_back(t.info);
    return out;
}

void SqlDumpImportSource::readRows(
    const std::string& table,
    const std::function<void(const std::vector<Value>&)>& rowCb) const {
    impl_->parse();
    for (const auto& t : impl_->tables) {
        if (t.info.name == table) {
            for (const auto& r : t.rows)
                rowCb(detail::rowToValues(r, t.info.columns));
            return;
        }
    }
    throw Error("SQL-Dump: unbekannte Tabelle '" + table + "'");
}

} // namespace memdb
