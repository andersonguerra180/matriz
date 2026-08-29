#include "BarraProgressoGlobalComponent.h"
#include "Tokens.h"

namespace matriz::ui {

BarraProgressoGlobalComponent::BarraProgressoGlobalComponent() {
    const auto& tk = tema();

    lblTitulo_ = std::make_unique<juce::Label>("", "READY");
    lblTitulo_->setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    lblTitulo_->setColour(juce::Label::textColourId, tk.acento);
    lblTitulo_->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*lblTitulo_);

    lblDetalhe_ = std::make_unique<juce::Label>("", "");
    lblDetalhe_->setFont(juce::Font(juce::FontOptions(11.0f)));
    lblDetalhe_->setColour(juce::Label::textColourId, tk.textoSecundario);
    lblDetalhe_->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*lblDetalhe_);

    btnCancelar_ = std::make_unique<juce::TextButton>("Cancel");
    btnCancelar_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnCancelar_->setColour(juce::TextButton::textColourOffId, tk.perigo);
    btnCancelar_->onClick = [this] {
        if (estado_.ativo && estado_.id.isNotEmpty()) {
            ProgressoGlobal::obterInstancia().cancelarTarefa(estado_.id);
        }
    };
    btnCancelar_->setVisible(false);
    addAndMakeVisible(*btnCancelar_);

    ProgressoGlobal::obterInstancia().adicionarListener(this);
    estado_ = ProgressoGlobal::obterInstancia().obterEstado();
    atualizarVisual();

    startTimerHz(30); // 30 FPS for smooth progress smoothing and animation
}

BarraProgressoGlobalComponent::~BarraProgressoGlobalComponent() {
    stopTimer();
    ProgressoGlobal::obterInstancia().removerListener(this);
}

void BarraProgressoGlobalComponent::aoProgressoAtualizado(const EstadoProgresso& estado) {
    estado_ = estado;
    atualizarVisual();
}

void BarraProgressoGlobalComponent::timerCallback() {
    bool precisaRepaint = false;

    if (estado_.ativo) {
        animOffset_ += 0.05f;
        if (animOffset_ > 1.0f) animOffset_ -= 1.0f;
        precisaRepaint = true;

        // Smooth progress interpolation
        double target = (estado_.fracao >= 0.0) ? estado_.fracao : 0.0;
        if (std::abs(progressoSuavizado_ - target) > 0.001) {
            progressoSuavizado_ += (target - progressoSuavizado_) * 0.25;
            precisaRepaint = true;
        }
    } else {
        progressoSuavizado_ = 0.0;
    }

    if (precisaRepaint) {
        repaint();
    }
}

void BarraProgressoGlobalComponent::atualizarVisual() {
    const auto& tk = tema();

    if (estado_.ativo) {
        lblTitulo_->setText(estado_.titulo.toUpperCase(), juce::dontSendNotification);
        lblTitulo_->setColour(juce::Label::textColourId, tk.acento);

        juce::String detalhe = estado_.detalhe;
        if (detalhe.isEmpty() && estado_.totalItens > 0) {
            detalhe = juce::String(estado_.itensConcluidos) + " / " + juce::String(estado_.totalItens);
        }
        lblDetalhe_->setText(detalhe, juce::dontSendNotification);
        lblDetalhe_->setColour(juce::Label::textColourId, tk.textoPrimario);

        btnCancelar_->setVisible(estado_.cancelavel);
    } else {
        btnCancelar_->setVisible(false);

        if (estado_.mensagemConclusao.isNotEmpty()) {
            lblTitulo_->setText("COMPLETED", juce::dontSendNotification);
            lblTitulo_->setColour(juce::Label::textColourId, juce::Colour(0xff22c55e));
            lblDetalhe_->setText(estado_.mensagemConclusao, juce::dontSendNotification);
            lblDetalhe_->setColour(juce::Label::textColourId, tk.textoSecundario);
        } else {
            lblTitulo_->setText("READY", juce::dontSendNotification);
            lblTitulo_->setColour(juce::Label::textColourId, tk.textoTerciario);
            lblDetalhe_->setText("No background tasks running", juce::dontSendNotification);
            lblDetalhe_->setColour(juce::Label::textColourId, tk.textoTerciario);
        }
    }

    resized();
    repaint();
}

void BarraProgressoGlobalComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    auto area = getLocalBounds();

    // Background & Top Border
    g.fillAll(tk.painel);
    g.setColour(tk.borda);
    g.fillRect(0, 0, getWidth(), 1);

    // Left indicator dot / spinner
    int dotX = 12;
    int dotY = getHeight() / 2;
    int dotRadius = 4;

    if (estado_.ativo) {
        // Pulsing active dot
        float pulse = 0.5f + 0.5f * std::sin(animOffset_ * juce::MathConstants<float>::twoPi);
        g.setColour(tk.acento.withAlpha(0.4f + 0.6f * pulse));
        g.fillEllipse(dotX - dotRadius - 1, dotY - dotRadius - 1, (dotRadius + 1) * 2, (dotRadius + 1) * 2);
    } else if (estado_.mensagemConclusao.isNotEmpty()) {
        // Green completed dot
        g.setColour(juce::Colour(0xff22c55e));
        g.fillEllipse(dotX - dotRadius, dotY - dotRadius, dotRadius * 2, dotRadius * 2);
    } else {
        // Subtle idle dot
        g.setColour(tk.textoTerciario.withAlpha(0.5f));
        g.fillEllipse(dotX - dotRadius, dotY - dotRadius, dotRadius * 2, dotRadius * 2);
    }

    // Draw Progress Bar when active
    if (estado_.ativo) {
        int barW = juce::jlimit(160, 320, getWidth() / 3);
        int barH = 12;
        int barX = getWidth() - barW - (btnCancelar_->isVisible() ? 90 : 16);
        int barY = (getHeight() - barH) / 2;

        juce::Rectangle<float> barArea((float)barX, (float)barY, (float)barW, (float)barH);

        // Progress bar background track
        g.setColour(tk.painelAlt);
        g.fillRoundedRectangle(barArea, 4.0f);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(barArea, 4.0f, 1.0f);

        if (estado_.fracao >= 0.0) {
            // Determinate progress bar
            float fillW = barArea.getWidth() * (float)progressoSuavizado_;
            if (fillW > 0.5f) {
                juce::Rectangle<float> fillArea(barArea.getX(), barArea.getY(), fillW, barArea.getHeight());
                g.setColour(tk.acento);
                g.fillRoundedRectangle(fillArea, 4.0f);
            }

            // Percentage Text
            int percent = static_cast<int>(std::round(progressoSuavizado_ * 100.0));
            juce::String pctStr = juce::String(percent) + "%";
            g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
            g.setColour(tk.textoPrimario);
            g.drawText(pctStr, barArea, juce::Justification::centred, true);
        } else {
            // Indeterminate animation shimmer
            float shimmerPos = barArea.getX() + (barArea.getWidth() - 40.0f) * animOffset_;
            juce::Rectangle<float> shimmerArea(shimmerPos, barArea.getY(), 40.0f, barArea.getHeight());
            g.setColour(tk.acento.withAlpha(0.7f));
            g.fillRoundedRectangle(shimmerArea, 4.0f);
        }
    }
}

void BarraProgressoGlobalComponent::resized() {
    auto area = getLocalBounds().reduced(26, 4);

    if (btnCancelar_->isVisible()) {
        int btnW = 74;
        int btnH = 22;
        btnCancelar_->setBounds(getWidth() - btnW - 10, (getHeight() - btnH) / 2, btnW, btnH);
    }

    int titleW = 100;
    lblTitulo_->setBounds(area.removeFromLeft(titleW));
    area.removeFromLeft(8);

    // Limit detail width so it doesn't overlap progress bar
    int progressW = estado_.ativo ? (juce::jlimit(160, 320, getWidth() / 3) + (btnCancelar_->isVisible() ? 100 : 20)) : 0;
    lblDetalhe_->setBounds(area.removeFromLeft(juce::jmax(50, area.getWidth() - progressW)));
}

} // namespace matriz::ui
