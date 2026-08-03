// Self-test headless da Etapa 1 (§16): parser/validador das 14 definições de
// ficha, e criação/abertura de projeto portátil com as regras de
// imutabilidade embutidas na estrutura (P1). Sem UI — só console, saída não
// zero em qualquer falha.

#include <JuceHeader.h>

#include <functional>
#include <iostream>

#include "Ficha/FichaDefinition.h"
#include "Model/Project.h"

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

void expectThrow(const std::string& description, const std::function<void()>& fn) {
    try {
        fn();
        std::cout << "  FAIL " << description << " (esperava exceção, nenhuma foi lançada)\n";
        ++failures;
    } catch (const std::exception& e) {
        std::cout << "  OK   " << description << " (" << e.what() << ")\n";
    }
}

const std::vector<std::string>& tiposEsperados() {
    static const std::vector<std::string> tipos = {
        "fita_rolo", "cassete", "vinil", "dat", "minidisc", "cd",
        "filme", "video", "foto", "negativo", "slide", "documento",
        "release", "sample"};
    return tipos;
}

void testarDefinicoesDeFicha() {
    std::cout << "== Parser/validador de ficha (" << tiposEsperados().size() << " tipos) ==\n";
    juce::File fichasDir(MATRIZ_FICHAS_DIR);
    check(fichasDir.isDirectory(), "pasta fichas/ existe: " + fichasDir.getFullPathName().toStdString());

    for (auto& tipo : tiposEsperados()) {
        juce::File f = fichasDir.getChildFile(tipo + ".yaml");
        try {
            matriz::ficha::FichaDefinition def = matriz::ficha::loadFromFile(f.getFullPathName().toStdString());
            bool tipoOk = def.tipo == tipo;
            bool temCampos = !def.todosCampos().empty();
            check(tipoOk && temCampos,
                  tipo + ".yaml carregou (" + std::to_string(def.todosCampos().size()) + " campos, " +
                      (def.usaNiveis() ? "níveis: " + std::to_string(def.niveis.size()) : "grupos: " + std::to_string(def.grupos.size())) + ")");
        } catch (const std::exception& e) {
            check(false, tipo + ".yaml: " + e.what());
        }
    }

    // Definição inválida deliberada: grupos E niveis juntos devem ser rejeitados.
    expectThrow("definição com grupos+niveis é rejeitada", [] {
        matriz::ficha::loadFromString(
            "tipo: invalido\n"
            "rotulo: Inválido\n"
            "niveis: [raiz]\n"
            "raiz:\n"
            "  campos: []\n"
            "grupos:\n"
            "  - rotulo: X\n"
            "    campos: []\n");
    });

    // Campo tipo "opcao" sem opcoes deve ser rejeitado.
    expectThrow("campo opcao sem opcoes é rejeitado", [] {
        matriz::ficha::loadFromString(
            "tipo: invalido2\n"
            "rotulo: Inválido 2\n"
            "grupos:\n"
            "  - rotulo: G\n"
            "    campos:\n"
            "      - id: c1\n"
            "        rotulo: C1\n"
            "        tipo: opcao\n");
    });

    // sugerido_por e herda_do_projeto juntos no mesmo campo devem ser rejeitados.
    expectThrow("origens mutuamente exclusivas são impostas", [] {
        matriz::ficha::loadFromString(
            "tipo: invalido3\n"
            "rotulo: Inválido 3\n"
            "grupos:\n"
            "  - rotulo: G\n"
            "    campos:\n"
            "      - id: c1\n"
            "        rotulo: C1\n"
            "        tipo: texto\n"
            "        herda_do_projeto: true\n"
            "        sugerido_por: algum_modelo\n");
    });

    // YAML genuinamente inválido (âncora indefinida) — confirma que o erro do
    // yaml-cpp chega como FichaDefinitionError, não como exceção crua.
    expectThrow("YAML malformado (âncora indefinida) é rejeitado com mensagem do parser real", [] {
        matriz::ficha::loadFromString(
            "tipo: invalido4\n"
            "rotulo: Inválido 4\n"
            "grupos: *ancora_que_nao_existe\n");
    });

    // Recurso de YAML "de verdade" que o parser artesanal antigo não suportava:
    // âncora e referência reaproveitando um bloco de opções.
    try {
        auto def = matriz::ficha::loadFromString(
            "tipo: teste_ancora\n"
            "rotulo: Teste âncora\n"
            "grupos:\n"
            "  - rotulo: G\n"
            "    campos:\n"
            "      - id: c1\n"
            "        rotulo: C1\n"
            "        tipo: opcao\n"
            "        opcoes: &opcoes_sim_nao [sim, não]\n"
            "      - id: c2\n"
            "        rotulo: C2\n"
            "        tipo: opcao\n"
            "        opcoes: *opcoes_sim_nao\n");
        check(def.todosCampos().size() == 2 &&
                  def.todosCampos()[0]->opcoes == def.todosCampos()[1]->opcoes,
              "âncora/referência YAML (recurso real, não suportado pelo parser artesanal antigo) funciona via yaml-cpp");
    } catch (const std::exception& e) {
        check(false, std::string("âncora/referência YAML: ") + e.what());
    }
}

