#include "HelpDialog.h"
#include "Tokens.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

// ==============================================================================
// HelpContentView implementation
// ==============================================================================

HelpContentView::HelpContentView() {
    setOpaque(false);
}

void HelpContentView::setContent(const juce::String& title, const juce::String& category, const juce::String& rawBody) {
    title_ = title;
    category_ = category;
    rawBody_ = rawBody;
    parseContent();
    repaint();
}

void HelpContentView::parseContent() {
    blocks_.clear();
    juce::StringArray lines;
    lines.addLines(rawBody_);

    for (int i = 0; i < lines.size(); ++i) {
        juce::String line = lines[i].trim();
        if (line.isEmpty()) continue;

        SectionBlock blk;
        if (line.startsWith("Q:") || line.startsWith("P:") || line.startsWith("[ FAQ ]") || line.startsWith("[ P ]") || line.startsWith("[ Q ]")) {
            blk.isFaqQuestion = true;
            blk.text = line;
        } else if (line.startsWith("R:") || line.startsWith("A:") || line.startsWith("[ R ]") || line.startsWith("[ A ]")) {
            blk.isFaqAnswer = true;
            blk.text = line;
        } else if (line.endsWith(":") && (line == line.toUpperCase() || line.startsWith("["))) {
            blk.isHeader = true;
            blk.text = line;
        } else if (line.startsWith("- ") || line.startsWith("* ") || line.startsWith(juce::String::fromUTF8("• "))) {
            blk.isBullet = true;
            if (line.startsWith("- ") || line.startsWith("* ")) {
                blk.text = line.substring(2).trim();
            } else {
                blk.text = line.substring(juce::String::fromUTF8("• ").length()).trim();
            }
        } else {
            blk.text = line;
        }
        blocks_.push_back(blk);
    }
}

int HelpContentView::getCalculatedHeight(int availableWidth) const {
    if (availableWidth < 100) availableWidth = 500;
    int h = 70; // Header area (Category badge + Title)

    juce::Font fontHeader(juce::FontOptions(14.0f, juce::Font::bold));
    juce::Font fontFaqQ(juce::FontOptions(13.5f, juce::Font::bold));
    juce::Font fontBody(juce::FontOptions(13.0f));

    int contentW = availableWidth - 32;

    for (const auto& blk : blocks_) {
        if (blk.isHeader) {
            h += 34;
        } else if (blk.isFaqQuestion) {
            juce::AttributedString as;
            as.append(blk.text, fontFaqQ);
            as.setWordWrap(juce::AttributedString::WordWrap::byWord);
            juce::TextLayout tl;
            tl.createLayout(as, (float)(contentW - 24));
            h += (int)tl.getHeight() + 24;
        } else if (blk.isFaqAnswer) {
            juce::AttributedString as;
            as.append(blk.text, fontBody);
            as.setWordWrap(juce::AttributedString::WordWrap::byWord);
            juce::TextLayout tl;
            tl.createLayout(as, (float)(contentW - 28));
            h += (int)tl.getHeight() + 16;
        } else if (blk.isBullet) {
            juce::AttributedString as;
            as.append(blk.text, fontBody);
            as.setWordWrap(juce::AttributedString::WordWrap::byWord);
            juce::TextLayout tl;
            tl.createLayout(as, (float)(contentW - 32));
            h += (int)tl.getHeight() + 10;
        } else {
            juce::AttributedString as;
            as.append(blk.text, fontBody);
            as.setWordWrap(juce::AttributedString::WordWrap::byWord);
            juce::TextLayout tl;
            tl.createLayout(as, (float)contentW);
            h += (int)tl.getHeight() + 12;
        }
    }
    return h + 40;
}

