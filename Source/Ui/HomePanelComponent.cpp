#include "HomePanelComponent.h"
#include "ProjetoAberto.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

class HomePanelComponent::CartaoTarefa : public juce::Component {
public:
    CartaoTarefa(const juce::String& titulo, const juce::String& descricao,
                 juce::Colour corBarra)
        : titulo_(titulo), descricao_(descricao), corBarra_(corBarra)
    {
        setInterceptsMouseClicks(true, false);
    }

    std::function<void()> aoClicar;

    void paint(juce::Graphics& g) override {
        auto area = getLocalBounds().toFloat();
        g.setColour(emHover_ ? tema().painelAlt : tema().painel);
        g.fillRoundedRectangle(area, tema().raioMedio);
        g.setColour(tema().borda);
        g.drawRoundedRectangle(area.reduced(0.5f), tema().raioMedio, 1.0f);

        g.setColour(corBarra_);
        g.fillRoundedRectangle(area.getX(), area.getY(), area.getWidth(), 4.0f,
                               tema().raioMedio);

        auto conteudo = getLocalBounds().reduced(tema().espacoPainel).withTrimmedTop(8);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteSubtitulo, juce::Font::bold)));
        g.setColour(tema().textoPrimario);
        g.drawText(titulo_, conteudo.removeFromTop(24), juce::Justification::centredLeft, true);

        conteudo.removeFromTop(tema().espacoPequeno);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
        g.setColour(tema().textoSecundario);
        g.drawFittedText(descricao_, conteudo, juce::Justification::topLeft, 4);
    }

    void mouseEnter(const juce::MouseEvent&) override { emHover_ = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { emHover_ = false; repaint(); }
    void mouseUp(const juce::MouseEvent& e) override {
        if (e.mouseWasClicked() && getLocalBounds().contains(e.getPosition()))
            if (aoClicar) aoClicar();
    }

private:
    juce::String titulo_, descricao_;
    juce::Colour corBarra_;
    bool emHover_ = false;
};

class HomePanelComponent::LinhaAtencao : public juce::Component {
public:
    LinhaAtencao(const juce::String& texto, int contagem, const std::string& chave)
        : texto_(texto), contagem_(contagem), chave_(chave)
    {
        setInterceptsMouseClicks(true, false);
    }

    std::function<void(const std::string&)> aoClicar;

    void paint(juce::Graphics& g) override {
        auto area = getLocalBounds().toFloat();
        if (emHover_) {
            g.setColour(tema().painelAlt.withAlpha(0.5f));
            g.fillRoundedRectangle(area, tema().raioPequeno);
        }

        auto conteudo = getLocalBounds().reduced(tema().espacoMedio, 2);

        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo, juce::Font::bold)));
        g.setColour(tema().alerta);
        g.drawText(juce::String(contagem_), conteudo.removeFromLeft(40),
                   juce::Justification::centredRight, true);

        conteudo.removeFromLeft(tema().espacoMedio);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
        g.setColour(tema().textoPrimario);
        g.drawText(texto_, conteudo, juce::Justification::centredLeft, true);
    }

    void mouseEnter(const juce::MouseEvent&) override { emHover_ = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { emHover_ = false; repaint(); }
    void mouseUp(const juce::MouseEvent& e) override {
        if (e.mouseWasClicked() && getLocalBounds().contains(e.getPosition()))
            if (aoClicar) aoClicar(chave_);
    }

    int contagem() const { return contagem_; }

private:
    juce::String texto_;
    int contagem_;
    std::string chave_;
    bool emHover_ = false;
};

