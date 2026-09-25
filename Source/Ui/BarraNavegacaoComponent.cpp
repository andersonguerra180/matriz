#include "BarraNavegacaoComponent.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

BarraNavegacaoComponent::BarraNavegacaoComponent() {
    reconstruirTabs();

    const auto& tk = tema();

    botaoAjuda_ = std::make_unique<juce::TextButton>("?");
    botaoAjuda_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    botaoAjuda_->setColour(juce::TextButton::textColourOffId, tk.acento);
    botaoAjuda_->setColour(juce::TextButton::textColourOnId, tk.acento);
    botaoAjuda_->setTooltip(obterTextoAjuda(selectedTab_));
    addAndMakeVisible(*botaoAjuda_);

    // CLOSE PROJECT saiu da barra: fechar a coleção/catálogo (e voltar ao
    // catálogo pai) vive só no menu File.
    setInterceptsMouseClicks(true, true);
}

BarraNavegacaoComponent::~BarraNavegacaoComponent() = default;

void BarraNavegacaoComponent::setProjectInfo(const juce::String& projectName, bool isCatalog) {
    // O selo "COLLECTION - <projeto>" saiu do canto superior esquerdo de
    // todas as abas; o nome do projeto continua no título da janela. Aqui só
    // interessa o modo, que define o conjunto de tabs.
    juce::ignoreUnused(projectName);
    isCatalog_ = isCatalog;
    reconstruirTabs();
    resized();
    repaint();
}

void BarraNavegacaoComponent::lookAndFeelChanged() {
    const auto& tk = tema();
    if (botaoAjuda_) {
        botaoAjuda_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        botaoAjuda_->setColour(juce::TextButton::textColourOffId, tk.acento);
        botaoAjuda_->setColour(juce::TextButton::textColourOnId, tk.acento);
        botaoAjuda_->setTooltip(obterTextoAjuda(selectedTab_));
    }
    reconstruirTabs();
    resized();
    repaint();
}

void BarraNavegacaoComponent::reconstruirTabs() {
    tabs_.clear();
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    if (isCatalog_) {
        tabs_.push_back({ Tab::Catalog, isPt ? juce::String::fromUTF8("1 - Coleções") : "1 - Collections", {}, {}, false });
        tabs_.push_back({ Tab::Duplicates, isPt ? juce::String::fromUTF8("2 - Duplicatas") : "2 - Duplicates", {}, {}, false });
        tabs_.push_back({ Tab::Analytics, isPt ? juce::String::fromUTF8("3 - Armazenamento") : "3 - Storage", {}, {}, false });
        tabs_.push_back({ Tab::Storage, isPt ? juce::String::fromUTF8("4 - Disco") : "4 - Disk", {}, {}, false });
        tabs_.push_back({ Tab::Backup, "5 - Backup", {}, {}, false });
    } else {
        tabs_.push_back({ Tab::Intake, isPt ? juce::String::fromUTF8("1 - Ingestão") : "1 - Intake", {}, {}, false });
        tabs_.push_back({ Tab::Grid, isPt ? juce::String::fromUTF8("2 - Metadados") : "2 - Metadata", {}, {}, false });
        tabs_.push_back({ Tab::Duplicates, isPt ? juce::String::fromUTF8("3 - Duplicatas") : "3 - Duplicates", {}, {}, false });
        // Item 4 (nova lista): STORAGE (Analytics) e TREEMAP (Tree) viraram
        // uma aba só — "Structure" — com sub-abas FOLDER MAP/SPACE MAP
        // dentro (ver MainComponent::mostrarStructure). Tab::Tree fica sem
        // entrada própria na barra; Tab::Analytics passa a representar a
        // aba combinada.
        tabs_.push_back({ Tab::Analytics, isPt ? juce::String::fromUTF8("4 - Estrutura") : "4 - Structure", {}, {}, false });
        tabs_.push_back({ Tab::Storage, isPt ? juce::String::fromUTF8("5 - Armazenamento") : "5 - Storage", {}, {}, false });
        tabs_.push_back({ Tab::Backup, "6 - Backup", {}, {}, false });
    }
}