void HelpContentView::paint(juce::Graphics& g) {
    const auto& tk = tema();
    int w = getWidth();
    int y = 16;
    int leftMargin = 16;
    int rightMargin = 16;
    int contentW = w - leftMargin - rightMargin;

    // 1. Category Tag / Badge
    if (!category_.isEmpty()) {
        juce::Font catFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.setFont(catFont);
        int badgeW = juce::GlyphArrangement::getStringWidthInt(catFont, category_.toUpperCase()) + 16;
        int badgeH = 18;
        juce::Rectangle<int> badgeRect(leftMargin, y, badgeW, badgeH);

        g.setColour(tk.acento.withAlpha(0.2f));
        g.fillRoundedRectangle(badgeRect.toFloat(), 4.0f);
        g.setColour(tk.acento);
        g.drawRoundedRectangle(badgeRect.toFloat(), 4.0f, 1.0f);

        g.setColour(tk.acento);
        g.drawText(category_.toUpperCase(), badgeRect, juce::Justification::centred);
        y += badgeH + 6;
    }

    // 2. Title
    g.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
    g.setColour(tk.textoPrimario);
    g.drawText(title_, leftMargin, y, contentW, 26, juce::Justification::left, true);
    y += 32;

    // Divider under title
    g.setColour(tk.borda.withAlpha(0.5f));
    g.drawLine((float)leftMargin, (float)y, (float)(w - rightMargin), (float)y, 1.0f);
    y += 16;

    // 3. Render Blocks
    juce::Font fontHeader(juce::FontOptions(13.5f, juce::Font::bold));
    juce::Font fontFaqQ(juce::FontOptions(13.5f, juce::Font::bold));
    juce::Font fontBody(juce::FontOptions(13.0f));

    for (const auto& blk : blocks_) {
        if (blk.isHeader) {
            y += 8;
            g.setFont(fontHeader);
            g.setColour(tk.acento);
            g.drawText(blk.text, leftMargin, y, contentW, 22, juce::Justification::left, true);
            y += 24;
        } else if (blk.isFaqQuestion) {
            juce::AttributedString as;
            as.append(blk.text, fontFaqQ, tk.textoPrimario);
            as.setWordWrap(juce::AttributedString::WordWrap::byWord);
            juce::TextLayout tl;
            tl.createLayout(as, (float)(contentW - 24));
            int boxH = (int)tl.getHeight() + 16;

            juce::Rectangle<float> qBox((float)leftMargin, (float)y, (float)contentW, (float)boxH);
            g.setColour(tk.painelAlt);
            g.fillRoundedRectangle(qBox, 6.0f);
            g.setColour(tk.borda);
            g.drawRoundedRectangle(qBox, 6.0f, 1.0f);

            // Left accent pill on card
            g.setColour(tk.acento);
            g.fillRoundedRectangle(qBox.removeFromLeft(4.0f), 2.0f);

            tl.draw(g, juce::Rectangle<float>((float)(leftMargin + 14), (float)(y + 8), (float)(contentW - 24), (float)tl.getHeight()));
            y += boxH + 8;
        } else if (blk.isFaqAnswer) {
            juce::AttributedString as;
            as.append(blk.text, fontBody, tk.textoSecundario);
            as.setWordWrap(juce::AttributedString::WordWrap::byWord);
            juce::TextLayout tl;
            tl.createLayout(as, (float)(contentW - 28));

            tl.draw(g, juce::Rectangle<float>((float)(leftMargin + 14), (float)y, (float)(contentW - 28), (float)tl.getHeight()));
            y += (int)tl.getHeight() + 14;
        } else if (blk.isBullet) {
            // Crisp vector dot bullet
            g.setColour(tk.acento);
            g.fillEllipse((float)(leftMargin + 4), (float)(y + 6), 5.0f, 5.0f);

            juce::AttributedString as;
            as.append(blk.text, fontBody, tk.textoPrimario);
            as.setWordWrap(juce::AttributedString::WordWrap::byWord);
            juce::TextLayout tl;
            tl.createLayout(as, (float)(contentW - 24));

            tl.draw(g, juce::Rectangle<float>((float)(leftMargin + 18), (float)y, (float)(contentW - 24), (float)tl.getHeight()));
            y += (int)tl.getHeight() + 8;
        } else {
            juce::AttributedString as;
            as.append(blk.text, fontBody, tk.textoPrimario);
            as.setWordWrap(juce::AttributedString::WordWrap::byWord);
            juce::TextLayout tl;
            tl.createLayout(as, (float)contentW);

            tl.draw(g, juce::Rectangle<float>((float)leftMargin, (float)y, (float)contentW, (float)tl.getHeight()));
            y += (int)tl.getHeight() + 10;
        }
    }
}

void HelpContentView::resized() {
}

// ==============================================================================
// HelpDialog implementation
// ==============================================================================

HelpDialog::HelpDialog() {
    const auto& tk = tema();
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    lblTitulo_ = std::make_unique<juce::Label>("", isPt ? juce::String::fromUTF8("GUIA DO USUÁRIO E AJUDA DO BKR MATRIZ") : juce::String::fromUTF8("BKR MATRIZ USER GUIDE & HELP"));
    lblTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
    lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    addAndMakeVisible(*lblTitulo_);

    searchEditor_ = std::make_unique<juce::TextEditor>();
    searchEditor_->setTextToShowWhenEmpty(isPt ? juce::String::fromUTF8("Buscar tópicos, FAQ, fluxos ou funções (ex: backup, intake, watermark, duplicatas)...")
                                              : juce::String::fromUTF8("Search topics, FAQ, workflows, or tools (e.g. backup, intake, watermark, duplicates)..."), tk.textoTerciario);
    searchEditor_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
    searchEditor_->setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
    searchEditor_->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
    searchEditor_->setColour(juce::TextEditor::outlineColourId, tk.borda);
    searchEditor_->setColour(juce::TextEditor::focusedOutlineColourId, tk.acento);
    searchEditor_->onTextChange = [this] { aplicarFiltro(); };
    addAndMakeVisible(*searchEditor_);

    topicList_ = std::make_unique<juce::ListBox>("HelpTopicList", this);
    topicList_->setColour(juce::ListBox::backgroundColourId, tk.painel);
    topicList_->setColour(juce::ListBox::outlineColourId, tk.borda);
    topicList_->setRowHeight(46);
    addAndMakeVisible(*topicList_);

    contentViewport_ = std::make_unique<juce::Viewport>();
    contentViewport_->setScrollBarsShown(true, false);
    
    contentView_ = std::make_unique<HelpContentView>();
    contentViewport_->setViewedComponent(contentView_.get(), false);
    addAndMakeVisible(*contentViewport_);

    btnClose_ = std::make_unique<juce::TextButton>(isPt ? juce::String::fromUTF8("FECHAR") : juce::String::fromUTF8("CLOSE"));
    btnClose_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
    btnClose_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    btnClose_->onClick = [this] {
        if (aoFechar) aoFechar();
    };
    addAndMakeVisible(*btnClose_);

    inicializarTopicos();
    aplicarFiltro();

    setSize(920, 640);
}

