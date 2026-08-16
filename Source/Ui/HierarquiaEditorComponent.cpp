#include "HierarquiaEditorComponent.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

namespace {

juce::String rotuloDoNivel(matriz::consolidacao::NivelHierarquia n) {
    using N = matriz::consolidacao::NivelHierarquia;
    switch (n) {
        case N::EstruturaOriginal: return "ORIGINAL FOLDERS";
        case N::Projeto:     return "PROJECT";
        case N::Ano:         return "YEAR";
        case N::TipoMidia:   return "MEDIA TYPE";
        case N::TipoArquivo: return "FILE TYPE";
        case N::Origem:      return "ORIGIN";
        case N::Artista:     return "ARTIST";
        case N::PastaManual: return "MANUAL FOLDERS";
    }
    return "?";
}

juce::Colour corDoNivel(matriz::consolidacao::NivelHierarquia n) {
    using N = matriz::consolidacao::NivelHierarquia;
    switch (n) {
        case N::EstruturaOriginal: return juce::Colour(0xff808080);
        case N::Projeto:     return juce::Colour(0xff4a90d9);
        case N::Ano:         return juce::Colour(0xff3d8f40);
        case N::TipoMidia:   return juce::Colour(0xffe07830);
        case N::TipoArquivo: return juce::Colour(0xff9060c0);
        case N::Origem:      return juce::Colour(0xff30a8a0);
        case N::Artista:     return juce::Colour(0xffc04070);
        case N::PastaManual: return juce::Colour(0xffc0a030);
    }
    return juce::Colours::grey;
}

juce::String exemploDoNivel(matriz::consolidacao::NivelHierarquia n) {
    using N = matriz::consolidacao::NivelHierarquia;
    switch (n) {
        case N::EstruturaOriginal: return "Folder/Subfolder/file.ext";
        case N::Projeto:     return "MyProject";
        case N::Ano:         return "1985";
        case N::TipoMidia:   return "Reel tape";
        case N::TipoArquivo: return "wav";
        case N::Origem:      return "Analog";
        case N::Artista:     return "Artist Name";
        case N::PastaManual: return "Category/Subcategory";
    }
    return "?";
}

} // namespace

HierarquiaEditorComponent::HierarquiaEditorComponent(const matriz::consolidacao::HierarquiaBackup& hierarquiaAtual) {
    using N = matriz::consolidacao::NivelHierarquia;
    std::vector<N> todos = {N::Projeto, N::Ano, N::TipoMidia, N::TipoArquivo, N::Origem, N::Artista, N::PastaManual};

    std::set<N> ativos(hierarquiaAtual.begin(), hierarquiaAtual.end());

    for (auto n : hierarquiaAtual) {
        blocos_.push_back({n, rotuloDoNivel(n), corDoNivel(n), true, {}});
    }
    for (auto n : todos) {
        if (!ativos.count(n))
            blocos_.push_back({n, rotuloDoNivel(n), corDoNivel(n), false, {}});
    }

    labelTitulo_ = std::make_unique<juce::Label>();
    labelTitulo_->setText("Backup Folder Hierarchy", juce::dontSendNotification);
    labelTitulo_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteTitulo, juce::Font::bold)));
    labelTitulo_->setColour(juce::Label::textColourId, tema().textoPrimario);
    labelTitulo_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*labelTitulo_);

    labelAtivos_ = std::make_unique<juce::Label>();
    labelAtivos_->setText("ACTIVE LEVELS", juce::dontSendNotification);
    labelAtivos_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
    labelAtivos_->setColour(juce::Label::textColourId, tema().textoSecundario);
    addAndMakeVisible(*labelAtivos_);

    labelDisponiveis_ = std::make_unique<juce::Label>();
    labelDisponiveis_->setText("AVAILABLE", juce::dontSendNotification);
    labelDisponiveis_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
    labelDisponiveis_->setColour(juce::Label::textColourId, tema().textoSecundario);
    addAndMakeVisible(*labelDisponiveis_);

    labelPreview_ = std::make_unique<juce::Label>();
    labelPreview_->setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteCorpo)));
    labelPreview_->setColour(juce::Label::textColourId, tema().acento);
    labelPreview_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*labelPreview_);

    btnOk_ = std::make_unique<juce::TextButton>("APPLY");
    btnOk_->setColour(juce::TextButton::buttonColourId, tema().acento);
    btnOk_->setColour(juce::TextButton::textColourOffId, tema().textoSobreAcento);
    btnOk_->onClick = [this] {
        if (aoConfirmar) aoConfirmar(hierarquiaResultante());
    };
    addAndMakeVisible(*btnOk_);

    btnCancelar_ = std::make_unique<juce::TextButton>("CANCEL");
    btnCancelar_->setColour(juce::TextButton::buttonColourId, tema().painelAlt);
    btnCancelar_->setColour(juce::TextButton::textColourOffId, tema().textoPrimario);
    btnCancelar_->onClick = [this] {
        if (aoCancelar) aoCancelar();
    };
    addAndMakeVisible(*btnCancelar_);

    setSize(560, 520);
}

