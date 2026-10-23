#include "CartoesAnalyticsComponent.h"
#include "ProgressoGlobal.h"

#include "ProjetoAberto.h"
#include "Tokens.h"
#include "Moldura.h"
#include "Tablet.h"
#include "VulnerabilidadesDialog.h"
#include "../I18n/Strings.h"

#include <map>

namespace matriz::ui {

namespace {

constexpr int kNumCards = 6;
constexpr int kLarguraMinCard = 150;
constexpr int kAlturaCard = 68;
constexpr int kGap = 10;
constexpr int kPadContainer = 10;

juce::String tamanhoHumano(juce::int64 bytes) {
    if (bytes <= 0) return "0 B";
    constexpr double kKB = 1024.0, kMB = kKB * 1024.0, kGB = kMB * 1024.0, kTB = kGB * 1024.0;
    const double d = static_cast<double>(bytes);
    if (d >= kTB) return juce::String(d / kTB, 2) + " TB";
    if (d >= kGB) return juce::String(d / kGB, 2) + " GB";
    if (d >= kMB) return juce::String(d / kMB, 1) + " MB";
    if (d >= kKB) return juce::String(d / kKB, 1) + " KB";
    return juce::String(bytes) + " B";
}

} // namespace

CartoesAnalyticsComponent::CartoesAnalyticsComponent(ProjetoAberto& projeto) : projeto_(projeto) {
    EventBus::obterInstancia().registrarListener(this);
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

CartoesAnalyticsComponent::~CartoesAnalyticsComponent() {
    stopTimer();
    EventBus::obterInstancia().removerListener(this);
    ++geracao_;
}

// Qualquer mudança de item / marcação / mapa ativo pede releitura, agrupada (não a cada evento).
void CartoesAnalyticsComponent::aoItemAlterado(const EventoItemAlterado& e) {
    // Estes campos não mudam nenhum número dos cards: não vale reler o registro por eles. "metadado" NÃO entra:
    // CONTENT (collection_type) e SOURCE MEDIA mudam o NEEDS ATTENTION, e o evento não diz qual campo foi.
    const auto& t = e.tipoAlteracao;
    if (t == "tags" || t == "titulo" || t == "marcado_revisado" || t == "publicacao") return;
    if (!isTimerRunning()) startTimer(500);
}
void CartoesAnalyticsComponent::timerCallback() {
    stopTimer();
    // Agora os cards existem em todas as abas: durante ingest/backup/lote (eventos a cada item) a releitura completa
    // do registro competiria com a tarefa; espera ela acabar e relê uma vez.
    if (ProgressoGlobal::obterInstancia().estaExecutando()) { startTimer(3000); return; }
    if (isShowing()) recarregar();
}


// ---------------------------------------------------------------------------
// Cálculo (thread de fundo): mesmas regras que EstatisticasComponent::carregarMetricasDoBanco usava.
// ---------------------------------------------------------------------------
CartoesAnalyticsComponent::Dados CartoesAnalyticsComponent::calcular(ProjetoAberto& projeto, const std::string& mapaId,
                                                                      bool mapaOriginal, const juce::String& mapaNome) {
    Dados d;
    d.semPastaMapaId = mapaId;
    d.semPastaOriginal = mapaOriginal;
    d.semPastaMapaNome = mapaNome;
    auto& db = projeto.projeto().registroLeitura();
    std::map<std::string, juce::uint64> formatos;
    try {
        auto stmt = db.prepare(
            "SELECT IFNULL(a.caminho_relativo, IFNULL(a.caminho_absoluto_origem, '')), IFNULL(a.tamanho_bytes, 0) "
            "FROM item i LEFT JOIN arquivo a ON a.item_id = i.id AND a.eh_master = 1 "
            "WHERE COALESCE(i.estado, '') != 'duplicata'");
        while (stmt.step()) {
            const std::string caminho = stmt.columnText(0);
            juce::String ext;
            const auto ponto = caminho.rfind('.');
            if (ponto != std::string::npos && ponto + 1 < caminho.size()) ext = juce::String(caminho.substr(ponto + 1)).toLowerCase();
            formatos[ext.isEmpty() ? std::string("No Format") : ext.toUpperCase().toStdString()]++;
            ++d.totalAssets;
            d.totalBytes += static_cast<juce::int64>(stmt.columnInt(1));
        }
    } catch (...) {}
    juce::uint64 topQtd = 0;
    std::string topNome = "Audio";
    for (const auto& [nome, qtd] : formatos)
        if (qtd > topQtd) { topQtd = qtd; topNome = nome; }
    d.formatoPrincipal = topNome;
    d.formatoPrincipalQtd = topQtd;
    d.vulneraveis = d.totalAssets;

    try {
        auto st = db.prepare(
            "SELECT id FROM item WHERE COALESCE(estado, '') != 'duplicata' AND ((ano IS NULL OR ano = 0) "
            "OR (source_media IS NULL OR TRIM(source_media) = '') "
            "OR (collection_type IS NULL OR TRIM(collection_type) = ''))");
        while (st.step()) d.idsPrecisaAtencao.insert(st.columnText(0));
        d.precisaAtencao = d.idsPrecisaAtencao.size();
    } catch (...) {}

    d.semPasta = mapaOriginal ? 0 : projeto.contarItensSemPasta(mapaId);
    return d;
}

void CartoesAnalyticsComponent::recarregar() {
    const int geracao = ++geracao_;
    // Mapa selecionado no dropdown do Folder Map (leitura barata, na message thread).
    const std::string mapaId = projeto_.mapaSelecionadoNoFolderMap();
    const bool original = (mapaId == ProjetoAberto::kMapaOriginal);
    juce::String nome;
    for (const auto& m : projeto_.listarFolderMaps())
        if (m.id == mapaId) nome = m.nome;

    juce::Component::SafePointer<CartoesAnalyticsComponent> safe(this);
    ProjetoAberto* proj = &projeto_;
    pool_.removeAllJobs(false, 0);
    pool_.addJob([safe, proj, geracao, mapaId, original, nome]() {
        Dados d;
        try { d = calcular(*proj, mapaId, original, nome); } catch (...) { return; }
        juce::MessageManager::callAsync([safe, geracao, d = std::move(d)]() mutable {
            if (!safe || geracao != safe->geracao_.load()) return;
            safe->dados_ = std::move(d);
            safe->repaint();
        });
    });
}

// ---------------------------------------------------------------------------
// Layout: 6 colunas; abaixo disso 3 colunas (2 linhas); abaixo disso 2 colunas (3 linhas). Os cards só
// encolhem/quebram — nada é cortado.
// ---------------------------------------------------------------------------
int CartoesAnalyticsComponent::colunasPara(int largura) const {
    const int util = largura - 2 * kPadContainer;
    for (int cols : {6, 3, 2}) {
        const int w = (util - (cols - 1) * kGap) / cols;
        if (w >= kLarguraMinCard) return cols;
    }
    return 1;
}

int CartoesAnalyticsComponent::alturaPara(int largura) const {
    const int cols = colunasPara(largura);
    const int linhas = (kNumCards + cols - 1) / cols;
    return 2 * kPadContainer + linhas * kAlturaCard + (linhas - 1) * kGap + 2 * 6;  // 6px de respiro fora do container
}

juce::Rectangle<int> CartoesAnalyticsComponent::retanguloCard(int indice, int largura) const {
    const int cols = colunasPara(largura);
    const int util = largura - 2 * kPadContainer;
    const int w = (util - (cols - 1) * kGap) / cols;
    const int col = indice % cols, lin = indice / cols;
    return {kPadContainer + col * (w + kGap), 6 + kPadContainer + lin * (kAlturaCard + kGap), w, kAlturaCard};
}

void CartoesAnalyticsComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.fundo);