void HelpDialog::inicializarTopicos() {
    allTopics_.clear();

    // 1. Visão Geral
    allTopics_.push_back({
        "workflow_overview",
        juce::String::fromUTF8("1. Visão Geral e Fluxo de Trabalho"),
        juce::String::fromUTF8("1. Workflow & System Overview"),
        juce::String::fromUTF8("Conceitos"),
        juce::String::fromUTF8("Concepts"),
        juce::String::fromUTF8(
            "O BKR Matriz é um ecossistema profissional projetado para catalogação técnica, curadoria de ativos de áudio/mídia e preservação digital segura a longo prazo.\n\n"
            "MODOS DE OPERAÇÃO:\n"
            "- Coleção (.mtz): Projeto focado em uma sessão, álbum, gravação, evento ou acervo específico com banco de dados SQLite local.\n"
            "- Catálogo (.bkm): Hub unificado master que consolida e indexa múltiplas Coleções em uma única interface global de consulta.\n\n"
            "PIPELINE PADRÃO DE PRESERVAÇÃO:\n"
            "- 1. Ingestão (Intake): Importação segura e leitura técnica de mídias (áudio, vídeo, imagens, documentos) com verificação de integridade.\n"
            "- 2. Curadoria e Ficha (Grid & Ficha): Revisão técnica, audição analítica, marcação com Tag Chips e preenchimento de metadados arquivísticos.\n"
            "- 3. Estruturação Virtual (Treemap): Organização de diretórios e categorias em árvore visual interativa.\n"
            "- 4. Verificação de Integridade (Duplicatas & Fixity): Detecção de duplicatas bit a bit e cálculo de SHA-256.\n"
            "- 5. Preservação & Backup (Backup & Vaults): Cópia verificada bit a bit para múltiplos Vaults e publicação estática HTML5.\n"
            "- 6. Monitoramento de Armazenamento (Storage): Diagnósticos SMART de saúde dos discos e reconexão automática de ativos offline."
        ),
        juce::String::fromUTF8(
            "BKR Matriz is a professional ecosystem designed for technical cataloging, media asset curation, and long-term secure digital preservation.\n\n"
            "OPERATING MODES:\n"
            "- Collection (.mtz): Project tailored to a specific recording session, album, event, or independent archive with local SQLite storage.\n"
            "- Catalog (.bkm): Unified master hub that consolidates and indexes multiple Collections in a single global interface.\n\n"
            "STANDARD PRESERVATION PIPELINE:\n"
            "- 1. Intake: Safe ingestion and technical analysis of media assets (audio, video, images, documents) with integrity checks.\n"
            "- 2. Curation & Ficha (Grid & Ficha): Technical review, analytical playback, Tag Chips categorization, and archival metadata editing.\n"
            "- 3. Virtual Hierarchy (Treemap): Visual directory and folder tree organization.\n"
            "- 4. Integrity & Duplicates (Duplicatas & Fixity): Bit-for-bit duplicate detection and SHA-256 fixity calculation.\n"
            "- 5. Preservation & Backup (Backup & Vaults): Bit-for-bit verified backups to multiple Vaults and standalone HTML5 export.\n"
            "- 6. Storage Diagnostics (Storage): Physical SMART drive health monitoring and automatic offline asset reconnection."
        ),
        "workflow overview conceito fluxo colecao catalogo pipeline preservacao"
    });

    // 2. Ingestão & Intake
    allTopics_.push_back({
        "intake_ingest",
        juce::String::fromUTF8("2. Ingestão de Mídias (Intake)"),
        juce::String::fromUTF8("2. Media Ingestion (Intake)"),
        juce::String::fromUTF8("Importação"),
        juce::String::fromUTF8("Import"),
        juce::String::fromUTF8(
            "A aba INTAKE realiza a entrada controlada, imutável e técnica de arquivos para o acervo.\n\n"
            "RECURSOS PRINCIPAIS:\n"
            "- Importação em Lote: Arraste pastas inteiras ou selecione arquivos avulsos de múltiplos volumes.\n"
            "- Leitura Técnica Profunda: Extração de taxa de amostragem, bit depth, canais, codec, medições EBU R128 Loudness (LUFS integrado e LRA), resoluções e metadados EXIF/IPTC/XMP.\n"
            "- Imutabilidade na Origem: O BKR Matriz nunca modifica ou corrompe arquivos originais durante a leitura e indexação.\n"
            "- Extração Automática de Metadados: Captura metadados embutidos e sugere preenchimento automático das fichas arquivísticas.\n"
            "- Painel de Inconsistências: Identifica e lista instantaneamente arquivos ilegíveis, extensões corrompidas ou caminhos inacessíveis."
        ),
        juce::String::fromUTF8(
            "The INTAKE workspace handles controlled, immutable, and technical media ingestion into the project.\n\n"
            "KEY FEATURES:\n"
            "- Batch Ingestion: Drag entire directories or browse individual files across multiple storage volumes.\n"
            "- Deep Technical Analysis: Automatic extraction of sample rate, bit depth, channel configuration, codecs, EBU R128 Loudness (LUFS-I and LRA), video dimensions, and EXIF/IPTC/XMP metadata.\n"
            "- Source Immutability: BKR Matriz strictly operates read-only on source assets, never modifying originals during ingestion.\n"
            "- Embedded Metadata Extraction: Gathers native container tags to populate archival metadata fields.\n"
            "- Inconsistency Inspector: Automatically flags unreadable files, broken extensions, or offline volumes."
        ),
        "intake ingest importacao arquivos leitura tecnica lufs exif metadata"
    });

    // 3. Mosaico & Grid
    allTopics_.push_back({
        "grid_mosaic",
        juce::String::fromUTF8("3. Mosaico, Grade e Curadoria (Grid)"),
        juce::String::fromUTF8("3. Mosaic Grid & Curation (Grid)"),
        juce::String::fromUTF8("Curadoria"),
        juce::String::fromUTF8("Curation"),
        juce::String::fromUTF8(
            "A aba GRID (Mosaico) é o espaço central para curadoria, audição e enriquecimento metadados.\n\n"
            "RECURSOS PRINCIPAIS:\n"
            "- Modos Grade e Lista: Cartões visuais em alta resolução com selos de status em tempo real (Online, Sem Backup, QC OK, Falhas).\n"
            "- Ficha Arquivística Lateral: Edição de campos formais (autoria, direitos autorais, datação histórica, instrumentos, localização).\n"
            "- Sistema de Tag Chips: Aplicação instantânea de palavras-chave e categorias nos itens selecionados.\n"
            "- Barra de Filtros Rápidos: Filtragem instantânea por tipo de mídia (Áudio, Vídeo, Fotos, Docs, Sessões DAW), ano, tags e estado de preservação.\n"
            "- Ações em Lote: Seleção múltipla para edição compartilhada, atribuição de pastas virtuais e exclusão de itens do backup."
        ),
        juce::String::fromUTF8(
            "The GRID workspace is the core hub for visual exploration, playback, and metadata curation.\n\n"
            "KEY FEATURES:\n"
            "- Mosaic and Table Views: High-resolution cards with real-time status badges (Online, No Backup, QC OK, Issues).\n"
            "- Archival Ficha Side Panel: Edit archival fields (authorship, copyright, historical dates, instruments, location).\n"
            "- Tag Chips System: Instantly assign or remove categorical tags on selected assets.\n"
            "- Fast Filtering Bar: Filter on-the-fly by media type (Audio, Video, Photo, Docs, DAW Sessions), release year, tags, or preservation state.\n"
            "- Batch Operations: Multi-select assets for bulk tag updates, virtual folder allocation, and backup exclusion."
        ),
        "grid mosaico grade curadoria ficha tags filtros miniaturas"
    });

    // 4. Players e Pré-visualização
    allTopics_.push_back({
        "players_preview",
        juce::String::fromUTF8("4. Players e Pré-visualização Integrada"),
        juce::String::fromUTF8("4. Integrated Players & Preview"),
        juce::String::fromUTF8("Reprodução"),
        juce::String::fromUTF8("Playback"),
        juce::String::fromUTF8(
            "O BKR Matriz conta com players nativos de alta performance integrados para todos os formatos de mídia:\n\n"
            "RECURSOS:\n"
            "- Player de Áudio: Reprodução bit-perfect para WAV, FLAC, AIFF, MP3, AAC, OGG com forma de onda interativa, medidores de pico e transporte completo.\n"
            "- Player de Vídeo: Motor AVFoundation acelerado por hardware com navegação quadro a quadro e exibição de timecode preciso.\n"
            "- Visualizador de Documentos: Leitor nativo de PDF com suporte a paginação, zoom e visualização de documentos técnicos.\n"
            "- Visualizador de Imagens: Renderização otimizada para fotos e artes em alta resolução."
        ),
        juce::String::fromUTF8(
            "BKR Matriz features built-in high-performance playback engines across all supported media formats:\n\n"
            "FEATURES:\n"
            "- Audio Player: Bit-perfect audio playback for WAV, FLAC, AIFF, MP3, AAC, OGG with interactive waveform, peak metering, and transport controls.\n"
            "- Video Player: Hardware-accelerated AVFoundation engine with frame-accurate scrubbing and millisecond timecode display.\n"
            "- Document Viewer: Native PDF engine with page navigation, zoom, and technical document inspection.\n"
            "- Image Viewer: High-resolution hardware-rendered graphics viewer."
        ),
        "player audio video pdf preview reproducao waveform visualizacao"
    });

    // 5. Treemap & Mapa de Pastas
    allTopics_.push_back({
        "treemap_folders",
        juce::String::fromUTF8("5. Treemap e Organização Hierárquica"),
        juce::String::fromUTF8("5. Treemap & Hierarchical Structure"),
        juce::String::fromUTF8("Estrutura"),
        juce::String::fromUTF8("Structure"),
        juce::String::fromUTF8(
            "A aba TREEMAP oferece um mapa visual baseado em nós para estruturação lógica do acervo.\n\n"
            "COMO UTILIZAR:\n"
            "- Criar Nova Pasta: Clique com o botão direito em qualquer área vazia do canvas e selecione 'NOVA PASTA' (ou use o botão superior '+ NOVA PASTA').\n"
            "- Criar Subpastas: Clique com o botão direito sobre um nó existente e selecione 'Nova Subpasta'.\n"
            "- Reorganizar Estrutura: Arraste nós e conecte entradas e saídas para montar a hierarquia desejada.\n"
            "- Zoom e Navegação: Use a roda do mouse para aproximar/afastar e arraste o espaço vazio para mover (pan).\n"
            "- Filtrar na Grade: Clique com o botão direito em uma pasta para abrir e isolar seus arquivos na Grade."
        ),
        juce::String::fromUTF8(
            "The TREEMAP workspace provides a node-based visual tree for directory organization.\n\n"
            "HOW TO USE:\n"
            "- Create New Folder: Right-click anywhere in empty canvas space and choose 'NEW FOLDER' (or click '+ NEW FOLDER' on the top toolbar).\n"
            "- Create Subfolders: Right-click an existing node and select 'New Subfolder'.\n"
            "- Reorganize Hierarchy: Drag nodes and connect sockets to model the desired directory hierarchy.\n"
            "- Zoom and Pan: Use mouse wheel to zoom in/out and drag empty canvas to pan.\n"
            "- Filter in Grid: Right-click any folder node to filter and inspect its assets in the Grid."
        ),
        "treemap mapa pastas arvore new folder subpasta organizacao"
    });

    // 6. Duplicatas & Integridade
    allTopics_.push_back({
        "duplicates_fixity",
        juce::String::fromUTF8("6. Duplicatas e Integridade (Fixity)"),
        juce::String::fromUTF8("6. Duplicates & Checksum Integrity"),
        juce::String::fromUTF8("Integridade"),
        juce::String::fromUTF8("Integrity"),
        juce::String::fromUTF8(
            "A aba DUPLICATAS audita o acervo para detectar redundâncias e proteger a integridade dos dados.\n\n"
            "RECURSOS:\n"
            "- Varredura Bit a Bit: Comparação simultânea de hash SHA-256 e contagem exata de bytes.\n"
            "- Comparativo Lado a Lado: Agrupamento visual das duplicatas com caminhos originais, datas e tamanhos.\n"
            "- Consolidação Inteligente: Permite eleger o arquivo master oficial e desmarcar cópias excedentes do backup, poupando espaço valioso."
        ),
        juce::String::fromUTF8(
            "The DUPLICATES workspace audits the archive for redundant files and protects data fixity.\n\n"
            "FEATURES:\n"
            "- Bit-for-Bit Audit: Simultaneous verification of SHA-256 cryptographic hashes and exact byte counts.\n"
            "- Side-by-Side Comparison: Visual grouping of duplicate instances showing original paths, timestamps, and sizes.\n"
            "- Smart Consolidation: Choose the designated master asset and exclude redundant copies from backup storage."
        ),
        "duplicatas checksum sha256 fixity integridade espaco consolidacao"
    });

    // 7. Analytics & Mapas
    allTopics_.push_back({
        "analytics_maps",
        juce::String::fromUTF8("7. Analytics, Estatísticas e Mapa Global"),
        juce::String::fromUTF8("7. Analytics, Statistics & Global Map"),
        juce::String::fromUTF8("Relatórios"),
        juce::String::fromUTF8("Reports"),
        juce::String::fromUTF8(
            "A aba ANALYTICS sintetiza métricas essenciais e inteligência sobre a totalidade do acervo.\n\n"
            "PAINÉIS DISPONÍVEIS:\n"
            "- Distribuição de Mídias e Codecs: Gráficos de proporção entre Áudio, Vídeo, Fotos e Documentos.\n"
            "- Métricas de Armazenamento: Análise volumétrica de consumo em Gigabytes e contagem de itens por categoria.\n"
            "- GeoMap Mundial: Mapa interativo baseado em coordenadas GPS extraídas dos metadados EXIF das mídias.\n"
            "- Exportação Analítica: Geração de relatórios consolidados para prestação de contas arquivística."
        ),
        juce::String::fromUTF8(
            "The ANALYTICS workspace synthesizes archive volume metrics and spatial distribution.\n\n"
            "AVAILABLE PANELS:\n"
            "- Media & Codec Breakdown: Proportional charts for Audio, Video, Photo, and Document categories.\n"
            "- Storage Consumption: Volumetric byte metrics and asset density per folder and medium.\n"
            "- World GeoMap: Interactive geographic map plotting GPS coordinates extracted from EXIF metadata.\n"
            "- Analytical Export: Export structured summary reports for archival curation records."
        ),
        "analytics estatisticas mapa geomap relatorios gps exif volumetria"
    });

    // 8. Backup & Publicação HTML
    allTopics_.push_back({
        "backup_html",
        juce::String::fromUTF8("8. Backup, Vaults e Publicação HTML"),
        juce::String::fromUTF8("8. Backup, Vaults & HTML Publication"),
        juce::String::fromUTF8("Preservação"),
        juce::String::fromUTF8("Preservation"),
        juce::String::fromUTF8(
            "A aba BACKUP gerencia a replicação física para unidades externas e a exportação pública.\n\n"
            "FLUXOS DE PRESERVAÇÃO:\n"
            "- Destino de Vaults: Configure múltiplos discos externos como cofres seguros de preservação.\n"
            "- Verificação Criptográfica Bit a Bit: Cada arquivo copiado é recalculado e comparado contra o hash original antes de concluir.\n"
            "- Botão PUBLICAR SITE HTML: Gera um site HTML5 estático, autocontido e moderno, com reprodutor de áudio, galeria e fichas técnicas completas para compartilhamento web sem dependências."
        ),
        juce::String::fromUTF8(
            "The BACKUP workspace manages physical media replication and public web export.\n\n"
            "PRESERVATION WORKFLOWS:\n"
            "- Vaults Destination: Register and configure multiple external drives as official preservation vaults.\n"
            "- Bit-for-Bit Fixity Check: Every copied asset has its SHA-256 recalculated and verified against the master hash before completion.\n"
            "- PUBLISH HTML Button: Generates a self-contained, responsive HTML5 portal featuring an embedded audio player, gallery, and archival metadata for offline or web distribution."
        ),
        "backup vaults publicacao html preservacao copias seguras"
    });

    // 9. Diagnóstico SMART & Armazenamento
    allTopics_.push_back({
        "storage_smart",
        juce::String::fromUTF8("9. Diagnóstico SMART e Armazenamento"),
        juce::String::fromUTF8("9. SMART Diagnostics & Storage"),
        juce::String::fromUTF8("Hardware"),
        juce::String::fromUTF8("Hardware"),
        juce::String::fromUTF8(
            "A aba STORAGE monitora a integridade dos discos físicos e permite reconectar ativos ausentes.\n\n"
            "FUNCIONALIDADES:\n"
            "- Telemetria SMART: Monitoramento de temperatura, horas de uso, setores defeituosos e risco de falha de SSDs e HDs.\n"
            "- Asset Relink Engine: Se um disco for desconectado ou arquivos forem movidos, o assistente localiza e reconecta todos os caminhos automaticamente."
        ),
        juce::String::fromUTF8(
            "The STORAGE workspace monitors physical drive reliability and resolves offline assets.\n\n"
            "FEATURES:\n"
            "- SMART Telemetry: Real-time monitoring of disk temperature, power-on hours, bad sectors, and failure probability.\n"
            "- Asset Relink Engine: If external drives are swapped or folders relocated, the relink engine discovers and updates paths automatically."
        ),
        "storage smart saude discos ssd hd relink reconexao hardware"
    });

    // 10. Atalhos de Teclado
    allTopics_.push_back({
        "shortcuts_keys",
        juce::String::fromUTF8("10. Atalhos de Teclado e Produtividade"),
        juce::String::fromUTF8("10. Keyboard Shortcuts & Productivity"),
        juce::String::fromUTF8("Atalhos"),
        juce::String::fromUTF8("Shortcuts"),
        juce::String::fromUTF8(
            "ATALHOS GLOBAIS:\n"
            "- Cmd+S: Salvar Coleção / Catálogo atual.\n"
            "- Cmd+Shift+S: Salvar Coleção Como...\n"
            "- Cmd+Z: Desfazer última alteração.\n"
            "- Barra de Espaço: Play / Pause no player de áudio ou vídeo.\n"
            "- R: Renomear item selecionado.\n"
            "- C: Excluir/Restaurar item selecionado do backup.\n"
            "- H: Marcar/desmarcar item para publicação HTML.\n"
            "- K: Marcar/desmarcar item para exportação ZIP.\n"
            "- P: Marcar/desmarcar item para impressão (Print).\n"
            "- Teclas 1 a 8: Navegação rápida entre as abas da barra superior.\n"
            "- F: Focar no campo de busca rápida."
        ),
        juce::String::fromUTF8(
            "GLOBAL SHORTCUTS:\n"
            "- Cmd+S: Save current Collection / Catalog.\n"
            "- Cmd+Shift+S: Save Collection As...\n"
            "- Cmd+Z: Undo last change.\n"
            "- Spacebar: Play / Pause audio or video playback.\n"
            "- R: Rename selected item.\n"
            "- C: Exclude/Include selected item from backup.\n"
            "- H: Toggle HTML Publish mark on selected items.\n"
            "- K: Toggle ZIP Export mark on selected items.\n"
            "- P: Toggle Print mark on selected items.\n"
            "- Keys 1 to 8: Fast navigation across upper workspace tabs.\n"
            "- F: Focus global search filter bar."
        ),
        "atalhos teclado shortcuts teclas produtividade cmd espaco salvar"
    });

    // 11. Perguntas Frequentes (FAQ)
    allTopics_.push_back({
        "faq_section",
        juce::String::fromUTF8("11. Perguntas Frequentes (FAQ)"),
        juce::String::fromUTF8("11. Frequently Asked Questions (FAQ)"),
        juce::String::fromUTF8("Dúvidas"),
        juce::String::fromUTF8("FAQ"),
        juce::String::fromUTF8(
            "Q: Qual a diferença fundamental entre Coleção (.mtz) e Catálogo (.bkm)?\n"
            "R: Uma Coleção (.mtz) é o projeto ativo de preservação, contendo mídias, fichas técnicas, edições e definições de backup. O Catálogo (.bkm) é um índice master unificado que consolida e pesquisa através de dezenas ou centenas de Coleções sem precisar abri-las uma a uma.\n\n"
            "Q: O BKR Matriz altera meus arquivos originais durante a ingestão?\n"
            "R: Não. O BKR Matriz trabalha sob estrita política de imutabilidade na origem. Ele lê os arquivos em modo somente-leitura, extrai os metadados técnicos e gera miniaturas no banco de dados sem modificar nem 1 byte do arquivo original.\n\n"
            "Q: Como funciona o cálculo de Fixity e SHA-256?\n"
            "R: O SHA-256 é uma impressão digital criptográfica única gerada para cada arquivo. Durante a ingestão e nos backups para Vaults, o hash é verificado bit a bit. Se 1 único bit se corromper ao longo dos anos (bit rot), o BKR Matriz alerta imediatamente.\n\n"
            "Q: Como utilizar a ferramenta de Marca d'Água em Lote (Batch Watermark)?\n"
            "R: Acesse o menu 'Ferramentas > Marca d'Água em Lote...'. Você pode carregar as fotos da grade atual ou arrastar um lote de fotos, selecionar o arquivo da sua logo, regular a opacidade e o tamanho com preview em tempo real e salvar na pasta de origem com sufixo '-w' ou em uma nova pasta.\n\n"
            "Q: O que é e para que serve a opção 'Publicar Site HTML'?\n"
            "R: Ao clicar em 'PUBLISH HTML' na aba de Backup ou Catálogo, o sistema gera uma pasta web completa e estática contendo um site com player de áudio interativo, galeria de fotos e fichas técnicas. Você pode abrir o 'index.html' em qualquer navegador ou hospedá-lo em qualquer servidor web.\n\n"
            "Q: Como gerenciar e remover arquivos duplicados?\n"
            "R: Na aba DUPLICATAS, o sistema agrupa automaticamente todos os arquivos idênticos por SHA-256. Você pode eleger o arquivo principal de preservação e desmarcar as cópias excedentes do backup.\n\n"
            "Q: O que fazer quando um disco externo estiver desconectado e os arquivos ficarem offline?\n"
            "R: O BKR Matriz possui o 'Asset Relink Engine'. Conecte o disco ou informe a nova pasta de localização em 'Storage > Relink' para que todos os vínculos sejam restabelecidos automaticamente.\n\n"
            "Q: Quais são as limitações da versão BKR Matriz Trial?\n"
            "R: A versão Trial é destinada exclusivamente para avaliação técnica (NOT FOR SALE). Ela oferece acesso a todas as ferramentas principais, porém possui um limite máximo de itens e cópias por sessão para avaliação antes da aquisição da licença definitiva em www.bunkeranalog.com/bkr.\n\n"
            "Q: Como alternar entre o Modo Escuro (Dark Theme) e Modo Claro (Light)?\n"
            "R: Vá no menu superior 'Preferências > Preferências...' e escolha entre o Tema Escuro ou Tema Claro. A interface atualiza instantaneamente com alto contraste de cores e bordas nítidas."
        ),
        juce::String::fromUTF8(
            "Q: What is the fundamental difference between a Collection (.mtz) and a Catalog (.bkm)?\n"
            "A: A Collection (.mtz) is an active preservation project containing media assets, technical fichas, virtual hierarchies, and backup configurations. A Catalog (.bkm) is a master index that consolidates and queries across multiple independent Collections from a single global view.\n\n"
            "Q: Does BKR Matriz alter or modify my original files during intake?\n"
            "A: No. BKR Matriz operates under strict source immutability principles. All assets are inspected in read-only mode, generating thumbnails and extracting technical metadata without touching a single byte of your original files.\n\n"
            "Q: How does SHA-256 Fixity verification work?\n"
            "A: SHA-256 is an industry-standard cryptographic fingerprint computed for every ingested asset. When performing backups to Vaults, hashes are compared bit-for-bit. If silent corruption or bit rot occurs, BKR Matriz flags the inconsistency immediately.\n\n"
            "Q: How do I use the Image Batch Watermark tool?\n"
            "A: Go to the menu 'Tools > Image Batch Watermark...'. You can load images directly from the active project grid or drag and drop image files, choose your logo graphic, adjust opacity and scale with real-time preview, and export either with a '-w' suffix in the source folder or to a custom output directory.\n\n"
            "Q: What is the 'Publish HTML' feature and how is it used?\n"
            "A: Clicking 'PUBLISH HTML' on the Backup or Catalog workspace generates a self-contained, responsive HTML5 portal complete with an audio player, image galleries, and archival fichas. You can view 'index.html' locally in any browser or host it on any web server.\n\n"
            "Q: How can I manage and eliminate duplicate files?\n"
            "A: In the DUPLICATES tab, assets with identical SHA-256 hashes are automatically grouped side-by-side. You can select your primary master asset and exclude redundant copies from backup storage.\n\n"
            "Q: What happens if an external drive is disconnected or assets go offline?\n"
            "A: BKR Matriz includes the 'Asset Relink Engine'. Simply reconnect the drive or point to the new folder in 'Storage > Relink' to restore asset links automatically.\n\n"
            "Q: What are the terms of the BKR Matriz Trial edition?\n"
            "A: The Trial edition is provided for technical evaluation (NOT FOR SALE). It includes full access to core features with evaluation quotas. Commercial licenses can be obtained at www.bunkeranalog.com/bkr.\n\n"
            "Q: How do I toggle between Dark Theme and Light Theme?\n"
            "A: Open 'Preferences > Preferences...' from the menu bar and choose your desired Theme (Dark / Light). The user interface will update immediately with calibrated contrast and defined slate dividers."
        ),
        "faq duvidas perguntas frequentes como funciona backup trial watermark html relink sha256 diferenca"
    });
}

