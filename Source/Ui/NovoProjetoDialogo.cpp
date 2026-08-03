#include "NovoProjetoDialogo.h"

#include "../I18n/Strings.h"
#include "Tokens.h"

namespace matriz::ui {

namespace {

// Rótulo + widget de entrada, empilhados — uma "linha" de formulário dentro
// do AlertWindow (que só aceita Component por addCustomComponent()).
class LinhaFormulario : public juce::Component {
public:
    LinhaFormulario(const juce::String& rotulo, std::unique_ptr<juce::Component> widget) : widget_(std::move(widget)) {
        rotulo_.setText(rotulo, juce::dontSendNotification);
        rotulo_.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
        rotulo_.setColour(juce::Label::textColourId, tema().textoSecundario);
        addAndMakeVisible(rotulo_);
        addAndMakeVisible(*widget_);
        setSize(420, 50);
    }

    void resized() override {
        auto area = getLocalBounds();
        rotulo_.setBounds(area.removeFromTop(16));
        widget_->setBounds(area.reduced(0, 2));
    }

    juce::Component& widget() { return *widget_; }

private:
    juce::Label rotulo_;
    std::unique_ptr<juce::Component> widget_;
};

} // namespace

void mostrarDialogoNovoProjeto(std::function<void(std::optional<NovoProjetoResultado>)> aoConcluir) {
    auto janela = std::make_shared<juce::AlertWindow>(matriz::i18n::t("dialogo_novo_projeto.titulo"), juce::String(),
                                                        juce::MessageBoxIconType::NoIcon);

    auto nomeEditor = std::make_unique<juce::TextEditor>();
    auto* nomeEditorPtr = nomeEditor.get();
    auto linhaNome = std::make_unique<LinhaFormulario>(matriz::i18n::t("dialogo_novo_projeto.campo_nome"), std::move(nomeEditor));

    auto modoCombo = std::make_unique<juce::ComboBox>();
    modoCombo->addItem(matriz::i18n::t("dialogo_novo_projeto.campo_modo_preservacao"), 1);
    modoCombo->addItem(matriz::i18n::t("dialogo_novo_projeto.campo_modo_catalogo"), 2);
    modoCombo->setSelectedId(1, juce::dontSendNotification);
    auto* modoComboPtr = modoCombo.get();
    auto linhaModo = std::make_unique<LinhaFormulario>(matriz::i18n::t("dialogo_novo_projeto.campo_modo"), std::move(modoCombo));

    auto prefixoEditor = std::make_unique<juce::TextEditor>();
    auto* prefixoEditorPtr = prefixoEditor.get();
    auto linhaPrefixo =
        std::make_unique<LinhaFormulario>(matriz::i18n::t("dialogo_novo_projeto.campo_prefixo"), std::move(prefixoEditor));

    auto instituicaoEditor = std::make_unique<juce::TextEditor>();
    auto* instituicaoEditorPtr = instituicaoEditor.get();
    auto linhaInstituicao = std::make_unique<LinhaFormulario>(matriz::i18n::t("dialogo_novo_projeto.campo_instituicao"),
                                                                std::move(instituicaoEditor));

    auto responsavelEditor = std::make_unique<juce::TextEditor>();
    auto* responsavelEditorPtr = responsavelEditor.get();
    auto linhaResponsavel = std::make_unique<LinhaFormulario>(matriz::i18n::t("dialogo_novo_projeto.campo_responsavel"),
                                                                std::move(responsavelEditor));

    auto isrcEditor = std::make_unique<juce::TextEditor>();
    auto* isrcEditorPtr = isrcEditor.get();
    auto linhaIsrc = std::make_unique<LinhaFormulario>(matriz::i18n::t("dialogo_novo_projeto.campo_isrc"), std::move(isrcEditor));

    auto pastaBotao = std::make_unique<juce::TextButton>(matriz::i18n::t("dialogo_novo_projeto.botao_escolher_pasta"));
    auto pastaLabel = std::make_shared<juce::Label>();
    pastaLabel->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
    pastaLabel->setColour(juce::Label::textColourId, tema().textoTerciario);
    auto pastaEscolhida = std::make_shared<juce::File>();
    auto seletorPasta = std::make_shared<juce::FileChooser>(matriz::i18n::t("dialogo_novo_projeto.campo_pasta"));

    auto* pastaBotaoPtr = pastaBotao.get();
    pastaBotaoPtr->onClick = [seletorPasta, pastaEscolhida, pastaLabel] {
        seletorPasta->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                   [pastaEscolhida, pastaLabel](const juce::FileChooser& fc) {
                                       auto resultado = fc.getResult();
                                       if (resultado != juce::File()) {
                                           *pastaEscolhida = resultado;
                                           pastaLabel->setText(resultado.getFullPathName(), juce::dontSendNotification);
                                       }
                                   });
    };
    auto linhaPasta = std::make_unique<LinhaFormulario>(matriz::i18n::t("dialogo_novo_projeto.campo_pasta"), std::move(pastaBotao));
    linhaPasta->addAndMakeVisible(*pastaLabel);
    pastaLabel->setBounds(0, 34, 420, 16);
    linhaPasta->setSize(420, 54);

