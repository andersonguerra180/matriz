#include "JanelaPlayerDdp.h"

#include <cstdio>

#include "../I18n/Strings.h"
#include "Moldura.h"

namespace matriz::ui {

using matriz::i18n::t;

namespace {
constexpr int kLargura = 860, kAltura = 640;
const char* kNomesDdp[] = { "DDPID", "DDPMS" };

juce::String tempoCd(int64_t quadros) { return bkr::DdpParser::formatCdTime(quadros); }
}  // namespace

juce::File JanelaPlayerDdp::acharPastaDdp(const std::vector<juce::File>& arquivos) {
    for (const auto& f : arquivos) {
        auto dir = f.isDirectory() ? f : f.getParentDirectory();
        for (int nivel = 0; nivel < 3 && dir != juce::File(); ++nivel, dir = dir.getParentDirectory()) {
            for (const char* nome : kNomesDdp)
                for (const auto& c : dir.findChildFiles(juce::File::findFiles, false))
                    if (c.getFileName().equalsIgnoreCase(nome)) return dir;
        }
    }
    return {};
}

bool JanelaPlayerDdp::autoTeste(const juce::File& pastaDdp, const juce::File& png) {
    JanelaPlayerDdp janela(pastaDdp);
    if (!janela.carregado_) { std::printf("DDP: FALHOU: %s\n", janela.erro_.toRawUTF8()); return false; }
    const auto& d = janela.disco_;
    std::printf("DDP: album='%s' artista='%s' upc='%s' faixas=%d duracao=%s big-endian=%d\n", d.albumTitle.toRawUTF8(), d.discArtist.toRawUTF8(),
                d.upcEan.toRawUTF8(), static_cast<int>(d.tracks.size()), tempoCd(d.totalFrames).toRawUTF8(), d.isBigEndian ? 1 : 0);
    for (const auto& f : d.tracks)
        std::printf("  %02d | %s | %s | %s | inicio %s | dur %s\n", f.trackNumber, f.title.toRawUTF8(), f.performer.toRawUTF8(), f.isrc.toRawUTF8(),
                    tempoCd(f.index1Frame).toRawUTF8(), tempoCd(f.trackFrames()).toRawUTF8());
    janela.setSize(moldura::tamanhoComMargem(kLargura), moldura::tamanhoComMargem(kAltura));
    janela.resized();
    const auto img = janela.createComponentSnapshot(janela.getLocalBounds(), true, 1.0f);
    png.getParentDirectory().createDirectory();
    png.deleteFile();
    juce::FileOutputStream out(png);
    if (out.openedOk()) juce::PNGImageFormat().writeImageToStream(img, out);
    return true;
}

bool JanelaPlayerDdp::abrir(const juce::File& pastaDdp, juce::String& erro) {
    auto* janela = new JanelaPlayerDdp(pastaDdp);
    if (!janela->carregado_) {
        erro = janela->erro_;
        delete janela;
        return false;
    }
    moldura::abrirJanela(janela, t("ddp.title"), true);
    return true;
}

JanelaPlayerDdp::JanelaPlayerDdp(const juce::File& pastaDdp) : pasta_(pastaDdp) {
    setSize(moldura::tamanhoComMargem(kLargura), moldura::tamanhoComMargem(kAltura));
    carregado_ = bkr::DdpParser::parseFolder(pasta_, disco_, erro_) && disco_.isValid;
    if (!carregado_) return;

    player_ = std::make_unique<bkr::DdpPlayback>();
    if (!player_->loadDisc(disco_)) { carregado_ = false; erro_ = t("ddp.err.noAudio"); return; }

    tabela_.setModel(this);
    tabela_.setRowHeight(26);
    tabela_.setHeaderHeight(26);
    tabela_.setOutlineThickness(0);
    auto& cab = tabela_.getHeader();
    cab.addColumn("#", 1, 36, 36, 36, juce::TableHeaderComponent::notSortable);
    cab.addColumn(t("ddp.col.title"), 2, 230, 120, 600, juce::TableHeaderComponent::notSortable);
    cab.addColumn(t("ddp.col.performer"), 3, 140, 80, 400, juce::TableHeaderComponent::notSortable);
    cab.addColumn("ISRC", 4, 112, 80, 200, juce::TableHeaderComponent::notSortable);
    cab.addColumn(t("ddp.col.pregap"), 5, 72, 60, 120, juce::TableHeaderComponent::notSortable);
    cab.addColumn(t("ddp.col.start"), 6, 82, 70, 140, juce::TableHeaderComponent::notSortable);
    cab.addColumn(t("ddp.col.length"), 7, 82, 70, 140, juce::TableHeaderComponent::notSortable);
    addAndMakeVisible(tabela_);

    const auto pal = moldura::paleta();
    tabela_.setColour(juce::ListBox::backgroundColourId, pal.campo);
    tabela_.setColour(juce::ListBox::outlineColourId, pal.linha);

    juce::TextButton* botoes[] = { &btnAnterior_, &btnPlay_, &btnParar_, &btnProximo_ };
    const juce::String rotulos[] = { juce::String::fromUTF8("\xe2\x8f\xae"), juce::String::fromUTF8("\xe2\x96\xb6"),
                                     juce::String::fromUTF8("\xe2\x96\xa0"), juce::String::fromUTF8("\xe2\x8f\xad") };
    for (int i = 0; i < 4; ++i) {
        botoes[i]->setButtonText(rotulos[i]);
        moldura::estilizarBotao(*botoes[i], i == 1);
        addAndMakeVisible(*botoes[i]);
    }
    btnAnterior_.onClick = [this] { player_->previousTrack(); };
    btnProximo_.onClick = [this] { player_->nextTrack(); };
    btnParar_.onClick = [this] { player_->stop(); atualizarBotoes(); };
    btnPlay_.onClick = [this] {
        if (player_->isPlaying()) player_->pause();
        else player_->play();
        atualizarBotoes();
    };

    btnInverter_.setButtonText(t("ddp.invert"));
    btnInverter_.setClickingTogglesState(true);
    btnInverter_.setToggleState(false, juce::dontSendNotification);
    moldura::estilizarBotao(btnInverter_, false);
    btnInverter_.onClick = [this] { player_->setBigEndian(btnInverter_.getToggleState() ? !disco_.isBigEndian : disco_.isBigEndian); };
    addAndMakeVisible(btnInverter_);

    posicao_.setSliderStyle(juce::Slider::LinearHorizontal);
    posicao_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    posicao_.setRange(0.0, juce::jmax(1.0, static_cast<double>(disco_.totalSamples)), 1.0);
    posicao_.setColour(juce::Slider::trackColourId, pal.grafite);
    posicao_.setColour(juce::Slider::thumbColourId, pal.grafite);
    posicao_.setColour(juce::Slider::backgroundColourId, pal.linha);
    posicao_.onDragStart = [this] { arrastando_ = true; };
    posicao_.onDragEnd = [this] { arrastando_ = false; player_->setPositionSample(static_cast<int64_t>(posicao_.getValue())); };
    posicao_.onValueChange = [this] { if (arrastando_) player_->setPositionSample(static_cast<int64_t>(posicao_.getValue())); };
    addAndMakeVisible(posicao_);

    // MD5 dos arquivos do DDP em segundo plano (pode demorar em imagens grandes).
    juce::Component::SafePointer<JanelaPlayerDdp> seguro(this);
    bkr::DdpDisc copia = disco_;
    juce::Thread::launch([seguro, copia]() mutable {
        bkr::DdpParser::verifyChecksums(copia);
        juce::MessageManager::callAsync([seguro, copia] {
            if (seguro == nullptr) return;
            seguro->disco_.checksums = copia.checksums;
            seguro->checksumsProntos_ = true;
            seguro->repaint();
        });
    });

    atualizarBotoes();
    startTimerHz(20);
}

JanelaPlayerDdp::~JanelaPlayerDdp() {
    stopTimer();
    tabela_.setModel(nullptr);
    if (player_) player_->pause();
}

void JanelaPlayerDdp::atualizarBotoes() {
    if (!player_) return;
    btnPlay_.setButtonText(juce::String::fromUTF8(player_->isPlaying() ? "\xe2\x8f\xb8" : "\xe2\x96\xb6"));
}

void JanelaPlayerDdp::timerCallback() {
    if (!player_) return;
    const auto pos = player_->getPositionSample();
    if (!arrastando_) posicao_.setValue(static_cast<double>(pos), juce::dontSendNotification);
    const int faixa = player_->getCurrentTrackIndex();
    if (faixa != faixaAtual_) {
        faixaAtual_ = faixa;
        tabela_.selectRow(faixa, true, true);
        tabela_.scrollToEnsureRowIsOnscreen(faixa);
    }
    atualizarBotoes();
    repaint(areaDisplay_);
}

void JanelaPlayerDdp::fechar() {
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0);
}