    // Card coletivo: mais escuro que o fundo da tela, nos dois temas.
    const bool claro = tk.fundo.getBrightness() > 0.5f;
    const juce::Colour fundoColetivo = claro ? tk.painelAlt : tk.fundo.darker(0.55f);
    const auto container = getLocalBounds().reduced(8, 6).toFloat();
    g.setColour(fundoColetivo);
    g.fillRoundedRectangle(container, tk.raioCartao);
    g.setColour(tk.borda.withAlpha(0.6f));
    g.drawRoundedRectangle(container.reduced(0.5f), tk.raioCartao, 1.0f);

    const juce::String semPastaValor = dados_.semPastaOriginal
        ? juce::String::fromUTF8("\xe2\x80\x94")
        : i18n::t("analytics.sem_pasta_valor").replace("{n}", juce::String(dados_.semPasta));
    // Onde estão: a pasta `_no folder` da STRUCTURE, no mapa selecionado no Folder Map.
    const juce::String semPastaSub = dados_.semPastaOriginal ? i18n::t("analytics.sem_pasta_original")
                                                             : i18n::t("analytics.sem_pasta_onde").replace("{mapa}", dados_.semPastaMapaNome);

    struct Card { juce::String titulo, valor, sub; juce::Colour cor; bool clicavel; };
    const Card cards[kNumCards] = {
        {i18n::t("analytics.total_assets"), juce::String(dados_.totalAssets), i18n::t("analytics.total_assets_sub"), tk.secaoBackup, false},
        {i18n::t("analytics.storage_size"), tamanhoHumano(dados_.totalBytes), i18n::t("analytics.storage_size_sub"), tk.secaoExplorer, false},
        {i18n::t("analytics.primary_format"), juce::String(dados_.formatoPrincipal),
         juce::String(dados_.formatoPrincipalQtd) + " " + i18n::t("analytics.ativos"), tk.secaoStats, false},
        {i18n::t("analytics.needs_attention"), juce::String(dados_.precisaAtencao), i18n::t("analytics.needs_attention_sub"), juce::Colour(0xfff97316), true},
        {i18n::t("analytics.backup_health"), juce::String(dados_.saudeBackupPct, 0) + "%",
         juce::String(dados_.vulneraveis) + " " + i18n::t("analytics.vulneraveis_clique"), juce::Colour(0xffef4444), true},
        {i18n::t("analytics.sem_pasta"), semPastaValor, semPastaSub,
         dados_.semPastaOriginal ? juce::Colour(0xff6b7280) : juce::Colour(0xff6366f1), !dados_.semPastaOriginal},
    };

