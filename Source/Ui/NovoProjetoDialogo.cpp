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

// Prefixo de nomenclatura derivado do nome do projeto.
//
// Este era um campo de texto obrigatório, com o rótulo "Prefixo de
// nomenclatura" — jargão de arquivologia que quem só quer organizar um HD
// não tem como responder, e que travava a criação do projeto até ser
// preenchido. Agora sai do nome automaticamente: iniciais quando o nome
// tem várias palavras, senão as primeiras letras. O valor continua
// existindo no banco e nos códigos de acervo (ACR-00001), só deixou de ser
// uma pergunta feita ao operador.
juce::String prefixoAPartirDoNome(const juce::String& nome) {
    juce::StringArray palavras;
    palavras.addTokens(nome, " -_.", "");
    palavras.removeEmptyStrings();

    juce::String prefixo;
    if (palavras.size() >= 2) {
        for (auto& palavra : palavras) {
            juce::juce_wchar inicial = palavra[0];
            if (juce::CharacterFunctions::isLetterOrDigit(inicial)) prefixo << inicial;
            if (prefixo.length() >= 5) break;
        }
    } else if (palavras.size() == 1) {
        for (int i = 0; i < palavras[0].length() && prefixo.length() < 5; ++i) {
            juce::juce_wchar c = palavras[0][i];
            if (juce::CharacterFunctions::isLetterOrDigit(c)) prefixo << c;
        }
    }

    prefixo = prefixo.toUpperCase();
    // Nome só de pontuação/emoji não gera nada aproveitável — o código de
    // acervo precisa de algum prefixo, então cai num neutro em vez de
    // gerar "-00001".
    return prefixo.isEmpty() ? juce::String("MTZ") : prefixo;
}

} // namespace

std::shared_ptr<juce::AlertWindow> mostrarDialogoNovoProjeto(
    matriz::model::Modo modo, std::function<void(std::optional<NovoProjetoResultado>)> aoConcluir) {
    juce::String tituloModo = modo == matriz::model::Modo::Catalogo
                                   ? matriz::i18n::t("dialogo_novo_projeto.campo_modo_catalogo")
                                   : matriz::i18n::t("dialogo_novo_projeto.campo_modo_preservacao");
    auto janela = std::make_shared<juce::AlertWindow>(tituloModo, juce::String(), juce::MessageBoxIconType::NoIcon);

    auto nomeEditor = std::make_unique<juce::TextEditor>();
    auto* nomeEditorPtr = nomeEditor.get();
    auto linhaNome = std::make_unique<LinhaFormulario>(matriz::i18n::t("dialogo_novo_projeto.campo_nome"), std::move(nomeEditor));

    auto instituicaoEditor = std::make_unique<juce::TextEditor>();
    auto* instituicaoEditorPtr = instituicaoEditor.get();
    auto linhaInstituicao = std::make_unique<LinhaFormulario>(matriz::i18n::t("dialogo_novo_projeto.campo_instituicao"),
                                                                std::move(instituicaoEditor));

    auto responsavelEditor = std::make_unique<juce::TextEditor>();
    auto* responsavelEditorPtr = responsavelEditor.get();
    auto linhaResponsavel = std::make_unique<LinhaFormulario>(matriz::i18n::t("dialogo_novo_projeto.campo_responsavel"),
                                                                std::move(responsavelEditor));

    // ISRC só faz sentido no modo Catálogo (SPEC §5.2: "código de registrante
    // ISRC (uso catálogo)") — no modo Acervo/Preservação o campo nem aparece,
    // em vez de ficar vazio e sem sentido no formulário.
    std::unique_ptr<LinhaFormulario> linhaIsrc;
    juce::TextEditor* isrcEditorPtr = nullptr;
    if (modo == matriz::model::Modo::Catalogo) {
        auto isrcEditor = std::make_unique<juce::TextEditor>();
        isrcEditorPtr = isrcEditor.get();
        linhaIsrc = std::make_unique<LinhaFormulario>(matriz::i18n::t("dialogo_novo_projeto.campo_isrc"), std::move(isrcEditor));
    }

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

    // "É pra lá que seus arquivos serão copiados — os originais não são
    // movidos nem apagados": a dúvida que trava quem tem um HD cheio é
    // exatamente essa, e ela precisa ser respondida ANTES de escolher a
    // pasta, não num FAQ.
    auto pastaAjuda = std::make_shared<juce::Label>();
    pastaAjuda->setText(matriz::i18n::t("dialogo_novo_projeto.campo_pasta_ajuda"), juce::dontSendNotification);
    pastaAjuda->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
    pastaAjuda->setColour(juce::Label::textColourId, tema().textoSecundario);
    linhaPasta->addAndMakeVisible(*pastaAjuda);
    pastaAjuda->setBounds(0, 52, 420, 30);
    linhaPasta->setSize(420, 84);

    for (auto* linha : {linhaNome.get(), linhaPasta.get(), linhaInstituicao.get(), linhaResponsavel.get(),
                         linhaIsrc.get()})
        if (linha) janela->addCustomComponent(linha);

    janela->addButton(matriz::i18n::t("dialogo_novo_projeto.botao_criar"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    janela->addButton(matriz::i18n::t("dialogo_novo_projeto.botao_cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));

    // As LinhaFormulario precisam sobreviver até o callback rodar — o
    // AlertWindow não assume posse dos componentes que recebe.
    struct EstadoVivo {
        std::shared_ptr<juce::AlertWindow> janela;
        std::unique_ptr<LinhaFormulario> nome, instituicao, responsavel, isrc, pasta;
        std::shared_ptr<juce::Label> pastaAjuda;
    };
    auto estado = std::make_shared<EstadoVivo>();
    estado->janela = janela;
    estado->nome = std::move(linhaNome);
    estado->pastaAjuda = pastaAjuda;
    estado->instituicao = std::move(linhaInstituicao);
    estado->responsavel = std::move(linhaResponsavel);
    estado->isrc = std::move(linhaIsrc);
    estado->pasta = std::move(linhaPasta);

    janela->enterModalState(
        true,
        juce::ModalCallbackFunction::create([estado, aoConcluir, modo, nomeEditorPtr, instituicaoEditorPtr,
                                              responsavelEditorPtr, isrcEditorPtr, pastaEscolhida,
                                              seletorPasta](int resultadoBotao) {
            if (resultadoBotao != 1) {
                aoConcluir(std::nullopt);
                return;
            }

            juce::String nome = nomeEditorPtr->getText().trim();
            juce::String prefixo = prefixoAPartirDoNome(nome);
            if (nome.isEmpty() || *pastaEscolhida == juce::File()) {
                juce::String erro = nome.isEmpty() ? matriz::i18n::t("dialogo_novo_projeto.erro_nome_vazio")
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
            r.params.modo = modo;
            r.params.prefixoNomenclatura = prefixo.toStdString();
            r.params.instituicaoOuSelo = instituicaoEditorPtr->getText().trim().toStdString();
            r.params.responsavel = responsavelEditorPtr->getText().trim().toStdString();
            r.params.isrcRegistrante = isrcEditorPtr ? isrcEditorPtr->getText().trim().toStdString() : std::string();

            aoConcluir(r);
        }));

    return janela;
}

} // namespace matriz::ui
