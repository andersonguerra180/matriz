#include <JuceHeader.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <iomanip>

#include "Model/Project.h"
#include "Model/CaminhosBanco.h"
#include "Model/NomesCanonicos.h"
#include "Model/NomesSeguros.h"
#include "Ingest/Checksum.h"
#include "Db/Database.h"

namespace {

int failures = 0;

void check(bool condition, const std::string& description) {
    if (condition) {
        std::cout << "  OK   " << description << "\n";
    } else {
        std::cout << "  FAIL " << description << "\n";
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

    // 2. Cria projeto no banco SQLite
    matriz::model::NovoProjetoParams params;
    params.nome = "Projeto Interop Mac-Win";
    params.prefixoNomenclatura = "PROJ-INT";
    params.instituicaoOuSelo = "2026";
    params.responsavel = "Criador Interop";
    auto proj = matriz::model::Project::criar(pastaProj, params);
    check(proj != nullptr, "Project created successfully");
    if (!proj) return 1;

    auto& db = proj->registro();

    // 3. Ingestão e catalogação de itens
    auto ck1 = matriz::ingest::calcularChecksums(arq1);
    auto ck2 = matriz::ingest::calcularChecksums(arq2);
    auto ckH = matriz::ingest::calcularChecksums(arqHidden);
    auto ckP = matriz::ingest::calcularChecksums(arqProfundo);

    std::string id1 = matriz::model::novoUuid();
    std::string id2 = matriz::model::novoUuid();
    std::string idH = matriz::model::novoUuid();
    std::string idP = matriz::model::novoUuid();

    std::string arqId1 = matriz::model::novoUuid();
    std::string arqId2 = matriz::model::novoUuid();
    std::string arqIdH = matriz::model::novoUuid();
    std::string arqIdP = matriz::model::novoUuid();

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

    // 4. Cria folder map
    std::string mapaId = matriz::model::novoUuid();
    std::string pastaMapId = matriz::model::novoUuid();
    db.run("INSERT INTO folder_map (id, projeto_id, nome, ordem, criado_em, atualizado_em) VALUES (?, ?, 'Mapa Shows 2026', 1, ?, ?)",
           {Value::of(mapaId), Value::of(proj->projetoId()), Value::of(agora), Value::of(agora)});
    db.run("INSERT INTO acervo_pasta (id, projeto_id, pasta_pai_id, nome, ordem, criado_em, atualizado_em) VALUES (?, ?, NULL, 'Shows 2026', 0, ?, ?)",
           {Value::of(pastaMapId), Value::of(proj->projetoId()), Value::of(agora), Value::of(agora)});
    db.run("INSERT INTO acervo_item_pasta (id, item_id, pasta_id, criado_em) VALUES (?, ?, ?, ?)",
           {Value::of(matriz::model::novoUuid()), Value::of(id1), Value::of(pastaMapId), Value::of(agora)});

    // 5. Simula cópias no MAIN
    juce::File mainArq1 = pastaMain.getChildFile("Media/Áudio e Música/Gravação_2026_SãoPaulo_Éxito.txt");
    arq1.copyFileTo(mainArq1);
    juce::File mainArq2 = pastaMain.getChildFile("Media/Fotos & Vídeos/Apresentação/Foto_Ção_Ão.txt");
    arq2.copyFileTo(mainArq2);

    // 6. Simula pacote de EXPORT
    juce::File exportMedia = pastaExport.getChildFile("Media");
    exportMedia.createDirectory();
    mainArq1.copyFileTo(exportMedia.getChildFile(mainArq1.getFileName()));
    mainArq2.copyFileTo(exportMedia.getChildFile(mainArq2.getFileName()));

    // 7. Checkpoint WAL
    try {
        db.exec("PRAGMA wal_checkpoint(TRUNCATE);");
    } catch (...) {}

    // 8. Grava manifesto JSON da fixture
    juce::DynamicObject::Ptr manifest = new juce::DynamicObject();
    manifest->setProperty("total_itens", 4);
    manifest->setProperty("total_arquivos", 4);
    manifest->setProperty("id1", juce::String(id1));
    manifest->setProperty("id2", juce::String(id2));
    manifest->setProperty("id_hidden", juce::String(idH));
    manifest->setProperty("id_profundo", juce::String(idP));
    manifest->setProperty("sha256_1", juce::String(ck1.sha256));
    manifest->setProperty("sha256_2", juce::String(ck2.sha256));
    manifest->setProperty("sha256_hidden", juce::String(ckH.sha256));
    manifest->setProperty("sha256_profundo", juce::String(ckP.sha256));

    juce::File manifestFile = baseDir.getChildFile("manifesto_interop.json");
    manifestFile.replaceWithText(juce::JSON::toString(juce::var(manifest.get()), true));

    std::cout << "== FIXTURE GERADA COM SUCESSO EM " << baseDir.getFullPathName() << " ==\n";
    return 0;
}

int executarVerificacao(const juce::File& baseDir) {
    std::cout << "== MATRIZ INTEROP: VERIFICANDO FIXTURE EM " << baseDir.getFullPathName() << " ==\n";

    juce::File manifestFile = baseDir.getChildFile("manifesto_interop.json");
    check(manifestFile.existsAsFile(), "manifesto_interop.json exists");
    if (!manifestFile.existsAsFile()) return 1;

    auto varManifest = juce::JSON::parse(manifestFile);
    check(varManifest.isObject(), "manifesto_interop.json parsed successfully");
    if (!varManifest.isObject()) return 1;

    juce::File pastaProj = baseDir.getChildFile("projeto");
    auto proj = matriz::model::Project::abrir(pastaProj);
    check(proj != nullptr, "Project opened cleanly from " + pastaProj.getFullPathName().toStdString());
    if (!proj) return 1;

    auto& db = proj->registro();

    // 1. Contagens de itens e integridade
    auto stmtCount = db.prepare("SELECT COUNT(*) FROM item;");
    if (stmtCount.step()) {
        int count = stmtCount.columnInt(0);
        check(count == 4, "total items in database matches expected (4), got " + std::to_string(count));
    }

    // 2. Caminhos relativos SEMPRE com '/' (Zero barras invertidas '\' no banco)
    auto stmtPaths = db.prepare("SELECT caminho_relativo FROM arquivo;");
    bool todosCaminhosComBarra = true;
    while (stmtPaths.step()) {
        std::string rel = stmtPaths.columnText(0);
        if (rel.find('\\') != std::string::npos) {
            todosCaminhosComBarra = false;
            std::cout << "  FAIL Caminho com barra invertida encontrado no banco: " << rel << "\n";
            ++failures;
        }
    }
    check(todosCaminhosComBarra, "all relative paths in database strictly use '/' format (no backslashes)");

    // 3. Verifica item Hidden
    auto stmtHidden = db.prepare("SELECT c.valor FROM item_campo c JOIN item i ON c.item_id = i.id WHERE i.titulo = 'Documento Oculto' AND c.campo_id = 'collection_type';");
    bool achouHidden = false;
    if (stmtHidden.step()) {
        achouHidden = (stmtHidden.columnText(0) == "Hidden");
    }
    check(achouHidden, "Hidden item preserved and correctly classified");

    // 4. Verifica campos e tags
    auto stmtArtist = db.prepare("SELECT c.valor FROM item_campo c JOIN item i ON c.item_id = i.id WHERE i.titulo = 'Gravação do Show de Sucesso' AND c.campo_id = 'artista_principal';");
    bool achouArtista = false;
    if (stmtArtist.step()) {
        achouArtista = (stmtArtist.columnText(0) == "Anderson Guerra");
    }
    check(achouArtista, "Field 'artista_principal' preserved");

    // 5. Verifica normalização canônica de nomes (NFC)
    std::string chave1 = matriz::model::nomes::chave("São Paulo");
    std::string chave2 = matriz::model::nomes::chave("são paulo");
    check(chave1 == chave2, "Canonical Unicode key comparison matches ('São Paulo' == 'são paulo')");

    // 6. Verifica integridade de checksums
    std::string expSha1 = varManifest["sha256_1"].toString().toStdString();
    auto stmtSha = db.prepare("SELECT a.checksum_sha256 FROM arquivo a JOIN item i ON a.item_id = i.id WHERE i.titulo = 'Gravação do Show de Sucesso';");
    if (stmtSha.step()) {
        std::string dbSha = stmtSha.columnText(0);
        check(dbSha == expSha1, "SHA-256 matches exact hash: " + dbSha);
    }

    // 7. Verifica folder map
    auto stmtMap = db.prepare("SELECT nome FROM folder_map WHERE nome = 'Mapa Shows 2026';");
    bool mapaOk = false;
    if (stmtMap.step()) {
        if (stmtMap.columnText(0) == "Mapa Shows 2026") mapaOk = true;
    }
    check(mapaOk, "Folder Map structure preserved and matched exactly");

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