    for (auto* linha : {linhaNome.get(), linhaModo.get(), linhaPrefixo.get(), linhaInstituicao.get(), linhaResponsavel.get(),
                         linhaIsrc.get(), linhaPasta.get()})
        janela->addCustomComponent(linha);

    janela->addButton(matriz::i18n::t("dialogo_novo_projeto.botao_criar"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    janela->addButton(matriz::i18n::t("dialogo_novo_projeto.botao_cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));

    // As LinhaFormulario precisam sobreviver até o callback rodar — o
    // AlertWindow não assume posse dos componentes que recebe.
    struct EstadoVivo {
        std::shared_ptr<juce::AlertWindow> janela;
        std::unique_ptr<LinhaFormulario> nome, modo, prefixo, instituicao, responsavel, isrc, pasta;
    };
    auto estado = std::make_shared<EstadoVivo>();
    estado->janela = janela;
    estado->nome = std::move(linhaNome);
    estado->modo = std::move(linhaModo);
    estado->prefixo = std::move(linhaPrefixo);
    estado->instituicao = std::move(linhaInstituicao);
    estado->responsavel = std::move(linhaResponsavel);
    estado->isrc = std::move(linhaIsrc);
    estado->pasta = std::move(linhaPasta);

    janela->enterModalState(
        true,
        juce::ModalCallbackFunction::create([estado, aoConcluir, nomeEditorPtr, modoComboPtr, prefixoEditorPtr,
                                              instituicaoEditorPtr, responsavelEditorPtr, isrcEditorPtr, pastaEscolhida,
                                              seletorPasta](int resultadoBotao) {
            if (resultadoBotao != 1) {
                aoConcluir(std::nullopt);
                return;
            }

            juce::String nome = nomeEditorPtr->getText().trim();
            juce::String prefixo = prefixoEditorPtr->getText().trim();
            if (nome.isEmpty() || prefixo.isEmpty() || *pastaEscolhida == juce::File()) {
                juce::String erro = nome.isEmpty()      ? matriz::i18n::t("dialogo_novo_projeto.erro_nome_vazio")
                                     : prefixo.isEmpty() ? matriz::i18n::t("dialogo_novo_projeto.erro_prefixo_vazio")
                                                          : matriz::i18n::t("dialogo_novo_projeto.erro_pasta_vazia");
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle(matriz::i18n::t("dialogo_novo_projeto.erro_titulo"))
                        .withMessage(erro)
                        .withButton(matriz::i18n::t("comum.ok")),
                    static_cast<juce::ModalComponentManager::Callback*>(nullptr));
                aoConcluir(std::nullopt);
                return;
            }

            NovoProjetoResultado r;
            r.pasta = *pastaEscolhida;
            r.params.nome = nome.toStdString();
            r.params.modo = modoComboPtr->getSelectedId() == 2 ? matriz::model::Modo::Catalogo : matriz::model::Modo::Preservacao;
            r.params.prefixoNomenclatura = prefixo.toStdString();
            r.params.instituicaoOuSelo = instituicaoEditorPtr->getText().trim().toStdString();
            r.params.responsavel = responsavelEditorPtr->getText().trim().toStdString();
            r.params.isrcRegistrante = isrcEditorPtr->getText().trim().toStdString();

            aoConcluir(r);
        }));
}

} // namespace matriz::ui