    for (int i = 0; i < kNumCards; ++i) {
        const auto r = retanguloCard(i, getWidth());
        const auto& c = cards[i];
        g.setColour(i == hover_ && c.clicavel ? tk.painelAlt : tk.painel);
        g.fillRoundedRectangle(r.toFloat(), tk.raioCartao);
        g.setColour(tk.borda);
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), tk.raioCartao, 1.0f);

        // Sem barra lateral colorida: a cor vira um ponto de 8 px antes do rótulo.
        auto inner = r.reduced(14, 6);
        auto linhaRotulo = inner.removeFromTop(13);
        g.setColour(c.cor);
        g.fillEllipse(static_cast<float>(linhaRotulo.getX()), static_cast<float>(linhaRotulo.getCentreY()) - 4.0f, 8.0f, 8.0f);
        linhaRotulo.removeFromLeft(14);
        g.setColour(tk.textoSecundario);
        g.setFont(fonteUi(10.0f, juce::Font::bold).withExtraKerningFactor(0.06f));
        g.drawFittedText(moldura::caixaAlta(c.titulo), linhaRotulo, juce::Justification::centredLeft, 1, 0.7f);
        g.setColour(tk.textoPrimario);
        g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
        g.drawFittedText(c.valor, inner.removeFromTop(22), juce::Justification::centredLeft, 1, 0.6f);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tema().tamanhoFonteMicro)));
        g.drawFittedText(c.sub, inner, juce::Justification::centredLeft, 2, 0.8f);  // até 2 linhas, encolhe se preciso
    }
}

void CartoesAnalyticsComponent::mouseMove(const juce::MouseEvent& e) {
    int h = -1;
    for (int i = 0; i < kNumCards; ++i)
        if (retanguloCard(i, getWidth()).contains(e.getPosition())) h = i;
    const bool clicavel = (h == 3 || h == 4 || (h == 5 && !dados_.semPastaOriginal));
    setMouseCursor(clicavel ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (h != hover_) { hover_ = h; repaint(); }
}

void CartoesAnalyticsComponent::mouseDown(const juce::MouseEvent& e) {
    int h = -1;
    for (int i = 0; i < kNumCards; ++i)
        if (retanguloCard(i, getWidth()).contains(e.getPosition())) h = i;
    if (h == 3) {
        if (aoClicarNeedsAttention) aoClicarNeedsAttention(dados_.idsPrecisaAtencao);
        else if (aoAbrirNoGrid) aoAbrirNoGrid(dados_.idsPrecisaAtencao);
    } else if (h == 4) {
        VulnerabilidadesDialog::exibirModal(projeto_, this, [this](const std::set<std::string>& ids) {
            if (aoAbrirNoGrid) aoAbrirNoGrid(ids);
        });
    } else if (h == 5 && !dados_.semPastaOriginal && aoAbrirSemPasta) {
        aoAbrirSemPasta();
    }
}

} // namespace matriz::ui