void testarProjetoPortavel() {
    std::cout << "== Projeto portátil (criação, imutabilidade de master, reabertura) ==\n";

    juce::File tmpRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("matriz_selftest_" + juce::Uuid().toDashedString());
    juce::File pastaProjeto = tmpRoot.getChildFile("Acervo Teste");

    matriz::model::NovoProjetoParams params;
    params.nome = "Acervo de Teste";
    params.modo = matriz::model::Modo::Preservacao;
    params.prefixoNomenclatura = "TST";
    params.instituicaoOuSelo = "Instituição de Teste";
    params.responsavel = "Operador Selftest";

    std::string itemId, arquivoId, projetoId;

    try {
        auto projeto = matriz::model::Project::criar(pastaProjeto, params);
        check(pastaProjeto.getChildFile("registro.sqlite").existsAsFile(), "registro.sqlite criado");
        check(pastaProjeto.getChildFile("indice.sqlite").existsAsFile(), "indice.sqlite criado");
        projetoId = projeto->projetoId();
        check(!projetoId.empty(), "projeto tem id: " + projetoId);

        // Segundo projeto no mesmo banco deve ser rejeitado (linha única).
        expectThrow("segunda linha em projeto é rejeitada (linha única)", [&] {
            projeto->registro().run(
                "INSERT INTO projeto (id, modo, nome, prefixo_nomenclatura, criado_em, atualizado_em) "
                "VALUES (?, 'catalogo', 'Outro', 'OUT', ?, ?)",
                {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(matriz::model::agoraIso8601()),
                 matriz::db::Value::of(matriz::model::agoraIso8601())});
        });

        std::string agora = matriz::model::agoraIso8601();
        itemId = matriz::model::novoUuid();
        projeto->registro().run(
            "INSERT INTO item (id, projeto_id, codigo_acervo, titulo, tipo_midia, criado_em, atualizado_em) "
            "VALUES (?, ?, 'TST-001', 'Rolo de teste', 'fita_rolo', ?, ?)",
            {matriz::db::Value::of(itemId), matriz::db::Value::of(projetoId), matriz::db::Value::of(agora),
             matriz::db::Value::of(agora)});
        check(true, "item inserido: " + itemId);

        projeto->registro().run(
            "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
            "VALUES (?, ?, 'raiz', 0, 'velocidade', '19 cm/s', 'humano', ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId), matriz::db::Value::of(agora)});
        check(true, "item_campo (velocidade, fonte humano) inserido");

        arquivoId = matriz::model::novoUuid();
        projeto->registro().run(
            "INSERT INTO arquivo (id, item_id, caminho_relativo, papel, eh_master, checksum_sha256, criado_em, atualizado_em) "
            "VALUES (?, ?, 'masters/tst-001.wav', 'preservation_master', 1, 'abc123', ?, ?)",
            {matriz::db::Value::of(arquivoId), matriz::db::Value::of(itemId), matriz::db::Value::of(agora),
             matriz::db::Value::of(agora)});
        check(true, "arquivo master inserido: " + arquivoId);

        expectThrow("master travado bloqueia sobrescrita de checksum (P1)", [&] {
            projeto->registro().run("UPDATE arquivo SET checksum_sha256 = 'zzz999' WHERE id = ?",
                                     {matriz::db::Value::of(arquivoId)});
        });

        projeto->registro().run("UPDATE projeto SET permitir_sobrescrever_master = 1 WHERE id = ?",
                                 {matriz::db::Value::of(projetoId)});
        projeto->registro().run("UPDATE arquivo SET checksum_sha256 = 'zzz999' WHERE id = ?",
                                 {matriz::db::Value::of(arquivoId)});

        auto stmt = projeto->registro().prepare("SELECT checksum_sha256 FROM arquivo WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(arquivoId));
        stmt.step();
        check(stmt.columnText(0) == "zzz999", "checksum atualizado após permitir_sobrescrever_master = 1");
    } catch (const std::exception& e) {
        check(false, std::string("fluxo de criação do projeto: ") + e.what());
    }

    // Reabre a pasta como se fosse outra máquina/outra sessão (P5).
    try {
        auto reaberto = matriz::model::Project::abrir(pastaProjeto);
        check(reaberto->projetoId() == projetoId, "projeto reaberto tem o mesmo id");

        auto stmt = reaberto->registro().prepare("SELECT codigo_acervo, titulo FROM item WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(itemId));
        bool achou = stmt.step();
        check(achou && stmt.columnText(0) == "TST-001", "item persistiu após reabrir o projeto");
    } catch (const std::exception& e) {
        check(false, std::string("reabertura do projeto: ") + e.what());
    }

    tmpRoot.deleteRecursively();
}

} // namespace

int main() {
    testarDefinicoesDeFicha();
    testarProjetoPortavel();

    std::cout << "\n" << (failures == 0 ? "TODOS OS TESTES PASSARAM" : std::to_string(failures) + " FALHA(S)") << "\n";
    return failures == 0 ? 0 : 1;
}
