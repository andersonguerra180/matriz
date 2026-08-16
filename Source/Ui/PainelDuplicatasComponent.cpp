#include "PainelDuplicatasComponent.h"
#include "Tokens.h"
#include "ModalMitigacao.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

PainelDuplicatasComponent::PainelDuplicatasComponent(ProjetoAberto& projeto)
    : projeto_(projeto)
{
    conteudo_ = std::make_unique<ContentComponent>(*this);
    viewport_.setViewedComponent(conteudo_.get(), false);
    addAndMakeVisible(viewport_);
}

void PainelDuplicatasComponent::recarregar() {
    auto grupos = projeto_.listarGruposDuplicados();
    conteudo_->atualizarGrupos(std::move(grupos));
    resized();
}

void PainelDuplicatasComponent::paint(juce::Graphics& g) {
    g.fillAll(tema().fundo);
}

void PainelDuplicatasComponent::resized() {
    viewport_.setBounds(getLocalBounds());
    if (conteudo_) {
        conteudo_->setBounds(0, 0, viewport_.getWidth() - viewport_.getScrollBarThickness(), conteudo_->getHeight());
    }
}

// =============================================================================
// ContentComponent Implementation
// =============================================================================

PainelDuplicatasComponent::ContentComponent::ContentComponent(PainelDuplicatasComponent& owner)
    : owner_(owner)
{
}

void PainelDuplicatasComponent::ContentComponent::atualizarGrupos(std::vector<ProjetoAberto::ParDuplicatas> novosGrupos) {
    grupos_ = std::move(novosGrupos);
    botoesPorGrupo_.clear();
    removeAllChildren();

    for (size_t gIdx = 0; gIdx < grupos_.size(); ++gIdx) {
        std::vector<BotaoInfo> botoesItem;
        const auto& grupo = grupos_[gIdx];

        for (size_t iIdx = 0; iIdx < grupo.itens.size(); ++iIdx) {
            const auto& item = grupo.itens[iIdx];
            std::string itemId = item.id;

            BotaoInfo b;

            b.btnManter = std::make_unique<juce::TextButton>("Keep Correct");
            b.btnManter->setColour(juce::TextButton::buttonColourId, tema().estadoQcOk.withAlpha(0.12f));
            b.btnManter->setColour(juce::TextButton::textColourOffId, tema().estadoQcOk);
            b.btnManter->onClick = [this, itemId] { acaoManter(itemId); };
            addAndMakeVisible(*b.btnManter);

            b.btnDuplicata = std::make_unique<juce::TextButton>("Mark Duplicate");
            b.btnDuplicata->setColour(juce::TextButton::buttonColourId, tema().alerta.withAlpha(0.12f));
            b.btnDuplicata->setColour(juce::TextButton::textColourOffId, tema().alerta);
            b.btnDuplicata->onClick = [this, itemId] { acaoDuplicata(itemId); };
            addAndMakeVisible(*b.btnDuplicata);

            b.btnRemover = std::make_unique<juce::TextButton>("Remove Copy");
            b.btnRemover->setColour(juce::TextButton::buttonColourId, tema().perigo.withAlpha(0.12f));
            b.btnRemover->setColour(juce::TextButton::textColourOffId, tema().perigo);
            b.btnRemover->onClick = [this, itemId] { acaoRemover(itemId); };
            addAndMakeVisible(*b.btnRemover);

            botoesItem.push_back(std::move(b));
        }

        botoesPorGrupo_.push_back(std::move(botoesItem));
    }

    resized();
    repaint();
}