matriz::consolidacao::HierarquiaBackup HierarquiaEditorComponent::hierarquiaResultante() const {
    matriz::consolidacao::HierarquiaBackup h;
    for (const auto& b : blocos_)
        if (b.ativo) h.push_back(b.nivel);
    return h;
}

juce::String HierarquiaEditorComponent::previewCaminho() const {
    juce::String path = "/Backup Drive";
    for (const auto& b : blocos_) {
        if (b.ativo)
            path += "/" + exemploDoNivel(b.nivel);
    }
    path += "/filename.wav";
    return path;
}

void HierarquiaEditorComponent::recalcularLayout() {
    int yAtivo = kTopoArea;
    int yDisponivel = kTopoArea;

    for (size_t i = 0; i < blocos_.size(); ++i) {
        if (static_cast<int>(i) == arrastando_) continue;

        if (blocos_[i].ativo) {
            blocos_[i].bounds = juce::Rectangle<float>(
                static_cast<float>(kAreaAtivosX), static_cast<float>(yAtivo),
                kBlocoLargura, kBlocoAltura);
            yAtivo += static_cast<int>(kBlocoAltura + kEspaco);
        } else {
            blocos_[i].bounds = juce::Rectangle<float>(
                static_cast<float>(kAreaDisponiveisX), static_cast<float>(yDisponivel),
                kBlocoLargura, kBlocoAltura);
            yDisponivel += static_cast<int>(kBlocoAltura + kEspaco);
        }
    }

    if (arrastando_ >= 0) {
        blocos_[static_cast<size_t>(arrastando_)].bounds = juce::Rectangle<float>(
            posicaoArrasto_.x, posicaoArrasto_.y, kBlocoLargura, kBlocoAltura);
    }
}

int HierarquiaEditorComponent::indiceBlocoNaPosicao(juce::Point<int> pos) const {
    for (int i = 0; i < static_cast<int>(blocos_.size()); ++i) {
        if (blocos_[static_cast<size_t>(i)].bounds.contains(pos.toFloat()))
            return i;
    }
    return -1;
}

int HierarquiaEditorComponent::indiceDropNaPosicao(int y) const {
    int idx = 0;
    for (const auto& b : blocos_) {
        if (!b.ativo) continue;
        float meio = b.bounds.getCentreY();
        if (static_cast<float>(y) > meio) ++idx;
        else break;
    }
    return idx;
}

