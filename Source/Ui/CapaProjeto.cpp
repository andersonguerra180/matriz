#include "CapaProjeto.h"

#include "../I18n/Strings.h"
#include "ContainerVisual.h"
#include "EventBus.h"
#include "ImportarMenu.h"
#include "Moldura.h"
#include "ProjetoAberto.h"
#include "Tokens.h"

namespace matriz::ui {

using matriz::i18n::t;

CapaProjetoComponent::CapaProjetoComponent(ProjetoAberto& projeto, const std::string& nestId, Modo modo)
    : projeto_(projeto), nestId_(nestId), modo_(modo) {
    btnTrocar_.setButtonText(t("projeto.capa.trocar"));
    btnTrocar_.onClick = [this] { escolherNovaCapa(); };
    btnRemover_.setButtonText(t("projeto.capa.remover"));
    btnRemover_.onClick = [this] { removerCapa(); };
    if (modo_ == Modo::Detalhado) {
        addAndMakeVisible(btnTrocar_);
        addAndMakeVisible(btnRemover_);
    }
    recarregar();
}

CapaProjetoComponent::~CapaProjetoComponent() = default;

void CapaProjetoComponent::definirContainer(const std::string& nestId) {
    nestId_ = nestId;
    recarregar();
}

void CapaProjetoComponent::recarregar() {
    nome_ = {};
    artista_ = {};
    caminhoCapa_ = {};
    origem_.clear();
    const auto membros = projeto_.membrosDoNest(nestId_);
    if (!membros.empty()) {
        if (const auto info = projeto_.nestDoItem(membros.front())) {
            origem_ = info->origem;
            nome_ = !info->nome.empty() ? juce::String::fromUTF8(info->nome.c_str()) : t("intake.container_" + juce::String(origem_));
            if (!info->capaImagem.empty()) {
                const auto arquivo = projeto_.projeto().pasta().getChildFile(juce::String::fromUTF8(info->capaImagem.c_str()));
                if (arquivo.existsAsFile()) caminhoCapa_ = arquivo.getFullPathName();
            }
            // Artista/banda: a resposta do questionário (campo "artist"); sem ela, o SUBJECT do arquivo-capa.
            std::string folha;
            std::map<std::string, std::string> respostas;
            if (questionarioDeJson(projeto_.questionarioDoContainer(nestId_), folha, respostas) && respostas.count("artist"))
                artista_ = juce::String::fromUTF8(respostas["artist"].c_str());
            if (artista_.isEmpty()) artista_ = juce::String::fromUTF8(projeto_.lerMetadado(info->capaId, "dc_subject").value_or("").c_str());
        }
    }
    btnRemover_.setEnabled(caminhoCapa_.isNotEmpty());
    repaint();
}

bool CapaProjetoComponent::isInterestedInFileDrag(const juce::StringArray& arquivos) {
    if (modo_ != Modo::Detalhado || arquivos.size() != 1) return false;
    return juce::File(arquivos[0]).hasFileExtension("jpg;jpeg;png;tif;tiff;bmp;gif;webp");
}

void CapaProjetoComponent::filesDropped(const juce::StringArray& arquivos, int, int) {
    soltando_ = false;
    if (arquivos.size() == 1) trocarCapa(juce::File(arquivos[0]));
    repaint();
}

void CapaProjetoComponent::escolherNovaCapa() {
    seletor_ = std::make_shared<juce::FileChooser>(t("imp.ficha.capa.titulo"), juce::File(), "*.jpg;*.jpeg;*.png;*.tif;*.tiff;*.bmp;*.gif;*.webp");
    juce::Component::SafePointer<CapaProjetoComponent> seguro(this);
    seletor_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [seguro](const juce::FileChooser& fc) {
        const auto escolhido = fc.getResult();
        if (seguro != nullptr && escolhido.existsAsFile()) seguro->trocarCapa(escolhido);
    });
}

// A imagem é reduzida e gravada fora da message thread; o resultado volta por callAsync.
void CapaProjetoComponent::trocarCapa(const juce::File& imagem) {
    if (projeto_.somenteLeitura() || nestId_.empty() || !imagem.existsAsFile()) return;
    juce::Component::SafePointer<CapaProjetoComponent> seguro(this);
    ProjetoAberto* proj = &projeto_;
    const std::string nest = nestId_;
    juce::Thread::launch([seguro, proj, nest, imagem] {
        const bool ok = proj->definirCapaDoContainer(nest, imagem);
        juce::MessageManager::callAsync([seguro, proj, nest, ok, imagem] {
            if (!ok) return;
            proj->definirRespostaDoQuestionario(nest, "capa", imagem.getFullPathName().toStdString());  // Edit Project Info abre com a capa nova
            if (seguro != nullptr) seguro->aposMudar();
        });
    });
}

void CapaProjetoComponent::removerCapa() {
    if (projeto_.somenteLeitura() || nestId_.empty() || caminhoCapa_.isEmpty()) return;
    const auto antigo = caminhoCapa_;
    if (projeto_.removerCapaDoContainer(nestId_)) {
        esquecerImagemDaCapa(antigo);
        aposMudar();
    }
}