void HelpDialog::aplicarFiltro() {
    juce::String q = searchEditor_ ? searchEditor_->getText().trim().toLowerCase() : juce::String();
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    filteredIndices_.clear();
    for (int i = 0; i < (int)allTopics_.size(); ++i) {
        if (q.isEmpty()) {
            filteredIndices_.push_back(i);
            continue;
        }
        const auto& t = allTopics_[i];
        juce::String titulo = (isPt ? t.titlePt : t.titleEn).toLowerCase();
        juce::String cat = (isPt ? t.categoryPt : t.categoryEn).toLowerCase();
        juce::String cont = (isPt ? t.contentPt : t.contentEn).toLowerCase();
        juce::String kw = t.keywords.toLowerCase();

        if (titulo.contains(q) || cat.contains(q) || cont.contains(q) || kw.contains(q)) {
            filteredIndices_.push_back(i);
        }
    }

    if (topicList_) {
        topicList_->updateContent();
        if (!filteredIndices_.empty()) {
            topicList_->selectRow(0);
        } else {
            if (contentView_) contentView_->setContent("", "", isPt ? juce::String::fromUTF8("Nenhum tópico encontrado para a busca.") : "No topics found for this query.");
        }
    }
    atualizarVisualizacaoConteudo();
}

int HelpDialog::getNumRows() {
    return (int)filteredIndices_.size();
}

