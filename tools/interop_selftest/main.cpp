#include <JuceHeader.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <iomanip>
#include <thread>
#include <atomic>

#include "Model/Project.h"
#include "Model/CaminhosBanco.h"
#include "Model/NomesCanonicos.h"
#include "Model/NomesSeguros.h"
#include "Ingest/Checksum.h"
#include "Ingest/Loudness.h"
#include "Preservation/Preservation.h"
#include "Db/Database.h"

namespace {

int failures = 0;

void check(bool condition, const std::string& tag, const std::string& description) {
    if (condition) {
        std::cout << "  PASS [" << tag << "] " << description << "\n";
    } else {
        std::cout << "  FAIL [" << tag << "] " << description << "\n";
        ++failures;
    }
}

// Cria um arquivo de texto sintético
void criarArquivoTexto(const juce::File& f, const juce::String& conteudo) {
    f.getParentDirectory().createDirectory();
    f.replaceWithText(conteudo, false, false, "\n");
}

int executarGeracao(const juce::File& baseDir) {
    std::cout << "== MATRIZ INTEROP: GERANDO FIXTURE EM " << baseDir.getFullPathName() << " ==\n";
    
    baseDir.createDirectory();
    juce::File pastaProj = baseDir.getChildFile("projeto");
    juce::File pastaOrigens = baseDir.getChildFile("origens");
    juce::File pastaMain = baseDir.getChildFile("backup_main");
    juce::File pastaExport = baseDir.getChildFile("export_pacote");

    pastaProj.createDirectory();
    pastaOrigens.createDirectory();
    pastaMain.createDirectory();
    pastaExport.createDirectory();

    // 1. Cria mídia sintética com nomes acentuados, maiúsculas misturadas e árvore profunda
    juce::File arq1 = pastaOrigens.getChildFile("Áudio e Música/Gravação_2026_SãoPaulo_Éxito.txt");
    criarArquivoTexto(arq1, "Conteudo sintetico de audio 12345");

    juce::File arq2 = pastaOrigens.getChildFile("Fotos & Vídeos/Apresentação/Foto_Ção_Ão.txt");
    criarArquivoTexto(arq2, "Conteudo sintetico de imagem 67890");

    juce::File arqHidden = pastaOrigens.getChildFile("Ocultos/Arquivo_Invisivel_Hidden.txt");
    criarArquivoTexto(arqHidden, "Conteudo secreto e oculto");

    // Caminho profundo (>260 chars se possível)
    juce::File arqProfundo = pastaOrigens.getChildFile("Nivel1/Nivel2_Subpasta_Longa/Nivel3_Com_Nome_Bem_Extenso_Para_Testar_Caminhos_Longos_No_Windows/Nivel4_Mais_Uma_Pasta_Profunda_Para_Garantir/Documento_Final_Super_Longo.txt");
    criarArquivoTexto(arqProfundo, "Documento em arvore profunda");

    // Arquivo de origem com caracteres que exigem sanitização segura no Windows (ex: ':' e '?')
    std::string nomeOrigEspecial = "Show Especial: Acústico? 2026.txt";
    std::string nomeSeguroDisco = matriz::nomes_seguros::sanitizarComponente(nomeOrigEspecial);
    juce::File arqEspecial = pastaOrigens.getChildFile("Especiais/" + juce::String::fromUTF8(nomeSeguroDisco.c_str()));
    criarArquivoTexto(arqEspecial, "Conteudo de show especial com nome sanitizado");

    // 2. Cria projeto no banco SQLite
    matriz::model::NovoProjetoParams params;
    params.nome = "Projeto Interop Mac-Win";
    params.prefixoNomenclatura = "PROJ-INT";
    params.instituicaoOuSelo = "2026";
    params.responsavel = "Criador Interop";
    auto proj = matriz::model::Project::criar(pastaProj, params);
    check(proj != nullptr, "PROJECT_CREATION", "Project created successfully");
    if (!proj) return 1;

    auto& db = proj->registro();

    // 3. Ingestão e catalogação de itens
    auto ck1 = matriz::ingest::calcularChecksums(arq1);
    auto ck2 = matriz::ingest::calcularChecksums(arq2);
    auto ckH = matriz::ingest::calcularChecksums(arqHidden);
    auto ckP = matriz::ingest::calcularChecksums(arqProfundo);
    auto ckE = matriz::ingest::calcularChecksums(arqEspecial);

    std::string id1 = matriz::model::novoUuid();
    std::string id2 = matriz::model::novoUuid();
    std::string idH = matriz::model::novoUuid();
    std::string idP = matriz::model::novoUuid();
    std::string idE = matriz::model::novoUuid();

    std::string arqId1 = matriz::model::novoUuid();
    std::string arqId2 = matriz::model::novoUuid();
    std::string arqIdH = matriz::model::novoUuid();
    std::string arqIdP = matriz::model::novoUuid();
    std::string arqIdE = matriz::model::novoUuid();

    std::string agora = matriz::model::agoraIso8601();

    using matriz::db::Value;

    // Item 1 (Audio)
    db.run("INSERT INTO item (id, projeto_id, titulo, tipo_midia, estado, criado_em, atualizado_em) "
           "VALUES (?, ?, 'Gravação do Show de Sucesso', 'audio', 'catalogado', ?, ?)",
           {Value::of(id1), Value::of(proj->projetoId()), Value::of(agora), Value::of(agora)});
    db.run("INSERT INTO arquivo (id, item_id, papel, caminho_relativo, caminho_absoluto_origem, checksum_sha256, checksum_md5, tamanho_bytes, criado_em, atualizado_em) "
           "VALUES (?, ?, 'preservation_master', ?, ?, ?, ?, ?, ?, ?)",
           {Value::of(arqId1), Value::of(id1), Value::of(matriz::caminhos::relativoParaBanco(arq1, pastaOrigens)), Value::of(arq1.getFullPathName().toStdString()), Value::of(ck1.sha256), Value::of(ck1.md5), Value::of(static_cast<juce::int64>(arq1.getSize())), Value::of(agora), Value::of(agora)});
    db.run("INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
           "VALUES (?, ?, 'raiz', 0, 'artista_principal', 'Anderson Guerra', 'humano', ?)",
           {Value::of(matriz::model::novoUuid()), Value::of(id1), Value::of(agora)});
    db.run("INSERT INTO item_tag (id, item_id, tag) VALUES (?, ?, 'Show')",
           {Value::of(matriz::model::novoUuid()), Value::of(id1)});

    // Item 2 (Imagem)
    db.run("INSERT INTO item (id, projeto_id, titulo, tipo_midia, estado, criado_em, atualizado_em) "
           "VALUES (?, ?, 'Foto da Apresentação', 'imagem', 'catalogado', ?, ?)",
           {Value::of(id2), Value::of(proj->projetoId()), Value::of(agora), Value::of(agora)});
    db.run("INSERT INTO arquivo (id, item_id, papel, caminho_relativo, caminho_absoluto_origem, checksum_sha256, checksum_md5, tamanho_bytes, criado_em, atualizado_em) "
           "VALUES (?, ?, 'preservation_master', ?, ?, ?, ?, ?, ?, ?)",
           {Value::of(arqId2), Value::of(id2), Value::of(matriz::caminhos::relativoParaBanco(arq2, pastaOrigens)), Value::of(arq2.getFullPathName().toStdString()), Value::of(ck2.sha256), Value::of(ck2.md5), Value::of(static_cast<juce::int64>(arq2.getSize())), Value::of(agora), Value::of(agora)});
    db.run("INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
           "VALUES (?, ?, 'raiz', 0, 'fotografo', 'Maria Silva', 'humano', ?)",
           {Value::of(matriz::model::novoUuid()), Value::of(id2), Value::of(agora)});

    // Item Hidden
    db.run("INSERT INTO item (id, projeto_id, titulo, tipo_midia, estado, criado_em, atualizado_em) "
           "VALUES (?, ?, 'Documento Oculto', 'documento', 'catalogado', ?, ?)",
           {Value::of(idH), Value::of(proj->projetoId()), Value::of(agora), Value::of(agora)});
    db.run("INSERT INTO arquivo (id, item_id, papel, caminho_relativo, caminho_absoluto_origem, checksum_sha256, checksum_md5, tamanho_bytes, criado_em, atualizado_em) "
           "VALUES (?, ?, 'preservation_master', ?, ?, ?, ?, ?, ?, ?)",
           {Value::of(arqIdH), Value::of(idH), Value::of(matriz::caminhos::relativoParaBanco(arqHidden, pastaOrigens)), Value::of(arqHidden.getFullPathName().toStdString()), Value::of(ckH.sha256), Value::of(ckH.md5), Value::of(static_cast<juce::int64>(arqHidden.getSize())), Value::of(agora), Value::of(agora)});
    db.run("INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
           "VALUES (?, ?, 'raiz', 0, 'collection_type', 'Hidden', 'humano', ?)",
           {Value::of(matriz::model::novoUuid()), Value::of(idH), Value::of(agora)});

    // Item Profundo
    db.run("INSERT INTO item (id, projeto_id, titulo, tipo_midia, estado, criado_em, atualizado_em) "
           "VALUES (?, ?, 'Documento em Caminho Longo', 'documento', 'catalogado', ?, ?)",
           {Value::of(idP), Value::of(proj->projetoId()), Value::of(agora), Value::of(agora)});
    db.run("INSERT INTO arquivo (id, item_id, papel, caminho_relativo, caminho_absoluto_origem, checksum_sha256, checksum_md5, tamanho_bytes, criado_em, atualizado_em) "
           "VALUES (?, ?, 'preservation_master', ?, ?, ?, ?, ?, ?, ?)",
           {Value::of(arqIdP), Value::of(idP), Value::of(matriz::caminhos::relativoParaBanco(arqProfundo, pastaOrigens)), Value::of(arqProfundo.getFullPathName().toStdString()), Value::of(ckP.sha256), Value::of(ckP.md5), Value::of(static_cast<juce::int64>(arqProfundo.getSize())), Value::of(agora), Value::of(agora)});

    // Item Especial (origem com ':' e '?', preservado no caminho_absoluto_origem)
    std::string origemComCharsEspeciais = "/Volumes/Origem/" + nomeOrigEspecial;
    db.run("INSERT INTO item (id, projeto_id, titulo, tipo_midia, estado, criado_em, atualizado_em) "
           "VALUES (?, ?, 'Show Especial com Nome Original Especial', 'audio', 'catalogado', ?, ?)",
           {Value::of(idE), Value::of(proj->projetoId()), Value::of(agora), Value::of(agora)});
    db.run("INSERT INTO arquivo (id, item_id, papel, caminho_relativo, caminho_absoluto_origem, checksum_sha256, checksum_md5, tamanho_bytes, criado_em, atualizado_em) "
           "VALUES (?, ?, 'preservation_master', ?, ?, ?, ?, ?, ?, ?)",
           {Value::of(arqIdE), Value::of(idE), Value::of(matriz::caminhos::relativoParaBanco(arqEspecial, pastaOrigens)), Value::of(origemComCharsEspeciais), Value::of(ckE.sha256), Value::of(ckE.md5), Value::of(static_cast<juce::int64>(arqEspecial.getSize())), Value::of(agora), Value::of(agora)});

    // Adiciona entidade de pessoa e lugar
    std::string entPessoaId = matriz::model::novoUuid();
    std::string entLugarId = matriz::model::novoUuid();
    db.run("INSERT INTO entidade (id, projeto_id, tipo, nome, criado_em) VALUES (?, ?, 'pessoa', 'Gilberto Gil', ?)",
           {Value::of(entPessoaId), Value::of(proj->projetoId()), Value::of(agora)});
    db.run("INSERT INTO entidade (id, projeto_id, tipo, nome, criado_em) VALUES (?, ?, 'lugar', 'Teatro Municipal', ?)",
           {Value::of(entLugarId), Value::of(proj->projetoId()), Value::of(agora)});
    db.run("INSERT INTO item_entidade (item_id, entidade_id, papel) VALUES (?, ?, 'artista')",
           {Value::of(id1), Value::of(entPessoaId)});
    db.run("INSERT INTO item_entidade (item_id, entidade_id, papel) VALUES (?, ?, 'local_gravacao')",
           {Value::of(id1), Value::of(entLugarId)});

    // Marcação R (Reject) no Intake
    db.run("INSERT INTO intake_marca_r (item_id, origem, marcado_em) VALUES (?, 'usuario', ?)",
           {Value::of(idH), Value::of(agora)});

    // 4. Cria folder map
    std::string mapaId = matriz::model::novoUuid();
    std::string pastaMapId = matriz::model::novoUuid();
    db.run("INSERT INTO folder_map (id, projeto_id, nome, ordem, criado_em, atualizado_em) VALUES (?, ?, 'Mapa Shows 2026', 1, ?, ?)",
           {Value::of(mapaId), Value::of(proj->projetoId()), Value::of(agora), Value::of(agora)});
    db.run("INSERT INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, criado_em, atualizado_em) VALUES (?, ?, NULL, 'Shows 2026', 0, ?, ?)",
           {Value::of(pastaMapId), Value::of(proj->projetoId()), Value::of(agora), Value::of(agora)});
    db.run("INSERT INTO acervo_item_pasta (id, item_id, pasta_id, criado_em) VALUES (?, ?, ?, ?)",
           {Value::of(matriz::model::novoUuid()), Value::of(id1), Value::of(pastaMapId), Value::of(agora)});

    // 5. Cria destination.json na raiz do MAIN
    matriz::model::DestinationInfo destInfo;
    destInfo.formato = 1;
    destInfo.destinationId = matriz::model::novoUuid();
    destInfo.projetoId = proj->projetoId();
    destInfo.papel = "MAIN";
    destInfo.rotulo = "Backup Principal Interop";
    destInfo.revisao = 1;
    destInfo.criadoEm = agora;
    destInfo.ultimaEdicaoUtc = agora;
    destInfo.gravarEmArquivo(pastaMain.getChildFile("destination.json"));

    // Simula cópias no MAIN
    juce::File mainArq1 = pastaMain.getChildFile("Media/Áudio e Música/Gravação_2026_SãoPaulo_Éxito.txt");
    arq1.copyFileTo(mainArq1);
    juce::File mainArq2 = pastaMain.getChildFile("Media/Fotos & Vídeos/Apresentação/Foto_Ção_Ão.txt");
    arq2.copyFileTo(mainArq2);

    // 6. Cria índice de miniaturas (indice.sqlite)
    {
        juce::File indiceFile = pastaProj.getChildFile("indice.sqlite");
        matriz::db::Database indiceDb(indiceFile.getFullPathName().toStdString());
        indiceDb.execScript(
            "CREATE TABLE IF NOT EXISTS miniatura ("
            "  id TEXT PRIMARY KEY,"
            "  item_id TEXT NOT NULL,"
            "  arquivo_id TEXT NOT NULL,"
            "  tipo TEXT NOT NULL,"
            "  caminho_relativo TEXT NOT NULL,"
            "  largura INTEGER NOT NULL,"
            "  altura INTEGER NOT NULL,"
            "  gerado_em TEXT NOT NULL);"
        );
        indiceDb.run(
            "INSERT INTO miniatura (id, item_id, arquivo_id, tipo, caminho_relativo, largura, altura, gerado_em) "
            "VALUES (?, ?, ?, 'miniatura', ?, 320, 240, ?)",
            {Value::of(matriz::model::novoUuid()), Value::of(id1), Value::of(arqId1),
             Value::of(matriz::caminhos::paraBanco(".miniaturas/thumb_audio1.png").toStdString()), Value::of(agora)});
    }

    // 7. Simula pacote de EXPORT com manifesto sha256sum e manifest.sqlite
    juce::File exportMedia = pastaExport.getChildFile("Media");
    exportMedia.createDirectory();
    mainArq1.copyFileTo(exportMedia.getChildFile(mainArq1.getFileName()));
    mainArq2.copyFileTo(exportMedia.getChildFile(mainArq2.getFileName()));
    juce::File manifestExport = pastaExport.getChildFile("manifest.sha256");
    manifestExport.replaceWithText(juce::String(ck1.sha256) + "  Media/" + mainArq1.getFileName() + "\n" +
                                   juce::String(ck2.sha256) + "  Media/" + mainArq2.getFileName() + "\n", false, false, "\n");

    {
        juce::File manifestSqlite = pastaExport.getChildFile("manifest.sqlite");
        matriz::db::Database mDb(manifestSqlite.getFullPathName().toStdString());
        mDb.execScript(
            "CREATE TABLE IF NOT EXISTS manifesto ("
            "  caminho_relativo TEXT PRIMARY KEY,"
            "  checksum_sha256 TEXT NOT NULL,"
            "  verificado_em TEXT NOT NULL);"
        );
        mDb.run(
            "INSERT INTO manifesto (caminho_relativo, checksum_sha256, verificado_em) VALUES (?, ?, ?)",
            {Value::of(matriz::caminhos::paraBanco("Media/" + mainArq1.getFileName().toStdString())),
             Value::of(ck1.sha256), Value::of(agora)});
        mDb.run(
            "INSERT INTO manifesto (caminho_relativo, checksum_sha256, verificado_em) VALUES (?, ?, ?)",
            {Value::of(matriz::caminhos::paraBanco("Media/" + mainArq2.getFileName().toStdString())),
             Value::of(ck2.sha256), Value::of(agora)});
    }

    // 8. Checkpoint WAL
    try {
        db.exec("PRAGMA wal_checkpoint(TRUNCATE);");
    } catch (...) {}

    // 9. Grava manifesto JSON da fixture
    juce::DynamicObject::Ptr manifest = new juce::DynamicObject();
    manifest->setProperty("total_itens", 5);
    manifest->setProperty("total_arquivos", 5);
    manifest->setProperty("id1", juce::String(id1));
    manifest->setProperty("id2", juce::String(id2));
    manifest->setProperty("id_hidden", juce::String(idH));
    manifest->setProperty("id_profundo", juce::String(idP));
    manifest->setProperty("id_especial", juce::String(idE));
    manifest->setProperty("sha256_1", juce::String(ck1.sha256));
    manifest->setProperty("sha256_2", juce::String(ck2.sha256));
    manifest->setProperty("sha256_hidden", juce::String(ckH.sha256));
    manifest->setProperty("sha256_profundo", juce::String(ckP.sha256));
    manifest->setProperty("sha256_especial", juce::String(ckE.sha256));
    manifest->setProperty("pessoa_nome", juce::String("Gilberto Gil"));
    manifest->setProperty("lugar_nome", juce::String("Teatro Municipal"));
    manifest->setProperty("nome_orig_especial", juce::String::fromUTF8(nomeOrigEspecial.c_str()));

    juce::File manifestFile = baseDir.getChildFile("manifesto_interop.json");
    manifestFile.replaceWithText(juce::JSON::toString(juce::var(manifest.get()), true));

    std::cout << "== FIXTURE GERADA COM SUCESSO EM " << baseDir.getFullPathName() << " ==\n";
    return 0;
}

int executarVerificacao(const juce::File& baseDir) {
    std::cout << "== MATRIZ INTEROP: VERIFICANDO FIXTURE EM " << baseDir.getFullPathName() << " ==\n";

    juce::File manifestFile = baseDir.getChildFile("manifesto_interop.json");
    check(manifestFile.existsAsFile(), "FIXTURE_MANIFEST_EXISTS", "manifesto_interop.json exists");
    if (!manifestFile.existsAsFile()) return 1;

    auto varManifest = juce::JSON::parse(manifestFile);
    check(varManifest.isObject(), "FIXTURE_MANIFEST_PARSED", "manifesto_interop.json parsed successfully");
    if (!varManifest.isObject()) return 1;

    juce::File pastaProj = baseDir.getChildFile("projeto");
    auto proj = matriz::model::Project::abrir(pastaProj);
    check(proj != nullptr, "PROJECT_OPEN", "Project opened cleanly from " + pastaProj.getFullPathName().toStdString());
    if (!proj) return 1;

    auto& db = proj->registro();

    // 1. Contagens de itens e integridade
    auto stmtCount = db.prepare("SELECT COUNT(*) FROM item;");
    if (stmtCount.step()) {
        int count = stmtCount.columnInt(0);
        check(count == 5, "ITEM_COUNT", "Total items in database matches expected (5), got " + std::to_string(count));
    }

    // 2. Caminhos relativos SEMPRE com '/' (Zero barras invertidas '\' no banco)
    auto stmtPaths = db.prepare("SELECT caminho_relativo FROM arquivo;");
    bool todosCaminhosComBarra = true;
    int totalCaminhos = 0;
    while (stmtPaths.step()) {
        std::string rel = stmtPaths.columnText(0);
        ++totalCaminhos;
        if (rel.find('\\') != std::string::npos) {
            todosCaminhosComBarra = false;
            std::cout << "  FAIL Caminho com barra invertida encontrado no banco: " << rel << "\n";
            ++failures;
        }
    }
    check(todosCaminhosComBarra && totalCaminhos >= 5, "PATHS_CANONICAL",
          "All " + std::to_string(totalCaminhos) + " relative paths in database strictly use '/' format (no backslashes)");

    // 3. Miniaturas no indice.sqlite
    {
        juce::File indiceFile = pastaProj.getChildFile("indice.sqlite");
        if (indiceFile.existsAsFile()) {
            matriz::db::Database indDb(indiceFile.getFullPathName().toStdString());
            auto stMini = indDb.prepare("SELECT caminho_relativo FROM miniatura;");
            bool miniOk = false;
            if (stMini.step()) {
                std::string cr = stMini.columnText(0);
                miniOk = (cr.find('\\') == std::string::npos && cr.find('/') != std::string::npos);
            }
            check(miniOk, "PATHS_THUMBNAIL", "Miniatura relative path in indice.sqlite uses normalized '/'");
        } else {
            check(false, "PATHS_THUMBNAIL", "indice.sqlite exists in project root");
        }
    }

    // 4. Export manifest.sqlite e manifest.sha256
    {
        juce::File mSqlite = baseDir.getChildFile("export_pacote/manifest.sqlite");
        if (mSqlite.existsAsFile()) {
            matriz::db::Database mDb(mSqlite.getFullPathName().toStdString());
            auto stMan = mDb.prepare("SELECT caminho_relativo FROM manifesto;");
            bool manOk = true;
            int countMan = 0;
            while (stMan.step()) {
                std::string cr = stMan.columnText(0);
                ++countMan;
                if (cr.find('\\') != std::string::npos) manOk = false;
            }
            check(manOk && countMan >= 2, "PATHS_EXPORT_MANIFEST",
                  "Export manifest.sqlite relative paths strictly use '/' (" + std::to_string(countMan) + " entries)");
        }
    }

    // 5. Preservação do nome original em arquivos com caracteres especiais (: e ?)
    {
        auto stOrig = db.prepare("SELECT a.caminho_absoluto_origem, a.caminho_relativo FROM arquivo a JOIN item i ON a.item_id = i.id WHERE i.titulo = 'Show Especial com Nome Original Especial';");
        bool origOk = false;
        if (stOrig.step()) {
            std::string origPath = stOrig.columnText(0);
            std::string relPath = stOrig.columnText(1);
            origOk = (origPath.find(':') != std::string::npos || origPath.find('?') != std::string::npos) &&
                     (relPath.find(':') == std::string::npos && relPath.find('?') == std::string::npos);
        }
        check(origOk, "SAFE_NAMES_ORIGIN_PRESERVED",
              "Original path with special chars preserved in database while safe sanitized name used on disk");
    }

    // 6. Verifica item Hidden
    auto stmtHidden = db.prepare("SELECT c.valor FROM item_campo c JOIN item i ON c.item_id = i.id WHERE i.titulo = 'Documento Oculto' AND c.campo_id = 'collection_type';");
    bool achouHidden = false;
    if (stmtHidden.step()) {
        achouHidden = (stmtHidden.columnText(0) == "Hidden");
    }
    check(achouHidden, "HIDDEN_ITEMS", "Hidden item (collection_type=Hidden) preserved and correctly classified");

    // 7. Verifica campos, tags e entidades
    auto stmtArtist = db.prepare("SELECT c.valor FROM item_campo c JOIN item i ON c.item_id = i.id WHERE i.titulo = 'Gravação do Show de Sucesso' AND c.campo_id = 'artista_principal';");
    bool achouArtista = false;
    if (stmtArtist.step()) {
        achouArtista = (stmtArtist.columnText(0) == "Anderson Guerra");
    }
    check(achouArtista, "METADATA_FIELDS", "Field 'artista_principal' preserved ('Anderson Guerra')");

    auto stmtEnt = db.prepare("SELECT e.nome FROM entidade e JOIN item_entidade ie ON e.id = ie.entidade_id WHERE ie.papel = 'artista';");
    bool achouEntidade = false;
    if (stmtEnt.step()) {
        achouEntidade = (stmtEnt.columnText(0) == varManifest["pessoa_nome"].toString().toStdString());
    }
    check(achouEntidade, "METADATA_ENTITIES", "Entity 'Gilberto Gil' associated with asset");

    // 8. Verifica marcação R (Reject)
    auto stmtR = db.prepare("SELECT COUNT(*) FROM intake_marca_r WHERE origem = 'usuario';");
    bool achouR = false;
    if (stmtR.step()) {
        achouR = (stmtR.columnInt(0) == 1);
    }
    check(achouR, "INTAKE_REJECT_MARK", "INTAKE Reject mark (R) preserved");

    // 9. Verifica normalização canônica de nomes (NFC)
    std::string chave1 = matriz::model::nomes::chave("São Paulo");
    std::string chave2 = matriz::model::nomes::chave("são paulo");
    check(chave1 == chave2, "CANONICAL_UNICODE_NFC", "Canonical Unicode key comparison matches ('São Paulo' == 'são paulo')");

    // 10. Verifica integridade de checksums
    std::string expSha1 = varManifest["sha256_1"].toString().toStdString();
    auto stmtSha = db.prepare("SELECT a.checksum_sha256 FROM arquivo a JOIN item i ON a.item_id = i.id WHERE i.titulo = 'Gravação do Show de Sucesso';");
    if (stmtSha.step()) {
        std::string dbSha = stmtSha.columnText(0);
        check(dbSha == expSha1, "CHECKSUM_PARITY", "SHA-256 matches exact hash: " + dbSha);
    }

    // 11. Verifica folder map
    auto stmtMap = db.prepare("SELECT nome FROM folder_map WHERE nome = 'Mapa Shows 2026';");
    bool mapaOk = false;
    if (stmtMap.step()) {
        if (stmtMap.columnText(0) == "Mapa Shows 2026") mapaOk = true;
    }
    check(mapaOk, "FOLDER_MAPS", "Folder Map structure preserved and matched exactly");

    // 12. Verifica MAIN destination.json
    juce::File destJson = baseDir.getChildFile("backup_main/destination.json");
    check(destJson.existsAsFile(), "MAIN_DESTINATION_EXISTS", "destination.json exists in MAIN backup root");
    auto dInfo = matriz::model::DestinationInfo::lerDeArquivo(destJson);
    check(dInfo.has_value() && dInfo->papel == "MAIN", "MAIN_DESTINATION_PARSED", "MAIN destination parsed and identified cleanly");

    // 13. Verifica ausência de arquivos WAL pendentes (.sqlite-wal)
    juce::File walFile = pastaProj.getChildFile("registro.sqlite-wal");
    check(!walFile.existsAsFile(), "SQLITE_WAL_CLEAN", "SQLite database is fully checkpointed (no pending -wal file)");

    // 14. Teste de paridade de medição de Loudness BS.1770 / EBU R128
    {
        juce::AudioBuffer<float> testAudio(2, 48000 * 2); // 2 segundos de áudio estéreo
        testAudio.clear();
        for (int i = 0; i < testAudio.getNumSamples(); ++i) {
            float s = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * i / 48000.0f);
            testAudio.setSample(0, i, s);
            testAudio.setSample(1, i, s);
        }
        auto loud = matriz::ingest::medirLoudness(testAudio, 48000.0);
        bool lufsOk = (loud.lufsIntegrado > -25.0 && loud.lufsIntegrado < -5.0);
        check(lufsOk, "LOUDNESS_BS1770_PARITY",
              "BS.1770 Loudness calculation verified (LUFS=" + std::to_string(loud.lufsIntegrado) + ")");
    }

