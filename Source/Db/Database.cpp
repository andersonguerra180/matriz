#include "Database.h"

#include <utility>

namespace matriz::db {

// ---------------------------------------------------------------------------
// Statement
// ---------------------------------------------------------------------------

Statement::Statement(sqlite3* db, const std::string& sql) : db_(db) {
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK)
        throw DatabaseError(std::string("falha ao preparar statement: ") + sqlite3_errmsg(db_) + "\nSQL: " + sql);
}

Statement::~Statement() {
    if (stmt_) sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& other) noexcept : db_(other.db_), stmt_(other.stmt_) {
    other.stmt_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (stmt_) sqlite3_finalize(stmt_);
        db_ = other.db_;
        stmt_ = other.stmt_;
        other.stmt_ = nullptr;
    }
    return *this;
}

void Statement::bind(int oneBasedIndex, const Value& value) {
    int rc = SQLITE_OK;
    switch (value.kind) {
        case Value::Kind::Null: rc = sqlite3_bind_null(stmt_, oneBasedIndex); break;
        case Value::Kind::Text: rc = sqlite3_bind_text(stmt_, oneBasedIndex, value.text.c_str(), -1, SQLITE_TRANSIENT); break;
        case Value::Kind::Int: rc = sqlite3_bind_int64(stmt_, oneBasedIndex, value.integer); break;
        case Value::Kind::Real: rc = sqlite3_bind_double(stmt_, oneBasedIndex, value.real); break;
    }
    if (rc != SQLITE_OK)
        throw DatabaseError(std::string("falha ao vincular parâmetro: ") + sqlite3_errmsg(db_));
}

bool Statement::step() {
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    throw DatabaseError(std::string("falha ao executar statement: ") + sqlite3_errmsg(db_));
}

void Statement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

std::string Statement::columnText(int index) const {
    const unsigned char* text = sqlite3_column_text(stmt_, index);
    return text ? reinterpret_cast<const char*>(text) : std::string();
}

long long Statement::columnInt(int index) const { return sqlite3_column_int64(stmt_, index); }
double Statement::columnReal(int index) const { return sqlite3_column_double(stmt_, index); }
bool Statement::columnIsNull(int index) const { return sqlite3_column_type(stmt_, index) == SQLITE_NULL; }

// ---------------------------------------------------------------------------
// Database
// ---------------------------------------------------------------------------

Database::Database(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        std::string msg = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw DatabaseError("falha ao abrir banco em \"" + path + "\": " + msg);
    }
    sqlite3_busy_timeout(db_, 5000);
}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

void Database::execScript(const std::string& sqlScript) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sqlScript.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string msg = errMsg ? errMsg : "erro desconhecido";
        sqlite3_free(errMsg);
        throw DatabaseError("falha ao executar script SQL: " + msg);
    }
}

void Database::exec(const std::string& sql) { execScript(sql); }

Statement Database::prepare(const std::string& sql) { return Statement(db_, sql); }

void Database::run(const std::string& sql, const std::vector<Value>& params) {
    Statement stmt = prepare(sql);
    for (size_t i = 0; i < params.size(); ++i)
        stmt.bind(static_cast<int>(i) + 1, params[i]);
    stmt.step();
}

} // namespace matriz::db