HomePanelComponent::HomePanelComponent(ProjetoAberto& projeto)
    : projeto_(projeto)
{
    titulo_ = std::make_unique<juce::Label>();
    titulo_->setText(matriz::i18n::t("home.titulo"), juce::dontSendNotification);
    titulo_->setJustificationType(juce::Justification::centred);
    titulo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteTitulo, juce::Font::bold)));
    titulo_->setColour(juce::Label::textColourId, tema().textoPrimario);
    addAndMakeVisible(*titulo_);

    subtitulo_ = std::make_unique<juce::Label>();
    subtitulo_->setText(matriz::i18n::t("home.subtitulo"), juce::dontSendNotification);
    subtitulo_->setJustificationType(juce::Justification::centred);
    subtitulo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
    subtitulo_->setColour(juce::Label::textColourId, tema().textoSecundario);
    addAndMakeVisible(*subtitulo_);

    totalAssets_ = std::make_unique<juce::Label>();
    totalAssets_->setJustificationType(juce::Justification::centred);
    totalAssets_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
    totalAssets_->setColour(juce::Label::textColourId, tema().textoTerciario);
    addAndMakeVisible(*totalAssets_);

    cartaoIngest_ = std::make_unique<CartaoTarefa>(
        matriz::i18n::t("home.ingest_titulo"),
        matriz::i18n::t("home.ingest_desc"),
        tema().acento);
    cartaoIngest_->aoClicar = [this] { if (aoIngerir) aoIngerir(); };
    addAndMakeVisible(*cartaoIngest_);

    cartaoCatalog_ = std::make_unique<CartaoTarefa>(
        matriz::i18n::t("home.catalog_titulo"),
        matriz::i18n::t("home.catalog_desc"),
        tema().acento);
    cartaoCatalog_->aoClicar = [this] { if (aoCatalogar) aoCatalogar(); };
    addAndMakeVisible(*cartaoCatalog_);

    cartaoBackup_ = std::make_unique<CartaoTarefa>(
        matriz::i18n::t("home.backup_titulo"),
        matriz::i18n::t("home.backup_desc"),
        tema().haloSincronizado);
    cartaoBackup_->aoClicar = [this] { if (aoBackup) aoBackup(); };
    addAndMakeVisible(*cartaoBackup_);

    cartaoPreservacao_ = std::make_unique<CartaoTarefa>(
        matriz::i18n::t("home.preservacao_titulo"),
        matriz::i18n::t("home.preservacao_desc"),
        tema().alerta);
    cartaoPreservacao_->aoClicar = [this] { if (aoPreservacao) aoPreservacao(); };
    addAndMakeVisible(*cartaoPreservacao_);

    atencaoTitulo_ = std::make_unique<juce::Label>();
    atencaoTitulo_->setText(matriz::i18n::t("home.atencao_titulo"), juce::dontSendNotification);
    atencaoTitulo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
    atencaoTitulo_->setColour(juce::Label::textColourId, tema().textoTerciario);
    addAndMakeVisible(*atencaoTitulo_);

    recarregar();
}

HomePanelComponent::~HomePanelComponent() = default;