void JanelaPlayerDdp::mouseDown(const juce::MouseEvent& e) {
    if (moldura::geometria(moldura::areaDoCartaoNativo(getLocalBounds()), false).botaoFechar.contains(e.getPosition())) fechar();
}

void JanelaPlayerDdp::paintRowBackground(juce::Graphics& g, int row, int, int, bool sel) {
    const auto pal = moldura::paleta();
    if (sel) g.fillAll(pal.grafite.withAlpha(0.14f));
    else if (row % 2) g.fillAll(pal.poco.withAlpha(0.5f));
}

void JanelaPlayerDdp::paintCell(juce::Graphics& g, int row, int col, int w, int h, bool) {
    if (row < 0 || row >= static_cast<int>(disco_.tracks.size())) return;
    const auto& f = disco_.tracks[static_cast<size_t>(row)];
    juce::String txt;
    switch (col) {
        case 1: txt = juce::String(f.trackNumber).paddedLeft('0', 2); break;
        case 2: txt = f.title.isNotEmpty() ? f.title : t("ddp.untitled"); break;
        case 3: txt = f.performer; break;
        case 4: txt = f.isrc; break;
        case 5: txt = f.pregapFrames() > 0 ? tempoCd(f.pregapFrames()) : juce::String("-"); break;
        case 6: txt = tempoCd(f.index1Frame); break;
        case 7: txt = tempoCd(f.trackFrames()); break;
        default: break;
    }
    const auto pal = moldura::paleta();
    g.setColour(row == faixaAtual_ && player_ && player_->isPlaying() ? pal.grafite : pal.tinta);
    g.setFont(fonteUi(13.0f, (row == faixaAtual_ && player_ && player_->isPlaying()) ? juce::Font::bold : juce::Font::plain));
    g.drawText(txt, 6, 0, w - 10, h, juce::Justification::centredLeft, true);
}

