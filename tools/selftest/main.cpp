// Self-test headless da Etapa 1 (§16): parser/validador das 14 definições de
// ficha, e criação/abertura de projeto portátil com as regras de
// imutabilidade embutidas na estrutura (P1). Sem UI — só console, saída não
// zero em qualquer falha.

#include <JuceHeader.h>

#include <functional>
#include <iostream>

#include "App/Preferencias.h"
#include "Ficha/CatalogoDeFichas.h"
#include "Ficha/FichaDefinition.h"
#include "I18n/Strings.h"
#include "Model/Project.h"
#include "Vault/SmartHealth.h"

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
        std::cout << "  FAIL " << description << " (expected an exception, none was thrown)\n";
        ++failures;
    } catch (const std::exception& e) {
        std::cout << "  OK   " << description << " (" << e.what() << ")\n";
    }
}

// Tipos descobertos em tempo de execução em fichas/*.yaml (§6.1) — nenhuma
// lista de ids hardcoded. Se algum YAML for inválido, listarTodosOsTipos
// lança; deixamos propagar pro catch abaixo, que reporta e para o teste
// (falha visível é o comportamento desejado, não pular o arquivo quebrado).
void testarDefinicoesDeFicha() {
    juce::File fichasDir(MATRIZ_FICHAS_DIR);
    check(fichasDir.isDirectory(), "fichas/ directory exists: " + fichasDir.getFullPathName().toStdString());

    std::vector<matriz::ficha::TipoFichaInfo> tipos;
    try {
        tipos = matriz::ficha::listarTodosOsTipos(MATRIZ_FICHAS_DIR);
    } catch (const std::exception& e) {
        check(false, std::string("listarTodosOsTipos(fichas/): ") + e.what());
        return;
    }
    std::cout << "== Record parser/validator (" << tipos.size() << " types discovered in fichas/) ==\n";

    for (auto& info : tipos) {
        bool tipoOk = info.definicao.tipo == info.id;
        bool temCampos = !info.definicao.todosCampos().empty();
        check(tipoOk && temCampos,
              info.id + ".yaml loaded (" + std::to_string(info.definicao.todosCampos().size()) + " fields, " +
                  (info.definicao.usaNiveis() ? "levels: " + std::to_string(info.definicao.niveis.size())
                                               : "groups: " + std::to_string(info.definicao.grupos.size())) +
                  ")");
    }

    // Definição inválida deliberada: grupos E niveis juntos devem ser rejeitados.
    expectThrow("a definition with both groups and levels is rejected", [] {
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
    expectThrow("an option field with no options is rejected", [] {
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
    expectThrow("mutually exclusive value origins are enforced", [] {
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
    expectThrow("malformed YAML (undefined anchor) is rejected with the real parser message", [] {
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
              "YAML anchors/references work through yaml-cpp");
    } catch (const std::exception& e) {
        check(false, std::string("YAML anchor/reference: ") + e.what());
    }
}

// Passo 5 da Fatia A (correção do usuário): tradução de grupo por nome, não
// por posição. Todo grupo de toda ficha precisa de uma chave correspondente
// em i18n/en.yaml (ficha_grupos.<tipo>.<chave>) — esquecer uma vira erro de
// teste aqui, não um rótulo trocado em silêncio em produção.
void testarTraducaoDeGrupos() {
    std::cout << "== Group label coverage (ficha_grupos.<type>.<key>) ==\n";
    matriz::i18n::carregar("en");
    for (auto& info : matriz::ficha::listarTodosOsTipos(MATRIZ_FICHAS_DIR)) {
        if (info.definicao.usaNiveis()) continue; // fichas com níveis não têm grupos
        for (auto& grupo : info.definicao.grupos) {
            std::string chave = "ficha_grupos." + info.id + "." + grupo.chave;
            check(matriz::i18n::existe(chave), chave + " exists in the English table");
        }
    }
}

void testarProjetoPortavel() {
    std::cout << "== Portable project (creation, master immutability, reopening) ==\n";

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
        check(pastaProjeto.getChildFile("registro.sqlite").existsAsFile(), "registro.sqlite created");
        check(pastaProjeto.getChildFile("indice.sqlite").existsAsFile(), "indice.sqlite created");
        projetoId = projeto->projetoId();
        check(!projetoId.empty(), "project has id: " + projetoId);

        // Segundo projeto no mesmo banco deve ser rejeitado (linha única).
        expectThrow("a second row in `projeto` is rejected (single-row table)", [&] {
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
        check(true, "item inserted: " + itemId);

        projeto->registro().run(
            "INSERT INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
            "VALUES (?, ?, 'raiz', 0, 'velocidade', '19 cm/s', 'humano', ?)",
            {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId), matriz::db::Value::of(agora)});
        check(true, "item_campo (velocidade, human source) inserted");

        arquivoId = matriz::model::novoUuid();
        projeto->registro().run(
            "INSERT INTO arquivo (id, item_id, caminho_relativo, papel, eh_master, checksum_sha256, criado_em, atualizado_em) "
            "VALUES (?, ?, 'masters/tst-001.wav', 'preservation_master', 1, 'abc123', ?, ?)",
            {matriz::db::Value::of(arquivoId), matriz::db::Value::of(itemId), matriz::db::Value::of(agora),
             matriz::db::Value::of(agora)});
        check(true, "master file inserted: " + arquivoId);

        expectThrow("a locked master blocks checksum overwrite (P1)", [&] {
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
        check(stmt.columnText(0) == "zzz999", "checksum updated once permitir_sobrescrever_master = 1");
    } catch (const std::exception& e) {
        check(false, std::string("project creation flow: ") + e.what());
    }

    // Reabre a pasta como se fosse outra máquina/outra sessão (P5).
    try {
        auto reaberto = matriz::model::Project::abrir(pastaProjeto);
        check(reaberto->projetoId() == projetoId, "the reopened project has the same id");

        auto stmt = reaberto->registro().prepare("SELECT codigo_acervo, titulo FROM item WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(itemId));
        bool achou = stmt.step();
        check(achou && stmt.columnText(0) == "TST-001", "the item survived reopening the project");
    } catch (const std::exception& e) {
        check(false, std::string("project reopening: ") + e.what());
    }

    tmpRoot.deleteRecursively();
}

// Idioma único (§6): a interface inteira é em inglês, vinda da tabela
// estática de Source/Ui/Strings.h. Não há mais troca de locale em tempo real
// nem tabela pt_BR — carregar() continua existindo (chamado no start do
// programa) mas é no-op, e pedir outro locale não pode mudar nada nem travar.
void testarI18n() {
    std::cout << "== i18n (single language: English) ==\n";

    matriz::i18n::carregar("en");
    check(matriz::i18n::t("menu.arquivo") == "File", "menu.arquivo = \"File\"");
    // O rótulo do modo deixou de ser o jargão "Archive"/"Acervo" e passou a
    // dizer o que a pessoa tem em mãos ("mixed files") — mudança deliberada
    // de vocabulário, não regressão.
    check(matriz::i18n::t("dialogo_novo_projeto.campo_modo_preservacao") == "New Collection project",
          "preservation mode is called \"New Collection project\"");
    check(matriz::i18n::t("chave.que.nao.existe") == "[chave.que.nao.existe]",
          "a missing key returns [key], never throws and never hangs");

    // Pedir outro locale não derruba nem devolve string vazia: continua tudo
    // em inglês (critério 13 — interface, logs e relatórios 100% em inglês).
    matriz::i18n::carregar("pt_BR");
    check(matriz::i18n::t("menu.arquivo") == "File",
          "asking for pt_BR changes nothing: the interface stays in English");
    check(matriz::i18n::t("dialogo_novo_projeto.campo_modo_preservacao") == "New Collection project",
          "no Portuguese string survives after asking for pt_BR");

    matriz::i18n::carregar("en");
    check(matriz::i18n::t("menu.arquivo") == "File", "switching back to en is idempotent");

    // Valores de banco traduzidos em tempo real (§6) — estado de item,
    // prioridade e estado de presença vêm do SQLite em português.
    check(matriz::i18n::t("nao_baixado") == "Not Downloaded",
          "the database value 'nao_baixado' is displayed as \"Not Downloaded\"");
    check(matriz::i18n::t("ausente") == "Missing", "the database value 'ausente' is displayed as \"Missing\"");
}

// Lista de projetos recentes (Parte 1 da correção de fluxo — tela inicial
// com Archive/Catalog/Open + recentes). Testa só a lógica pura de
// dedup+topo+limite (matriz::app::comRecenteNoTopo), sem tocar no arquivo
// real de preferências do usuário.
void testarRecentes() {
    std::cout << "== Recent projects (pure logic, no real file touched) ==\n";
    using matriz::app::comRecenteNoTopo;
    using matriz::app::ProjetoRecente;

    std::vector<ProjetoRecente> lista;
    lista = comRecenteNoTopo(lista, {"/a", "Projeto A", "preservacao"});
    lista = comRecenteNoTopo(lista, {"/b", "Projeto B", "catalogo"});
    check(lista.size() == 2 && lista[0].pasta == "/b" && lista[1].pasta == "/a",
          "the most recent one goes to the top");

    lista = comRecenteNoTopo(lista, {"/a", "Projeto A (renomeado)", "preservacao"});
    check(lista.size() == 2 && lista[0].pasta == "/a" && lista[0].nome == "Projeto A (renomeado)",
          "reopening a project already in the list moves it to the top instead of duplicating");

    std::vector<ProjetoRecente> cheia;
    for (int i = 0; i < 5; ++i) cheia = comRecenteNoTopo(cheia, {"/p" + std::to_string(i), "P", "catalogo"}, 3);
    check(cheia.size() == 3 && cheia[0].pasta == "/p4" && cheia[2].pasta == "/p2",
          "the size cap drops the oldest (3 most recent out of 5 insertions)");
}

void testarSmartHealth() {
    std::cout << "== SMART Drive Health Evaluation & JSON Parsing ==\n";
    using namespace matriz::vault;

    // 1. Healthy ATA Drive Payload
    juce::String healthyJson = R"({
        "smartctl": { "exit_status": 0 },
        "smart_status": { "passed": true },
        "temperature": { "current": 34 },
        "power_on_time": { "hours": 18432 },
        "ata_smart_attributes": {
            "table": [
                { "id": 5, "raw": { "value": 0 } },
                { "id": 197, "raw": { "value": 0 } },
                { "id": 198, "raw": { "value": 0 } }
            ]
        }
    })";
    auto repH = parseSmartctlJson(healthyJson);
    check(repH.state == HealthState::Healthy, "healthy payload produces HEALTHY state");
    check(repH.smartStatus == "PASSED", "SMART status is PASSED");
    check(repH.temperatureC == 34, "temperature is 34 °C");
    check(repH.powerOnHours == 18432, "power-on hours is 18432 h");
    check(repH.reallocatedSectors == 0, "reallocated sectors is 0");
    check(repH.pendingSectors == 0, "pending sectors is 0");
    check(repH.uncorrectableSectors == 0, "uncorrectable sectors is 0");

    // 2. Warning Payload (Reallocated sectors present)
    juce::String warningJson = R"({
        "smartctl": { "exit_status": 0 },
        "smart_status": { "passed": true },
        "temperature": { "current": 36 },
        "power_on_time": { "hours": 8120 },
        "ata_smart_attributes": {
            "table": [
                { "id": 5, "raw": { "value": 12 } },
                { "id": 197, "raw": { "value": 0 } },
                { "id": 198, "raw": { "value": 0 } }
            ]
        }
    })";
    auto repW = parseSmartctlJson(warningJson);
    check(repW.state == HealthState::Warning, "reallocated sectors trigger WARNING state");
    check(repW.reallocatedSectors == 12, "reallocated count is 12");

    // 3. Failing Payload
    juce::String failingJson = R"({
        "smartctl": { "exit_status": 8 },
        "smart_status": { "passed": false },
        "temperature": { "current": 42 }
    })";
    auto repF = parseSmartctlJson(failingJson);
    check(repF.state == HealthState::Failing, "failed SMART status triggers FAILING state");
    check(repF.smartStatus == "FAILED", "smartStatus is FAILED");

    // 4. Unavailable Payload (e.g. exit_status bit 1 - device open failed)
    juce::String unavailJson = R"({
        "smartctl": { "exit_status": 2 }
    })";
    auto repU = parseSmartctlJson(unavailJson);
    check(repU.state == HealthState::Unavailable, "device open failure triggers UNAVAILABLE state");
    check(repU.unavailableMessage.contains("unavailable"), "unavailable message is informative");

    // 5. Empty / Invalid string
    auto repEmpty = parseSmartctlJson("");
    check(repEmpty.state == HealthState::Unavailable, "empty input triggers UNAVAILABLE state");
}

} // namespace

int main() {
    testarDefinicoesDeFicha();
    testarTraducaoDeGrupos();
    testarProjetoPortavel();
    testarI18n();
    testarRecentes();
    testarSmartHealth();

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : std::to_string(failures) + " FAILURE(S)") << "\n";
    return failures == 0 ? 0 : 1;
}
