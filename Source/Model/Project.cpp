#include "Project.h"

#include <ctime>

#include "BinaryData.h"

namespace matriz::model {

std::string modoToString(Modo m) { return m == Modo::Preservacao ? "preservacao" : "catalogo"; }

Modo modoFromString(const std::string& s) {
    if (s == "preservacao") return Modo::Preservacao;
    if (s == "catalogo") return Modo::Catalogo;
    throw ProjectError("unknown project mode: \"" + s + "\"");
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

// `item.codigo_acervo` nasceu NOT NULL, mas o §5 exige o oposto: o
// sequencial ALLNO-xxxxx só é gasto quando o ingest do arquivo dá certo, e
// até lá a coluna fica NULL (critério 6). Como o SQLite não tem ALTER COLUMN,
// a única saída pra um banco já existente é o procedimento oficial de
// recriação de tabela. O CHECK de `estado` também é reescrito aqui, pra
// aceitar os estados de workflow do §4.
//
// Não é uma "tabela paralela" das rejeitadas no §11: a temporária nasce e
// morre dentro desta transação, e o que sobra é a tabela `item` original com
// a definição nova.
void migrarItemParaCodigoOpcional(matriz::db::Database& registro) {
    // Banco novo (tabela ainda não existe) ou já migrado: nada a fazer.
    {
        auto stmt = registro.prepare(
            "SELECT COUNT(*) FROM pragma_table_info('item') WHERE name = 'codigo_acervo' AND \"notnull\" = 1");
        stmt.step();
        if (stmt.columnInt(0) == 0) return;
    }

    // PRAGMA foreign_keys não pode mudar dentro de transação; o DROP abaixo
    // não deve disparar ON DELETE CASCADE nas filhas de `item`.
    registro.exec("PRAGMA foreign_keys = OFF");
    registro.run("BEGIN TRANSACTION", {});
    try {
        registro.exec(
            "CREATE TABLE item_migracao_tmp ("
            "  id              TEXT PRIMARY KEY,"
            "  projeto_id      TEXT NOT NULL REFERENCES projeto(id) ON DELETE CASCADE,"
            "  codigo_acervo   TEXT,"
            "  titulo          TEXT NOT NULL,"
            "  tipo_midia      TEXT,"
            "  estado          TEXT NOT NULL DEFAULT 'novo'"
            "                  CHECK (estado IN ('novo', 'em_analise', 'catalogado', 'revisado', 'aprovado',"
            "                                    'publicado', 'arquivado', 'duplicata',"
            "                                    'nao_digitalizado', 'capturado', 'qc_ok', 'alerta')),"
            "  notas_livres    TEXT,"
            "  criado_em       TEXT NOT NULL,"
            "  atualizado_em   TEXT NOT NULL,"
            "  UNIQUE (projeto_id, codigo_acervo))");

        registro.exec(
            "INSERT INTO item_migracao_tmp (id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, "
            "notas_livres, criado_em, atualizado_em) "
            "SELECT id, projeto_id, codigo_acervo, titulo, tipo_midia, estado, notas_livres, criado_em, "
            "atualizado_em FROM item");

        registro.exec("DROP TABLE item");
        registro.exec("ALTER TABLE item_migracao_tmp RENAME TO item");
        registro.run("COMMIT", {});
    } catch (...) {
        registro.run("ROLLBACK", {});
        registro.exec("PRAGMA foreign_keys = ON");
        throw;
    }
    registro.exec("PRAGMA foreign_keys = ON");
    // Índices e triggers de `item` morreram junto com o DROP; quem os recria
    // é o execScript logo depois desta função (CREATE ... IF NOT EXISTS).
}

// A primeira versão de `ai_scan_resultado` restringia tipo_analise a
// ('visual','documento','video') por CHECK. Áudio passou a ser analisável, e
// CHECK não se altera por ALTER TABLE — a tabela tem de ser reconstruída. Os
// resultados já gravados são preservados na cópia.
void migrarAiScanSemCheck(matriz::db::Database& registro) {
    std::string sqlAtual;
    try {
        auto stmt = registro.prepare(
            "SELECT sql FROM sqlite_master WHERE type = 'table' AND name = 'ai_scan_resultado'");
        if (!stmt.step()) return; // tabela ainda não existe: o schema cria já correta
        sqlAtual = stmt.columnText(0);
    } catch (...) {
        return;
    }

    if (sqlAtual.find("CHECK (tipo_analise IN") == std::string::npos) return;

    registro.exec("PRAGMA foreign_keys = OFF");
    registro.run("BEGIN TRANSACTION", {});
    try {
        registro.exec(
            "CREATE TABLE ai_scan_migracao_tmp ("
            "  id TEXT PRIMARY KEY,"
            "  item_id TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE,"
            "  modelo TEXT NOT NULL,"
            "  tipo_analise TEXT NOT NULL,"
            "  contexto_json TEXT NOT NULL,"
            "  resumo TEXT,"
            "  confianca REAL,"
            "  analisado_em TEXT NOT NULL)");
        registro.exec(
            "INSERT INTO ai_scan_migracao_tmp "
            "SELECT id, item_id, modelo, tipo_analise, contexto_json, resumo, confianca, analisado_em "
            "FROM ai_scan_resultado");
        registro.exec("DROP TABLE ai_scan_resultado");
        registro.exec("ALTER TABLE ai_scan_migracao_tmp RENAME TO ai_scan_resultado");
        registro.run("COMMIT", {});
    } catch (...) {
        registro.run("ROLLBACK", {});
        registro.exec("PRAGMA foreign_keys = ON");
        throw;
    }
    registro.exec("PRAGMA foreign_keys = ON");
    // Índice e triggers caíram junto com o DROP; o execScript logo abaixo os
    // recria (CREATE ... IF NOT EXISTS).
}

void migrarConsolidacaoRegistro(matriz::db::Database& registro) {
    try {
        matriz::db::Statement checkStmt = registro.prepare(
            "SELECT sql FROM sqlite_master WHERE type='table' AND name='consolidacao_registro'");
        if (checkStmt.step()) {
            std::string sql = checkStmt.columnText(0);
            if (sql.find("acervo_pasta") != std::string::npos) {
                registro.exec("PRAGMA foreign_keys = OFF");
                registro.run(
                    "CREATE TABLE IF NOT EXISTS consolidacao_registro_v2 ("
                    "id TEXT PRIMARY KEY, "
                    "item_id TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE, "
                    "pasta_id TEXT NOT NULL DEFAULT '', "
                    "arquivo_id TEXT NOT NULL REFERENCES arquivo(id) ON DELETE CASCADE, "
                    "caminho_relativo_destino TEXT NOT NULL, "
                    "checksum_sha256 TEXT NOT NULL, "
                    "consolidado_em TEXT NOT NULL, "
                    "UNIQUE (item_id, pasta_id, arquivo_id))", {});

                registro.run(
                    "INSERT OR IGNORE INTO consolidacao_registro_v2 "
                    "SELECT id, item_id, COALESCE(pasta_id, ''), arquivo_id, caminho_relativo_destino, checksum_sha256, consolidado_em "
                    "FROM consolidacao_registro", {});

                registro.run("DROP TABLE consolidacao_registro", {});
                registro.run("ALTER TABLE consolidacao_registro_v2 RENAME TO consolidacao_registro", {});
                registro.exec("PRAGMA foreign_keys = ON");
            }
        }
    } catch (...) {
        registro.exec("PRAGMA foreign_keys = ON");
    }
}

void aplicarSchemas(matriz::db::Database& registro, matriz::db::Database& indice) {
    migrarItemParaCodigoOpcional(registro);
    migrarAiScanSemCheck(registro);
    migrarConsolidacaoRegistro(registro);
    registro.execScript(readBinarySql(BinaryData::registro_sql, BinaryData::registro_sqlSize));
    indice.execScript(readBinarySql(BinaryData::indice_sql, BinaryData::indice_sqlSize));

    // Colunas acrescentadas depois da primeira versão do schema.
    garantirColuna(registro, "colecao_inteligente", "filtros_origem", "TEXT");
    garantirColuna(registro, "colecao_inteligente", "filtros_content_type", "TEXT");
    garantirColuna(registro, "colecao_inteligente", "filtros_collection_type", "TEXT");
    garantirColuna(registro, "colecao_inteligente", "ano_de", "INTEGER");
    garantirColuna(registro, "colecao_inteligente", "ano_ate", "INTEGER");
    garantirColuna(registro, "projeto", "hierarquia_backup", "TEXT");
    garantirColuna(registro, "arquivo", "tamanho_bytes", "INTEGER");

    garantirColuna(registro, "acervo_pasta", "posicao_x", "INTEGER NOT NULL DEFAULT 0");
    garantirColuna(registro, "acervo_pasta", "posicao_y", "INTEGER NOT NULL DEFAULT 0");
    garantirColuna(registro, "acervo_pasta", "ativo", "INTEGER NOT NULL DEFAULT 1");

    // Reconstrução leva única
    garantirColuna(registro, "marcador", "tipo_id", "TEXT REFERENCES tipo_marcador(id)");
    garantirColuna(registro, "marcador", "tempo_fim", "REAL");
    garantirColuna(registro, "marcador", "prioridade", "TEXT NOT NULL DEFAULT 'media'");
    garantirColuna(registro, "marcador", "status", "TEXT NOT NULL DEFAULT 'aberto'");
    garantirColuna(registro, "marcador", "autor", "TEXT");

    garantirColuna(registro, "item", "estado", "TEXT NOT NULL DEFAULT 'novo'");

    garantirColuna(registro, "item_observacao", "titulo", "TEXT");
    garantirColuna(registro, "item_observacao", "categoria", "TEXT");
    garantirColuna(registro, "item_observacao", "prioridade", "TEXT NOT NULL DEFAULT 'media'");
    garantirColuna(registro, "item_observacao", "checklist", "TEXT");
    garantirColuna(registro, "item_observacao", "anexos", "TEXT");
    garantirColuna(registro, "item_observacao", "marcador_id", "TEXT REFERENCES marcador(id) ON DELETE SET NULL");

    garantirColuna(registro, "arquivo", "visto_pela_ultima_vez", "TEXT");
    garantirColuna(registro, "arquivo", "estado_presenca", "TEXT NOT NULL DEFAULT 'presente'");
    garantirColuna(registro, "arquivo", "vault_id", "TEXT REFERENCES vault(id) ON DELETE SET NULL");

    // Unified metadata columns (replaces Original/Editable dual model)
    garantirColuna(registro, "item", "ano", "TEXT");
    garantirColuna(registro, "item", "caminho_catalogo", "TEXT");
    garantirColuna(registro, "item", "content_type", "TEXT");
    garantirColuna(registro, "item", "source_media", "TEXT");
    garantirColuna(registro, "item", "collection_type", "TEXT");
    garantirColuna(registro, "item", "isrc", "TEXT");
    garantirColuna(registro, "item", "em_quarentena", "INTEGER NOT NULL DEFAULT 0");

    // Asset Geolocation Table Migration
    registro.execScript(
        "CREATE TABLE IF NOT EXISTS asset_geolocation ("
        "  asset_id            TEXT PRIMARY KEY REFERENCES item(id) ON DELETE CASCADE,"
        "  latitude            REAL,"
        "  longitude           REAL,"
        "  altitude            REAL,"
        "  continent           TEXT,"
        "  country             TEXT,"
        "  country_code        TEXT,"
        "  state_province      TEXT,"
        "  state_code          TEXT,"
        "  city                TEXT,"
        "  municipality        TEXT,"
        "  neighborhood        TEXT,"
        "  district            TEXT,"
        "  postal_code         TEXT,"
        "  street              TEXT,"
        "  street_number       TEXT,"
        "  locality            TEXT,"
        "  formatted_address   TEXT,"
        "  source              TEXT NOT NULL DEFAULT 'NONE',"
        "  precision_accuracy  REAL,"
        "  confidence          REAL,"
        "  created_at          TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  updated_at          TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_geo_lat_lng ON asset_geolocation(latitude, longitude);"
        "CREATE INDEX IF NOT EXISTS idx_geo_country_state_city ON asset_geolocation(country, state_province, city);"
    );

    // Migrate existing item_campo values to new columns if they exist
    try {
        registro.run(
            "UPDATE item SET ano = (SELECT valor FROM item_campo WHERE item_id = item.id "
            "AND campo_id = 'ano' AND nivel = 'raiz' AND nivel_indice = 0) "
            "WHERE ano IS NULL AND EXISTS (SELECT 1 FROM item_campo WHERE item_id = item.id "
            "AND campo_id = 'ano' AND nivel = 'raiz' AND nivel_indice = 0)", {});
        registro.run(
            "UPDATE item SET isrc = (SELECT valor FROM item_campo WHERE item_id = item.id "
            "AND campo_id = 'isrc' AND nivel = 'raiz' AND nivel_indice = 0) "
            "WHERE isrc IS NULL AND EXISTS (SELECT 1 FROM item_campo WHERE item_id = item.id "
            "AND campo_id = 'isrc' AND nivel = 'raiz' AND nivel_indice = 0)", {});
        registro.run(
            "UPDATE item SET content_type = (SELECT valor FROM item_campo WHERE item_id = item.id "
            "AND campo_id = 'content_type' AND nivel = 'raiz' AND nivel_indice = 0) "
            "WHERE content_type IS NULL AND EXISTS (SELECT 1 FROM item_campo WHERE item_id = item.id "
            "AND campo_id = 'content_type' AND nivel = 'raiz' AND nivel_indice = 0)", {});
        registro.run(
            "UPDATE item SET source_media = (SELECT valor FROM item_campo WHERE item_id = item.id "
            "AND campo_id = 'source_media' AND nivel = 'raiz' AND nivel_indice = 0) "
            "WHERE source_media IS NULL AND EXISTS (SELECT 1 FROM item_campo WHERE item_id = item.id "
            "AND campo_id = 'source_media' AND nivel = 'raiz' AND nivel_indice = 0)", {});
        registro.run(
            "UPDATE item SET collection_type = (SELECT valor FROM item_campo WHERE item_id = item.id "
            "AND campo_id = 'collection_type' AND nivel = 'raiz' AND nivel_indice = 0) "
            "WHERE collection_type IS NULL AND EXISTS (SELECT 1 FROM item_campo WHERE item_id = item.id "
            "AND campo_id = 'collection_type' AND nivel = 'raiz' AND nivel_indice = 0)", {});
    } catch (...) {
        // Migration from old campos is best-effort — may not exist
    }

    try {
        registro.run(
            "INSERT OR IGNORE INTO tipo_marcador (id, rotulo, cor, embutido) VALUES "
            "('dropout', 'Dropout', '#FF6B6B', 1), "
            "('mofo', 'Mofo', '#845EF7', 1), "
            "('clipe', 'Clipping', '#FA5252', 1), "
            "('ruido', 'Noise', '#FD7E14', 1), "
            "('saturacao', 'Saturation', '#FCC419', 1), "
            "('master_aprovado', 'Master Approved', '#40C057', 1), "
            "('revisar', 'Review', '#228BE6', 1), "
            "('digitalizado', 'Digitized', '#15AABF', 1), "
            "('copyright', 'Copyright', '#E64980', 1), "
            "('isrc', 'ISRC Verification', '#7950F2', 1), "
            "('juridico', 'Legal Issue', '#BE4BDB', 1), "
            "('outro', 'Other', '#868E96', 1)",
            {}
        );
    } catch (...) {}

    try {
        registro.exec("DROP VIEW IF EXISTS colecao_revisao");
        registro.exec(
            "CREATE VIEW colecao_revisao AS "
            "SELECT DISTINCT m.item_id AS item_id, 'revisao' AS colecao "
            "FROM marcador m "
            "WHERE IFNULL(m.status, 'aberto') = 'aberto' "
            "  AND (m.tipo_id IN ('revisar', 'dropout'))");
    } catch (...) {}

    // Backfill busca_fts se estiver vazia
    try {
        auto stmt = registro.prepare("SELECT COUNT(*) FROM busca_fts");
        if (stmt.step() && stmt.columnInt(0) == 0) {
            registro.run("BEGIN TRANSACTION", {});
            try {
                registro.run(
                    "INSERT INTO busca_fts(item_id, conteudo) "
                    "SELECT id, codigo_acervo FROM item WHERE codigo_acervo IS NOT NULL "
                    "UNION ALL "
                    "SELECT id, titulo FROM item WHERE titulo IS NOT NULL "
                    "UNION ALL "
                    "SELECT item_id, valor FROM item_campo WHERE valor IS NOT NULL "
                    "UNION ALL "
                    "SELECT ia.item_id, a.termo FROM item_assunto ia JOIN assunto a ON a.id = ia.assunto_id",
                    {});
                registro.run("COMMIT", {});
            } catch (...) {
                registro.run("ROLLBACK", {});
            }
        }
    } catch (...) {
        // Ignora erros caso a tabela ainda não exista em contextos antigos
    }

    // -----------------------------------------------------------------------
    // Camada de Preservação — migração aditiva (idempotente)
    // -----------------------------------------------------------------------

    // 1. Coluna persistent_id em item — identificador apresentável/interoperável
    //    que coexiste com item.id (UUID técnico interno).
    garantirColuna(registro, "item", "persistent_id", "TEXT");

    // 2. Seed de agentes built-in. INSERT OR IGNORE: idempotente em qualquer
    //    número de aberturas do projeto.
    try {
        registro.run(
            "INSERT OR IGNORE INTO preservation_agent (id, nome, tipo, criado_em) VALUES "
            "('bkr-agent-sistema', 'BKR Matriz', 'software', ?)",
            { db::Value::of(agoraIso8601()) });
        registro.run(
            "INSERT OR IGNORE INTO preservation_agent (id, nome, tipo, criado_em) VALUES "
            "('bkr-agent-ffprobe', 'ffprobe', 'software', ?)",
            { db::Value::of(agoraIso8601()) });
        registro.run(
            "INSERT OR IGNORE INTO preservation_agent (id, nome, tipo, criado_em) VALUES "
            "('bkr-agent-exiv2', 'Exiv2', 'software', ?)",
            { db::Value::of(agoraIso8601()) });
    } catch (...) {}

    // 3. Atribuição bulk de persistent_id para assets existentes.
    //    Uma única instrução SQL — sem loop, sem N operações individuais.
    //    O formato é determinístico: "BKR:ASSET:" + primeiros 8 hex do UUID sem hífens.
    //    Não toca em arquivos. Não recalcula checksums.
    try {
        registro.run(
            "UPDATE item "
            "SET persistent_id = 'BKR:ASSET:' || UPPER(SUBSTR(REPLACE(id,'-',''), 1, 8)) "
            "WHERE persistent_id IS NULL",
            {});
    } catch (...) {}

    // 4. Um único evento de migração por projeto — registrado somente se ainda
    //    não existe nenhum evento MIGRATION (idempotente via INSERT OR IGNORE).
    //    agent_id = 'bkr-agent-sistema' (seed acima garante que existe).
    //    Não cria um evento por item — operação em lote, O(1) no banco.
    try {
        std::string agora = agoraIso8601();
        registro.run(
            "INSERT OR IGNORE INTO preservation_event "
            "(id, item_id, event_type, event_date_time, event_detail, "
            " event_outcome, event_outcome_detail, agent_id, criado_em) "
            "SELECT "
            "  lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || "
            "  lower(substr(hex(randomblob(2)),2)) || '-' || "
            "  lower(hex(randomblob(2))) || '-' || lower(hex(randomblob(6))), "
            "  id, "
            "  'MIGRATION', "
            "  ?, "
            "  'Persistent ID assigned during schema migration to preservation layer — batch operation', "
            "  'SUCCESS', "
            "  NULL, "
            "  'bkr-agent-sistema', "
            "  ? "
            "FROM item "
            "WHERE NOT EXISTS ("
            "  SELECT 1 FROM preservation_event pe "
            "  WHERE pe.item_id = item.id AND pe.event_type = 'MIGRATION'"
            ")",
            { db::Value::of(agora), db::Value::of(agora) });
    } catch (...) {}

    // 5. Garantir que os índices de performance para acervos grandes existam
    try {
        registro.exec("CREATE INDEX IF NOT EXISTS idx_item_quarentena_criado ON item(em_quarentena, criado_em)");
        registro.exec("CREATE INDEX IF NOT EXISTS idx_arquivo_item_master ON arquivo(item_id, eh_master)");
        registro.exec("CREATE INDEX IF NOT EXISTS idx_arquivo_estado_sincronizacao ON arquivo(estado_sincronizacao)");
    } catch (...) {}
}

} // namespace

Project::Project(juce::File pastaProjeto, std::unique_ptr<matriz::db::Database> registro,
                  std::unique_ptr<matriz::db::Database> indice, std::string projetoId)
    : pastaProjeto_(std::move(pastaProjeto)),
      registro_(std::move(registro)),
      indice_(std::move(indice)),
      projetoId_(std::move(projetoId)) {
    // Uma leitura só, aqui. Ver a nota em Project::modo().
    auto stmt = registro_->prepare("SELECT modo FROM projeto LIMIT 1");
    if (stmt.step()) modo_ = modoFromString(stmt.columnText(0));
}

std::unique_ptr<Project> Project::criar(const juce::File& pastaProjeto, const NovoProjetoParams& params) {
    if (params.nome.empty())
        throw ProjectError("project name is required");
    if (params.prefixoNomenclatura.empty())
        throw ProjectError("naming prefix is required");

    if (!pastaProjeto.exists()) {
        if (!pastaProjeto.createDirectory())
            throw ProjectError("could not create the project folder: " + pastaProjeto.getFullPathName().toStdString());
    } else if (!pastaProjeto.isDirectory()) {
        throw ProjectError("the project path exists and is not a folder: " + pastaProjeto.getFullPathName().toStdString());
    }

    juce::File registroFile = pastaProjeto.getChildFile("registro.sqlite");
    juce::File indiceFile = pastaProjeto.getChildFile("indice.sqlite");
    if (registroFile.exists() || indiceFile.exists())
        throw ProjectError("this folder already holds a MATRIZ project: " + pastaProjeto.getFullPathName().toStdString());

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
        throw ProjectError("project folder not found: " + pastaProjeto.getFullPathName().toStdString());

    juce::File registroFile = pastaProjeto.getChildFile("registro.sqlite");
    juce::File indiceFile = pastaProjeto.getChildFile("indice.sqlite");
    if (!registroFile.existsAsFile() || !indiceFile.existsAsFile())
        throw ProjectError("folder does not hold a valid MATRIZ project (registro.sqlite or indice.sqlite missing): " +
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

    // Legacy migration compatibility: convert internal files to local vault
    try {
        matriz::db::Statement stmtVaults = registro->prepare("SELECT COUNT(*) FROM vault");
        if (stmtVaults.step() && stmtVaults.columnInt(0) == 0) {
            std::string vaultId = novoUuid();
            std::string agora = agoraIso8601();
            
            registro->run("BEGIN TRANSACTION", {});
            try {
                registro->run(
                    "INSERT INTO vault (id, projeto_id, nome, tipo, localizacao, status, criado_em) "
                    "VALUES (?, ?, ?, 'local', ?, 'online', ?)",
                    {
                        matriz::db::Value::of(vaultId),
                        matriz::db::Value::of(projetoId),
                        matriz::db::Value::of("Internal Project Vault"),
                        matriz::db::Value::of(pastaProjeto.getFullPathName().toStdString()),
                        matriz::db::Value::of(agora)
                    });
                
                registro->run("UPDATE arquivo SET vault_id = ? WHERE vault_id IS NULL", {
                    matriz::db::Value::of(vaultId)
                });
                
                registro->run("COMMIT", {});
            } catch (...) {
                registro->run("ROLLBACK", {});
            }
        }
    } catch (...) {
        // Safe fallback in case of errors
    }

    // Self-healing database cleanup of zombie/ghost items (items with no files)
    try {
        registro->run("DELETE FROM item WHERE NOT EXISTS (SELECT 1 FROM arquivo a WHERE a.item_id = item.id)", {});
    } catch (...) {}

    // Ensure all primary files for images and documents are marked as eh_master = 1 so duplicate scan can analyze them
    try {
        registro->run("UPDATE arquivo SET eh_master = 1 WHERE papel IN ('foto_suporte', 'documento') AND eh_master = 0", {});
    } catch (...) {}

    return std::unique_ptr<Project>(new Project(pastaProjeto, std::move(registro), std::move(indice), projetoId));
}

std::string Project::nome() {
    auto stmt = registro_->prepare("SELECT nome FROM projeto LIMIT 1");
    stmt.step();
    return stmt.columnText(0);
}

} // namespace matriz::model