void HelpDialog::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (rowNumber < 0 || rowNumber >= (int)filteredIndices_.size()) return;

    const auto& tk = tema();
    int topicIdx = filteredIndices_[rowNumber];
    const auto& topic = allTopics_[topicIdx];
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    juce::Rectangle<int> bounds(0, 0, width, height);

    if (rowIsSelected) {
        g.setColour(tk.acento.withAlpha(0.18f));
        g.fillRoundedRectangle(bounds.reduced(3, 2).toFloat(), 4.0f);
        g.setColour(tk.acento);
        g.drawRoundedRectangle(bounds.reduced(3, 2).toFloat(), 4.0f, 1.0f);
    }

    juce::String titulo = isPt ? topic.titlePt : topic.titleEn;
    juce::String cat = isPt ? topic.categoryPt : topic.categoryEn;

    g.setFont(juce::Font(juce::FontOptions(13.0f, rowIsSelected ? juce::Font::bold : juce::Font::plain)));
    g.setColour(rowIsSelected ? tk.acento : tk.textoPrimario);
    g.drawText(titulo, 12, 6, width - 24, 20, juce::Justification::left, true);

    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.setColour(tk.textoTerciario);
    g.drawText(cat.toUpperCase(), 12, 24, width - 24, 16, juce::Justification::left, true);

    g.setColour(tk.borda.withAlpha(0.25f));
    g.drawLine(8.0f, (float)(height - 1), (float)(width - 8), (float)(height - 1));
}