void JanelaPlayerDdp::cellClicked(int row, int, const juce::MouseEvent& e) {
    if (e.getNumberOfClicks() >= 2 && player_) player_->playTrack(row);
}

void JanelaPlayerDdp::paint(juce::Graphics& g) {
    const auto pal = moldura::paleta();
    g.fillAll(pal.fundo);
    const auto geo = moldura::geometria(moldura::areaDoCartaoNativo(getLocalBounds()), false);
    moldura::desenhar(g, geo, moldura::Secao::Auto, t("ddp.trail"), t("ddp.title"), false);
    if (!carregado_) return;

    // Dados do disco (poço à esquerda) + visor tipo CD à direita
    g.setColour(pal.poco);
    g.fillRoundedRectangle(areaInfo_.toFloat(), 12.0f);
    auto a = areaInfo_.reduced(16, 12);
    auto linha = [&](const juce::String& rot, const juce::String& val, bool destaque = false) {
        auto l = a.removeFromTop(destaque ? 30 : 20);
        g.setColour(pal.mudo);
        g.setFont(fonteUi(10.0f, juce::Font::bold).withExtraKerningFactor(0.12f));
        g.drawText(moldura::caixaAlta(rot), l.removeFromLeft(96), juce::Justification::centredLeft, true);
        g.setColour(pal.tinta);
        g.setFont(fonteUi(destaque ? 18.0f : 13.0f, destaque ? juce::Font::bold : juce::Font::plain));
        g.drawText(val, l, juce::Justification::centredLeft, true);
    };
    linha(t("ddp.album"), disco_.albumTitle, true);
    linha(t("ddp.artist"), disco_.discArtist.isNotEmpty() ? disco_.discArtist : "-");
    linha(t("ddp.upc"), disco_.upcEan.isNotEmpty() ? disco_.upcEan : t("ddp.none"));
    linha(t("ddp.format"), disco_.formatVersion + juce::String::fromUTF8(" \xc2\xb7 ") + (player_->isBigEndian() ? "big-endian" : "little-endian") + juce::String::fromUTF8(" \xc2\xb7 ") + "44.1 kHz / 16 bit");
    linha(t("ddp.tracks"), juce::String(static_cast<int>(disco_.tracks.size())) + juce::String::fromUTF8("   \xc2\xb7   ") + t("ddp.duration") + " " + tempoCd(disco_.totalFrames));
    int ok = 0, falhas = 0;
    for (const auto& c : disco_.checksums) { if (c.matched) ++ok; else if (c.fileExists || c.expectedMd5.isNotEmpty()) ++falhas; }
    linha(t("ddp.checksums"), !checksumsProntos_ ? t("ddp.checking")
                              : disco_.checksums.empty() ? t("ddp.none")
                              : (falhas == 0 ? "OK (" + juce::String(ok) + ")" : t("ddp.fail") + " (" + juce::String(falhas) + ")"));
    if (!disco_.warnings.empty()) linha(t("ddp.warnings"), disco_.warnings.front());

    // visor
    g.setColour(pal.grafite);
    g.fillRoundedRectangle(areaDisplay_.toFloat(), 12.0f);
    const int64_t pos = player_->getPositionSample();
    const int faixa = player_->getCurrentTrackIndex();
    const auto& f = disco_.tracks[static_cast<size_t>(faixa)];
    auto d = areaDisplay_.reduced(14, 10);
    g.setColour(pal.branco.withAlpha(0.7f));
    g.setFont(fonteUi(10.0f, juce::Font::bold).withExtraKerningFactor(0.15f));
    g.drawText(("TRACK " + juce::String(f.trackNumber).paddedLeft('0', 2) + " / " + juce::String(static_cast<int>(disco_.tracks.size())).paddedLeft('0', 2)),
               d.removeFromTop(16), juce::Justification::centredLeft, false);
    g.setColour(pal.branco);
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 34.0f, juce::Font::bold)));
    g.drawText(tempoCd(pos / 588), d.removeFromTop(46), juce::Justification::centredLeft, false);
    g.setColour(pal.branco.withAlpha(0.85f));
    g.setFont(fonteUi(12.0f));
    g.drawText(f.title.isNotEmpty() ? f.title : t("ddp.untitled"), d, juce::Justification::topLeft, true);
}

void JanelaPlayerDdp::resized() {
    const auto geo = moldura::geometria(moldura::areaDoCartaoNativo(getLocalBounds()), false);
    auto corpo = geo.corpo.reduced(moldura::kMargemCorpo);
    auto topo = corpo.removeFromTop(176);
    areaDisplay_ = topo.removeFromRight(230);
    topo.removeFromRight(12);
    areaInfo_ = topo;
    corpo.removeFromTop(12);
    auto transporte = corpo.removeFromBottom(44);
    corpo.removeFromBottom(8);
    auto barra = corpo.removeFromBottom(24);
    corpo.removeFromBottom(8);
    tabela_.setBounds(corpo);
    posicao_.setBounds(barra);
    btnAnterior_.setBounds(transporte.removeFromLeft(56));
    transporte.removeFromLeft(8);
    btnPlay_.setBounds(transporte.removeFromLeft(80));
    transporte.removeFromLeft(8);
    btnParar_.setBounds(transporte.removeFromLeft(56));
    transporte.removeFromLeft(8);
    btnProximo_.setBounds(transporte.removeFromLeft(56));
    btnInverter_.setBounds(transporte.removeFromRight(230));
}

}  // namespace matriz::ui