void HomePanelComponent::recarregar() {
    int total = 0;
    try {
        auto stmt = projeto_.projeto().registro().prepare("SELECT COUNT(*) FROM item WHERE em_quarentena = 0");
        if (stmt.step()) total = stmt.columnInt(0);
    } catch (...) {}

    if (total > 0) {
        auto texto = matriz::i18n::t("home.total_assets");
        totalAssets_->setText(texto.replace("{0}", juce::String(total)), juce::dontSendNotification);
    } else {
        totalAssets_->setText(matriz::i18n::t("home.total_assets_vazio"), juce::dontSendNotification);
    }

    linhasAtencao_.clear();

    // 1. Items need review
    int countRevisao = 0;
    try {
        auto stmt = projeto_.projeto().registro().prepare("SELECT COUNT(DISTINCT item_id) FROM colecao_revisao");
        if (stmt.step()) countRevisao = stmt.columnInt(0);
    } catch (...) {}
    if (countRevisao > 0) {
        auto label = juce::String(countRevisao) + (countRevisao == 1 ? " item needs review" : " items need review");
        auto linha = std::make_unique<LinhaAtencao>(label, countRevisao, "revisao");
        linha->aoClicar = [this](const std::string& chave) { if (aoClicarAtencao) aoClicarAtencao(chave); };
        addAndMakeVisible(*linha);
        linhasAtencao_.push_back(std::move(linha));
    }

    // 2. Assets have only one copy (vulneraveis)
    int countVulneraveis = 0;
    try {
        auto stmt = projeto_.projeto().registro().prepare("SELECT COUNT(DISTINCT item_id) FROM colecao_vulneraveis");
        if (stmt.step()) countVulneraveis = stmt.columnInt(0);
    } catch (...) {}
    if (countVulneraveis > 0) {
        auto label = juce::String(countVulneraveis) + (countVulneraveis == 1 ? " asset has only one copy" : " assets have only one copy");
        auto linha = std::make_unique<LinhaAtencao>(label, countVulneraveis, "vulneraveis");
        linha->aoClicar = [this](const std::string& chave) { if (aoClicarAtencao) aoClicarAtencao(chave); };
        addAndMakeVisible(*linha);
        linhasAtencao_.push_back(std::move(linha));
    }

    // 3. Checksum problems (corrupted files)
    int countCorrompido = 0;
    try {
        auto stmt = projeto_.projeto().registro().prepare("SELECT COUNT(DISTINCT item_id) FROM arquivo WHERE estado_presenca = 'corrompido'");
        if (stmt.step()) countCorrompido = stmt.columnInt(0);
    } catch (...) {}
    if (countCorrompido > 0) {
        auto label = juce::String(countCorrompido) + (countCorrompido == 1 ? " checksum problem" : " checksum problems");
        auto linha = std::make_unique<LinhaAtencao>(label, countCorrompido, "corrompido");
        linha->aoClicar = [this](const std::string& chave) { if (aoClicarAtencao) aoClicarAtencao(chave); };
        addAndMakeVisible(*linha);
        linhasAtencao_.push_back(std::move(linha));
    }

    atencaoTitulo_->setVisible(!linhasAtencao_.empty());
    resized();
    repaint();
}

void HomePanelComponent::paint(juce::Graphics& g) {
    g.fillAll(tema().fundo);
}

void HomePanelComponent::resized() {
    constexpr int kLarguraCartao = 280;
    constexpr int kAlturaCartao = 120;
    constexpr int kGapCartao = 16;
    constexpr int kLarguraGrade = kLarguraCartao * 2 + kGapCartao;

    int alturaAtencao = 0;
    if (!linhasAtencao_.empty())
        alturaAtencao = 24 + static_cast<int>(linhasAtencao_.size()) * 30 + 16;

    int alturaTotal = 30 + 4 + 24 + 4 + 20 + 24
                      + (kAlturaCartao + kGapCartao) * 2
                      + alturaAtencao;

    auto coluna = getLocalBounds().withSizeKeepingCentre(kLarguraGrade, alturaTotal);

    titulo_->setBounds(coluna.removeFromTop(30));
    coluna.removeFromTop(4);
    subtitulo_->setBounds(coluna.removeFromTop(24));
    coluna.removeFromTop(4);
    totalAssets_->setBounds(coluna.removeFromTop(20));
    coluna.removeFromTop(24);

    auto linha1 = coluna.removeFromTop(kAlturaCartao);
    cartaoIngest_->setBounds(linha1.removeFromLeft(kLarguraCartao));
    linha1.removeFromLeft(kGapCartao);
    cartaoCatalog_->setBounds(linha1.removeFromLeft(kLarguraCartao));

    coluna.removeFromTop(kGapCartao);

    auto linha2 = coluna.removeFromTop(kAlturaCartao);
    cartaoBackup_->setBounds(linha2.removeFromLeft(kLarguraCartao));
    linha2.removeFromLeft(kGapCartao);
    cartaoPreservacao_->setBounds(linha2.removeFromLeft(kLarguraCartao));

    if (!linhasAtencao_.empty()) {
        coluna.removeFromTop(kGapCartao);
        atencaoTitulo_->setBounds(coluna.removeFromTop(24));
        for (auto& l : linhasAtencao_)
            l->setBounds(coluna.removeFromTop(30));
    }
}

} // namespace matriz::ui