void HelpDialog::selectedRowsChanged(int) {
    atualizarVisualizacaoConteudo();
}

void HelpDialog::atualizarVisualizacaoConteudo() {
    int sel = topicList_ ? topicList_->getSelectedRow() : -1;
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    if (sel >= 0 && sel < (int)filteredIndices_.size()) {
        int topicIdx = filteredIndices_[sel];
        const auto& topic = allTopics_[topicIdx];
        juce::String titulo = isPt ? topic.titlePt : topic.titleEn;
        juce::String cat = isPt ? topic.categoryPt : topic.categoryEn;
        juce::String texto = isPt ? topic.contentPt : topic.contentEn;

        if (contentView_) {
            contentView_->setContent(titulo, cat, texto);
        }
    } else if (filteredIndices_.empty()) {
        if (contentView_) {
            contentView_->setContent("", "", isPt ? juce::String::fromUTF8("Nenhum tópico correspondente à pesquisa.") : "No matching help topics found.");
        }
    }
    resized();
}

void HelpDialog::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    // Header bar background
    g.setColour(tk.painel);
    g.fillRect(0, 0, getWidth(), 52);

    // Bottom border of header bar
    g.setColour(tk.borda);
    g.drawLine(0.0f, 52.0f, (float)getWidth(), 52.0f, 1.0f);

    // Bottom button bar background
    g.setColour(tk.painel);
    g.fillRect(0, getHeight() - 52, getWidth(), 52);
    g.setColour(tk.borda);
    g.drawLine(0.0f, (float)(getHeight() - 52), (float)getWidth(), (float)(getHeight() - 52), 1.0f);
}