void HierarquiaEditorComponent::paint(juce::Graphics& g) {
    g.fillAll(tema().fundo);

    auto divider = getWidth() / 2;
    g.setColour(tema().borda);
    g.fillRect(divider - 1, kTopoArea - 10, 2, getHeight() - kTopoArea - 60);

    // Connection lines between active blocks
    juce::Point<float> prevBottom;
    bool firstActive = true;
    for (size_t i = 0; i < blocos_.size(); ++i) {
        if (static_cast<int>(i) == arrastando_ || !blocos_[i].ativo) continue;
        auto& b = blocos_[i].bounds;
        auto center = juce::Point<float>(b.getCentreX(), b.getY());
        if (!firstActive) {
            g.setColour(tema().acento.withAlpha(0.5f));
            juce::Path path;
            path.startNewSubPath(prevBottom);
            float midY = (prevBottom.y + center.y) / 2.0f;
            path.cubicTo(prevBottom.x, midY, center.x, midY, center.x, center.y);
            g.strokePath(path, juce::PathStrokeType(2.5f));

            // Arrow head
            float arrowSize = 6.0f;
            juce::Path arrow;
            arrow.startNewSubPath(center.x - arrowSize, center.y - arrowSize);
            arrow.lineTo(center.x, center.y);
            arrow.lineTo(center.x + arrowSize, center.y - arrowSize);
            g.strokePath(arrow, juce::PathStrokeType(2.0f));
        }
        prevBottom = juce::Point<float>(b.getCentreX(), b.getBottom());
        firstActive = false;
    }

    // Drop indicator
    if (arrastando_ >= 0 && indiceDropAlvo_ >= 0) {
        int yDrop = kTopoArea;
        int count = 0;
        for (size_t i = 0; i < blocos_.size(); ++i) {
            if (static_cast<int>(i) == arrastando_ || !blocos_[i].ativo) continue;
            if (count == indiceDropAlvo_) {
                yDrop = static_cast<int>(blocos_[i].bounds.getY()) - static_cast<int>(kEspaco / 2);
                break;
            }
            yDrop = static_cast<int>(blocos_[i].bounds.getBottom() + kEspaco / 2);
            ++count;
        }
        g.setColour(tema().acento);
        g.fillRoundedRectangle(static_cast<float>(kAreaAtivosX) - 8.0f,
                               static_cast<float>(yDrop) - 2.0f,
                               kBlocoLargura + 16.0f, 4.0f, 2.0f);
    }

    // Draw blocks
    for (size_t i = 0; i < blocos_.size(); ++i) {
        const auto& b = blocos_[i];
        bool isDragging = static_cast<int>(i) == arrastando_;

        float alpha = isDragging ? 0.85f : (b.ativo ? 1.0f : 0.5f);
        float shadowOffset = isDragging ? 4.0f : 0.0f;

        if (isDragging) {
            g.setColour(juce::Colours::black.withAlpha(0.3f));
            g.fillRoundedRectangle(b.bounds.translated(shadowOffset, shadowOffset), kRaio);
        }

        // Block body
        g.setColour(b.cor.withAlpha(alpha));
        g.fillRoundedRectangle(b.bounds, kRaio);

        // Highlight stripe on left
        g.setColour(b.cor.brighter(0.3f).withAlpha(alpha));
        juce::Path stripe;
        stripe.addRoundedRectangle(b.bounds.getX(), b.bounds.getY(), 8.0f, b.bounds.getHeight(),
                                    kRaio, kRaio, true, false, true, false);
        g.fillPath(stripe);

        // Border
        g.setColour(b.cor.darker(0.2f).withAlpha(alpha));
        g.drawRoundedRectangle(b.bounds, kRaio, 1.5f);

        // Text
        g.setColour(juce::Colours::white.withAlpha(alpha));
        g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        g.drawText(b.rotulo, b.bounds.reduced(14.0f, 4.0f), juce::Justification::centredLeft);

        // Drag handle dots
        float dotX = b.bounds.getRight() - 20.0f;
        float dotY = b.bounds.getCentreY() - 8.0f;
        g.setColour(juce::Colours::white.withAlpha(0.4f * alpha));
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 2; ++col) {
                g.fillEllipse(dotX + col * 5.0f, dotY + row * 6.0f, 3.0f, 3.0f);
            }
        }

        // Inactive: "+" icon hint
        if (!b.ativo && !isDragging) {
            g.setColour(juce::Colours::white.withAlpha(0.6f));
            g.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
            g.drawText("+", b.bounds.withLeft(b.bounds.getX() + kBlocoLargura - 32.0f).withWidth(24.0f),
                        juce::Justification::centred);
        }
    }
}

