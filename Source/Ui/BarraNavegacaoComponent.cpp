#include "BarraNavegacaoComponent.h"
#include "Tokens.h"
#include "Moldura.h"
#include "Tablet.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

BarraNavegacaoComponent::BarraNavegacaoComponent() {
    reconstruirTabs();

    const auto& tk = tema();

    botaoAjuda_ = std::make_unique<juce::TextButton>("?");
    moldura::estilizarBotao(*botaoAjuda_, false);
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
        tabs_.push_back({ Tab::Catalog, isPt ? juce::String::fromUTF8("Coleções") : "Collections", {}, {}, false, 1 });
        tabs_.push_back({ Tab::Analytics, isPt ? juce::String::fromUTF8("Estrutura") : "Structure", {}, {}, false, 2 });
        tabs_.push_back({ Tab::Grid, isPt ? juce::String::fromUTF8("Metadados") : "Metadata", {}, {}, false, 3 });
        tabs_.push_back({ Tab::Storage, isPt ? juce::String::fromUTF8("Armazenamento") : "Storage", {}, {}, false, 4 });
        tabs_.push_back({ Tab::Backup, "Backup", {}, {}, false, 5 });
    } else {
        // As 5 abas principais pedidas: (1) Intake, (2) Structure, (3) Metadata, (4) Storage, (5) Backup.
        // Duplicates agora vive como sub-aba dentro de Structure (Folder Map, Space Map, Duplicates).
        tabs_.push_back({ Tab::Intake, isPt ? juce::String::fromUTF8("Ingestão") : "Intake", {}, {}, false, 1 });
        tabs_.push_back({ Tab::Analytics, isPt ? juce::String::fromUTF8("Estrutura") : "Structure", {}, {}, false, 2 });
        tabs_.push_back({ Tab::Grid, isPt ? juce::String::fromUTF8("Metadados") : "Metadata", {}, {}, false, 3 });
        tabs_.push_back({ Tab::Storage, isPt ? juce::String::fromUTF8("Armazenamento") : "Storage", {}, {}, false, 4 });
        tabs_.push_back({ Tab::Backup, "Backup", {}, {}, false, 5 });
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
    const auto pal = moldura::paleta();

    // Faixa do topo: cartão creme com linha inferior.
    g.fillAll(pal.janela);
    g.setColour(pal.linha);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);

    // Barra de etapas: pílulas creme com número em círculo; a etapa ativa é uma pílula grafite com texto branco.
    for (const auto& tab : tabs_) {
        const bool ativo = (tab.tab == selectedTab_);
        auto pilula = tab.bounds.toFloat().reduced(0.0f, 5.0f);
        const float raio = pilula.getHeight() * 0.5f;

        if (ativo) {
            g.setColour(pal.grafite);
            g.fillRoundedRectangle(pilula, raio);
        } else {
            g.setColour(tab.hover ? pal.poco.darker(0.03f) : pal.poco);
            g.fillRoundedRectangle(pilula, raio);
            g.setColour(pal.linha);
            g.drawRoundedRectangle(pilula.reduced(0.5f), raio, 1.0f);
        }

        // Número no círculo, seguido do nome; o conjunto fica centralizado na pílula.
        const float d = 18.0f, folga = 6.0f;
        const auto fonteNome = fonteUi(tk.tamanhoFonteCorpo, ativo ? juce::Font::bold : juce::Font::plain);
        const float textW = static_cast<float>(juce::GlyphArrangement::getStringWidthInt(fonteNome, tab.label));
        const float x0 = pilula.getCentreX() - (d + folga + textW) * 0.5f;
        const juce::Rectangle<float> selo(x0, pilula.getCentreY() - d * 0.5f, d, d);
        g.setColour(ativo ? pal.branco : pal.grafite);
        g.fillEllipse(selo);
        g.setColour(ativo ? pal.grafite : pal.branco);
        g.setFont(fonteUi(11.0f, juce::Font::bold));
        g.drawText(juce::String(tab.numero), selo, juce::Justification::centred, false);
        g.setColour(ativo ? pal.branco : pal.tinta);
        g.setFont(fonteNome);
        g.drawText(tab.label, juce::Rectangle<float>(x0 + d + folga, pilula.getY(), textW + 2.0f, pilula.getHeight()),
                   juce::Justification::centredLeft, false);

        // Seta fina cinza entre etapas sequenciais.
        if (!tab.sepBounds.isEmpty()) {
            const auto cx = static_cast<float>(tab.sepBounds.getCentreX());
            const auto cy = static_cast<float>(tab.sepBounds.getCentreY());
            juce::Path seta;
            seta.startNewSubPath(cx - 5.0f, cy);
            seta.lineTo(cx + 5.0f, cy);
            seta.startNewSubPath(cx + 1.5f, cy - 3.5f);
            seta.lineTo(cx + 5.5f, cy);
            seta.lineTo(cx + 1.5f, cy + 3.5f);
            g.setColour(tk.textoTerciario);
            g.strokePath(seta, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
        // 26px de folga + selo numérico (18px) e seu espaçamento (6px); mínimo de 88px
        int w = std::max(88, textW + 26 + 24);
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
            return isPt ? juce::String::fromUTF8("Nesta aba, você navega pela estrutura de pastas (Folder Map), distribuição de espaço (Space Map) e identificação de duplicatas (Duplicates).")
                        : "On this tab, you can explore the folder structure (Folder Map), space distribution (Space Map), and duplicate management (Duplicates).";
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
                case Tab::Analytics: return isPt ? juce::String::fromUTF8("Navegar pela estrutura do projeto: Folder Map, Space Map e Duplicates") : "Explore project structure: Folder Map, Space Map, and Duplicates";
                case Tab::Tree: return isPt ? juce::String::fromUTF8("Explorar estrutura de arquivos através do mapa visual de diretórios") : "Explore assets structure via vault directories treemap";
                case Tab::Backup: return isPt ? juce::String::fromUTF8("Planejar, verificar conflitos e consolidar pacote de publicação do backup") : "Plan, check conflicts, and consolidate backup publication package";
                case Tab::Storage: return isPt ? juce::String::fromUTF8("Inspecionar e gerenciar dispositivos físicos de disco e histórico") : "Inspect and manage physical disk devices and history";
            }
        }
    }
    return "";
}

} // namespace matriz::ui
