#include "SelecionarTipoMidiaDialogo.h"

#include "../Ficha/CatalogoDeFichas.h"
#include "../Ficha/FichaI18n.h"
#include "../I18n/Strings.h"
#include "Tokens.h"

namespace matriz::ui {

// Tipos de mídia descobertos em tempo de execução em fichas/*.yaml (§6.1:
// "adicionar tipo novo é escrever um arquivo, não recompilar") — nenhuma
// lista de ids hardcoded aqui. Cada ficha declara seu próprio `modos`
// (archive e/ou catalog) e `ordem` (posição de exibição); ficha sem
// `modos` fica disponível nos dois. Um YAML inválido em fichas/ derruba o
// diálogo inteiro (listarTiposPorModo lança, não pula em silêncio) —
// falha visível é preferível a um tipo sumir sem explicação.
std::vector<TipoMidiaOpcao> listarTiposMidiaDisponiveis(ProjetoAberto& projeto) {
    std::vector<TipoMidiaOpcao> out;
    std::string modo = projeto.projeto().modo() == matriz::model::Modo::Catalogo ? "catalog" : "archive";
    for (auto& info : matriz::ficha::listarTiposPorModo(MATRIZ_FICHAS_DIR, modo)) {
        juce::String rotulo = matriz::ficha::rotuloTipo(info.id, info.definicao.rotulo.empty() ? info.id : info.definicao.rotulo);
        out.push_back({info.id, rotulo});
    }
    return out;
}

std::shared_ptr<juce::AlertWindow> mostrarDialogoSelecionarTipoMidia(
    std::vector<TipoMidiaOpcao> opcoes, int quantidadeArquivos,
    std::function<void(std::optional<std::string> tipoEscolhido)> aoConcluir) {
    auto janela = std::make_shared<juce::AlertWindow>(
        matriz::i18n::t("ingest.selecionar_tipo_titulo"),
        matriz::i18n::t("ingest.selecionar_tipo_corpo").replace("{n}", juce::String(quantidadeArquivos)),
        juce::MessageBoxIconType::QuestionIcon);

    auto combo = std::make_shared<juce::ComboBox>();
    for (size_t i = 0; i < opcoes.size(); ++i) combo->addItem(opcoes[i].rotulo, static_cast<int>(i) + 1);
    combo->setSelectedId(opcoes.empty() ? 0 : 1, juce::dontSendNotification);
    combo->setSize(420, 26);
    janela->addCustomComponent(combo.get());

    janela->addButton(matriz::i18n::t("ingest.selecionar_tipo_confirmar"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    janela->addButton(matriz::i18n::t("comum.cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));

    janela->enterModalState(true, juce::ModalCallbackFunction::create([janela, combo, opcoes, aoConcluir](int resultado) {
                                 if (resultado != 1 || opcoes.empty()) {
                                     aoConcluir(std::nullopt);
                                     return;
                                 }
                                 int indice = combo->getSelectedId() - 1;
                                 if (indice < 0 || indice >= static_cast<int>(opcoes.size())) {
                                     aoConcluir(std::nullopt);
                                     return;
                                 }
                                 aoConcluir(opcoes[static_cast<size_t>(indice)].id);
                             }));

    return janela;
}

} // namespace matriz::ui
