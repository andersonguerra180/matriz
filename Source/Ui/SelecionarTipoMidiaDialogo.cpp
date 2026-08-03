#include "SelecionarTipoMidiaDialogo.h"

#include "../I18n/Strings.h"
#include "Tokens.h"

namespace matriz::ui {

namespace {

// Ordem de exibição segue §6.6. Nomes internos (id) nunca mudam — são a
// chave pro arquivo fichas/<id>.yaml e pro item_midia.tipo_midia no banco.
//
// Restrito por modo (Parte 1 da correção de fluxo — §1.2/§1.3): Archive é
// multiformato, os 14 tipos valem; Catalog é sobre a obra musical, não o
// suporte físico, então só oferece o que é musicalmente relevante —
// release e sample têm ficha própria; "faixa" é um nível dentro de
// release (não um tipo_midia à parte, já existente em release.yaml), e
// "master"/"stems" são papéis de arquivo (§5.4: preservation_master,
// stem), não tipos de mídia novos — nenhum dos dois tem ficha própria e o
// spec não define uma, então não inventamos aqui (§0). Os únicos suportes
// físicos de origem que o Catalog precisa são fita de rolo e vinil,
// citados explicitamente no documento.
const std::vector<std::string>& idsTiposMidiaArchive() {
    static const std::vector<std::string> ids = {"fita_rolo", "cassete", "vinil", "dat", "minidisc", "cd",
                                                   "filme", "video", "foto", "negativo", "slide",
                                                   "documento", "release", "sample"};
    return ids;
}

const std::vector<std::string>& idsTiposMidiaCatalog() {
    static const std::vector<std::string> ids = {"release", "sample", "fita_rolo", "vinil"};
    return ids;
}

} // namespace

std::vector<TipoMidiaOpcao> listarTiposMidiaDisponiveis(ProjetoAberto& projeto) {
    std::vector<TipoMidiaOpcao> out;
    const auto& ids = projeto.projeto().modo() == matriz::model::Modo::Catalogo ? idsTiposMidiaCatalog()
                                                                                  : idsTiposMidiaArchive();
    for (auto& id : ids) {
        try {
            const auto& def = projeto.definicaoPara(id);
            out.push_back({id, def.rotulo.empty() ? juce::String(id) : juce::String(def.rotulo)});
        } catch (const std::exception&) {
            // Definição ausente/corrompida pra esse tipo específico — não
            // trava o diálogo inteiro, só não oferece essa opção.
        }
    }
    return out;
}

void mostrarDialogoSelecionarTipoMidia(std::vector<TipoMidiaOpcao> opcoes, int quantidadeArquivos,
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
}

} // namespace matriz::ui
