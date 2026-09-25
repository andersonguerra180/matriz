#include "Project.h"
#include "ProjectLog.h"

#include <ctime>

#include "BinaryData.h"
#include "../Ingest/LeituraTecnica.h"

namespace matriz::model {

std::optional<DestinationInfo> DestinationInfo::lerDeArquivo(const juce::File& arquivoJson) {
    if (!arquivoJson.existsAsFile()) return std::nullopt;
    juce::var parsed = juce::JSON::parse(arquivoJson);
    if (!parsed.isObject()) return std::nullopt;

    DestinationInfo info;
    info.formato = parsed.getProperty("formato", 1);
    info.destinationId = parsed.getProperty("destination_id", "").toString().toStdString();
    info.projetoId = parsed.getProperty("projeto_id", "").toString().toStdString();
    info.papel = parsed.getProperty("papel", "ORIGINAL").toString().toStdString();
    info.rotulo = parsed.getProperty("rotulo", "").toString().toStdString();
    info.revisao = static_cast<int64_t>(static_cast<juce::int64>(parsed.getProperty("revisao", 1)));
    info.ultimaEdicaoUtc = parsed.getProperty("ultima_edicao_utc", "").toString().toStdString();
    info.criadoEm = parsed.getProperty("criado_em", "").toString().toStdString();
    return info;
}

bool DestinationInfo::gravarEmArquivo(const juce::File& arquivoJson) const {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("formato", formato);
    obj->setProperty("destination_id", juce::String(destinationId));
    obj->setProperty("projeto_id", juce::String(projetoId));
    obj->setProperty("papel", juce::String(papel));
    obj->setProperty("rotulo", juce::String(rotulo));
    obj->setProperty("revisao", static_cast<juce::int64>(revisao));
    obj->setProperty("ultima_edicao_utc", juce::String(ultimaEdicaoUtc));
    obj->setProperty("criado_em", juce::String(criadoEm));

    juce::String jsonStr = juce::JSON::toString(juce::var(obj.get()), false);

    // Escrita atômica: arquivo temporário no mesmo diretório + renomear/substituir
    juce::File parentDir = arquivoJson.getParentDirectory();
    if (!parentDir.exists()) parentDir.createDirectory();

    juce::File tempFile = parentDir.getChildFile(arquivoJson.getFileName() + ".tmp_" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()));
    if (!tempFile.replaceWithText(jsonStr)) return false;

    if (arquivoJson.exists()) arquivoJson.deleteFile();
    return tempFile.moveFileTo(arquivoJson);
}

juce::File Project::resolverPastaProjeto(const juce::File& qualquerPasta) {
    if (!qualquerPasta.exists()) return qualquerPasta;

    juce::File pastaBase = qualquerPasta;
    if (qualquerPasta.existsAsFile()) {
        pastaBase = qualquerPasta.getParentDirectory();
    }

    // Caso 1: Raiz de um DESTINATION contendo a subpasta Project/ com registro.sqlite
    juce::File projectSub = pastaBase.getChildFile("Project");
    if (projectSub.isDirectory() && projectSub.getChildFile("registro.sqlite").existsAsFile()) {
        return projectSub;
    }

    // Caso 2: A própria pasta Project/ ou pasta legada contendo registro.sqlite
    if (pastaBase.getChildFile("registro.sqlite").existsAsFile()) {
        return pastaBase;
    }

    // Caso 3: destination.json presente na raiz com subpasta Project
    if (pastaBase.getChildFile("destination.json").existsAsFile() && projectSub.isDirectory()) {
        return projectSub;
    }

    return pastaBase;
}

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

juce::File normalizarParaRaizDestino(const juce::File& f) {
    if (f == juce::File()) return f;

    juce::File cur = f;
    if (cur.existsAsFile()) {
        cur = cur.getParentDirectory();
    }

    // Se o caminho estiver dentro de (ou for) uma pasta "Media" ou "Project",
    // encontrar o ancestral mais alto com esse nome e subir para o pai dele.
    juce::File temp = cur;
    juce::File highestMediaOrProject;
    while (temp != juce::File() && temp != temp.getParentDirectory()) {
        juce::String name = temp.getFileName();
        if (name.equalsIgnoreCase("Media") || name.equalsIgnoreCase("Project")) {
            highestMediaOrProject = temp;
        }
        temp = temp.getParentDirectory();
    }

    if (highestMediaOrProject != juce::File()) {
        cur = highestMediaOrProject.getParentDirectory();
    }

    // Se a raiz encontrada ainda não tem destination.json, mas algum ancestral tem,
    // subir até a raiz que contém destination.json.
    if (!cur.getChildFile("destination.json").existsAsFile()) {
        juce::File check = cur;
        while (check != juce::File() && check != check.getParentDirectory()) {
            if (check.getChildFile("destination.json").existsAsFile()) {
                cur = check;
                break;
            }
            check = check.getParentDirectory();
        }
    }

    return cur;
}

