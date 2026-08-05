#include "FiltrosComponent.h"

#include "../Ficha/FichaI18n.h"
#include "../I18n/Strings.h"
#include "Tokens.h"

#include <algorithm>

namespace matriz::ui {

namespace {

// Mesmo diálogo de texto único usado em ArvoreComponent (criar/renomear
// pasta) — assíncrono, nunca um loop modal síncrono.
void pedirTexto(const juce::String& titulo, const juce::String& mensagem, const juce::String& valorInicial,
                 std::function<void(std::optional<juce::String>)> aoConcluir) {
    auto janela = std::make_shared<juce::AlertWindow>(titulo, mensagem, juce::MessageBoxIconType::NoIcon);
    janela->addTextEditor("valor", valorInicial);
    janela->addButton(matriz::i18n::t("comum.ok"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    janela->addButton(matriz::i18n::t("comum.cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    janela->enterModalState(true, juce::ModalCallbackFunction::create([janela, aoConcluir](int resultado) {
        if (resultado == 1) aoConcluir(janela->getTextEditorContents("valor"));
        else aoConcluir(std::nullopt);
    }));
}

juce::String rotuloEstado(const std::string& estado) { return matriz::i18n::t("mosaico.estado_" + estado); }

} // namespace

FiltrosComponent::FiltrosComponent(ProjetoAberto& projeto, MosaicoComponent& mosaico)
    : projeto_(projeto), mosaico_(mosaico) {
    recarregar();
}

void FiltrosComponent::recarregar() {
    reconstruirLinhas();
    resized();
    repaint();
}

void FiltrosComponent::reconstruirLinhas() {
    linhas_.clear();

    auto contagensTipo = projeto_.contagensPorTipoMidia();
    if (!contagensTipo.empty()) {
        linhas_.push_back({TipoLinha::CabecalhoSecao, matriz::i18n::t("mosaico.filtrar_tipo"), "", -1, false});
        for (auto& [chave, contagem] : contagensTipo) {
            juce::String rotulo = chave.empty() ? matriz::i18n::t("grade.nao_classificado") : juce::String(chave);
            if (!chave.empty()) {
                try {
                    auto& def = projeto_.definicaoPara(chave);
                    rotulo = matriz::ficha::rotuloTipo(chave, def.rotulo.empty() ? chave : def.rotulo);
                } catch (const std::exception&) {
                    // tipo_midia sem definição de ficha correspondente — usa o token cru mesmo, nunca esconde o chip
                }
            }
            linhas_.push_back({TipoLinha::ChipTipoMidia, rotulo, juce::String(chave), contagem,
                                mosaico_.filtrosTipoMidiaAtivos().count(juce::String(chave)) > 0});
        }
    }

    auto contagensEstado = projeto_.contagensPorEstado();
    if (!contagensEstado.empty()) {
        linhas_.push_back({TipoLinha::CabecalhoSecao, matriz::i18n::t("mosaico.filtrar_estado"), "", -1, false});
        for (auto& [chave, contagem] : contagensEstado) {
            linhas_.push_back({TipoLinha::ChipEstado, rotuloEstado(chave), juce::String(chave), contagem,
                                mosaico_.filtrosEstadoAtivos().count(juce::String(chave)) > 0});
        }
    }

    auto contagensExtensao = projeto_.contagensPorExtensao();
    if (!contagensExtensao.empty()) {
        linhas_.push_back({TipoLinha::CabecalhoSecao, matriz::i18n::t("filtros.secao_extensao"), "", -1, false});
        for (auto& [chave, contagem] : contagensExtensao) {
            linhas_.push_back({TipoLinha::ChipExtensao, "." + juce::String(chave), juce::String(chave), contagem,
                                mosaico_.filtrosExtensaoAtivos().count(juce::String(chave)) > 0});
        }
    }

    // Origem digital/analógico (item 4.2 — "a origem entra como critério de
    // busca, de filtro e de organização"). O valor gravado é o texto bruto
    // do YAML ("Digital"/"Analógico"); o rótulo do chip passa pelo i18n do
    // campo pra aparecer traduzido, sem mexer no valor do banco.
    auto contagensOrigem = projeto_.contagensPorOrigem();
    contagensOrigem.erase(std::string()); // "sem origem" não vira chip — é ausência, não valor
    if (!contagensOrigem.empty()) {
        linhas_.push_back({TipoLinha::CabecalhoSecao, matriz::i18n::t("filtros.secao_origem"), "", -1, false});
        for (auto& [chave, contagem] : contagensOrigem) {
            juce::String rotulo = chave == "Digital" ? matriz::i18n::t("filtros.origem_digital")
                                                     : matriz::i18n::t("filtros.origem_analogico");
            linhas_.push_back({TipoLinha::ChipOrigem, rotulo, juce::String(chave), contagem,
                                mosaico_.filtrosOrigemAtivos().count(juce::String(chave)) > 0});
        }
    }

    // Eixo de agrupamento da grade (item 4.3 — ano é eixo de agrupamento).
    bool porAno = mosaico_.modoAgrupamentoAtual() == MosaicoComponent::ModoAgrupamento::PorAno;
    linhas_.push_back({TipoLinha::ChipAgrupamento,
                        matriz::i18n::t(porAno ? "grade.agrupar_por_ano" : "grade.agrupar_automatico"), "", -1, porAno});

    bool algumFiltroAtivo = !mosaico_.filtrosTipoMidiaAtivos().empty() || !mosaico_.filtrosEstadoAtivos().empty() ||
                            !mosaico_.filtrosExtensaoAtivos().empty() || !mosaico_.filtrosOrigemAtivos().empty() ||
                            mosaico_.filtroFaixaAnoAtivo().has_value() || mosaico_.buscaAtual().isNotEmpty();
    if (algumFiltroAtivo) linhas_.push_back({TipoLinha::Limpar, matriz::i18n::t("filtros.limpar"), "", -1, false});

    linhas_.push_back({TipoLinha::CabecalhoSecao, matriz::i18n::t("filtros.secao_colecoes"), "", -1, false});
    for (auto& colecao : projeto_.listarColecoes())
        linhas_.push_back({TipoLinha::Colecao, colecao.nome, juce::String(colecao.id), -1, false});
    linhas_.push_back({TipoLinha::SalvarColecao, matriz::i18n::t("filtros.salvar_colecao"), "", -1, false});
}

juce::Rectangle<int> FiltrosComponent::boundsDaLinha(int indice) const {
    int y = 0;
    for (int i = 0; i < indice; ++i)
        y += linhas_[static_cast<size_t>(i)].tipo == TipoLinha::CabecalhoSecao ? kAlturaCabecalhoSecao : kAlturaLinha;
    int altura = linhas_[static_cast<size_t>(indice)].tipo == TipoLinha::CabecalhoSecao ? kAlturaCabecalhoSecao : kAlturaLinha;
    return {0, y, getWidth(), altura};
}

int FiltrosComponent::indiceLinhaNaPosicao(int y) const {
    int cursor = 0;
    for (int i = 0; i < static_cast<int>(linhas_.size()); ++i) {
        int altura = linhas_[static_cast<size_t>(i)].tipo == TipoLinha::CabecalhoSecao ? kAlturaCabecalhoSecao : kAlturaLinha;
        if (y >= cursor && y < cursor + altura) return i;
        cursor += altura;
    }
    return -1;
}

void FiltrosComponent::resized() {
    int altura = 0;
    for (auto& linha : linhas_) altura += linha.tipo == TipoLinha::CabecalhoSecao ? kAlturaCabecalhoSecao : kAlturaLinha;
    setSize(getWidth(), altura);
}

void FiltrosComponent::paint(juce::Graphics& g) {
    const auto& tk = tema();
    g.fillAll(tk.painel);

    for (int i = 0; i < static_cast<int>(linhas_.size()); ++i) {
        auto& linha = linhas_[static_cast<size_t>(i)];
        auto bounds = boundsDaLinha(i);

        if (linha.tipo == TipoLinha::CabecalhoSecao) {
            g.setColour(tk.painelAlt);
            g.fillRect(bounds);
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
            g.drawText(linha.rotulo.toUpperCase(), bounds.reduced(tk.espacoMedio, 0), juce::Justification::centredLeft);
            continue;
        }

        if (linha.ativo) {
            g.setColour(tk.acento.withAlpha(0.18f));
            g.fillRect(bounds);
        }

        auto miolo = bounds.reduced(tk.espacoMedio, 0);
        if (linha.contagem >= 0) {
            auto areaContagem = miolo.removeFromRight(36);
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            g.drawText(juce::String(linha.contagem), areaContagem, juce::Justification::centredRight);
        }

        bool acao = linha.tipo == TipoLinha::Limpar || linha.tipo == TipoLinha::SalvarColecao;
        g.setColour(linha.ativo ? tk.acento : (acao ? tk.textoSecundario : tk.textoPrimario));
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo, acao ? juce::Font::italic : juce::Font::plain)));
        g.drawText(linha.rotulo, miolo, juce::Justification::centredLeft, true);
    }
}