void BarraNavegacaoComponent::setComponentesExtras(const std::vector<ComponenteExtra>& esquerda,
                                                   const std::vector<ComponenteExtra>& direita) {
    auto mesmoConteudo = [](const std::vector<ExtraSlot>& atual, const std::vector<ComponenteExtra>& novo) {
        if (atual.size() != novo.size()) return false;
        for (size_t i = 0; i < novo.size(); ++i) {
            if (atual[i].comp.getComponent() != novo[i].first || atual[i].largura != novo[i].second)
                return false;
        }
        return true;
    };
    if (mesmoConteudo(extrasEsquerda_, esquerda) && mesmoConteudo(extrasDireita_, direita)) return;

    for (auto& slot : extrasEsquerda_)
        if (auto* c = slot.comp.getComponent(); c != nullptr && c->getParentComponent() == this) removeChildComponent(c);
    for (auto& slot : extrasDireita_)
        if (auto* c = slot.comp.getComponent(); c != nullptr && c->getParentComponent() == this) removeChildComponent(c);

    extrasEsquerda_.clear();
    extrasDireita_.clear();

    auto adotar = [this](const std::vector<ComponenteExtra>& origem, std::vector<ExtraSlot>& destino) {
        for (const auto& par : origem) {
            if (par.first == nullptr) continue;
            addAndMakeVisible(*par.first);
            destino.push_back({juce::Component::SafePointer<juce::Component>(par.first), par.second});
        }
    };
    adotar(esquerda, extrasEsquerda_);
    adotar(direita, extrasDireita_);

    resized();
    repaint();
}

int BarraNavegacaoComponent::larguraExtras(const std::vector<ExtraSlot>& slots) const {
    int total = 0;
    int visiveis = 0;
    for (const auto& slot : slots) {
        if (slot.comp.getComponent() == nullptr) continue;
        total += slot.largura;
        ++visiveis;
    }
    if (visiveis > 1) total += (visiveis - 1) * 8;
    return total;
}

void BarraNavegacaoComponent::setSelectedTab(Tab tab) {
    if (selectedTab_ != tab) {
        selectedTab_ = tab;
        if (botaoAjuda_) botaoAjuda_->setTooltip(obterTextoAjuda(selectedTab_));
        if (botaoAjuda_) botaoAjuda_->setVisible(selectedTab_ != Tab::Intake);
        resized();
        repaint();
    }
}

void BarraNavegacaoComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    
    // Header background fill - 20% brighter in both light and dark modes
    g.fillAll(tk.painel.brighter(0.20f));
    
    // Bottom border separating header from content
    g.setColour(tk.borda);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
    
    // Draw Tabs in Folder-Tab style with rounded top corners
    for (const auto& tab : tabs_) {
        bool ativo = (tab.tab == selectedTab_);
        
        auto tb = tab.bounds.toFloat();
        float r = 5.0f;
        float bx = tb.getX();
        float by = tb.getY();
        float bw = tb.getWidth();
        float bh = tb.getHeight();
        
        // Build folder tab path (rounded top corners, straight vertical sides, flush bottom)
        juce::Path tabPath;
        tabPath.startNewSubPath(bx, by + bh);
        tabPath.lineTo(bx, by + r);
        tabPath.addArc(bx, by, r * 2.0f, r * 2.0f, juce::MathConstants<float>::pi, juce::MathConstants<float>::pi * 1.5f, false);
        tabPath.lineTo(bx + bw - r, by);
        tabPath.addArc(bx + bw - r * 2.0f, by, r * 2.0f, r * 2.0f, juce::MathConstants<float>::pi * 1.5f, juce::MathConstants<float>::twoPi, false);
        tabPath.lineTo(bx + bw, by + bh);
        
        if (ativo) {
            // Active folder tab: interior inteiro em amarelo clarinho a 30%
            // de opacidade; contorno preto sólido (como era antes das
            // últimas duas tentativas — nem tk.acento, nem cor cheia sem
            // opacidade, nem só uma tarja fina no topo).
            static const juce::Colour corAmareloClaro(0xfffde047);
            juce::Path fillPath(tabPath);
            fillPath.closeSubPath();
            g.setColour(corAmareloClaro.withAlpha(0.30f));
            g.fillPath(fillPath);

            // Contorno da aba pressionada
            g.setColour(juce::Colours::black);
            g.strokePath(tabPath, juce::PathStrokeType(1.5f));

            g.setColour(tk.acento);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold)));
        } else if (tab.hover) {
            juce::Path fillPath(tabPath);
            fillPath.closeSubPath();
            g.setColour(tk.painelAlt.withAlpha(0.40f));
            g.fillPath(fillPath);
            
            g.setColour(tk.borda.withAlpha(0.60f));
            g.strokePath(tabPath, juce::PathStrokeType(1.0f));
            
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        } else {
            juce::Path fillPath(tabPath);
            fillPath.closeSubPath();
            g.setColour(tk.painelAlt.withAlpha(0.15f));
            g.fillPath(fillPath);
            
            g.setColour(tk.borda.withAlpha(0.40f));
            g.strokePath(tabPath, juce::PathStrokeType(0.8f));
            
            g.setColour(tk.textoSecundario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
        }
        
        g.drawText(tab.label, tab.bounds, juce::Justification::centred, true);
        
        // Draw chronological workflow arrow (──›) between sequential tabs with prominent accent contrast
        if (!tab.sepBounds.isEmpty()) {
            auto cx = static_cast<float>(tab.sepBounds.getCentreX());
            auto cy = static_cast<float>(tab.sepBounds.getCentreY());
            juce::Path arrow;
            // Horizontal arrow shaft
            arrow.startNewSubPath(cx - 6.0f, cy);
            arrow.lineTo(cx + 6.0f, cy);
            // Arrowhead
            arrow.startNewSubPath(cx + 1.5f, cy - 4.5f);
            arrow.lineTo(cx + 6.5f, cy);
            arrow.lineTo(cx + 1.5f, cy + 4.5f);
            g.setColour(tk.acento);
            g.strokePath(arrow, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
}

void BarraNavegacaoComponent::resized() {
    const auto& tk = tema();

    // Help button on the right edge (a aba INTAKE traz o "?" dela própria
    // entre os componentes extras, então o daqui fica oculto lá).
    bool hideNavButtons = (selectedTab_ == Tab::Intake);
    if (botaoAjuda_) botaoAjuda_->setVisible(!hideNavButtons);

    int helpBtnSize = 28;
    if (!hideNavButtons && botaoAjuda_)
        botaoAjuda_->setBounds(getWidth() - helpBtnSize - 16, (getHeight() - helpBtnSize) / 2, helpBtnSize, helpBtnSize);

    // Comandos emprestados pela aba ativa (hoje só o INTAKE usa): o grupo da
    // esquerda encosta na margem esquerda, o da direita na margem direita.
    const int kExtraH = 28;
    int extrasDirW = larguraExtras(extrasDireita_);
    {
        int x = 16;
        for (auto& slot : extrasEsquerda_) {
            auto* c = slot.comp.getComponent();
            if (c == nullptr) continue;
            c->setBounds(x, (getHeight() - kExtraH) / 2, slot.largura, kExtraH);
            x += slot.largura + 8;
        }
        int xDir = getWidth() - 16 - extrasDirW;
        for (auto& slot : extrasDireita_) {
            auto* c = slot.comp.getComponent();
            if (c == nullptr) continue;
            c->setBounds(xDir, (getHeight() - kExtraH) / 2, slot.largura, kExtraH);
            xDir += slot.largura + 8;
        }
    }

    // O cluster de tabs ocupa exatamente a mesma posição nas 7 abas: é
    // centralizado na largura inteira da barra, sem depender do que cada aba
    // pendura nas extremidades (o "?" e, no INTAKE, os botões de ingestão).
    // Era essa dependência que deslocava o menu numerado de uma aba pra outra.
    int leftBoundary = 16;
    int rightBoundary = getWidth() - 16;
    int availableWidth = std::max(0, rightBoundary - leftBoundary);
    
    // Measure tabs width dynamically to ensure words like "ARMAZENAMENTO" fit without truncation
    auto fonteTab = juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, juce::Font::bold));
    std::vector<int> tabWidths;
    tabWidths.reserve(tabs_.size());
    int totalTabsW = 0;
    const int sepWidth = 22;
    
    for (const auto& tab : tabs_) {
        int textW = juce::GlyphArrangement::getStringWidthInt(fonteTab, tab.label);
        // Add 26px padding so text never clips, with minimum width of 88px
        int w = std::max(88, textW + 26);
        tabWidths.push_back(w);
        totalTabsW += w;
    }
    
    if (tabs_.size() > 1) {
        totalTabsW += static_cast<int>(tabs_.size() - 1) * sepWidth;
    }
    
    // Center the entire tabs cluster between leftBoundary and rightBoundary
    int startX = leftBoundary + std::max(0, (availableWidth - totalTabsW) / 2);

    // Rede de segurança só para janelas estreitas: a centralização acima é a
    // mesma nas 7 abas, mas numa janela pequena o cluster chegaria a cobrir os
    // botões de ingestão do INTAKE. Nesse caso — e só nesse — ele desliza o
    // mínimo necessário para não sobrepô-los.
    int extrasEsqW = larguraExtras(extrasEsquerda_);
    if (extrasEsqW > 0) startX = std::max(startX, 16 + extrasEsqW + 8);
    int tabY = 5;
    int tabH = getHeight() - 6; // Rests on bottom edge like a folder tab
    
    for (size_t i = 0; i < tabs_.size(); ++i) {
        tabs_[i].bounds = juce::Rectangle<int>(startX, tabY, tabWidths[i], tabH);
        startX += tabWidths[i];
        
        if (i + 1 < tabs_.size()) {
            tabs_[i].sepBounds = juce::Rectangle<int>(startX, tabY, sepWidth, tabH);
            startX += sepWidth;
        } else {
            tabs_[i].sepBounds = {};
        }
    }
}