void sanitizarEstruturaDestino(const juce::File& pasta) {
    if (pasta == juce::File()) return;
    juce::File raiz = normalizarParaRaizDestino(pasta);
    if (!raiz.isDirectory()) return;

    juce::File projDir = raiz.getChildFile("Project");
    juce::File mediaDir = raiz.getChildFile("Media");

    auto moverConteudoDiretorio = [](const juce::File& origemDir, const juce::File& destinoDir) {
        if (!origemDir.isDirectory()) return;
        if (!destinoDir.isDirectory()) destinoDir.createDirectory();
        for (const auto& f : origemDir.findChildFiles(juce::File::findFilesAndDirectories, false)) {
            juce::File destFile = destinoDir.getChildFile(f.getFileName());
            if (f.isDirectory()) {
                if (destFile.exists()) {
                    for (const auto& sub : f.findChildFiles(juce::File::findFilesAndDirectories, false)) {
                        juce::File subDest = destFile.getChildFile(sub.getFileName());
                        if (!subDest.exists()) sub.moveFileTo(subDest);
                    }
                    f.deleteRecursively();
                } else {
                    f.moveFileTo(destFile);
                }
            } else {
                if (destFile.exists()) f.deleteFile();
                else f.moveFileTo(destFile);
            }
        }
        origemDir.deleteRecursively();
    };

    // 1. Limpeza na raiz do destino:
    // log/ na raiz -> mover para Project/log/
    juce::File rootLog = raiz.getChildFile("log");
    if (rootLog.isDirectory()) {
        moverConteudoDiretorio(rootLog, projDir.getChildFile("log"));
    }

    // _lixeira/ na raiz -> mover para Project/_lixeira/
    juce::File rootLixeira = raiz.getChildFile("_lixeira");
    if (rootLixeira.isDirectory()) {
        moverConteudoDiretorio(rootLixeira, projDir.getChildFile("_lixeira"));
    }

    // 2. Limpeza profunda dentro de Media/:
    if (mediaDir.isDirectory()) {
        // destination.json dentro de Media/ -> mover para raiz se faltar, ou apagar
        juce::File mediaDestJson = mediaDir.getChildFile("destination.json");
        if (mediaDestJson.existsAsFile()) {
            juce::File raizDestJson = raiz.getChildFile("destination.json");
            if (!raizDestJson.existsAsFile()) {
                mediaDestJson.moveFileTo(raizDestJson);
            } else {
                mediaDestJson.deleteFile();
            }
        }

        // _lixeira dentro de Media/ -> mover para Project/_lixeira/
        juce::File mediaLixeira = mediaDir.getChildFile("_lixeira");
        if (mediaLixeira.isDirectory()) {
            moverConteudoDiretorio(mediaLixeira, projDir.getChildFile("_lixeira"));
        }

        // log dentro de Media/ -> mover para Project/log/
        juce::File mediaLog = mediaDir.getChildFile("log");
        if (mediaLog.isDirectory()) {
            moverConteudoDiretorio(mediaLog, projDir.getChildFile("log"));
        }

        // relatorios dentro de Media/ -> mover para Project/relatorios/
        juce::File mediaRel = mediaDir.getChildFile("relatorios");
        if (mediaRel.isDirectory()) {
            moverConteudoDiretorio(mediaRel, projDir.getChildFile("relatorios"));
        }

        // catalogo dentro de Media/ -> mover para Project/catalogo/
        juce::File mediaCat = mediaDir.getChildFile("catalogo");
        if (mediaCat.isDirectory()) {
            moverConteudoDiretorio(mediaCat, projDir.getChildFile("catalogo"));
        }

        // Project dentro de Media/ -> mover conteúdo para Project/ e remover
        juce::File mediaProject = mediaDir.getChildFile("Project");
        if (mediaProject.isDirectory()) {
            moverConteudoDiretorio(mediaProject, projDir);
        }

        // Media aninhada dentro de Media/ (Media/Media) -> mover conteúdo para Media/ e remover
        juce::File mediaNested = mediaDir.getChildFile("Media");
        if (mediaNested.isDirectory()) {
            moverConteudoDiretorio(mediaNested, mediaDir);
        }

        // Arquivos de sistema/banco soltos dentro de Media/ -> mover para Project/
        for (const auto& f : mediaDir.findChildFiles(juce::File::findFiles, false)) {
            juce::String ext = f.getFileExtension().toLowerCase();
            juce::String name = f.getFileName();
            if (ext == ".sqlite" || ext.startsWith(".sqlite-") || ext == ".mtz" || ext == ".bkm" ||
                name == "matriz_operacoes.log" || name == "log.md") {
                if (!projDir.isDirectory()) projDir.createDirectory();
                juce::File dest = projDir.getChildFile(name);
                if (!dest.exists()) f.moveFileTo(dest);
                else f.deleteFile();
            }
        }
    }
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

// Migra ai_scan_resultado do banco de registro para o banco de índice (P2: IA nunca escreve no registro).
void migrarAiScanParaIndice(matriz::db::Database& registro, matriz::db::Database& indice) {
    try {
        auto stmt = registro.prepare(
            "SELECT sql FROM sqlite_master WHERE type = 'table' AND name = 'ai_scan_resultado'");
        if (!stmt.step()) return; // não existe no registro: nada a migrar
    } catch (...) {
        return;
    }

    try {
        // Garante que a tabela existe no índice
        indice.exec(
            "CREATE TABLE IF NOT EXISTS ai_scan_resultado ("
            "  id TEXT PRIMARY KEY,"
            "  item_id TEXT NOT NULL,"
            "  modelo TEXT NOT NULL,"
            "  tipo_analise TEXT NOT NULL,"
            "  contexto_json TEXT NOT NULL,"
            "  resumo TEXT,"
            "  confianca REAL,"
            "  analisado_em TEXT NOT NULL)");

        // Lê todos os dados existentes no registro
        auto sel = registro.prepare(
            "SELECT id, item_id, modelo, tipo_analise, contexto_json, resumo, confianca, analisado_em "
            "FROM ai_scan_resultado");

        indice.run("BEGIN TRANSACTION", {});
        try {
            while (sel.step()) {
                indice.run(
                    "INSERT OR REPLACE INTO ai_scan_resultado "
                    "(id, item_id, modelo, tipo_analise, contexto_json, resumo, confianca, analisado_em) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                    {matriz::db::Value::of(sel.columnText(0)), matriz::db::Value::of(sel.columnText(1)),
                     matriz::db::Value::of(sel.columnText(2)), matriz::db::Value::of(sel.columnText(3)),
                     matriz::db::Value::of(sel.columnText(4)),
                     sel.columnIsNull(5) ? matriz::db::Value::null() : matriz::db::Value::of(sel.columnText(5)),
                     sel.columnIsNull(6) ? matriz::db::Value::null() : matriz::db::Value::of(sel.columnReal(6)),
                     matriz::db::Value::of(sel.columnText(7))});
            }
            indice.run("COMMIT", {});
        } catch (...) {
            indice.run("ROLLBACK", {});
        }

        // Remove triggers e a tabela do banco de registro
        try { registro.exec("DROP TRIGGER IF EXISTS trg_ai_scan_busca_insert"); } catch (...) {}
        try { registro.exec("DROP TRIGGER IF EXISTS trg_ai_scan_busca_delete"); } catch (...) {}
        try { registro.exec("DROP TABLE IF EXISTS ai_scan_resultado"); } catch (...) {}
    } catch (...) {}
}

void migrarConsolidacaoRegistro(matriz::db::Database& registro) {
    try {
        matriz::db::Statement checkStmt = registro.prepare(
            "SELECT sql FROM sqlite_master WHERE type='table' AND name='consolidacao_registro'");
        if (checkStmt.step()) {
            std::string sql = checkStmt.columnText(0);
            if (sql.find("destino_path") == std::string::npos || sql.find("acervo_pasta") != std::string::npos) {
                registro.exec("PRAGMA foreign_keys = OFF");
                registro.run(
                    "CREATE TABLE IF NOT EXISTS consolidacao_registro_v3 ("
                    "id TEXT PRIMARY KEY, "
                    "item_id TEXT NOT NULL REFERENCES item(id) ON DELETE CASCADE, "
                    "pasta_id TEXT NOT NULL DEFAULT '', "
                    "arquivo_id TEXT NOT NULL REFERENCES arquivo(id) ON DELETE CASCADE, "
                    "caminho_relativo_destino TEXT NOT NULL, "
                    "checksum_sha256 TEXT NOT NULL, "
                    "consolidado_em TEXT NOT NULL, "
                    "destino_path TEXT NOT NULL DEFAULT '', "
                    "UNIQUE (item_id, pasta_id, arquivo_id, destino_path))", {});

                if (sql.find("destino_path") != std::string::npos) {
                    registro.run(
                        "INSERT OR IGNORE INTO consolidacao_registro_v3 "
                        "SELECT id, item_id, COALESCE(pasta_id, ''), arquivo_id, caminho_relativo_destino, checksum_sha256, consolidado_em, COALESCE(destino_path, '') "
                        "FROM consolidacao_registro", {});
                } else {
                    registro.run(
                        "INSERT OR IGNORE INTO consolidacao_registro_v3 "
                        "SELECT id, item_id, COALESCE(pasta_id, ''), arquivo_id, caminho_relativo_destino, checksum_sha256, consolidado_em, '' "
                        "FROM consolidacao_registro", {});
                }

                registro.run("DROP TABLE consolidacao_registro", {});
                registro.run("ALTER TABLE consolidacao_registro_v3 RENAME TO consolidacao_registro", {});
                registro.exec("PRAGMA foreign_keys = ON");
            }
        }
    } catch (...) {
        registro.exec("PRAGMA foreign_keys = ON");
    }
}

// Achado investigando um projeto que ficava preso em "Loading Project..."
// depois de um lote de ingest grande interrompido: trg_item_busca_update
// disparava em QUALQUER UPDATE de `item` (não só titulo/codigo_acervo, os
// únicos campos que o corpo do gatilho usa), e cada disparo faz um DELETE
// por valor na busca_fts (FTS5 — sem índice pra isso, é uma varredura do
// índice inteiro). A própria aplicarSchemas() roda um UPDATE em massa pra
// sincronizar item_campo -> colunas de `item` em TODO item que abre o
// projeto — com milhares de itens recém-ingeridos (ano/isrc/content_type/
// source_media/collection_type ainda NULL), isso virava milhares de
// varreduras completas da FTS, uma abertura de projeto genuinamente lenta
// (não travada, só muito lenta) sendo confundida com "storage travado".
//
// `registro.sql` já tem a definição corrigida (AFTER UPDATE OF titulo,
// codigo_acervo), mas CREATE TRIGGER IF NOT EXISTS não recria um gatilho
// que já existe — projetos criados antes desta correção continuam com a
// definição antiga presa no banco pra sempre, a menos que alguém troque
// explicitamente. É isso que esta migração faz.
void migrarEscopoGatilhoBusca(matriz::db::Database& registro) {
    try {
        matriz::db::Statement checkStmt = registro.prepare(
            "SELECT sql FROM sqlite_master WHERE type='trigger' AND name='trg_item_busca_update'");
        if (checkStmt.step()) {
            std::string sql = checkStmt.columnText(0);
            if (sql.find("UPDATE OF") == std::string::npos) {
                registro.exec("DROP TRIGGER trg_item_busca_update");
                registro.exec(
                    "CREATE TRIGGER trg_item_busca_update AFTER UPDATE OF titulo, codigo_acervo ON item "
                    "FOR EACH ROW BEGIN "
                    "DELETE FROM busca_fts WHERE item_id = old.id "
                    "AND (conteudo = IFNULL(old.codigo_acervo, '') OR conteudo = old.titulo); "
                    "INSERT INTO busca_fts(item_id, conteudo) SELECT new.id, new.codigo_acervo WHERE new.codigo_acervo IS NOT NULL; "
                    "INSERT INTO busca_fts(item_id, conteudo) VALUES (new.id, new.titulo); "
                    "END");
            }
        }
    } catch (...) {}
}