void PainelDuplicatasComponent::ContentComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    
    // Draw Title & Subtitle at the top
    auto area = getLocalBounds();
    auto areaTitulo = area.removeFromTop(70).reduced(tk.espacoGrande, tk.espacoMedio);
    
    g.setColour(tk.textoPrimario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
    g.drawText("Duplicates Manager", areaTitulo.removeFromTop(30), juce::Justification::centredLeft);
    
    g.setColour(tk.textoSecundario);
    g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    g.drawText("The assets listed below contain identical contents based on Name, Size, and Checksum.", 
               areaTitulo, juce::Justification::centredLeft, true);

    if (grupos_.empty()) {
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::italic)));
        g.drawText("No duplicate assets found in the project. You are all set!", 
                   area.reduced(tk.espacoGrande), juce::Justification::centred, true);
        return;
    }

    // Draw cards for each duplicate group
    for (size_t gIdx = 0; gIdx < grupos_.size(); ++gIdx) {
        const auto& grupo = grupos_[gIdx];
        
        int cardY = 80 + static_cast<int>(gIdx) * 190;
        juce::Rectangle<int> cardBounds(16, cardY, getWidth() - 32, 175);
        
        // Draw card background
        g.setColour(tk.painelAlt);
        g.fillRoundedRectangle(cardBounds.toFloat(), tk.raioMedio);
        
        g.setColour(tk.borda);
        g.drawRoundedRectangle(cardBounds.toFloat(), tk.raioMedio, 1.0f);
        
        // Draw card header
        auto headerArea = cardBounds.removeFromTop(32).reduced(tk.espacoMedio, 0);
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteSubtitulo, juce::Font::bold)));
        g.drawText("File: " + grupo.filename + " (" + juce::File::descriptionOfSizeInBytes(grupo.tamanhoBytes) + ")", 
                   headerArea, juce::Justification::centredLeft, true);
                   
        // Draw checksum tag on the right of the header
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        juce::String checksumTag = "SHA256: " + juce::String(grupo.checksumSha256).substring(0, 16) + "...";
        g.drawText(checksumTag, headerArea, juce::Justification::centredRight, true);
        
        // Draw column divider
        if (grupo.itens.size() > 1) {
            int colW = cardBounds.getWidth() / static_cast<int>(grupo.itens.size());
            g.setColour(tk.borda);
            for (size_t colIdx = 1; colIdx < grupo.itens.size(); ++colIdx) {
                int dividerX = cardBounds.getX() + static_cast<int>(colIdx) * colW;
                g.drawVerticalLine(dividerX, static_cast<float>(cardBounds.getY() + 4), static_cast<float>(cardBounds.getBottom() - 4));
            }
        }
        
        // Draw each item info inside the card columns
        int colW = cardBounds.getWidth() / static_cast<int>(grupo.itens.size());
        for (size_t iIdx = 0; iIdx < grupo.itens.size(); ++iIdx) {
            const auto& item = grupo.itens[iIdx];
            juce::Rectangle<int> colBounds(cardBounds.getX() + static_cast<int>(iIdx) * colW, cardBounds.getY(), colW, cardBounds.getHeight());
            auto infoArea = colBounds.reduced(tk.espacoMedio, 6);
            
            // Draw asset metadata
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
            g.drawText("Code: " + juce::String(item.codigoAcervo.empty() ? "(Ingesting...)" : item.codigoAcervo), 
                       infoArea.removeFromTop(18), juce::Justification::centredLeft, true);
            
            g.setColour(tk.textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            g.drawText("Title: " + juce::String(item.titulo), infoArea.removeFromTop(18), juce::Justification::centredLeft, true);
            
            // Draw state with corresponding status color
            juce::Colour stateCol = tk.textoTerciario;
            if (item.estado == "qc_ok" || item.estado == "aprovado") stateCol = tk.estadoQcOk;
            else if (item.estado == "alerta") stateCol = tk.estadoAlerta;
            else if (item.estado == "capturado") stateCol = tk.estadoCapturado;
            
            g.setColour(stateCol);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            g.drawText("State: " + juce::String(item.estado).toUpperCase(), infoArea.removeFromTop(18), juce::Justification::centredLeft, true);
            
            // Draw paths
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            juce::String caminhoExibir = item.caminhoOrigem.isNotEmpty() ? item.caminhoOrigem : item.caminhoRelativo;
            g.drawFittedText("Path: " + caminhoExibir, infoArea.removeFromTop(36), juce::Justification::topLeft, 2);
        }
    }
}

void PainelDuplicatasComponent::ContentComponent::resized() {
    // Layout action buttons inside columns
    const auto& tk = tema();
    for (size_t gIdx = 0; gIdx < grupos_.size(); ++gIdx) {
        const auto& grupo = grupos_[gIdx];
        
        int cardY = 80 + static_cast<int>(gIdx) * 190;
        juce::Rectangle<int> cardBounds(16, cardY, getWidth() - 32, 175);
        cardBounds.removeFromTop(32); // skip header
        
        int colW = cardBounds.getWidth() / static_cast<int>(grupo.itens.size());
        for (size_t iIdx = 0; iIdx < grupo.itens.size(); ++iIdx) {
            juce::Rectangle<int> colBounds(cardBounds.getX() + static_cast<int>(iIdx) * colW, cardBounds.getY(), colW, cardBounds.getHeight());
            auto buttonsArea = colBounds.reduced(tk.espacoMedio, 6).removeFromBottom(26);
            
            // Divide buttons area into three
            int btnW = (buttonsArea.getWidth() - 8) / 3;
            auto& b = botoesPorGrupo_[gIdx][iIdx];
            
            if (b.btnManter) {
                b.btnManter->setBounds(buttonsArea.removeFromLeft(btnW));
                buttonsArea.removeFromLeft(4);
            }
            if (b.btnDuplicata) {
                b.btnDuplicata->setBounds(buttonsArea.removeFromLeft(btnW));
                buttonsArea.removeFromLeft(4);
            }
            if (b.btnRemover) {
                b.btnRemover->setBounds(buttonsArea);
            }
        }
    }

    int totalH = 100 + static_cast<int>(grupos_.size()) * 190;
    setSize(getWidth(), juce::jmax(totalH, getParentHeight()));
}

void PainelDuplicatasComponent::ContentComponent::acaoManter(const std::string& itemId) {
    // "Keep Correct" moves the item to 'aprovado' state to certify it.
    owner_.projeto_.atualizarEstadoItem(itemId, "aprovado");
    owner_.recarregar();
}

void PainelDuplicatasComponent::ContentComponent::acaoDuplicata(const std::string& itemId) {
    // "Mark Duplicate" sets state to 'duplicata'
    owner_.projeto_.atualizarEstadoItem(itemId, "duplicata");
    owner_.recarregar();
}

void PainelDuplicatasComponent::ContentComponent::acaoRemover(const std::string& itemId) {
    // "Remove Copy" prompts and deletes the item from project
    auto janela = std::make_shared<juce::AlertWindow>(
        "Confirm Removal",
        "Are you sure you want to remove this copy from the project? The source file on disk will remain untouched.",
        juce::MessageBoxIconType::WarningIcon);
        
    janela->addButton("Remove Item", 1);
    janela->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    
    juce::Component::SafePointer<ContentComponent> safeThis(this);
    janela->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, janela, itemId](int resultado) mutable {
        retirarPeerDaTela(*janela);
        if (safeThis && resultado == 1) {
            safeThis->owner_.projeto_.removerItensDoProjeto({itemId});
            safeThis->owner_.recarregar();
        }
        janela.reset();
    }));
}

} // namespace matriz::ui