void BarraNavegacaoComponent::mouseDown(const juce::MouseEvent& e) {
    auto pos = e.getPosition();
    for (const auto& tab : tabs_) {
        if (tab.bounds.contains(pos)) {
            setSelectedTab(tab.tab);
            if (aoMudarTab) aoMudarTab(tab.tab);
            break;
        }
    }
}

void BarraNavegacaoComponent::mouseMove(const juce::MouseEvent& e) {
    auto pos = e.getPosition();
    bool mudou = false;
    bool hoverAny = false;
    for (auto& tab : tabs_) {
        bool hover = tab.bounds.contains(pos);
        if (hover) hoverAny = true;
        if (tab.hover != hover) {
            tab.hover = hover;
            mudou = true;
        }
    }
    setMouseCursor(hoverAny ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (mudou) repaint();
}

void BarraNavegacaoComponent::mouseExit(const juce::MouseEvent&) {
    bool mudou = false;
    for (auto& tab : tabs_) {
        if (tab.hover) {
            tab.hover = false;
            mudou = true;
        }
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
    if (mudou) repaint();
}

juce::String BarraNavegacaoComponent::obterTextoAjuda(Tab tab) const {
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    switch (tab) {
        case Tab::Grid:
            return isPt ? juce::String::fromUTF8("Nesta aba, você pode selecionar um arquivo ou múltiplos arquivos em lote para editar seus metadados e categorização. Preencha com o máximo de detalhes possível.")
                        : "On this tab, you will be able to select a file or a batch of files to edit the metadata and categorize. Fill it with as much details as you can.";
        case Tab::Intake:
            return isPt ? juce::String::fromUTF8("Nesta aba, você pode inspecionar arquivos recém-ingeridos aguardando verificação e integrá-los ao catálogo de metadados.")
                        : "On this tab, you can inspect recently ingested files awaiting verification and confirm them into the metadata catalog.";
        case Tab::Duplicates:
            return isPt ? juce::String::fromUTF8("Nesta aba, você pode identificar e revisar arquivos duplicados na coleção, comparando assinaturas e resolvendo redundâncias.")
                        : "On this tab, you can identify and resolve duplicate files across the project, comparing signatures and managing redundancy.";
        case Tab::Analytics:
            return isPt ? juce::String::fromUTF8("Nesta aba, você visualiza a distribuição de espaço, tipos de mídia, integridade e métricas de armazenamento.")
                        : "On this tab, you can analyze storage usage, media distribution breakdown, and fixity preservation metrics.";
        case Tab::Tree:
            return isPt ? juce::String::fromUTF8("Nesta aba, você navega pela estrutura hierárquica de pastas e volumes do projeto através de um mapa visual.")
                        : "On this tab, you can explore the directory structure and assets distribution through an interactive treemap.";
        case Tab::Storage:
            return isPt ? juce::String::fromUTF8("Nesta aba, você inspeciona discos físicos conectados, volumes externos e saúde dos dispositivos.")
                        : "On this tab, you can monitor connected storage drives, external volumes, and physical disk health.";
        case Tab::Backup:
            return isPt ? juce::String::fromUTF8("Nesta aba, você configura destinos de backup, verifica integridade e sincroniza arquivos pendentes com segurança.")
                        : "On this tab, you can configure backup destinations, review sync/orphan statuses, and replicate assets safely.";
        case Tab::Catalog:
            return isPt ? juce::String::fromUTF8("Nesta aba, você visualiza e gerencia todas as coleções associadas a este catálogo mestre.")
                        : "On this tab, you can browse and manage all sub-collections linked to this master catalog.";
    }
    return "";
}

juce::String BarraNavegacaoComponent::getTooltip() {
    auto pos = getMouseXYRelative();
    if (botaoAjuda_ && botaoAjuda_->getBounds().contains(pos)) {
        return obterTextoAjuda(selectedTab_);
    }
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    for (const auto& tab : tabs_) {
        if (tab.bounds.contains(pos)) {
            switch (tab.tab) {
                case Tab::Catalog: return isPt ? juce::String::fromUTF8("Gerenciar e explorar coleções vinculadas a este catálogo") : "Manage and explore collections linked to this catalog";
                case Tab::Intake: return isPt ? juce::String::fromUTF8("Gerenciar arquivos recém-ingeridos aguardando verificação para METADADOS") : "Manage recently ingested files awaiting verification to METADATA";
                case Tab::Grid: return isPt ? juce::String::fromUTF8("Navegar, filtrar e editar metadados de todos os arquivos") : "Browse, filter, and edit metadata of all assets";
                case Tab::Duplicates: return isPt ? juce::String::fromUTF8("Escanear e resolver arquivos duplicados no projeto ativo") : "Scan and resolve duplicate files in active project";
                case Tab::Analytics: return isPt ? juce::String::fromUTF8("Ver capacidade de armazenamento, gráficos e métricas de preservação") : "View storage capacity, charts, and preservation metrics";
                case Tab::Tree: return isPt ? juce::String::fromUTF8("Explorar estrutura de arquivos através do mapa visual de diretórios") : "Explore assets structure via vault directories treemap";
                case Tab::Backup: return isPt ? juce::String::fromUTF8("Planejar, verificar conflitos e consolidar pacote de publicação do backup") : "Plan, check conflicts, and consolidate backup publication package";
                case Tab::Storage: return isPt ? juce::String::fromUTF8("Inspecionar e gerenciar dispositivos físicos de disco e histórico") : "Inspect and manage physical disk devices and history";
            }
        }
    }
    return "";
}

} // namespace matriz::ui