void FiltrosComponent::mouseDown(const juce::MouseEvent& e) {
    int indice = indiceLinhaNaPosicao(e.getPosition().y);
    if (indice < 0) return;
    auto& linha = linhas_[static_cast<size_t>(indice)];

    if (e.mods.isPopupMenu() && linha.tipo == TipoLinha::Colecao) {
        abrirMenuContextoColecao(indice);
        return;
    }

    switch (linha.tipo) {
        case TipoLinha::ChipTipoMidia: mosaico_.alternarFiltroTipoMidia(linha.chave); break;
        case TipoLinha::ChipEstado: mosaico_.alternarFiltroEstado(linha.chave); break;
        case TipoLinha::ChipExtensao: mosaico_.alternarFiltroExtensao(linha.chave); break;
        case TipoLinha::ChipOrigem: mosaico_.alternarFiltroOrigem(linha.chave); break;
        case TipoLinha::ChipAgrupamento:
            mosaico_.definirModoAgrupamento(mosaico_.modoAgrupamentoAtual() == MosaicoComponent::ModoAgrupamento::PorAno
                                                 ? MosaicoComponent::ModoAgrupamento::Automatico
                                                 : MosaicoComponent::ModoAgrupamento::PorAno);
            break;
        case TipoLinha::Limpar:
            mosaico_.limparFiltros();
            if (aoSincronizarBusca) aoSincronizarBusca({});
            break;
        case TipoLinha::Colecao: {
            auto colecoes = projeto_.listarColecoes();
            auto it = std::find_if(colecoes.begin(), colecoes.end(),
                                    [&](const auto& c) { return juce::String(c.id) == linha.chave; });
            if (it == colecoes.end()) break;
            // Reaplica a DEFINIÇÃO salva por inteiro — nunca um resultado
            // congelado (Acréscimos §10.2): limpa o que estava ativo e
            // aplica texto + cada categoria de chip da coleção.
            mosaico_.limparFiltros();
            if (aoSincronizarBusca) aoSincronizarBusca(it->buscaTexto);
            mosaico_.definirBusca(it->buscaTexto);
            for (auto& v : it->filtrosTipoMidia) mosaico_.alternarFiltroTipoMidia(v);
            for (auto& v : it->filtrosEstado) mosaico_.alternarFiltroEstado(v);
            for (auto& v : it->filtrosExtensao) mosaico_.alternarFiltroExtensao(v);
            for (auto& v : it->filtrosOrigem) mosaico_.alternarFiltroOrigem(v);
            if (it->anoDe && it->anoAte) mosaico_.definirFiltroFaixaAno(*it->anoDe, *it->anoAte);
            break;
        }
        case TipoLinha::SalvarColecao: salvarColecaoAtual(); break;
        case TipoLinha::CabecalhoSecao: break;
    }

    recarregar();
}