void HierarquiaEditorComponent::resized() {
    auto area = getLocalBounds().reduced(16);

    labelTitulo_->setBounds(area.removeFromTop(36));
    area.removeFromTop(8);

    // Bottom buttons and preview
    auto bottom = area.removeFromBottom(40);
    btnCancelar_->setBounds(bottom.removeFromRight(100).reduced(4));
    btnOk_->setBounds(bottom.removeFromRight(100).reduced(4));

    auto previewArea = area.removeFromBottom(28);
    labelPreview_->setBounds(previewArea);
    area.removeFromBottom(8);

    // Column headers
    auto headerArea = area.removeFromTop(20);
    labelAtivos_->setBounds(headerArea.removeFromLeft(getWidth() / 2 - 16));
    labelDisponiveis_->setBounds(headerArea);

    recalcularLayout();
    labelPreview_->setText(previewCaminho(), juce::dontSendNotification);
}

void HierarquiaEditorComponent::mouseDown(const juce::MouseEvent& e) {
    int idx = indiceBlocoNaPosicao(e.getPosition());
    if (idx < 0) return;

    arrastando_ = idx;
    auto& b = blocos_[static_cast<size_t>(idx)];
    offsetArrasto_ = e.getPosition() - b.bounds.getPosition().toInt();
    posicaoArrasto_ = b.bounds.getPosition();
}

void HierarquiaEditorComponent::mouseDrag(const juce::MouseEvent& e) {
    if (arrastando_ < 0) return;

    posicaoArrasto_ = (e.getPosition() - offsetArrasto_).toFloat();
    blocos_[static_cast<size_t>(arrastando_)].bounds.setPosition(posicaoArrasto_);

    bool inActiveZone = e.getPosition().x < getWidth() / 2;
    if (inActiveZone) {
        indiceDropAlvo_ = indiceDropNaPosicao(e.getPosition().y);
    } else {
        indiceDropAlvo_ = -1;
    }

    repaint();
}

void HierarquiaEditorComponent::mouseUp(const juce::MouseEvent& e) {
    if (arrastando_ < 0) return;

    auto& bloco = blocos_[static_cast<size_t>(arrastando_)];
    bool inActiveZone = e.getPosition().x < getWidth() / 2;

    if (inActiveZone) {
        // Moving to active or reordering within active
        bool wasActive = bloco.ativo;
        bloco.ativo = true;

        // Remove from current position
        auto blocoCopia = bloco;
        blocos_.erase(blocos_.begin() + arrastando_);

        // Find insertion index among active blocks
        int insertAt = 0;
        int activeCount = 0;
        for (size_t i = 0; i < blocos_.size(); ++i) {
            if (!blocos_[i].ativo) continue;
            if (activeCount >= indiceDropAlvo_) break;
            insertAt = static_cast<int>(i) + 1;
            ++activeCount;
        }
        if (indiceDropAlvo_ < 0) {
            // Append to active
            int lastActive = -1;
            for (int i = 0; i < static_cast<int>(blocos_.size()); ++i) {
                if (blocos_[static_cast<size_t>(i)].ativo) lastActive = i;
            }
            insertAt = lastActive + 1;
        }
        insertAt = juce::jlimit(0, static_cast<int>(blocos_.size()), insertAt);
        blocos_.insert(blocos_.begin() + insertAt, blocoCopia);
    } else {
        // Moving to available (deactivate)
        bloco.ativo = false;
    }

    arrastando_ = -1;
    indiceDropAlvo_ = -1;
    recalcularLayout();
    labelPreview_->setText(previewCaminho(), juce::dontSendNotification);
    repaint();
}

// --- Window ---

HierarquiaEditorWindow::HierarquiaEditorWindow(
    const matriz::consolidacao::HierarquiaBackup& hierarquiaAtual,
    std::function<void(const matriz::consolidacao::HierarquiaBackup&)> aoConfirmar)
    : juce::DocumentWindow("Backup Folder Hierarchy",
                            tema().fundo,
                            juce::DocumentWindow::closeButton)
{
    auto* editor = new HierarquiaEditorComponent(hierarquiaAtual);
    editor->aoConfirmar = [this, aoConfirmar](const matriz::consolidacao::HierarquiaBackup& h) {
        if (aoConfirmar) aoConfirmar(h);
        delete this;
    };
    editor->aoCancelar = [this] { delete this; };
    setContentOwned(editor, true);
    setResizable(false, false);
    centreWithSize(560, 520);
    setVisible(true);
    toFront(true);
}

} // namespace matriz::ui
