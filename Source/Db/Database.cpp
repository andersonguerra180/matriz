#include "Database.h"

#include <chrono>
#include <cstdio>
#include <utility>

namespace matriz::db {

// ---------------------------------------------------------------------------
// Statement
// ---------------------------------------------------------------------------

Statement::Statement(sqlite3* db, const std::string& sql) : db_(db) {
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK)
        throw DatabaseError(std::string("failed to prepare statement: ") + sqlite3_errmsg(db_) + "\nSQL: " + sql);
}

Statement::~Statement() { liberar(); }

void Statement::liberar() {
    if (!stmt_) return;
    if (cache_) CacheStatements::devolver(cache_, sqlCache_, stmt_);
    else sqlite3_finalize(stmt_);
    stmt_ = nullptr;
    cache_.reset();
}

Statement::Statement(Statement&& other) noexcept
    : db_(other.db_), stmt_(other.stmt_), dono_(other.dono_), cache_(std::move(other.cache_)),
      sqlCache_(std::move(other.sqlCache_)) {
    other.stmt_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        liberar();
        db_ = other.db_;
        stmt_ = other.stmt_;
        dono_ = other.dono_;
        cache_ = std::move(other.cache_);
        sqlCache_ = std::move(other.sqlCache_);
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
        case Value::Kind::Blob:
            rc = sqlite3_bind_blob(stmt_, oneBasedIndex, value.blob.data(), static_cast<int>(value.blob.size()),
                                    SQLITE_TRANSIENT);
            break;
    }
    if (rc != SQLITE_OK)
        throw DatabaseError(std::string("failed to bind parameter: ") + sqlite3_errmsg(db_));
}

bool Statement::step() {
    int rc;
    if (dono_) {
        Database::Trava trava(*dono_);
        rc = sqlite3_step(stmt_);
        dono_->sincronizarTravaDeTransacao();
    } else {
        rc = sqlite3_step(stmt_);
    }
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    throw DatabaseError(std::string("failed to execute statement: ") + sqlite3_errmsg(db_));
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

std::vector<unsigned char> Statement::columnBlob(int index) const {
    // Ordem obrigatória do SQLite: sqlite3_column_bytes() DEPOIS de
    // sqlite3_column_blob(). Ao contrário, uma conversão de tipo pode
    // invalidar o ponteiro e o tamanho lido não corresponderia aos bytes.
    const void* dados = sqlite3_column_blob(stmt_, index);
    int tamanho = sqlite3_column_bytes(stmt_, index);
    if (dados == nullptr || tamanho <= 0) return {};
    auto* bytes = static_cast<const unsigned char*>(dados);
    return std::vector<unsigned char>(bytes, bytes + tamanho);
}

// ---------------------------------------------------------------------------
// Database
// ---------------------------------------------------------------------------

Database::Database(const std::string& path, Modo modo) {
    if (modo == Modo::SomenteLeitura) {
        // Segunda conexão só pra leitura (WAL: não espera a transação da conexão de escrita).
        // Não mexe no journal_mode: quem o define é a conexão de escrita.
        if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            std::string msg = db_ ? sqlite3_errmsg(db_) : "unknown error";
            if (db_) sqlite3_close(db_);
            db_ = nullptr;
            throw DatabaseError("failed to open database read-only at \"" + path + "\": " + msg);
        }
        sqlite3_busy_timeout(db_, 5000);
        return;
    }
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        std::string msg = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw DatabaseError("failed to open database at \"" + path + "\": " + msg);
    }
    sqlite3_busy_timeout(db_, 5000);

    // Fix de performance (lag/spinning wheel, item 5): WAL deixa leitores não
    // bloquearem atrás de um writer (e vice-versa) — o modo padrão DELETE
    // serializa tudo. synchronous=NORMAL é seguro com WAL (só FULL protege
    // contra corrupção em power loss no modo antigo; WAL já tem essa
    // garantia com NORMAL) e evita um fsync a cada COMMIT.
    execScript("PRAGMA journal_mode=WAL;");
    execScript("PRAGMA synchronous=NORMAL;");
}