void FiltrosComponent::abrirMenuContextoColecao(int indiceLinha) {
    juce::String colecaoId = linhas_[static_cast<size_t>(indiceLinha)].chave;
    juce::PopupMenu menu;
    menu.addItem(1, matriz::i18n::t("arvore.menu_apagar"));
    menu.showMenuAsync(juce::PopupMenu::Options(), [this, colecaoId](int resultado) {
        if (resultado != 1) return;
        projeto_.apagarColecao(colecaoId.toStdString());
        recarregar();
    });
}

void FiltrosComponent::salvarColecaoAtual() {
    pedirTexto(matriz::i18n::t("filtros.nome_colecao_titulo"), matriz::i18n::t("filtros.nome_colecao_mensagem"), "",
               [this](std::optional<juce::String> nome) {
                   if (!nome || nome->trim().isEmpty()) return;
                   ProjetoAberto::ColecaoInteligente c;
                   c.nome = nome->trim();
                   c.buscaTexto = mosaico_.buscaAtual();
                   c.filtrosTipoMidia = mosaico_.filtrosTipoMidiaAtivos();
                   c.filtrosEstado = mosaico_.filtrosEstadoAtivos();
                   c.filtrosExtensao = mosaico_.filtrosExtensaoAtivos();
                   c.filtrosOrigem = mosaico_.filtrosOrigemAtivos();
                   if (auto faixa = mosaico_.filtroFaixaAnoAtivo()) {
                       c.anoDe = faixa->first;
                       c.anoAte = faixa->second;
                   }
                   projeto_.salvarColecao(c);
                   recarregar();
               });
}

} // namespace matriz::ui