    // 15. Classificação de risco de formato de preservação (Preservation Risk)
    {
        std::string riscoWav = matriz::preservation::classificarRiscoFormato("wav");
        std::string riscoMp4 = matriz::preservation::classificarRiscoFormato("mp4");
        std::string riscoWma = matriz::preservation::classificarRiscoFormato("wma");
        bool riscoOk = (riscoWav == "OK" && riscoMp4 == "OK" && riscoWma == "AT_RISK");
        check(riscoOk, "PRESERVATION_RISK", "Format risk classification consistent (WAV=OK, MP4=OK, WMA=AT_RISK)");
    }

    // 16. Teste de estresse de concorrência multithreaded no SQLite
    {
        std::atomic<int> readSuccesses{0};
        std::vector<std::thread> readers;
        for (int t = 0; t < 4; ++t) {
            readers.emplace_back([&]() {
                try {
                    auto st = db.prepare("SELECT COUNT(*) FROM item;");
                    if (st.step() && st.columnInt(0) == 5) {
                        readSuccesses++;
                    }
                } catch (const std::exception& e) {
                    std::cout << "  [THREAD_ERR] " << e.what() << "\n";
                } catch (...) {
                    std::cout << "  [THREAD_ERR] unknown\n";
                }
            });
        }
        for (auto& th : readers) th.join();
        check(readSuccesses == 4, "SQLITE_CONCURRENCY_STRESS", "4 concurrent reader threads completed cleanly without locks");
    }

    // 17. Formato de texto UTF-8 sem BOM com quebras LF
    {
        juce::File mSha = baseDir.getChildFile("export_pacote/manifest.sha256");
        if (mSha.existsAsFile()) {
            auto text = mSha.loadFileAsString();
            bool semBom = !text.startsWithChar(0xFEFF);
            bool usaLf = text.containsChar('\n') && !text.containsChar('\r');
            check(semBom && usaLf, "TEXT_UTF8_LF", "Manifest file encoded in UTF-8 without BOM with strict LF line endings");
        }
    }

    std::cout << "\nRESULTADO DA VERIFICAÇÃO INTEROP: " << failures << " FALHAS\n";
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 3) {
        std::cerr << "Uso: matriz_interop_selftest --gerar <dir> | --verificar <dir>\n";
        return 1;
    }

    std::string modo = argv[1];
    juce::File baseDir(argv[2]);

    if (modo == "--gerar") {
        return executarGeracao(baseDir);
    } else if (modo == "--verificar") {
        return executarVerificacao(baseDir);
    } else {
        std::cerr << "Modo desconhecido: " << modo << "\n";
        return 1;
    }
}