// Fase 2a (freeze de edição de metadado): busca_fts_map(fts_rowid, item_id,
// conteudo) espelha cada linha de busca_fts com o rowid dela, pra os
// gatilhos apagarem por rowid (rápido) em vez de "item_id = ? AND
// conteudo = ?" (varredura completa da FTS -- ver comentário em
// schema/registro.sql). CREATE TRIGGER IF NOT EXISTS não recria um gatilho
// que já existe, então projetos criados antes desta correção continuam com
// os gatilhos antigos presos no banco pra sempre -- é isso que esta
// migração resolve.
//
// Detecção: inspeciona o SQL de um gatilho representativo
// (trg_item_campo_busca_delete) em vez de checar se busca_fts_map existe --
// a tabela já foi criada pelo execScript() (CREATE TABLE IF NOT EXISTS)
// alguns instantes atrás, tanto em bancos novos quanto antigos, então sua
// mera existência não diferencia os dois casos.
//
// Reconstrução: mais simples e seguro reconstruir busca_fts + o mapa do
// zero a partir das tabelas de origem do que tentar inferir a qual linha
// de origem cada linha já existente na FTS pertence (sem essa informação,
// não haveria como preencher fts_rowid nela). Rowid do FTS5 é sequencial
// na ordem de inserção -- depois de repopular busca_fts, um único
// INSERT...SELECT copia (rowid, item_id, conteudo) pro mapa de uma vez,
// sem precisar rastrear rowid linha por linha como os gatilhos fazem.
void migrarBuscaFtsIndexada(matriz::db::Database& registro) {
    try {
        matriz::db::Statement check = registro.prepare(
            "SELECT sql FROM sqlite_master WHERE type='trigger' AND name='trg_item_campo_busca_delete'");
        if (check.step()) {
            std::string sql = check.columnText(0);
            if (sql.find("busca_fts_map") != std::string::npos) return; // já migrado (ou banco novo)
        } else {
            return; // gatilho nem existe ainda -- execScript() cuida disso
        }

        registro.exec("BEGIN IMMEDIATE");

        const char* gatilhosAntigos[] = {
            "trg_item_busca_insert", "trg_item_busca_delete", "trg_item_busca_update",
            "trg_item_campo_busca_insert", "trg_item_campo_busca_delete", "trg_item_campo_busca_update",
            "trg_item_assunto_busca_insert", "trg_item_assunto_busca_delete",
            "trg_item_tag_busca_insert", "trg_item_tag_busca_delete",
            "trg_item_observacao_busca_insert", "trg_item_observacao_busca_delete",
            "trg_arquivo_busca_insert", "trg_arquivo_busca_delete"
        };
        for (const char* nome : gatilhosAntigos) {
            registro.exec(std::string("DROP TRIGGER IF EXISTS ") + nome);
        }

        // Mesmos corpos de schema/registro.sql — CREATE TRIGGER (sem IF NOT
        // EXISTS: acabamos de garantir que não existem mais).
        registro.execScript(
            "CREATE TRIGGER trg_item_busca_insert AFTER INSERT ON item FOR EACH ROW BEGIN "
            "INSERT INTO busca_fts(item_id, conteudo) SELECT new.id, new.codigo_acervo WHERE new.codigo_acervo IS NOT NULL; "
            "INSERT INTO busca_fts_map(fts_rowid, item_id, conteudo) SELECT last_insert_rowid(), new.id, new.codigo_acervo WHERE new.codigo_acervo IS NOT NULL; "
            "INSERT INTO busca_fts(item_id, conteudo) VALUES (new.id, new.titulo); "
            "INSERT INTO busca_fts_map(fts_rowid, item_id, conteudo) VALUES (last_insert_rowid(), new.id, new.titulo); "
            "END;"

            "CREATE TRIGGER trg_item_busca_delete AFTER DELETE ON item FOR EACH ROW BEGIN "
            "DELETE FROM busca_fts WHERE rowid IN (SELECT fts_rowid FROM busca_fts_map WHERE item_id = old.id); "
            "DELETE FROM busca_fts_map WHERE item_id = old.id; "
            "END;"

            "CREATE TRIGGER trg_item_busca_update AFTER UPDATE OF titulo, codigo_acervo ON item FOR EACH ROW BEGIN "
            "DELETE FROM busca_fts WHERE rowid IN (SELECT fts_rowid FROM busca_fts_map WHERE item_id = old.id "
            "AND (conteudo = IFNULL(old.codigo_acervo, '') OR conteudo = old.titulo)); "
            "DELETE FROM busca_fts_map WHERE item_id = old.id "
            "AND (conteudo = IFNULL(old.codigo_acervo, '') OR conteudo = old.titulo); "
            "INSERT INTO busca_fts(item_id, conteudo) SELECT new.id, new.codigo_acervo WHERE new.codigo_acervo IS NOT NULL; "
            "INSERT INTO busca_fts_map(fts_rowid, item_id, conteudo) SELECT last_insert_rowid(), new.id, new.codigo_acervo WHERE new.codigo_acervo IS NOT NULL; "
            "INSERT INTO busca_fts(item_id, conteudo) VALUES (new.id, new.titulo); "
            "INSERT INTO busca_fts_map(fts_rowid, item_id, conteudo) VALUES (last_insert_rowid(), new.id, new.titulo); "
            "END;"

            "CREATE TRIGGER trg_item_campo_busca_insert AFTER INSERT ON item_campo FOR EACH ROW BEGIN "
            "INSERT INTO busca_fts(item_id, conteudo) VALUES (new.item_id, new.valor); "
            "INSERT INTO busca_fts_map(fts_rowid, item_id, conteudo) VALUES (last_insert_rowid(), new.item_id, new.valor); "
            "END;"

            "CREATE TRIGGER trg_item_campo_busca_delete AFTER DELETE ON item_campo FOR EACH ROW BEGIN "
            "DELETE FROM busca_fts WHERE rowid IN (SELECT fts_rowid FROM busca_fts_map WHERE item_id = old.item_id AND conteudo = old.valor); "
            "DELETE FROM busca_fts_map WHERE item_id = old.item_id AND conteudo = old.valor; "
            "END;"

            "CREATE TRIGGER trg_item_campo_busca_update AFTER UPDATE ON item_campo FOR EACH ROW BEGIN "
            "DELETE FROM busca_fts WHERE rowid IN (SELECT fts_rowid FROM busca_fts_map WHERE item_id = old.item_id AND conteudo = old.valor); "
            "DELETE FROM busca_fts_map WHERE item_id = old.item_id AND conteudo = old.valor; "
            "INSERT INTO busca_fts(item_id, conteudo) VALUES (new.item_id, new.valor); "
            "INSERT INTO busca_fts_map(fts_rowid, item_id, conteudo) VALUES (last_insert_rowid(), new.item_id, new.valor); "
            "END;"

            "CREATE TRIGGER trg_item_assunto_busca_insert AFTER INSERT ON item_assunto FOR EACH ROW BEGIN "
            "INSERT INTO busca_fts(item_id, conteudo) VALUES (new.item_id, (SELECT termo FROM assunto WHERE id = new.assunto_id)); "
            "INSERT INTO busca_fts_map(fts_rowid, item_id, conteudo) VALUES (last_insert_rowid(), new.item_id, (SELECT termo FROM assunto WHERE id = new.assunto_id)); "
            "END;"

            "CREATE TRIGGER trg_item_assunto_busca_delete AFTER DELETE ON item_assunto FOR EACH ROW BEGIN "
            "DELETE FROM busca_fts WHERE rowid IN (SELECT fts_rowid FROM busca_fts_map WHERE item_id = old.item_id AND conteudo = (SELECT termo FROM assunto WHERE id = old.assunto_id)); "
            "DELETE FROM busca_fts_map WHERE item_id = old.item_id AND conteudo = (SELECT termo FROM assunto WHERE id = old.assunto_id); "
            "END;"

            "CREATE TRIGGER trg_item_tag_busca_insert AFTER INSERT ON item_tag FOR EACH ROW BEGIN "
            "INSERT INTO busca_fts(item_id, conteudo) VALUES (new.item_id, new.tag); "
            "INSERT INTO busca_fts_map(fts_rowid, item_id, conteudo) VALUES (last_insert_rowid(), new.item_id, new.tag); "
            "INSERT INTO busca_fts(item_id, conteudo) VALUES (new.item_id, '#' || new.tag); "
            "INSERT INTO busca_fts_map(fts_rowid, item_id, conteudo) VALUES (last_insert_rowid(), new.item_id, '#' || new.tag); "
            "END;"

            "CREATE TRIGGER trg_item_tag_busca_delete AFTER DELETE ON item_tag FOR EACH ROW BEGIN "
            "DELETE FROM busca_fts WHERE rowid IN (SELECT fts_rowid FROM busca_fts_map WHERE item_id = old.item_id AND (conteudo = old.tag OR conteudo = '#' || old.tag)); "
            "DELETE FROM busca_fts_map WHERE item_id = old.item_id AND (conteudo = old.tag OR conteudo = '#' || old.tag); "
            "END;"

            "CREATE TRIGGER trg_item_observacao_busca_insert AFTER INSERT ON item_observacao FOR EACH ROW BEGIN "
            "INSERT INTO busca_fts(item_id, conteudo) VALUES (new.item_id, new.texto); "
            "INSERT INTO busca_fts_map(fts_rowid, item_id, conteudo) VALUES (last_insert_rowid(), new.item_id, new.texto); "
            "END;"

            "CREATE TRIGGER trg_item_observacao_busca_delete AFTER DELETE ON item_observacao FOR EACH ROW BEGIN "
            "DELETE FROM busca_fts WHERE rowid IN (SELECT fts_rowid FROM busca_fts_map WHERE item_id = old.item_id AND conteudo = old.texto); "
            "DELETE FROM busca_fts_map WHERE item_id = old.item_id AND conteudo = old.texto; "
            "END;"

            "CREATE TRIGGER trg_arquivo_busca_insert AFTER INSERT ON arquivo FOR EACH ROW BEGIN "
            "INSERT INTO busca_fts(item_id, conteudo) VALUES (new.item_id, new.caminho_relativo); "
            "INSERT INTO busca_fts_map(fts_rowid, item_id, conteudo) VALUES (last_insert_rowid(), new.item_id, new.caminho_relativo); "
            "INSERT INTO busca_fts(item_id, conteudo) SELECT new.item_id, new.caminho_absoluto_origem WHERE new.caminho_absoluto_origem IS NOT NULL; "
            "INSERT INTO busca_fts_map(fts_rowid, item_id, conteudo) SELECT last_insert_rowid(), new.item_id, new.caminho_absoluto_origem WHERE new.caminho_absoluto_origem IS NOT NULL; "
            "END;"

            "CREATE TRIGGER trg_arquivo_busca_delete AFTER DELETE ON arquivo FOR EACH ROW BEGIN "
            "DELETE FROM busca_fts WHERE rowid IN (SELECT fts_rowid FROM busca_fts_map WHERE item_id = old.item_id AND (conteudo = old.caminho_relativo OR conteudo = IFNULL(old.caminho_absoluto_origem, ''))); "
            "DELETE FROM busca_fts_map WHERE item_id = old.item_id AND (conteudo = old.caminho_relativo OR conteudo = IFNULL(old.caminho_absoluto_origem, '')); "
            "END;"
        );

        // Reconstrução completa: TRUNCATE + repopular a partir das tabelas
        // de origem, mesma lógica de conteúdo dos gatilhos de INSERT acima.
        registro.exec("DELETE FROM busca_fts");
        registro.exec("DELETE FROM busca_fts_map");

        registro.exec("INSERT INTO busca_fts(item_id, conteudo) SELECT id, codigo_acervo FROM item WHERE codigo_acervo IS NOT NULL");
        registro.exec("INSERT INTO busca_fts(item_id, conteudo) SELECT id, titulo FROM item");
        registro.exec("INSERT INTO busca_fts(item_id, conteudo) SELECT item_id, valor FROM item_campo");
        registro.exec("INSERT INTO busca_fts(item_id, conteudo) SELECT ia.item_id, a.termo FROM item_assunto ia JOIN assunto a ON a.id = ia.assunto_id");
        registro.exec("INSERT INTO busca_fts(item_id, conteudo) SELECT item_id, tag FROM item_tag");
        registro.exec("INSERT INTO busca_fts(item_id, conteudo) SELECT item_id, '#' || tag FROM item_tag");
        registro.exec("INSERT INTO busca_fts(item_id, conteudo) SELECT item_id, texto FROM item_observacao");
        registro.exec("INSERT INTO busca_fts(item_id, conteudo) SELECT item_id, caminho_relativo FROM arquivo");
        registro.exec("INSERT INTO busca_fts(item_id, conteudo) SELECT item_id, caminho_absoluto_origem FROM arquivo WHERE caminho_absoluto_origem IS NOT NULL");

        // rowid do FTS5 já está lá pra cada linha recém-inserida -- copia
        // tudo de uma vez, sem precisar dos INSERTs intercalados que os
        // gatilhos usam pra capturar rowid por linha.
        registro.exec("INSERT INTO busca_fts_map(fts_rowid, item_id, conteudo) SELECT rowid, item_id, conteudo FROM busca_fts");

        registro.exec("COMMIT");
    } catch (...) {
        try { registro.exec("ROLLBACK"); } catch (...) {}
    }
}

