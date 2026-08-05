#pragma once

#include <sqlite3.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// Wrapper fino sobre sqlite3. Não é um ORM — expõe exatamente o necessário
// para abrir um banco (registro.sqlite ou indice.sqlite), rodar um script de
// schema, e preparar/rodar statements parametrizados.

namespace matriz::db {

class DatabaseError : public std::runtime_error {
public:
    explicit DatabaseError(const std::string& message) : std::runtime_error(message) {}
};

// Valor de parâmetro/coluna. Null representa SQL NULL.
struct Value {
    enum class Kind { Null, Text, Int, Real, Blob } kind = Kind::Null;
    std::string text;
    long long integer = 0;
    double real = 0.0;
    std::vector<unsigned char> blob;

    static Value null() { return Value{}; }
    static Value of(const std::string& s) { Value v; v.kind = Kind::Text; v.text = s; return v; }
    static Value of(const char* s) { return of(std::string(s)); }
    static Value of(long long i) { Value v; v.kind = Kind::Int; v.integer = i; return v; }
    static Value of(int i) { return of(static_cast<long long>(i)); }
    static Value of(double d) { Value v; v.kind = Kind::Real; v.real = d; return v; }
    static Value of(bool b) { return of(static_cast<long long>(b ? 1 : 0)); }
    static Value ofBlob(std::vector<unsigned char> bytes) { Value v; v.kind = Kind::Blob; v.blob = std::move(bytes); return v; }
};

class Statement {
public:
    Statement(sqlite3* db, const std::string& sql);
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&&) noexcept;
    Statement& operator=(Statement&&) noexcept;

    void bind(int oneBasedIndex, const Value& value);

    // Executa um passo. Retorna true se produziu uma linha (SQLITE_ROW),
    // false quando terminou (SQLITE_DONE). Lança em erro.
    bool step();

    void reset();

    std::string columnText(int index) const;
    long long columnInt(int index) const;
    double columnReal(int index) const;
    bool columnIsNull(int index) const;

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Roda um script SQL com múltiplos statements (usado para aplicar schema/*.sql).
    void execScript(const std::string& sqlScript);

    // Prepara um statement parametrizado para uso com bind()/step().
    Statement prepare(const std::string& sql);

    // Executa um statement sem parâmetros e sem resultado esperado (ex.: BEGIN/COMMIT).
    void exec(const std::string& sql);

    // Roda um INSERT/UPDATE parametrizado de uma vez só.
    void run(const std::string& sql, const std::vector<Value>& params);

    sqlite3* handle() const { return db_; }

private:
    sqlite3* db_ = nullptr;
};

} // namespace matriz::db