namespace {
constexpr auto kEsperaMaximaTrava = std::chrono::seconds(60);
}

Database::Trava::Trava(Database& db) : db_(db) {
    travou_ = db_.conexaoMutex_.try_lock_for(kEsperaMaximaTrava);
    if (!travou_)
        std::fprintf(stderr, "[db] trava da conexao nao obtida em 60 s (transacao de outra thread aberta?) "
                             "- seguindo sem ela\n");
}

Database::Trava::~Trava() {
    if (travou_) db_.conexaoMutex_.unlock();
}

void Database::sincronizarTravaDeTransacao() {
    const bool emTransacao = db_ != nullptr && sqlite3_get_autocommit(db_) == 0;
    if (emTransacao && !travaDeTransacao_) {
        conexaoMutex_.lock();  // reentrante: já temos a trava, não bloqueia
        travaDeTransacao_ = true;
        donoDaTransacao_.store(std::this_thread::get_id());
    } else if (!emTransacao && travaDeTransacao_) {
        travaDeTransacao_ = false;
        donoDaTransacao_.store(std::thread::id());
        conexaoMutex_.unlock();
    }
}

Database::~Database() {
    {
        // Statements emprestados que ainda vivem (thread de fundo) vão se finalizar sozinhos
        // ao serem destruídos, em vez de voltar a um cache que deixou de existir.
        std::lock_guard<std::mutex> lock(cache_->mutex);
        cache_->fechado = true;
    }
    limparCache();  // statements ociosos precisam ser finalizados antes de fechar a conexão
    if (db_) sqlite3_close(db_);
}

// ---------------------------------------------------------------------------
// Cache de statements preparados
// ---------------------------------------------------------------------------

namespace {
constexpr size_t kMaxPorSql = 4;       // cópias ociosas do mesmo SQL (uma por thread concorrente, na prática)
constexpr size_t kMaxNoCache = 256;    // total de statements ociosos na conexão
constexpr size_t kMaxTamanhoSql = 2000;  // SQL montado dinamicamente em tamanho absurdo não vale o cache

bool comecaCom(const std::string& sql, const char* palavra) {
    size_t i = 0;
    while (i < sql.size() && (sql[i] == ' ' || sql[i] == '\n' || sql[i] == '\t' || sql[i] == '\r')) ++i;
    for (size_t k = 0; palavra[k] != '\0'; ++k, ++i)
        if (i >= sql.size() || (sql[i] | 0x20) != (palavra[k] | 0x20)) return false;
    return true;
}
} // namespace

static bool sqlConteemDdl(const std::string& sql) {
    for (const char* palavra : {"create ", "drop ", "alter "}) {
        const size_t n = std::char_traits<char>::length(palavra);
        for (size_t i = 0; i + n <= sql.size(); ++i) {
            size_t k = 0;
            while (k < n && (sql[i + k] | 0x20) == (palavra[k] | 0x20)) ++k;
            if (k == n) return true;
        }
    }
    return false;
}

bool Database::sqlCacheavel(const std::string& sql) {
    if (sql.size() > kMaxTamanhoSql) return false;
    return comecaCom(sql, "select") || comecaCom(sql, "insert") || comecaCom(sql, "update") ||
           comecaCom(sql, "delete") || comecaCom(sql, "with");
}

sqlite3_stmt* Database::emprestarDoCache(const std::string& sql) {
    std::lock_guard<std::mutex> lock(cache_->mutex);
    auto it = cache_->ociosos.find(sql);
    if (it == cache_->ociosos.end() || it->second.empty()) return nullptr;
    sqlite3_stmt* stmt = it->second.back();
    it->second.pop_back();
    --cache_->total;
    return stmt;
}