void aplicarSchemas(matriz::db::Database& registro, matriz::db::Database& indice) {
    migrarItemParaCodigoOpcional(registro);
    migrarAiScanParaIndice(registro, indice);
    migrarConsolidacaoRegistro(registro);
    registro.execScript(readBinarySql(BinaryData::registro_sql, BinaryData::registro_sqlSize));
    indice.execScript(readBinarySql(BinaryData::indice_sql, BinaryData::indice_sqlSize));
    // Precisa rodar DEPOIS do execScript acima (que só cria o gatilho se
    // ele ainda não existir) e ANTES das migrações de item_campo -> item
    // logo abaixo, que são justamente o UPDATE em massa que ficava lento.
    migrarEscopoGatilhoBusca(registro);
    // Idem: gatilhos antigos de busca_fts (sem busca_fts_map) precisam ser
    // substituídos antes de qualquer UPDATE/INSERT em massa que os dispare.
    migrarBuscaFtsIndexada(registro);

    // Colunas acrescentadas depois da primeira versão do schema.
    garantirColuna(registro, "consolidacao_registro", "destino_path", "TEXT NOT NULL DEFAULT ''");
    registro.exec(
        "CREATE TABLE IF NOT EXISTS backup_destino ("
        "id            TEXT PRIMARY KEY, "
        "destino_path  TEXT NOT NULL UNIQUE, "
        "rotulo        TEXT NOT NULL, "
        "ativo         INTEGER NOT NULL DEFAULT 1, "
        "criado_em     TEXT NOT NULL"
        ")");
    try {
        registro.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_backup_destino_path ON backup_destino(destino_path)");
    } catch (...) {}
    garantirColuna(registro, "backup_destino", "destination_id", "TEXT");
    garantirColuna(registro, "backup_destino", "papel", "TEXT NOT NULL DEFAULT 'CLONE'");
    garantirColuna(registro, "backup_destino", "ultima_revisao_conhecida", "INTEGER NOT NULL DEFAULT 0");
    garantirColuna(registro, "backup_destino", "ultimo_visto_em", "TEXT");
    garantirColuna(registro, "backup_destino", "ultima_edicao_conhecida", "TEXT");
    garantirColuna(registro, "colecao_inteligente", "filtros_origem", "TEXT");
    garantirColuna(registro, "colecao_inteligente", "filtros_content_type", "TEXT");
    garantirColuna(registro, "colecao_inteligente", "filtros_collection_type", "TEXT");
    garantirColuna(registro, "colecao_inteligente", "ano_de", "INTEGER");
    garantirColuna(registro, "colecao_inteligente", "ano_ate", "INTEGER");
    garantirColuna(registro, "projeto", "hierarquia_backup", "TEXT");
    garantirColuna(registro, "projeto", "destino_backup_ativo_path", "TEXT NOT NULL DEFAULT ''");
    // Fase 3 (Folder Color): histórico de até 10 cores usadas, JSON, por
    // projeto — compartilhado entre todas as pastas do Treemap/árvore.
    garantirColuna(registro, "projeto", "historico_cores_pasta", "TEXT");
    garantirColuna(registro, "arquivo", "tamanho_bytes", "INTEGER");

    garantirColuna(registro, "acervo_pasta", "posicao_x", "INTEGER NOT NULL DEFAULT 0");
    garantirColuna(registro, "acervo_pasta", "posicao_y", "INTEGER NOT NULL DEFAULT 0");
    garantirColuna(registro, "acervo_pasta", "ativo", "INTEGER NOT NULL DEFAULT 1");
    // FOLDER COLOR (item 12, Treemap/backup): overlay visual por pasta, ARGB
    // hex (ex.: "ffcc3333") — NULL/vazio = sem cor customizada, mantém a
    // aparência padrão do bloco.
    garantirColuna(registro, "acervo_pasta", "cor_customizada", "TEXT");

    // Reconstrução leva única
    garantirColuna(registro, "marcador", "tipo_id", "TEXT REFERENCES tipo_marcador(id)");
    garantirColuna(registro, "marcador", "tempo_fim", "REAL");
    garantirColuna(registro, "marcador", "prioridade", "TEXT NOT NULL DEFAULT 'media'");
    garantirColuna(registro, "marcador", "status", "TEXT NOT NULL DEFAULT 'aberto'");
    garantirColuna(registro, "marcador", "autor", "TEXT");

    garantirColuna(registro, "item", "estado", "TEXT NOT NULL DEFAULT 'novo'");
    garantirColuna(registro, "item", "marcado_publicacao", "INTEGER NOT NULL DEFAULT 0");

    garantirColuna(registro, "item_observacao", "titulo", "TEXT");
    garantirColuna(registro, "item_observacao", "categoria", "TEXT");
    garantirColuna(registro, "item_observacao", "prioridade", "TEXT NOT NULL DEFAULT 'media'");
    garantirColuna(registro, "item_observacao", "checklist", "TEXT");
    garantirColuna(registro, "item_observacao", "anexos", "TEXT");
    garantirColuna(registro, "item_observacao", "marcador_id", "TEXT REFERENCES marcador(id) ON DELETE SET NULL");

    garantirColuna(registro, "arquivo", "visto_pela_ultima_vez", "TEXT");
    garantirColuna(registro, "arquivo", "estado_presenca", "TEXT NOT NULL DEFAULT 'presente'");
    garantirColuna(registro, "arquivo", "vault_id", "TEXT REFERENCES vault(id) ON DELETE SET NULL");

    // Vault hardware & category columns (§2 Spec Storage)
    garantirColuna(registro, "vault", "vendor", "TEXT NOT NULL DEFAULT ''");
    garantirColuna(registro, "vault", "modelo", "TEXT NOT NULL DEFAULT ''");
    garantirColuna(registro, "vault", "numero_serie", "TEXT NOT NULL DEFAULT ''");
    garantirColuna(registro, "vault", "capacidade_bytes", "INTEGER NOT NULL DEFAULT 0");
    garantirColuna(registro, "vault", "removivel", "INTEGER NOT NULL DEFAULT 1");
    garantirColuna(registro, "vault", "sistema_arquivos", "TEXT NOT NULL DEFAULT ''");
    garantirColuna(registro, "vault", "categoria_dispositivo", "TEXT NOT NULL DEFAULT 'desconhecido'");
    garantirColuna(registro, "vault", "categoria_manual", "INTEGER NOT NULL DEFAULT 0");

    try {
        registro.execScript(
            "CREATE TABLE IF NOT EXISTS vault_evento ("
            "  id             TEXT PRIMARY KEY,"
            "  vault_id       TEXT NOT NULL REFERENCES vault(id) ON DELETE CASCADE,"
            "  modo           TEXT NOT NULL CHECK (modo IN ('collection', 'catalog')),"
            "  itens_copiados INTEGER NOT NULL DEFAULT 0,"
            "  itens_falha    INTEGER NOT NULL DEFAULT 0,"
            "  cancelado      INTEGER NOT NULL DEFAULT 0,"
            "  criado_em      TEXT NOT NULL"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_vault_evento_vault_id ON vault_evento (vault_id);"
            "CREATE INDEX IF NOT EXISTS idx_vault_evento_criado_em ON vault_evento (criado_em DESC);"
        );
        registro.run("DELETE FROM vault WHERE localizacao LIKE '/System/Volumes/%' OR localizacao IN ('/', '/dev', '/net', '/home') OR nome IN ('dev', 'System Drive', 'Preboot', 'Update', 'VM', 'xarts', 'Hardware');", {});
    } catch (...) {}

    // Unified metadata columns (replaces Original/Editable dual model)
    garantirColuna(registro, "item", "ano", "TEXT");
    garantirColuna(registro, "item", "caminho_catalogo", "TEXT");
    garantirColuna(registro, "item", "content_type", "TEXT");
    garantirColuna(registro, "item", "source_media", "TEXT");
    garantirColuna(registro, "item", "collection_type", "TEXT");
    garantirColuna(registro, "item", "isrc", "TEXT");
    garantirColuna(registro, "item", "em_quarentena", "INTEGER NOT NULL DEFAULT 0");

    // Dublin Core (ficha, seção DUBLIN CORE METADATA) — essas colunas nunca
    // tinham sido criadas: ProjetoAberto::salvarMetadado/lerMetadado já
    // referenciam "dc_*" direto na tabela item desde que a seção existe, mas
    // sem a coluna o UPDATE lançava DatabaseError, caía no catch(...) e a
    // edição era descartada em silêncio — nunca persistia. Faltando na
    // migração, não no código que já esperava a coluna existir.
    garantirColuna(registro, "item", "dc_title", "TEXT");
    garantirColuna(registro, "item", "dc_creator", "TEXT");
    garantirColuna(registro, "item", "dc_subject", "TEXT");
    garantirColuna(registro, "item", "dc_description", "TEXT");
    garantirColuna(registro, "item", "dc_publisher", "TEXT");
    garantirColuna(registro, "item", "dc_contributor", "TEXT");
    garantirColuna(registro, "item", "dc_created", "TEXT");
    garantirColuna(registro, "item", "dc_issued", "TEXT");
    garantirColuna(registro, "item", "dc_type", "TEXT");
    garantirColuna(registro, "item", "dc_format", "TEXT");
    garantirColuna(registro, "item", "dc_identifier", "TEXT");
    garantirColuna(registro, "item", "dc_source", "TEXT");
    garantirColuna(registro, "item", "dc_language", "TEXT");
    garantirColuna(registro, "item", "dc_relation", "TEXT");
    garantirColuna(registro, "item", "dc_coverage", "TEXT");
    garantirColuna(registro, "item", "dc_rights", "TEXT");

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
        registro.execScript(
            "CREATE TABLE IF NOT EXISTS catalog_colecao_link ("
            "  id TEXT PRIMARY KEY,"
            "  caminho_projeto TEXT NOT NULL UNIQUE,"
            "  nome TEXT NOT NULL,"
            "  grupo TEXT DEFAULT '',"
            "  criado_em TEXT NOT NULL"
            ");"
        );
    } catch (...) {}

    try {
        registro.execScript(
            "CREATE TABLE IF NOT EXISTS collection_person ("
            "  id TEXT PRIMARY KEY,"
            "  nome TEXT NOT NULL UNIQUE,"
            "  criado_em TEXT NOT NULL"
            ");"
        );
    } catch (...) {}

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

    // Migração de metadados_editados
    garantirColuna(registro, "item", "metadados_editados", "INTEGER NOT NULL DEFAULT 0");

    // Marcação manual "E" (Tag as Edited) — independente de metadados_editados,
    // que é automática; esta é só um helper visual que o operador liga/desliga
    // ele mesmo, começando sempre desmarcada.
    garantirColuna(registro, "item", "marcado_revisado", "INTEGER NOT NULL DEFAULT 0");

    // Garantir triggers de busca em projetos existentes
    try {
        registro.exec(
            "CREATE TRIGGER IF NOT EXISTS trg_item_tag_busca_insert AFTER INSERT ON item_tag FOR EACH ROW BEGIN "
            "    INSERT INTO busca_fts(item_id, conteudo) VALUES (new.item_id, new.tag); "
            "    INSERT INTO busca_fts(item_id, conteudo) VALUES (new.item_id, '#' || new.tag); "
            "END;"
        );
        registro.exec(
            "CREATE TRIGGER IF NOT EXISTS trg_item_tag_busca_delete AFTER DELETE ON item_tag FOR EACH ROW BEGIN "
            "    DELETE FROM busca_fts WHERE item_id = old.item_id AND (conteudo = old.tag OR conteudo = '#' || old.tag); "
            "END;"
        );
        registro.exec(
            "CREATE TRIGGER IF NOT EXISTS trg_item_observacao_busca_insert AFTER INSERT ON item_observacao FOR EACH ROW BEGIN "
            "    INSERT INTO busca_fts(item_id, conteudo) VALUES (new.item_id, new.texto); "
            "END;"
        );
        registro.exec(
            "CREATE TRIGGER IF NOT EXISTS trg_item_observacao_busca_delete AFTER DELETE ON item_observacao FOR EACH ROW BEGIN "
            "    DELETE FROM busca_fts WHERE item_id = old.item_id AND conteudo = old.texto; "
            "END;"
        );
        registro.exec(
            "CREATE TRIGGER IF NOT EXISTS trg_arquivo_busca_insert AFTER INSERT ON arquivo FOR EACH ROW BEGIN "
            "    INSERT INTO busca_fts(item_id, conteudo) VALUES (new.item_id, new.caminho_relativo); "
            "    INSERT INTO busca_fts(item_id, conteudo) SELECT new.item_id, new.caminho_absoluto_origem WHERE new.caminho_absoluto_origem IS NOT NULL; "
            "END;"
        );
        registro.exec(
            "CREATE TRIGGER IF NOT EXISTS trg_arquivo_busca_delete AFTER DELETE ON arquivo FOR EACH ROW BEGIN "
            "    DELETE FROM busca_fts WHERE item_id = old.item_id AND (conteudo = old.caminho_relativo OR conteudo = IFNULL(old.caminho_absoluto_origem, '')); "
            "END;"
        );
    } catch (...) {}

    // Backfill busca_fts completo e consistente
    try {
        auto stmt = registro.prepare("SELECT COUNT(*) FROM busca_fts");
        bool precisaBackfill = !stmt.step() || stmt.columnInt(0) == 0;
        if (precisaBackfill) {
            registro.run("BEGIN TRANSACTION", {});
            try {
                registro.run(
                    "INSERT INTO busca_fts(item_id, conteudo) "
                    "SELECT id, codigo_acervo FROM item WHERE codigo_acervo IS NOT NULL "
                    "UNION ALL "
                    "SELECT id, titulo FROM item WHERE titulo IS NOT NULL "
                    "UNION ALL "
                    "SELECT id, persistent_id FROM item WHERE persistent_id IS NOT NULL "
                    "UNION ALL "
                    "SELECT id, notas_livres FROM item WHERE notas_livres IS NOT NULL "
                    "UNION ALL "
                    "SELECT id, content_type FROM item WHERE content_type IS NOT NULL "
                    "UNION ALL "
                    "SELECT id, collection_type FROM item WHERE collection_type IS NOT NULL "
                    "UNION ALL "
                    "SELECT id, isrc FROM item WHERE isrc IS NOT NULL "
                    "UNION ALL "
                    "SELECT item_id, valor FROM item_campo WHERE valor IS NOT NULL AND TRIM(valor) <> '' "
                    "UNION ALL "
                    "SELECT item_id, tag FROM item_tag WHERE tag IS NOT NULL "
                    "UNION ALL "
                    "SELECT item_id, '#' || tag FROM item_tag WHERE tag IS NOT NULL "
                    "UNION ALL "
                    "SELECT item_id, texto FROM item_observacao WHERE texto IS NOT NULL "
                    "UNION ALL "
                    "SELECT item_id, caminho_relativo FROM arquivo WHERE caminho_relativo IS NOT NULL "
                    "UNION ALL "
                    "SELECT item_id, caminho_absoluto_origem FROM arquivo WHERE caminho_absoluto_origem IS NOT NULL "
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
                  std::unique_ptr<matriz::db::Database> indice, std::string projetoId,
                  DestinationInfo destinationInfo)
    : pastaProjeto_(std::move(pastaProjeto)),
      registro_(std::move(registro)),
      indice_(std::move(indice)),
      projetoId_(std::move(projetoId)),
      destinationInfo_(std::move(destinationInfo)) {
    // Uma leitura só, aqui. Ver a nota em Project::modo().
    auto stmt = registro_->prepare("SELECT modo FROM projeto LIMIT 1");
    if (stmt.step()) modo_ = modoFromString(stmt.columnText(0));
}

std::unique_ptr<Project> Project::criar(const juce::File& pastaRaiz, const NovoProjetoParams& params) {
    if (params.nome.empty())
        throw ProjectError("project name is required");
    if (params.prefixoNomenclatura.empty())
        throw ProjectError("naming prefix is required");

    if (!pastaRaiz.exists()) {
        if (!pastaRaiz.createDirectory())
            throw ProjectError("could not create the destination root folder: " + pastaRaiz.getFullPathName().toStdString());
    } else if (!pastaRaiz.isDirectory()) {
        throw ProjectError("the destination root path exists and is not a folder: " + pastaRaiz.getFullPathName().toStdString());
    }

    juce::File pastaProject = pastaRaiz.getChildFile("Project");
    juce::File pastaMedia = pastaRaiz.getChildFile("Media");
    juce::File destJsonFile = pastaRaiz.getChildFile("destination.json");

    if (destJsonFile.exists() || pastaProject.getChildFile("registro.sqlite").exists() || pastaRaiz.getChildFile("registro.sqlite").exists())
        throw ProjectError("this folder already holds a MATRIZ project: " + pastaRaiz.getFullPathName().toStdString());

    if (!pastaProject.exists() && !pastaProject.createDirectory())
        throw ProjectError("could not create Project directory: " + pastaProject.getFullPathName().toStdString());
    if (!pastaMedia.exists() && !pastaMedia.createDirectory())
        throw ProjectError("could not create Media directory: " + pastaMedia.getFullPathName().toStdString());

    juce::File registroFile = pastaProject.getChildFile("registro.sqlite");
    juce::File indiceFile = pastaProject.getChildFile("indice.sqlite");

    auto registro = std::make_unique<matriz::db::Database>(registroFile.getFullPathName().toStdString());
    auto indice = std::make_unique<matriz::db::Database>(indiceFile.getFullPathName().toStdString());

    registro->setRastrearSujo(false);
    indice->setRastrearSujo(false);
    aplicarSchemas(*registro, *indice);

    std::string projetoId = novoUuid();
    std::string destinationId = novoUuid();
    std::string agora = agoraIso8601();

    registro->run(
        "INSERT INTO projeto (id, modo, nome, instituicao_ou_selo, responsavel, prefixo_nomenclatura, "
        "isrc_registrante, criado_em, atualizado_em, destino_backup_ativo_path) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
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
            matriz::db::Value::of(pastaRaiz.getFullPathName().toStdString()),
        });

    registro->run(
        "INSERT OR REPLACE INTO backup_destino (id, destino_path, rotulo, ativo, criado_em, destination_id, papel, ultima_revisao_conhecida, ultimo_visto_em, ultima_edicao_conhecida) "
        "VALUES (?, ?, ?, 1, ?, ?, 'ORIGINAL', 1, ?, ?)",
        {
            matriz::db::Value::of(destinationId),
            matriz::db::Value::of(pastaRaiz.getFullPathName().toStdString()),
            matriz::db::Value::of(params.nome),
            matriz::db::Value::of(agora),
            matriz::db::Value::of(destinationId),
            matriz::db::Value::of(agora),
            matriz::db::Value::of(agora),
        });

    DestinationInfo destInfo;
    destInfo.formato = 1;
    destInfo.destinationId = destinationId;
    destInfo.projetoId = projetoId;
    destInfo.papel = "ORIGINAL";
    destInfo.rotulo = params.nome;
    destInfo.revisao = 1;
    destInfo.ultimaEdicaoUtc = agora;
    destInfo.criadoEm = agora;
    destInfo.gravarEmArquivo(destJsonFile);

    // Arquivo do projeto (.mtz para coleção ou .bkm para catálogo) dentro da pasta principal do backup (Tarefa 11)
    juce::String ext = (params.modo == Modo::Catalogo ? ".bkm" : ".mtz");
    juce::File arquivoProjeto = pastaRaiz.getChildFile(juce::File::createLegalFileName(params.nome) + ext);
    juce::DynamicObject::Ptr projObj = new juce::DynamicObject();
    projObj->setProperty("formato", 1);
    projObj->setProperty("modo", juce::String(modoToString(params.modo)));
    projObj->setProperty("nome", juce::String(params.nome));
    projObj->setProperty("projeto_id", juce::String(projetoId));
    projObj->setProperty("destination_id", juce::String(destinationId));
    projObj->setProperty("criado_em", juce::String(agora));
    arquivoProjeto.replaceWithText(juce::JSON::toString(juce::var(projObj.get()), false));

    // Initial log entry
    matriz::model::ProjectLog pl(pastaProject);
    pl.appendEntry("Project created", {"Mode: " + juce::String(modoToString(params.modo)), "Root: " + pastaRaiz.getFullPathName(), "Destination ID: " + juce::String(destinationId)});

    registro->limparSujo();
    indice->limparSujo();
    registro->setRastrearSujo(true);
    indice->setRastrearSujo(true);

    return std::unique_ptr<Project>(new Project(pastaProject, std::move(registro), std::move(indice), projetoId, destInfo));
}

std::unique_ptr<Project> Project::abrir(const juce::File& qualquerPasta) {
    juce::File pastaProjeto = resolverPastaProjeto(qualquerPasta);
    if (!pastaProjeto.isDirectory())
        throw ProjectError("project folder not found: " + pastaProjeto.getFullPathName().toStdString());

    juce::File registroFile = pastaProjeto.getChildFile("registro.sqlite");
    juce::File indiceFile = pastaProjeto.getChildFile("indice.sqlite");
    if (!registroFile.existsAsFile() || !indiceFile.existsAsFile())
        throw ProjectError("folder does not hold a valid MATRIZ project (registro.sqlite or indice.sqlite missing): " +
                            pastaProjeto.getFullPathName().toStdString());

    auto registro = std::make_unique<matriz::db::Database>(registroFile.getFullPathName().toStdString());
    auto indice = std::make_unique<matriz::db::Database>(indiceFile.getFullPathName().toStdString());

    registro->setRastrearSujo(false);
    indice->setRastrearSujo(false);
    aplicarSchemas(*registro, *indice);

    matriz::db::Statement stmt = registro->prepare("SELECT id FROM projeto LIMIT 1");
    if (!stmt.step())
        throw ProjectError("projeto corrompido: nenhuma linha em \"projeto\" no registro: " +
                            pastaProjeto.getFullPathName().toStdString());
    std::string projetoId = stmt.columnText(0);

    // Read or initialize DestinationInfo
    juce::File raiz = pastaProjeto.getParentDirectory();
    juce::File destJsonFile = raiz.getChildFile("destination.json");
    auto destOpt = DestinationInfo::lerDeArquivo(destJsonFile);
    DestinationInfo destInfo;
    std::string agora = agoraIso8601();

    if (destOpt && destOpt->projetoId == projetoId) {
        destInfo = *destOpt;
        // Update backup_destino row
        registro->run(
            "INSERT INTO backup_destino (id, destino_path, rotulo, ativo, criado_em, destination_id, papel, ultima_revisao_conhecida, ultimo_visto_em, ultima_edicao_conhecida) "
            "VALUES (?, ?, ?, 1, ?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(id) DO UPDATE SET destino_path = excluded.destino_path, ultimo_visto_em = excluded.ultimo_visto_em, "
            "ultima_edicao_conhecida = excluded.ultima_edicao_conhecida, ultima_revisao_conhecida = excluded.ultima_revisao_conhecida",
            {
                matriz::db::Value::of(destInfo.destinationId),
                matriz::db::Value::of(raiz.getFullPathName().toStdString()),
                matriz::db::Value::of(destInfo.rotulo.empty() ? raiz.getFileName().toStdString() : destInfo.rotulo),
                matriz::db::Value::of(destInfo.criadoEm.empty() ? agora : destInfo.criadoEm),
                matriz::db::Value::of(destInfo.destinationId),
                matriz::db::Value::of(destInfo.papel),
                matriz::db::Value::of(static_cast<long long>(destInfo.revisao)),
                matriz::db::Value::of(agora),
                matriz::db::Value::of(destInfo.ultimaEdicaoUtc.empty() ? agora : destInfo.ultimaEdicaoUtc)
            });
    } else {
        // Fallback / legacy project without destination.json
        destInfo.formato = 1;
        destInfo.destinationId = novoUuid();
        destInfo.projetoId = projetoId;
        destInfo.papel = "ORIGINAL";
        destInfo.rotulo = raiz.getFileName().toStdString();
        destInfo.revisao = 1;
        destInfo.ultimaEdicaoUtc = agora;
        destInfo.criadoEm = agora;
    }

    // Set active destination path
    registro->run("UPDATE projeto SET destino_backup_ativo_path = ? WHERE id = ?",
                  {matriz::db::Value::of(raiz.getFullPathName().toStdString()), matriz::db::Value::of(projetoId)});

    // Ensure Media directory exists
    juce::File mediaDir = raiz.getChildFile("Media");
    if (!mediaDir.exists()) mediaDir.createDirectory();

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

    // Higieniza a estrutura física do projeto para garantir Media/ estritamente limpa
    sanitizarEstruturaDestino(raiz);

    // 0. Desativar destinos inválidos que apontam para /Media ou /Project (sem apagar nada do disco)
    try {
        auto stmtBad = registro->prepare(
            "SELECT id, destino_path, rotulo FROM backup_destino "
            "WHERE ativo = 1 AND (destino_path LIKE '%/Media' OR destino_path LIKE '%/Media/' "
            "OR destino_path LIKE '%/Project' OR destino_path LIKE '%/Project/')");
        struct BadDest {
            std::string id;
            std::string path;
            std::string rotulo;
        };
        std::vector<BadDest> badDests;
        while (stmtBad.step()) {
            badDests.push_back({stmtBad.columnText(0), stmtBad.columnText(1), stmtBad.columnText(2)});
        }
        for (const auto& bd : badDests) {
            registro->run("UPDATE backup_destino SET ativo = 0 WHERE id = ?", {matriz::db::Value::of(bd.id)});
            matriz::model::ProjectLog pLog(pastaProjeto);
            juce::StringArray details;
            details.add("Path: " + juce::String::fromUTF8(bd.path.c_str()));
            details.add("Label: " + juce::String::fromUTF8(bd.rotulo.c_str()));
            details.add("Reason: Pointing to Media or Project subfolder");
            pLog.appendEntry("Destination Deactivated (Invalid Subfolder)", details);
        }
    } catch (...) {}

    // 1. Remove empty/invalid ghost arquivo rows where no paths exist
    try {
        registro->run(
            "DELETE FROM arquivo WHERE (caminho_relativo IS NULL OR caminho_relativo = '') "
            "AND (caminho_absoluto_origem IS NULL OR caminho_absoluto_origem = '')", {});
    } catch (...) {}

    // 2. Self-healing database cleanup of zombie/ghost items from interrupted ingests.
    // Clean up items that have NO arquivo row, NO archive code (never completed analysis),
    // unless they are intentionally non-digitized placeholders ('nao_digitalizado').
    try {
        registro->run(
            "DELETE FROM item WHERE id IN ("
            "  SELECT i.id FROM item i"
            "  WHERE NOT EXISTS (SELECT 1 FROM arquivo a WHERE a.item_id = i.id)"
            "    AND (i.codigo_acervo IS NULL OR i.codigo_acervo = '')"
            "    AND i.estado != 'nao_digitalizado'"
            ")", {});
    } catch (...) {}

    // 3. Ensure master flag is set if an item has files but no eh_master = 1
    try {
        registro->run(
            "UPDATE arquivo SET eh_master = 1 WHERE id IN ("
            "  SELECT a.id FROM arquivo a "
            "  JOIN (SELECT item_id FROM arquivo GROUP BY item_id HAVING SUM(eh_master) = 0) sub "
            "  ON sub.item_id = a.item_id "
            "  GROUP BY a.item_id"
            ")", {});
    } catch (...) {}

    // 4. Ensure all primary files for images and documents are marked as eh_master = 1
    try {
        registro->run("UPDATE arquivo SET eh_master = 1 WHERE papel IN ('foto_suporte', 'documento') AND eh_master = 0", {});
    } catch (...) {}

    // 5. Self-heal missing/unknown tipo_midia based on file extension
    try {
        auto stmtTipo = registro->prepare(
            "SELECT i.id, i.titulo, a.caminho_relativo, a.caminho_absoluto_origem "
            "FROM item i "
            "JOIN arquivo a ON a.item_id = i.id "
            "WHERE (i.tipo_midia IS NULL OR i.tipo_midia = '' OR i.tipo_midia = 'desconhecido') "
            "ORDER BY a.eh_master DESC, a.id ASC");
        struct ItemFix {
            std::string id;
            std::string tipo;
        };
        std::vector<ItemFix> fixes;
        while (stmtTipo.step()) {
            std::string id = stmtTipo.columnText(0);
            std::string tit = stmtTipo.columnText(1);
            std::string rel = stmtTipo.columnText(2);
            std::string abs = stmtTipo.columnText(3);

            juce::String path(abs);
            if (path.isEmpty()) path = juce::String(rel);
            if (path.isEmpty()) path = juce::String(tit);

            auto cat = matriz::ingest::categoriaPorExtensao(juce::File(path));
            std::string tipo;
            switch (cat) {
                case matriz::ingest::CategoriaMidia::Audio:     tipo = "digital_audio"; break;
                case matriz::ingest::CategoriaMidia::Video:     tipo = "digital_video"; break;
                case matriz::ingest::CategoriaMidia::Imagem:    tipo = "foto"; break;
                case matriz::ingest::CategoriaMidia::Documento: tipo = "documento"; break;
                case matriz::ingest::CategoriaMidia::Texto:     tipo = "documento"; break;
                case matriz::ingest::CategoriaMidia::Sessao:    tipo = "sessao"; break;
                default: break;
            }
            if (!tipo.empty()) {
                fixes.push_back({id, tipo});
            }
        }
        for (const auto& f : fixes) {
            registro->run("UPDATE item SET tipo_midia = ? WHERE id = ?",
                          {matriz::db::Value::of(f.tipo), matriz::db::Value::of(f.id)});
        }
    } catch (...) {}

    // 6. Self-heal missing caminho_absoluto_origem from caminho_relativo if file exists
    try {
        auto stmtCaminho = registro->prepare(
            "SELECT a.id, a.caminho_relativo FROM arquivo a "
            "WHERE (a.caminho_absoluto_origem IS NULL OR a.caminho_absoluto_origem = '') "
            "AND (a.caminho_relativo IS NOT NULL AND a.caminho_relativo != '')");
        struct PathFix {
            std::string arquivoId;
            std::string absolutePath;
        };
        std::vector<PathFix> pathFixes;
        while (stmtCaminho.step()) {
            std::string arqId = stmtCaminho.columnText(0);
            juce::String rel = stmtCaminho.columnText(1);
            juce::File f(rel);
            if (juce::File::isAbsolutePath(rel) && f.existsAsFile()) {
                pathFixes.push_back({arqId, f.getFullPathName().toStdString()});
            } else {
                juce::File inProj = pastaProjeto.getChildFile(rel);
                if (inProj.existsAsFile()) {
                    pathFixes.push_back({arqId, inProj.getFullPathName().toStdString()});
                } else {
                    juce::File inMedia = pastaProjeto.getParentDirectory().getChildFile("Media").getChildFile(rel);
                    if (inMedia.existsAsFile()) {
                        pathFixes.push_back({arqId, inMedia.getFullPathName().toStdString()});
                    } else {
                        juce::File inRaiz = pastaProjeto.getParentDirectory().getChildFile(rel);
                        if (inRaiz.existsAsFile()) {
                            pathFixes.push_back({arqId, inRaiz.getFullPathName().toStdString()});
                        }
                    }
                }
            }
        }
        for (const auto& pf : pathFixes) {
            registro->run("UPDATE arquivo SET caminho_absoluto_origem = ? WHERE id = ?",
                          {matriz::db::Value::of(pf.absolutePath), matriz::db::Value::of(pf.arquivoId)});
        }
    } catch (...) {}

    // Garante que o arquivo de projeto auto-contido (.mtz ou .bkm) existe na raiz do projeto/backup (Tarefa 11)
    try {
        juce::File pastaRaiz = pastaProjeto.getParentDirectory();
        std::string modoStr = "preservacao";
        {
            auto stmtModo = registro->prepare("SELECT modo FROM projeto LIMIT 1");
            if (stmtModo.step()) modoStr = stmtModo.columnText(0);
        }
        juce::String ext = (modoStr == "catalogo" ? ".bkm" : ".mtz");
        std::string nomeProj = "";
        {
            auto stmtNome = registro->prepare("SELECT nome FROM projeto LIMIT 1");
            if (stmtNome.step()) nomeProj = stmtNome.columnText(0);
        }
        if (nomeProj.empty()) nomeProj = pastaRaiz.getFileName().toStdString();
        juce::File arquivoProjeto = pastaRaiz.getChildFile(juce::File::createLegalFileName(nomeProj) + ext);
        if (!arquivoProjeto.existsAsFile()) {
            juce::DynamicObject::Ptr projObj = new juce::DynamicObject();
            projObj->setProperty("formato", 1);
            projObj->setProperty("modo", juce::String(modoStr));
            projObj->setProperty("nome", juce::String(nomeProj));
            projObj->setProperty("projeto_id", juce::String(projetoId));
            projObj->setProperty("destination_id", juce::String(destInfo.destinationId));
            projObj->setProperty("criado_em", juce::String(agora));
            arquivoProjeto.replaceWithText(juce::JSON::toString(juce::var(projObj.get()), false));
        }
    } catch (...) {}

    registro->limparSujo();
    indice->limparSujo();
    registro->setRastrearSujo(true);
    indice->setRastrearSujo(true);

    return std::unique_ptr<Project>(new Project(pastaProjeto, std::move(registro), std::move(indice), projetoId, destInfo));
}

void Project::confirmarRevisao() {
    if (!registro_ || !registro_->estaSujo()) return;

    destinationInfo_.revisao++;
    destinationInfo_.ultimaEdicaoUtc = agoraIso8601();
    juce::File destJsonFile = raiz().getChildFile("destination.json");
    destinationInfo_.gravarEmArquivo(destJsonFile);

    // Update backup_destino for this destination
    try {
        registro_->run("UPDATE backup_destino SET ultima_revisao_conhecida = ?, ultima_edicao_conhecida = ?, ultimo_visto_em = ? WHERE destination_id = ?",
                       {matriz::db::Value::of(static_cast<long long>(destinationInfo_.revisao)),
                        matriz::db::Value::of(destinationInfo_.ultimaEdicaoUtc),
                        matriz::db::Value::of(destinationInfo_.ultimaEdicaoUtc),
                        matriz::db::Value::of(destinationInfo_.destinationId)});
    } catch (...) {}

    registro_->limparSujo();
}

std::string Project::nome() {
    auto stmt = registro_->prepare("SELECT nome FROM projeto LIMIT 1");
    stmt.step();
    return stmt.columnText(0);
}

std::string Project::destinoBackupAtivo() {
    try {
        auto stmt = registro_->prepare("SELECT destino_backup_ativo_path FROM projeto LIMIT 1");
        if (stmt.step() && !stmt.columnIsNull(0)) {
            return stmt.columnText(0);
        }
    } catch (...) {}
    return {};
}

void Project::definirDestinoBackupAtivo(const std::string& path) {
    try {
        registro_->run("UPDATE projeto SET destino_backup_ativo_path = ?", {matriz::db::Value::of(path)});
    } catch (...) {}
}

} // namespace matriz::model