void HelpDialog::resized() {
    auto r = getLocalBounds();
    int pad = 12;

    // Header area
    auto headerArea = r.removeFromTop(52).reduced(pad, 8);
    lblTitulo_->setBounds(headerArea);

    // Bottom action area
    auto bottomArea = r.removeFromBottom(52).reduced(pad, 10);
    btnClose_->setBounds(bottomArea.removeFromRight(100));

    // Middle Content
    auto bodyArea = r.reduced(pad);
    
    // Left Column (Search + List)
    int listW = 270;
    auto leftCol = bodyArea.removeFromLeft(listW);
    searchEditor_->setBounds(leftCol.removeFromTop(32));
    leftCol.removeFromTop(8);
    topicList_->setBounds(leftCol);

    bodyArea.removeFromLeft(12);

    // Right Column (Viewer)
    contentViewport_->setBounds(bodyArea);

    int viewW = contentViewport_->getWidth() - 14;
    if (viewW > 50 && contentView_) {
        int contentH = contentView_->getCalculatedHeight(viewW);
        contentView_->setBounds(0, 0, viewW, std::max(bodyArea.getHeight(), contentH));
    }
}

void HelpDialog::lookAndFeelChanged() {
    const auto& tk = tema();
    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");

    if (lblTitulo_) {
        lblTitulo_->setText(isPt ? juce::String::fromUTF8("GUIA DO USUÁRIO E AJUDA DO BKR MATRIZ") : juce::String::fromUTF8("BKR MATRIZ USER GUIDE & HELP"), juce::dontSendNotification);
        lblTitulo_->setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteTitulo, juce::Font::bold)));
        lblTitulo_->setColour(juce::Label::textColourId, tk.textoPrimario);
    }
    if (searchEditor_) {
        searchEditor_->setColour(juce::TextEditor::backgroundColourId, tk.painelAlt);
        searchEditor_->setColour(juce::TextEditor::textColourId, tk.textoPrimario);
        searchEditor_->setColour(juce::TextEditor::outlineColourId, tk.borda);
        searchEditor_->setColour(juce::TextEditor::focusedOutlineColourId, tk.acento);
    }
    if (btnClose_) {
        btnClose_->setButtonText(isPt ? juce::String::fromUTF8("FECHAR") : juce::String::fromUTF8("CLOSE"));
        btnClose_->setColour(juce::TextButton::buttonColourId, tk.painelAlt);
        btnClose_->setColour(juce::TextButton::textColourOffId, tk.textoPrimario);
    }

    if (topicList_) topicList_->repaint();
    if (contentView_) contentView_->repaint();
    repaint();
}

void HelpDialog::exibirModal() {
    auto dlg = std::make_unique<HelpDialog>();
    dlg->setSize(920, 640);
    auto* rawDlg = dlg.get();

    bool isPt = (matriz::i18n::localeAtivo() == "pt_BR");
    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(dlg.release());
    opt.dialogTitle = isPt ? juce::String::fromUTF8("Guia do Usuário e Ajuda") : "User Guide & Help";
    opt.dialogBackgroundColour = juce::Colours::transparentBlack;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = false;
    opt.resizable = true;

    auto* win = opt.launchAsync();
    rawDlg->aoFechar = [win] {
        if (win) win->exitModalState(0);
    };
}

} // namespace matriz::ui