void CacheStatements::devolver(const std::shared_ptr<CacheStatements>& cache, const std::string& sql, sqlite3_stmt* stmt) {
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    {
        std::lock_guard<std::mutex> lock(cache->mutex);
        if (!cache->fechado && cache->total < kMaxNoCache) {
            auto& ociosos = cache->ociosos[sql];
            if (ociosos.size() < kMaxPorSql) {
                ociosos.push_back(stmt);
                ++cache->total;
                return;
            }
        }
    }
    sqlite3_finalize(stmt);
}

void Database::limparCache() {
    std::unordered_map<std::string, std::vector<sqlite3_stmt*>> antigos;
    {
        std::lock_guard<std::mutex> lock(cache_->mutex);
        antigos.swap(cache_->ociosos);
        cache_->total = 0;
    }
    for (auto& [sql, ociosos] : antigos)
        for (auto* stmt : ociosos) sqlite3_finalize(stmt);
}

void Database::execScript(const std::string& sqlScript) {
    Trava trava(*this);
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sqlScript.c_str(), nullptr, nullptr, &errMsg);
    sincronizarTravaDeTransacao();
    if (rc != SQLITE_OK) {
        std::string msg = errMsg ? errMsg : "unknown error";
        sqlite3_free(errMsg);
        throw DatabaseError("failed to run SQL script: " + msg);
    }
    marcarSujo();
    // Esquema mudou (migração aditiva, CREATE TABLE...): statements ociosos podem apontar
    // pra objetos que deixaram de existir — recomeça o cache. BEGIN/COMMIT/INSERT em
    // lote NÃO passam por aqui, então o cache vale dentro das transações.
    if (sqlConteemDdl(sqlScript)) limparCache();
}

void Database::exec(const std::string& sql) { execScript(sql); }

Statement Database::prepare(const std::string& sql) {
    const bool cacheavel = sqlCacheavel(sql);
    if (cacheavel) {
        if (sqlite3_stmt* emprestado = emprestarDoCache(sql)) {
            Statement stmt(db_, emprestado);
            stmt.dono_ = this;
            stmt.cache_ = cache_;
            stmt.sqlCache_ = sql;
            return stmt;
        }
    }
    Statement stmt(db_, sql);
    stmt.dono_ = this;
    if (cacheavel) { stmt.cache_ = cache_; stmt.sqlCache_ = sql; }
    return stmt;
}

void Database::run(const std::string& sql, const std::vector<Value>& params) {
    Trava trava(*this);
    Statement stmt = prepare(sql);
    for (size_t i = 0; i < params.size(); ++i)
        stmt.bind(static_cast<int>(i) + 1, params[i]);
    stmt.step();
    marcarSujo();
    // DDL via run() (ALTER TABLE de migração): o cache recomeça, como em execScript().
    if (!sqlCacheavel(sql) && sqlConteemDdl(sql)) limparCache();
}

void Database::copiarSeguroPara(const std::string& destinoPath) {
    sqlite3* pDest = nullptr;
    if (sqlite3_open(destinoPath.c_str(), &pDest) != SQLITE_OK) {
        std::string err = pDest ? sqlite3_errmsg(pDest) : "unknown error";
        if (pDest) sqlite3_close(pDest);
        throw DatabaseError("failed to open destination database for backup: " + err);
    }
    Trava trava(*this);
    sqlite3_backup* pBackup = sqlite3_backup_init(pDest, "main", db_, "main");
    if (!pBackup) {
        std::string err = sqlite3_errmsg(pDest);
        sqlite3_close(pDest);
        throw DatabaseError("failed to initialize backup: " + err);
    }
    int rc = sqlite3_backup_step(pBackup, -1);
    sqlite3_backup_finish(pBackup);
    if (rc != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(pDest);
        sqlite3_close(pDest);
        throw DatabaseError("failed to complete safe database backup: " + err);
    }
    sqlite3_close(pDest);
}

} // namespace matriz::db