void CapaProjetoComponent::aposMudar() {
    esquecerImagemDaCapa(caminhoCapa_);
    recarregar();
    EventBus::obterInstancia().dispararItemAlterado({}, "recarregar_tudo");  // INTAKE, METADATA e Folder Map redesenham a capa
    if (aoMudar) aoMudar();
}

void CapaProjetoComponent::resized() {
    auto area = getLocalBounds();
    if (modo_ == Modo::Compacto) {
        const int lado = juce::jmin(area.getWidth(), area.getHeight());
        areaCapa_ = area.withSizeKeepingCentre(lado, lado);
        areaTexto_ = areaSoltar_ = {};
        return;
    }
    const int lado = juce::jmin(area.getHeight(), juce::jmax(120, area.getWidth() / 2 - 16));
    areaCapa_ = area.removeFromLeft(lado).withHeight(lado);
    area.removeFromLeft(24);
    areaTexto_ = area;
    auto texto = area;
    texto.removeFromTop(112);
    auto botoes = texto.removeFromTop(30);
    btnTrocar_.setBounds(botoes.removeFromLeft(juce::jmin(150, botoes.getWidth() / 2)));
    botoes.removeFromLeft(10);
    btnRemover_.setBounds(botoes.removeFromLeft(juce::jmin(110, botoes.getWidth())));
    texto.removeFromTop(14);
    areaSoltar_ = texto;
}

void CapaProjetoComponent::paint(juce::Graphics& g) {
    const auto pal = moldura::paleta();
    // Capa quadrada: a imagem, ou o espaço reservado "CAPA 1:1".
    g.setColour(pal.grafite);
    g.fillRoundedRectangle(areaCapa_.toFloat(), 8.0f);
    const auto& imagem = imagemDaCapaDoContainer(caminhoCapa_);
    if (imagem.isValid()) {
        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle(areaCapa_.toFloat(), 8.0f);
        g.reduceClipRegion(clip);
        g.drawImage(imagem, areaCapa_.toFloat(), juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
        g.restoreState();
    } else {
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        const auto c = areaCapa_.getCentre().toFloat();
        g.drawEllipse(c.x - 17.0f, c.y - 32.0f, 34.0f, 34.0f, 2.0f);
        g.fillEllipse(c.x - 3.5f, c.y - 18.5f, 7.0f, 7.0f);
        g.setFont(fonteUi(12.0f, juce::Font::bold).withExtraKerningFactor(0.12f));
        g.drawText(t("projeto.capa.placeholder"), areaCapa_.withTrimmedTop(areaCapa_.getHeight() / 2 + 10).withHeight(18), juce::Justification::centred, false);
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.setFont(fonteUi(10.0f));
        g.drawText(modo_ == Modo::Compacto ? t("projeto.cd.placeholder2") : t("projeto.capa.placeholder2"), areaCapa_.withTrimmedTop(areaCapa_.getHeight() / 2 + 28).withHeight(16), juce::Justification::centred, false);
    }
    if (modo_ == Modo::Compacto) return;

    // Bloco de texto: rótulo, nome do projeto, artista, origem da capa.
    auto texto = areaTexto_;
    auto linhaRotulo = texto.removeFromTop(20);
    g.setColour(pal.mudo);
    g.setFont(fonteUi(10.0f, juce::Font::bold).withExtraKerningFactor(0.12f));
    g.drawText(t("projeto.capa.rotulo"), linhaRotulo, juce::Justification::centredLeft, false);
    g.drawText("[1:1]", linhaRotulo, juce::Justification::centredRight, false);
    g.setColour(pal.tinta);
    g.setFont(juce::Font(juce::FontOptions("Jost", 22.0f, juce::Font::bold)));
    g.drawText(nome_, texto.removeFromTop(34), juce::Justification::centredLeft, true);
    g.setColour(pal.mudo);
    g.setFont(fonteUi(13.0f));
    g.drawText(artista_.isNotEmpty() ? artista_ : juce::String("-"), texto.removeFromTop(24), juce::Justification::centredLeft, true);
    g.setFont(fonteUi(11.0f));
    g.drawFittedText(t("projeto.capa.origem"), texto.removeFromTop(34), juce::Justification::topLeft, 2, 1.0f);

    // Área tracejada para soltar uma imagem.
    if (!areaSoltar_.isEmpty()) {
        const auto r = areaSoltar_.toFloat().reduced(0.5f);
        juce::Path caixa;
        caixa.addRoundedRectangle(r, 8.0f);
        juce::Path tracejado;
        const float tracos[] = { 5.0f, 4.0f };
        juce::PathStrokeType(1.2f).createDashedStroke(tracejado, caixa, tracos, 2);
        g.setColour(soltando_ ? pal.grafite : pal.linha);
        g.fillPath(tracejado);
        if (soltando_) { g.setColour(pal.grafite.withAlpha(0.08f)); g.fillRoundedRectangle(r, 8.0f); }
        g.setColour(pal.mudo);
        g.setFont(fonteUi(11.0f));
        g.drawFittedText(t("projeto.capa.soltar"), areaSoltar_.reduced(12), juce::Justification::centred, 3, 1.0f);
    }
}

}  // namespace matriz::ui
