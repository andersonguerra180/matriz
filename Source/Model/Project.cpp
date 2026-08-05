#include "Project.h"

#include <ctime>

#include "BinaryData.h"

namespace matriz::model {

std::string modoToString(Modo m) { return m == Modo::Preservacao ? "preservacao" : "catalogo"; }

Modo modoFromString(const std::string& s) {
    if (s == "preservacao") return Modo::Preservacao;
    if (s == "catalogo") return Modo::Catalogo;
    throw ProjectError("modo de projeto desconhecido: \"" + s + "\"");
}

std::string agoraIso8601() {
    std::time_t t = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&t, &utc);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return std::string(buffer);
}

std::string novoUuid() {
    return juce::Uuid().toDashedString().toLowerCase().toStdString();
}

namespace {

std::string readBinarySql(const char* data, int size) { return std::string(data, static_cast<size_t>(size)); }

// CREATE TABLE IF NOT EXISTS cobre tabela nova, mas NÃO cobre coluna nova
// numa tabela que já existe — um projeto criado por uma versão anterior
// reabriria sem a coluna e a query falharia. Este é o mecanismo mínimo de
// migração aditiva: pergunta ao SQLite se a coluna existe (PRAGMA
// table_info) e só então faz o ALTER TABLE. Aditivo e idempotente, nunca
// remove nem renomeia nada (migração destrutiva continua fora de escopo
// até a Etapa 10).
void garantirColuna(matriz::db::Database& db, const std::string& tabela, const std::string& coluna,
                     const std::string& tipoEDefault) {
    auto stmt = db.prepare("SELECT COUNT(*) FROM pragma_table_info(?) WHERE name = ?");
    stmt.bind(1, matriz::db::Value::of(tabela));
    stmt.bind(2, matriz::db::Value::of(coluna));
    stmt.step();
    if (stmt.columnInt(0) > 0) return;
    db.exec("ALTER TABLE " + tabela + " ADD COLUMN " + coluna + " " + tipoEDefault);
}

void aplicarSchemas(matriz::db::Database& registro, matriz::db::Database& indice) {
    registro.execScript(readBinarySql(BinaryData::registro_sql, BinaryData::registro_sqlSize));
    indice.execScript(readBinarySql(BinaryData::indice_sql, BinaryData::indice_sqlSize));

    // Colunas acrescentadas depois da primeira versão do schema.
    garantirColuna(registro, "colecao_inteligente", "filtros_origem", "TEXT");
    garantirColuna(registro, "colecao_inteligente", "ano_de", "INTEGER");
    garantirColuna(registro, "colecao_inteligente", "ano_ate", "INTEGER");
    garantirColuna(registro, "projeto", "hierarquia_backup", "TEXT");
}

} // namespace

Project::Project(juce::File pastaProjeto, std::unique_ptr<matriz::db::Database> registro,
                  std::unique_ptr<matriz::db::Database> indice, std::string projetoId)
    : pastaProjeto_(std::move(pastaProjeto)),
      registro_(std::move(registro)),
      indice_(std::move(indice)),
      projetoId_(std::move(projetoId)) {}

std::unique_ptr<Project> Project::criar(const juce::File& pastaProjeto, const NovoProjetoParams& params) {
    if (params.nome.empty())
        throw ProjectError("nome do projeto é obrigatório");
    if (params.prefixoNomenclatura.empty())
        throw ProjectError("prefixo de nomenclatura é obrigatório");

    if (!pastaProjeto.exists()) {
        if (!pastaProjeto.createDirectory())
            throw ProjectError("não foi possível criar a pasta do projeto: " + pastaProjeto.getFullPathName().toStdString());
    } else if (!pastaProjeto.isDirectory()) {
        throw ProjectError("o caminho do projeto existe e não é uma pasta: " + pastaProjeto.getFullPathName().toStdString());
    }

    juce::File registroFile = pastaProjeto.getChildFile("registro.sqlite");
    juce::File indiceFile = pastaProjeto.getChildFile("indice.sqlite");
    if (registroFile.exists() || indiceFile.exists())
        throw ProjectError("a pasta já contém um projeto MATRIZ: " + pastaProjeto.getFullPathName().toStdString());

    auto registro = std::make_unique<matriz::db::Database>(registroFile.getFullPathName().toStdString());
    auto indice = std::make_unique<matriz::db::Database>(indiceFile.getFullPathName().toStdString());
    aplicarSchemas(*registro, *indice);

    std::string projetoId = novoUuid();
    std::string agora = agoraIso8601();

    registro->run(
        "INSERT INTO projeto (id, modo, nome, instituicao_ou_selo, responsavel, prefixo_nomenclatura, "
        "isrc_registrante, criado_em, atualizado_em) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
        {
            matriz::db::Value::of(projetoId),
            matriz::db::Value::of(modoToString(params.modo)),
            matriz::db::Value::of(params.nome),
            params.instituicaoOuSelo.empty() ? matriz::db::Value::null() : matriz::db::Value::of(params.instituicaoOuSelo),
            params.responsavel.empty() ? matriz::db::Value::null() : matriz::db::Value::of(params.responsavel),
            matriz::db::Value::of(params.prefixoNomenclatura),
            params.isrcRegistrante.empty() ? matriz::db::Value::null() : matriz::db::Value::of(params.isrcRegistrante),
            matriz::db::Value::of(agora),
            matriz::db::Value::of(agora),
        });

    return std::unique_ptr<Project>(new Project(pastaProjeto, std::move(registro), std::move(indice), projetoId));
}

std::unique_ptr<Project> Project::abrir(const juce::File& pastaProjeto) {
    if (!pastaProjeto.isDirectory())
        throw ProjectError("pasta de projeto não encontrada: " + pastaProjeto.getFullPathName().toStdString());

    juce::File registroFile = pastaProjeto.getChildFile("registro.sqlite");
    juce::File indiceFile = pastaProjeto.getChildFile("indice.sqlite");
    if (!registroFile.existsAsFile() || !indiceFile.existsAsFile())
        throw ProjectError("pasta não contém um projeto MATRIZ válido (faltando registro.sqlite ou indice.sqlite): " +
                            pastaProjeto.getFullPathName().toStdString());

    auto registro = std::make_unique<matriz::db::Database>(registroFile.getFullPathName().toStdString());
    auto indice = std::make_unique<matriz::db::Database>(indiceFile.getFullPathName().toStdString());
    // Scripts de schema são idempotentes (CREATE TABLE/INDEX/TRIGGER IF NOT
    // EXISTS) — reaplicar ao abrir é o mecanismo de auto-atualização de
    // schema entre versões até a Etapa 10 trazer migração formal.
    aplicarSchemas(*registro, *indice);

    matriz::db::Statement stmt = registro->prepare("SELECT id FROM projeto LIMIT 1");
    if (!stmt.step())
        throw ProjectError("projeto corrompido: nenhuma linha em \"projeto\" no registro: " +
                            pastaProjeto.getFullPathName().toStdString());
    std::string projetoId = stmt.columnText(0);

    return std::unique_ptr<Project>(new Project(pastaProjeto, std::move(registro), std::move(indice), projetoId));
}

std::string Project::nome() {
    auto stmt = registro_->prepare("SELECT nome FROM projeto LIMIT 1");
    stmt.step();
    return stmt.columnText(0);
}

Modo Project::modo() {
    auto stmt = registro_->prepare("SELECT modo FROM projeto LIMIT 1");
    stmt.step();
    return modoFromString(stmt.columnText(0));
}

} // namespace matriz::model
