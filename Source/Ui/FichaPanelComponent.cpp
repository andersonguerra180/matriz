#include "FichaPanelComponent.h"

#include "MetadadosOriginaisComponent.h"

#include "../Ficha/FichaI18n.h"
#include "../Ficha/OrigemPadrao.h"
#include "../I18n/Strings.h"
#include "../Ingest/FluxoLote.h"
#include "FormatoTempo.h"
#include "SelecionarTipoMidiaDialogo.h"
#include "Tokens.h"

#include <algorithm>
#include <regex>
#include <set>

namespace matriz::ui {

using matriz::ficha::Campo;
using matriz::ficha::CampoTipo;
using matriz::ficha::FichaDefinition;
using matriz::ficha::VisivelSeOperador;

namespace {

std::string autorAtual() { return juce::SystemStats::getFullUserName().toStdString(); }

// Default de "origem" (Digital/Analógico, §4.2) no momento em que o item
// ganha tipo_midia — único ponto em que o tipo fica conhecido (ver
// Source/Ficha/OrigemPadrao.h). Escreve direto em item_campo com
// fonte='leitura_tecnica', nunca sobrescreve (UNIQUE(item_id, nivel,
// nivel_indice, campo_id) faz o INSERT OR IGNORE virar no-op se já existir
// um valor — defesa barata, hoje sempre inédito nesse ponto do fluxo).
// Tipo sem default óbvio (origemPadraoParaTipo devolve nullopt) não grava
// nada — o campo fica vazio, humano decide.
void aplicarOrigemPadrao(matriz::db::Database& registro, const std::string& itemId, const std::string& tipoMidia) {
    auto origem = matriz::ficha::origemPadraoParaTipo(tipoMidia);
    if (!origem) return;
    registro.run(
        "INSERT OR IGNORE INTO item_campo (id, item_id, nivel, nivel_indice, campo_id, valor, fonte, atualizado_em) "
        "VALUES (?, ?, 'raiz', 0, 'origem', ?, 'leitura_tecnica', ?)",
        {matriz::db::Value::of(matriz::model::novoUuid()), matriz::db::Value::of(itemId),
         matriz::db::Value::of(*origem), matriz::db::Value::of(matriz::model::agoraIso8601())});
}

bool validarEan13(const juce::String& valor) {
    juce::String digitos;
    for (auto c : valor) if (juce::CharacterFunctions::isDigit(c)) digitos += juce::String::charToString(c);
    if (digitos.length() != 13) return false;
    int soma = 0;
    for (int i = 0; i < 12; ++i) {
        int d = digitos[i] - '0';
        soma += (i % 2 == 0) ? d : d * 3;
    }
    int checkEsperado = (10 - (soma % 10)) % 10;
    return (digitos[12] - '0') == checkEsperado;
}

bool validarIsrc(const juce::String& valor) {
    static const std::regex padrao(R"(^[A-Za-z]{2}-?[A-Za-z0-9]{3}-?\d{2}-?\d{5}$)");
    return std::regex_match(valor.toStdString(), padrao);
}

bool validarData(const juce::String& valor) {
    static const std::regex padrao(R"(^\d{4}-\d{2}-\d{2}$)");
    return valor.isEmpty() || std::regex_match(valor.toStdString(), padrao);
}

juce::var parseJsonSeguro(const juce::String& texto) {
    if (texto.isEmpty()) return juce::var(juce::Array<juce::var>());
    juce::var v = juce::JSON::parse(texto);
    return v.isArray() ? v : juce::var(juce::Array<juce::var>());
}

// --- Editor de tabela/lista_pessoas: N colunas, linhas dinâmicas ----------

class TabelaEditor : public juce::Component {
public:
    explicit TabelaEditor(std::vector<juce::String> colunas) : colunas_(std::move(colunas)) {
        addAndMakeVisible(botaoAdicionar_);
        botaoAdicionar_.setButtonText(matriz::i18n::t("ficha.tabela_adicionar_linha"));
        botaoAdicionar_.onClick = [this] {
            adicionarLinha({});
            if (aoMudar) aoMudar();
            if (auto* p = getParentComponent()) p->resized();
        };
    }

    void setValorJson(const juce::String& json) {
        linhas_.clear();
        juce::var array = parseJsonSeguro(json);
        if (auto* arr = array.getArray())
            for (auto& linha : *arr) {
                std::vector<juce::String> valores;
                for (auto& col : colunas_) valores.push_back(linha.getProperty(col, "").toString());
                adicionarLinha(valores);
            }
    }

    juce::String getValorJson() const {
        juce::Array<juce::var> arr;
        for (auto* linha : linhas_) {
            auto obj = std::make_unique<juce::DynamicObject>();
            for (size_t i = 0; i < colunas_.size(); ++i)
                obj->setProperty(colunas_[static_cast<size_t>(i)], linha->celulas[static_cast<int>(i)]->getText());
            arr.add(juce::var(obj.release()));
        }
        return juce::JSON::toString(juce::var(arr), true);
    }

    double somaColuna(const juce::String& coluna) const {
        double soma = 0.0;
        for (size_t i = 0; i < colunas_.size(); ++i)
            if (colunas_[static_cast<size_t>(i)] == coluna)
                for (auto* linha : linhas_) soma += linha->celulas[static_cast<int>(i)]->getText().getDoubleValue();
        return soma;
    }

    int alturaTotal() const { return static_cast<int>(linhas_.size() + 1) * kAlturaLinha; }

    void resized() override {
        int y = 0;
        for (auto* linha : linhas_) {
            int x = 0;
            int larguraCelula = colunas_.empty() ? getWidth() : (getWidth() - 28) / static_cast<int>(colunas_.size());
            for (auto* celula : linha->celulas) {
                celula->setBounds(x, y, larguraCelula, kAlturaLinha - 2);
                x += larguraCelula;
            }
            linha->remover->setBounds(x, y, 28, kAlturaLinha - 2);
            y += kAlturaLinha;
        }
        botaoAdicionar_.setBounds(0, y, 140, kAlturaLinha - 2);
    }

    std::function<void()> aoMudar;

private:
    struct Linha {
        juce::OwnedArray<juce::TextEditor> celulas;
        std::unique_ptr<juce::TextButton> remover;
    };

    void adicionarLinha(const std::vector<juce::String>& valores) {
        auto* linha = linhas_.add(new Linha());
        for (size_t i = 0; i < colunas_.size(); ++i) {
            auto* editor = linha->celulas.add(new juce::TextEditor());
            editor->setTextToShowWhenEmpty(colunas_[i], matriz::ui::tema().textoTerciario);
            if (i < valores.size()) editor->setText(valores[i], false);
            editor->onFocusLost = [this] { if (aoMudar) aoMudar(); };
            addAndMakeVisible(editor);
        }
        linha->remover.reset(new juce::TextButton("×"));
        linha->remover->onClick = [this, linha] {
            linhas_.removeObject(linha);
            if (aoMudar) aoMudar();
            if (auto* p = getParentComponent()) p->resized();
        };
        addAndMakeVisible(*linha->remover);
    }

    static constexpr int kAlturaLinha = 26;
    std::vector<juce::String> colunas_;
    juce::OwnedArray<Linha> linhas_;
    juce::TextButton botaoAdicionar_;
};

} // namespace

// ---------------------------------------------------------------------------
// FichaConteudo — o corpo real da ficha, reconstruído a cada mostrarItem().
// ---------------------------------------------------------------------------

class FichaConteudo : public juce::Component {
public:
    explicit FichaConteudo(ProjetoAberto& projeto) : projeto_(projeto) {}

    void limpar() {
        // Nada que o operador digitou é descartado, nunca, por nenhum
        // motivo (Parte 2 da correção crítica). Isto rodava só no blur de
        // cada editor (onFocusLost/onChange/onReturnKey) — se o operador
        // trocasse de item, ou qualquer outra coisa reconstruísse a ficha,
        // ENQUANTO um campo ainda tinha o texto digitado mas nunca perdeu
        // o foco, o valor morria junto com o widget, sem nunca chegar ao
        // banco. Commitar tudo aqui, incondicional a estado de foco,
        // fecha esse buraco de vez — troca de item, fechar o projeto,
        // reconstrução após confirmar sugestão, todos passam por aqui.
        for (auto* linha : linhas_) salvarValorSemEfeitosColaterais(*linha);

        linhas_.clear();
        secoes_.clear();
        destaque_.clear();
        cabecalho_.reset();
        mensagemNaoClassificado_.reset();
        botoesTipoNaoClassificado_.clear();
        botaoCancelarRecategorizar_.reset();
        botaoRecategorizar_.reset();
        arquivosEsperados_.titulo.reset();
        arquivosEsperados_.linhas.clear();
        observacoes_.titulo.reset();
        observacoes_.itens.clear();
        observacoes_.botaoNova.reset();
        observacoes_.editorTexto.reset();
        observacoes_.editorMinutagem.reset();
        observacoes_.botaoSalvar.reset();
        observacoes_.botaoCancelar.reset();
        botoesAdicionarFaixa_.clear();
        itemId_.clear();
        setSize(getWidth(), 0);
    }

    void construirParaItem(const std::string& itemId) {
        limpar();
        itemId_ = itemId;
        if (itemId.empty()) {
            repaint();
            return;
        }

        auto stmt = projeto_.projeto().registro().prepare("SELECT titulo, tipo_midia, codigo_acervo FROM item WHERE id = ?");
        stmt.bind(1, matriz::db::Value::of(itemId));
        if (!stmt.step()) return;
        juce::String titulo = stmt.columnText(0);
        std::string tipoMidia = stmt.columnText(1);
        juce::String codigo = stmt.columnText(2);

        cabecalho_ = std::make_unique<juce::Label>();
        cabecalho_->setText(codigo + " — " + (titulo.isNotEmpty() ? titulo : matriz::i18n::t("ficha.cabecalho_sem_titulo")),
                             juce::dontSendNotification);
        cabecalho_->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteSubtitulo, juce::Font::bold)));
        cabecalho_->setColour(juce::Label::textColourId, matriz::ui::tema().textoPrimario);
        addAndMakeVisible(*cabecalho_);

        // Material sem ficha nenhuma é estado legítimo e tem que continuar
        // navegável (§7.1/§12.1) — antes disto, clicar em QUALQUER item
        // recém-ingerido (tipo_midia NULL é o padrão desde a Reorientação)
        // lançava ProjetoAbertoError direto de dentro do clique do mouse,
        // sem ninguém pra pegar, e derrubava o app inteiro (bug real,
        // achado por relato do usuário + crash log). Em vez de lançar,
        // oferece classificar ali mesmo — fecha também um buraco que nunca
        // tinha jeito nenhum de dar tipo a um item avulso pela UI (só em
        // lote, com 2+ selecionados).
        const FichaDefinition* def = nullptr;
        if (!tipoMidia.empty()) {
            try {
                def = &projeto_.definicaoPara(tipoMidia);
            } catch (const std::exception&) {
                def = nullptr; // tipo gravado não bate com nenhuma fichas/*.yaml — mesmo tratamento de "não classificado"
            }
        }
        if (!def || recategorizando_) {
            construirSeletorTipoMidia(itemId, def != nullptr);
            relayoutEExibir();
            return;
        }
        const FichaDefinition& defRef = *def; // resto da função já era escrito contra uma referência
        tipoAtual_ = tipoMidia;

        // Recategorizar (item já classificado, operador quer trocar o
        // tipo) — botão fica ao lado do cabeçalho, sempre visível; não
        // precisa de confirmação porque não apaga nada, só reabre o
        // seletor (mesmo caminho de "não classificado").
        botaoRecategorizar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("ficha.recategorizar"));
        botaoRecategorizar_->onClick = [this] {
            recategorizando_ = true;
            construirParaItem(itemId_);
        };
        addAndMakeVisible(*botaoRecategorizar_);

        carregarValores(itemId, defRef);

        if (defRef.usaNiveis()) {
            construirSecao(matriz::ficha::rotuloTipo(defRef.tipo, defRef.rotulo.empty() ? defRef.tipo : defRef.rotulo),
                            defRef.camposPorNivel[0].second, "raiz", 0, false, true);

            const std::string& nivelRepetido = defRef.niveis[1];
            const auto* camposFaixa = defRef.camposDoNivel(nivelRepetido);
            std::set<int> indices = indicesExistentes(itemId, nivelRepetido);
            proximoIndiceFaixa_ = indices.empty() ? 0 : *indices.rbegin();

            for (int idx : indices) {
                juce::String rotulo = matriz::i18n::t("ficha.nivel_faixa_numero").replace("{n}", juce::String(idx));
                construirSecao(rotulo, *camposFaixa, nivelRepetido, idx, true);
            }

            auto botaoAdd = std::make_unique<juce::TextButton>(matriz::i18n::t("ficha.nivel_faixa_adicionar"));
            botaoAdd->onClick = [this, nivelRepetido = nivelRepetido, camposFaixa] {
                ++proximoIndiceFaixa_;
                juce::String rotulo = matriz::i18n::t("ficha.nivel_faixa_numero").replace("{n}", juce::String(proximoIndiceFaixa_));
                construirSecao(rotulo, *camposFaixa, nivelRepetido, proximoIndiceFaixa_, true);
                relayoutEExibir();
            };
            addAndMakeVisible(*botaoAdd);
            botoesAdicionarFaixa_.push_back(std::move(botaoAdd));
        } else {
            for (size_t i = 0; i < defRef.grupos.size(); ++i) {
                const auto& grupo = defRef.grupos[i];
                construirSecao(matriz::ficha::rotuloGrupo(defRef.tipo, grupo.chave, grupo.rotulo), grupo.campos,
                                "raiz", 0, false, i == 0);
            }
        }

        construirSecaoArquivosEsperados(itemId, defRef);
        construirSecaoObservacoes(itemId);

        atualizarVisibilidade();
        relayoutEExibir();
    }

    // Item sem classificação — nunca lança, oferece escolher aqui mesmo
    // (mesma lista de tipos disponíveis por modo do seletor em lote,
    // Source/Ui/SelecionarTipoMidiaDialogo.h). Mesmo caminho serve pra
    // recategorizar um item já classificado (jaClassificado=true) — só
    // muda a mensagem e acrescenta um botão Cancel pra voltar sem trocar.
    void construirSeletorTipoMidia(const std::string& itemId, bool jaClassificado = false) {
        mensagemNaoClassificado_ = std::make_unique<juce::Label>();
        mensagemNaoClassificado_->setText(
            matriz::i18n::t(jaClassificado ? "ficha.recategorizar_titulo" : "ficha.nao_classificado"), juce::dontSendNotification);
        mensagemNaoClassificado_->setColour(juce::Label::textColourId, matriz::ui::tema().textoSecundario);
        mensagemNaoClassificado_->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteCorpo)));
        addAndMakeVisible(*mensagemNaoClassificado_);

        for (auto& opcao : listarTiposMidiaDisponiveis(projeto_)) {
            auto botao = std::make_unique<juce::TextButton>(opcao.rotulo);
            std::string tipoId = opcao.id;
            botao->onClick = [this, itemId, tipoId] {
                std::string agora = matriz::model::agoraIso8601();
                projeto_.projeto().registro().run("UPDATE item SET tipo_midia = ?, atualizado_em = ? WHERE id = ?",
                                                    {matriz::db::Value::of(tipoId), matriz::db::Value::of(agora),
                                                     matriz::db::Value::of(itemId)});
                aplicarOrigemPadrao(projeto_.projeto().registro(), itemId, tipoId);
                recategorizando_ = false;
                if (aoMudarClassificacao) aoMudarClassificacao();
                construirParaItem(itemId); // reconstrói agora com o tipo escolhido — mostra a ficha de verdade na hora
                relayoutEExibir();
            };
            addAndMakeVisible(*botao);
            botoesTipoNaoClassificado_.push_back(std::move(botao));
        }

        if (jaClassificado) {
            botaoCancelarRecategorizar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("comum.cancelar"));
            botaoCancelarRecategorizar_->onClick = [this] {
                recategorizando_ = false;
                construirParaItem(itemId_);
            };
            addAndMakeVisible(*botaoCancelarRecategorizar_);
        }
    }

    void relayout(int largura) {
        const auto& tk = matriz::ui::tema();
        int y = tk.espacoPainel;
        int x = tk.espacoPainel;
        int larguraUtil = largura - 2 * tk.espacoPainel;

        if (cabecalho_) {
            cabecalho_->setBounds(x, y, larguraUtil, 24);
            y += 24 + tk.espacoPequeno;
        }

        if (botaoRecategorizar_) {
            botaoRecategorizar_->setBounds(x, y, 140, 24);
            y += 24 + tk.espacoPequeno;
        }

        y += tk.espacoMedio;

        if (mensagemNaoClassificado_) {
            mensagemNaoClassificado_->setBounds(x, y, larguraUtil, 40);
            y += 40 + tk.espacoMedio;
            for (auto& b : botoesTipoNaoClassificado_) {
                b->setBounds(x, y, larguraUtil, 26);
                y += 26 + tk.espacoPequeno;
            }
            if (botaoCancelarRecategorizar_) {
                botaoCancelarRecategorizar_->setBounds(x, y, larguraUtil, 26);
                y += 26 + tk.espacoPequeno;
            }
            setSize(largura, y + tk.espacoPainel);
            return;
        }

        if (!destaque_.empty()) {
            for (auto* linha : destaque_) {
                if (!linha->visivel) {
                    linha->setBoundsTudoZero();
                    continue;
                }
                int alturaLinha = linha->alturaNecessaria(larguraUtil);
                linha->aplicarBounds(x, y, larguraUtil, alturaLinha);
                y += alturaLinha + tk.espacoPequeno;
            }
            y += tk.espacoMedio;
        }

        for (auto& secao : secoes_) {
            if (secao.titulo) {
                secao.titulo->setBounds(x, y, larguraUtil, 20);
                y += 20 + tk.espacoPequeno;
            }
            for (auto* linha : secao.linhas) {
                if (!linha->visivel) {
                    linha->setBoundsTudoZero();
                    continue;
                }
                int alturaLinha = linha->alturaNecessaria(larguraUtil);
                linha->aplicarBounds(x, y, larguraUtil, alturaLinha);
                y += alturaLinha + tk.espacoPequeno;
            }
            y += tk.espacoMedio;
        }

        for (auto& b : botoesAdicionarFaixa_) {
            b->setBounds(x, y, 160, 26);
            y += 26 + tk.espacoMedio;
        }

        if (!arquivosEsperados_.linhas.empty()) {
            arquivosEsperados_.titulo->setBounds(x, y, larguraUtil, 20);
            y += 20 + tk.espacoPequeno;
            for (auto& linha : arquivosEsperados_.linhas) {
                linha->setBounds(x, y, larguraUtil, 20);
                y += 20 + tk.espacoPequeno;
            }
            y += tk.espacoMedio;
        }

        if (observacoes_.titulo) {
            observacoes_.titulo->setBounds(x, y, larguraUtil, 20);
            y += 20 + tk.espacoPequeno;

            for (auto& linha : observacoes_.itens) {
                int alturaTexto = 36; // duas linhas: texto + "{autor} — {data}"
                int larguraTexto = larguraUtil - 24 - tk.espacoPequeno;
                linha.texto->setBounds(x, y, larguraTexto, alturaTexto);
                linha.remover->setBounds(x + larguraTexto + tk.espacoPequeno, y, 24, 24);
                y += alturaTexto + tk.espacoPequeno;
            }

            if (adicionandoObservacao_) {
                observacoes_.editorTexto->setBounds(x, y, larguraUtil, 60);
                y += 60 + tk.espacoPequeno;
                observacoes_.editorMinutagem->setBounds(x, y, 120, 26);
                observacoes_.botaoSalvar->setBounds(x + 120 + tk.espacoPequeno, y, 80, 26);
                observacoes_.botaoCancelar->setBounds(x + 120 + 80 + 2 * tk.espacoPequeno, y, 80, 26);
                y += 26 + tk.espacoPequeno;
            } else {
                observacoes_.botaoNova->setBounds(x, y, 160, 26);
                y += 26 + tk.espacoPequeno;
            }

            y += tk.espacoMedio;
        }

        setSize(largura, y + tk.espacoPainel);
    }

    std::function<void()> aoRelayoutNecessario;
    // Item ganhou tipo_midia pela primeira vez (seletor de "não
    // classificado") — outros painéis (grade, árvore, chips) têm contagens
    // que dependem disso.
    std::function<void()> aoMudarClassificacao;

    // Introspecção pra teste (Parte 2 da correção crítica — perda de
    // dado): acesso direto ao editor de um campo específico, pra simular
    // digitação sem depender de foco/blur real. nullptr se não encontrado.
    juce::Component* editorDoCampoParaTeste(const std::string& nivel, int nivelIndice, const std::string& campoId) {
        for (auto* linha : linhas_) {
            if (linha->nivel == nivel && linha->nivelIndice == nivelIndice && linha->campo->id == campoId)
                return linha->editorTabela ? static_cast<juce::Component*>(linha->editorTabela.get())
                                            : linha->editorSimples.get();
        }
        return nullptr;
    }

    // Idem, pro botão de classificar um item sem tipo (construirSeletorTipoMidia).
    juce::TextButton* botaoTipoMidiaParaTeste(const std::string& tipoId) {
        auto opcoes = listarTiposMidiaDisponiveis(projeto_);
        for (size_t i = 0; i < opcoes.size() && i < botoesTipoNaoClassificado_.size(); ++i)
            if (opcoes[i].id == tipoId) return botoesTipoNaoClassificado_[i].get();
        return nullptr;
    }

private:
    struct LinhaCampo {
        const Campo* campo = nullptr;
        std::string nivel;
        int nivelIndice = 0;
        std::unique_ptr<juce::Label> rotulo;
        std::unique_ptr<juce::Component> editorSimples; // TextEditor, ComboBox ou ToggleButton
        std::unique_ptr<TabelaEditor> editorTabela;      // usado só quando campo->tipo é Tabela/ListaPessoas
        std::unique_ptr<juce::Label> indicador;
        std::unique_ptr<juce::TextButton> botaoConfirmar;
        std::unique_ptr<juce::Label> alerta;
        bool visivel = true;

        void setBoundsTudoZero() {
            if (rotulo) rotulo->setBounds({});
            if (editorSimples) editorSimples->setBounds({});
            if (editorTabela) editorTabela->setBounds({});
            if (indicador) indicador->setBounds({});
            if (botaoConfirmar) botaoConfirmar->setBounds({});
            if (alerta) alerta->setBounds({});
        }

        int alturaNecessaria(int largura) const {
            int altura = 18; // rótulo
            if (editorTabela) altura += editorTabela->alturaTotal() + 4;
            else altura += 24;
            if (indicador) altura += 16;
            if (alerta) altura += 16;
            return altura;
        }

        void aplicarBounds(int x, int y, int largura, int) {
            rotulo->setBounds(x, y, largura, 18);
            y += 18;
            if (editorTabela) {
                editorTabela->setBounds(x, y, largura, editorTabela->alturaTotal());
                editorTabela->resized();
                y += editorTabela->alturaTotal() + 4;
            } else {
                int larguraEditor = botaoConfirmar ? largura - 96 : largura;
                editorSimples->setBounds(x, y, larguraEditor, 22);
                if (botaoConfirmar) botaoConfirmar->setBounds(x + larguraEditor + 4, y, 92, 22);
                y += 24;
            }
            if (indicador) { indicador->setBounds(x, y, largura, 16); y += 16; }
            if (alerta) { alerta->setBounds(x, y, largura, 16); y += 16; }
        }
    };

    struct Secao {
        std::unique_ptr<juce::Label> titulo;
        std::vector<LinhaCampo*> linhas; // não-owning; dono é linhas_ (OwnedArray)
    };

    struct SecaoArquivos {
        std::unique_ptr<juce::Label> titulo;
        std::vector<std::unique_ptr<juce::Label>> linhas;
    };

    // Observações/Notes (item 9) — múltiplas entradas por item, texto +
    // autor + data + minutagem opcional. Sem edição de texto existente
    // (fora de escopo) — só criar, listar, remover. Quando a timeline de
    // áudio/vídeo existir, marcadores passam a alimentar esta mesma lista
    // automaticamente (item 9.2) — hoje é sempre só o que o operador digita.
    struct LinhaObservacao {
        std::unique_ptr<juce::Label> texto;
        std::unique_ptr<juce::TextButton> remover;
    };
    struct SecaoObservacoes {
        std::unique_ptr<juce::Label> titulo;
        std::vector<LinhaObservacao> itens;
        std::unique_ptr<juce::TextButton> botaoNova;
        std::unique_ptr<juce::TextEditor> editorTexto;
        std::unique_ptr<juce::TextEditor> editorMinutagem;
        std::unique_ptr<juce::TextButton> botaoSalvar;
        std::unique_ptr<juce::TextButton> botaoCancelar;
    };

    std::string chaveValor(const std::string& nivel, int nivelIndice, const std::string& campoId) const {
        return nivel + "|" + std::to_string(nivelIndice) + "|" + campoId;
    }

    void carregarValores(const std::string& itemId, const FichaDefinition& def) {
        valores_.clear();
        auto carregarPara = [&](const std::vector<Campo>& campos, const std::string& nivel, int idx) {
            for (auto& c : campos) {
                auto v = projeto_.valorCampo(itemId, nivel, idx, c.id);
                valores_[chaveValor(nivel, idx, c.id)] = v.value_or("");
            }
        };
        if (def.usaNiveis()) {
            carregarPara(def.camposPorNivel[0].second, "raiz", 0);
            const std::string& nivelRep = def.niveis[1];
            for (int idx : indicesExistentes(itemId, nivelRep)) carregarPara(*def.camposDoNivel(nivelRep), nivelRep, idx);
        } else {
            for (auto& g : def.grupos) carregarPara(g.campos, "raiz", 0);
        }
    }

    std::set<int> indicesExistentes(const std::string& itemId, const std::string& nivel) const {
        std::set<int> out;
        auto stmt = projeto_.projeto().registro().prepare(
            "SELECT DISTINCT nivel_indice FROM item_campo WHERE item_id = ? AND nivel = ? ORDER BY nivel_indice");
        stmt.bind(1, matriz::db::Value::of(itemId));
        stmt.bind(2, matriz::db::Value::of(nivel));
        while (stmt.step()) out.insert(static_cast<int>(stmt.columnInt(0)));
        return out;
    }

    void construirSecaoArquivosEsperados(const std::string& itemId, const FichaDefinition& def) {
        if (def.arquivosEsperados.empty()) return;
        auto presentes = projeto_.papeisArquivoPresentes(itemId);

        arquivosEsperados_.titulo = std::make_unique<juce::Label>();
        arquivosEsperados_.titulo->setText(matriz::i18n::t("ficha.secao_arquivos_esperados"), juce::dontSendNotification);
        arquivosEsperados_.titulo->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteCorpo, juce::Font::bold)));
        arquivosEsperados_.titulo->setColour(juce::Label::textColourId, matriz::ui::tema().textoSecundario);
        addAndMakeVisible(*arquivosEsperados_.titulo);

        for (auto& ae : def.arquivosEsperados) {
            bool presente = std::find(presentes.begin(), presentes.end(), ae.papel) != presentes.end();
            juce::String texto = juce::String(ae.papel) + " — " +
                                  (presente ? matriz::i18n::t("ficha.arquivo_presente") : matriz::i18n::t("ficha.arquivo_ausente"));
            if (ae.obrigatorio) texto += juce::String(" (") + matriz::i18n::t("ficha.arquivo_obrigatorio") + ")";
            if (!ae.minimo.empty()) texto += " — min " + juce::String(ae.minimo);

            auto label = std::make_unique<juce::Label>();
            label->setText(texto, juce::dontSendNotification);
            label->setColour(juce::Label::textColourId,
                              presente ? matriz::ui::tema().textoPrimario
                                       : (ae.obrigatorio ? matriz::ui::tema().perigo : matriz::ui::tema().textoSecundario));
            addAndMakeVisible(*label);
            arquivosEsperados_.linhas.push_back(std::move(label));
        }
    }

    void construirSecaoObservacoes(const std::string& itemId) {
        observacoes_.titulo = std::make_unique<juce::Label>();
        observacoes_.titulo->setText(matriz::i18n::t("ficha.secao_observacoes"), juce::dontSendNotification);
        observacoes_.titulo->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteCorpo, juce::Font::bold)));
        observacoes_.titulo->setColour(juce::Label::textColourId, matriz::ui::tema().textoSecundario);
        addAndMakeVisible(*observacoes_.titulo);

        for (auto& obs : projeto_.observacoesDoItem(itemId)) {
            LinhaObservacao linha;
            juce::String texto = obs.minutagemMs ? (matriz::ui::formatarMinutagem(*obs.minutagemMs) + "  ") : juce::String();
            texto += juce::String(obs.texto);
            texto += "\n" + juce::String(obs.autor) + " — " + juce::String(obs.criadoEm);

            linha.texto = std::make_unique<juce::Label>();
            linha.texto->setText(texto, juce::dontSendNotification);
            linha.texto->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFontePequena)));
            linha.texto->setColour(juce::Label::textColourId, matriz::ui::tema().textoPrimario);
            linha.texto->setMinimumHorizontalScale(1.0f);
            addAndMakeVisible(*linha.texto);

            linha.remover = std::make_unique<juce::TextButton>("x");
            std::string obsId = obs.id;
            linha.remover->onClick = [this, obsId] {
                projeto_.removerObservacao(obsId);
                construirParaItem(itemId_);
            };
            addAndMakeVisible(*linha.remover);

            observacoes_.itens.push_back(std::move(linha));
        }

        if (adicionandoObservacao_) {
            observacoes_.editorTexto = std::make_unique<juce::TextEditor>();
            observacoes_.editorTexto->setMultiLine(true, true);
            addAndMakeVisible(*observacoes_.editorTexto);

            observacoes_.editorMinutagem = std::make_unique<juce::TextEditor>();
            observacoes_.editorMinutagem->setTextToShowWhenEmpty(matriz::i18n::t("ficha.observacao_minutagem_dica"),
                                                                   matriz::ui::tema().textoTerciario);
            addAndMakeVisible(*observacoes_.editorMinutagem);

            auto* editorTextoPtr = observacoes_.editorTexto.get();
            auto* editorMinutagemPtr = observacoes_.editorMinutagem.get();

            observacoes_.botaoSalvar = std::make_unique<juce::TextButton>(matriz::i18n::t("comum.salvar"));
            observacoes_.botaoSalvar->onClick = [this, editorTextoPtr, editorMinutagemPtr] {
                juce::String texto = editorTextoPtr->getText().trim();
                if (texto.isEmpty()) return;
                auto minutagem = matriz::ui::parsearMinutagem(editorMinutagemPtr->getText());
                projeto_.adicionarObservacao(itemId_, texto.toStdString(), minutagem, autorAtual());
                adicionandoObservacao_ = false;
                construirParaItem(itemId_);
            };
            addAndMakeVisible(*observacoes_.botaoSalvar);

            observacoes_.botaoCancelar = std::make_unique<juce::TextButton>(matriz::i18n::t("comum.cancelar"));
            observacoes_.botaoCancelar->onClick = [this] {
                adicionandoObservacao_ = false;
                construirParaItem(itemId_);
            };
            addAndMakeVisible(*observacoes_.botaoCancelar);
        } else {
            observacoes_.botaoNova = std::make_unique<juce::TextButton>(matriz::i18n::t("ficha.observacao_nova"));
            observacoes_.botaoNova->onClick = [this] {
                adicionandoObservacao_ = true;
                construirParaItem(itemId_);
            };
            addAndMakeVisible(*observacoes_.botaoNova);
        }
    }

    // extrairDestaque puxa origem/ano (§4.2/§4.3 — campo de origem e ano em
    // destaque no topo de toda ficha) pra fora do fluxo normal de seção,
    // pra destaque_ (renderizado em faixa fixa acima das seções, ver
    // relayout). Só a primeira seção de cada ficha (raiz do nível único ou
    // primeiro grupo — onde origem/ano foram inseridos nas definições YAML)
    // passa true; seções repetidas (faixa) nunca extraem.
    void construirSecao(const juce::String& tituloSecao, const std::vector<Campo>& campos, const std::string& nivel,
                         int nivelIndice, bool comBotaoRemover = false, bool extrairDestaque = false) {
        Secao secao;
        secao.titulo = std::make_unique<juce::Label>();
        secao.titulo->setText(tituloSecao, juce::dontSendNotification);
        secao.titulo->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteCorpo, juce::Font::bold)));
        secao.titulo->setColour(juce::Label::textColourId, matriz::ui::tema().textoSecundario);
        addAndMakeVisible(*secao.titulo);

        for (auto& campo : campos) {
            if (extrairDestaque && (campo.id == "origem" || campo.id == "ano")) {
                auto* linha = construirLinha(campo, nivel, nivelIndice);
                linha->rotulo->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteSubtitulo, juce::Font::bold)));
                destaque_.push_back(linha);
                continue;
            }
            secao.linhas.push_back(construirLinha(campo, nivel, nivelIndice));
        }

        secoes_.push_back(std::move(secao));
        juce::ignoreUnused(comBotaoRemover);
    }

    LinhaCampo* construirLinha(const Campo& campo, const std::string& nivel, int nivelIndice) {
        auto* linha = linhas_.add(new LinhaCampo());
        linha->campo = &campo;
        linha->nivel = nivel;
        linha->nivelIndice = nivelIndice;

        juce::String textoRotulo = matriz::ficha::rotuloCampo(tipoAtual_, campo) + (campo.obrigatorio ? " *" : "");
        linha->rotulo = std::make_unique<juce::Label>();
        linha->rotulo->setText(textoRotulo, juce::dontSendNotification);
        linha->rotulo->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFontePequena)));
        linha->rotulo->setColour(juce::Label::textColourId, matriz::ui::tema().textoSecundario);
        addAndMakeVisible(*linha->rotulo);

        juce::String valorAtual = valores_[chaveValor(nivel, nivelIndice, campo.id)];

        bool ehTabela = campo.tipo == CampoTipo::Tabela || campo.tipo == CampoTipo::ListaPessoas;
        if (ehTabela) {
            std::vector<juce::String> colunas;
            if (campo.tipo == CampoTipo::ListaPessoas) colunas = {"nome", "papel"};
            else for (auto& c : campo.colunas) colunas.push_back(c);

            linha->editorTabela = std::make_unique<TabelaEditor>(colunas);
            linha->editorTabela->setValorJson(valorAtual);
            addAndMakeVisible(*linha->editorTabela);
            linha->editorTabela->aoMudar = [this, linha] { commitLinha(*linha); };
        } else {
            linha->editorSimples = criarEditorSimples(campo, valorAtual, linha);
            addAndMakeVisible(*linha->editorSimples);
        }

        // Proveniência (P3: herdado / leitura técnica / sugestão)
        if (campo.herdaDoProjeto) {
            linha->indicador = criarIndicador(matriz::i18n::t("ficha.herdado_do_projeto"), matriz::ui::tema().campoHerdado);
        } else if (!campo.preenchidoPor.empty()) {
            linha->indicador = criarIndicador(matriz::i18n::t("ficha.preenchido_por_leitura_tecnica"), matriz::ui::tema().campoLeituraTecnica);
        } else if (!campo.sugeridoPor.empty()) {
            auto sugestao = projeto_.sugestaoPendente(itemId_, nivel, nivelIndice, campo.id);
            if (sugestao) {
                juce::String texto = matriz::i18n::t("ficha.sugestao_rotulo") + ": \"" + sugestao->valor + "\" — " +
                                      matriz::i18n::t("ficha.sugestao_modelo")
                                          .replace("{modelo}", sugestao->modelo)
                                          .replace("{versao}", sugestao->modeloVersao);
                if (sugestao->confianca)
                    texto += " (" + matriz::i18n::t("ficha.sugestao_confianca").replace("{p}", juce::String(static_cast<int>(*sugestao->confianca * 100))) + ")";
                linha->indicador = criarIndicador(texto, matriz::ui::tema().campoSugestaoIa);

                linha->botaoConfirmar = std::make_unique<juce::TextButton>(matriz::i18n::t("ficha.sugestao_confirmar"));
                matriz::ui::ProjetoAberto::SugestaoCampo sugestaoCapturada = *sugestao;
                linha->botaoConfirmar->onClick = [this, linha, sugestaoCapturada] {
                    projeto_.confirmarSugestao(sugestaoCapturada, itemId_, linha->nivel, linha->nivelIndice, linha->campo->id, autorAtual());
                    construirParaItem(itemId_); // sugestão virou decisão humana — reconstrói pra refletir o novo estado
                };
                addAndMakeVisible(*linha->botaoConfirmar);
            }
        }

        if (campo.tipo == CampoTipo::Booleano && !campo.alertaSeTrue.empty() && valorAtual == "true") {
            linha->alerta = std::make_unique<juce::Label>();
            linha->alerta->setText(
                juce::String(matriz::i18n::t("ficha.alerta_rotulo")) + ": " + matriz::ficha::rotuloAlerta(tipoAtual_, campo),
                juce::dontSendNotification);
            linha->alerta->setColour(juce::Label::textColourId, matriz::ui::tema().alerta);
            linha->alerta->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFontePequena)));
            addAndMakeVisible(*linha->alerta);
        }

        return linha;
    }

    std::unique_ptr<juce::Label> criarIndicador(const juce::String& texto, juce::Colour cor) {
        auto label = std::make_unique<juce::Label>();
        label->setText(texto, juce::dontSendNotification);
        label->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFontePequena)));
        label->setColour(juce::Label::textColourId, cor);
        addAndMakeVisible(*label);
        return label;
    }

    std::unique_ptr<juce::Component> criarEditorSimples(const Campo& campo, const juce::String& valorAtual, LinhaCampo* linha) {
        switch (campo.tipo) {
            case CampoTipo::Booleano: {
                auto toggle = std::make_unique<juce::ToggleButton>();
                toggle->setToggleState(valorAtual == "true", juce::dontSendNotification);
                toggle->setButtonText(valorAtual == "true" ? matriz::i18n::t("ficha.campo_booleano_sim")
                                                             : matriz::i18n::t("ficha.campo_booleano_nao"));
                toggle->onClick = [this, linha, t = toggle.get()] {
                    t->setButtonText(t->getToggleState() ? matriz::i18n::t("ficha.campo_booleano_sim")
                                                          : matriz::i18n::t("ficha.campo_booleano_nao"));
                    commitLinha(*linha);
                };
                return toggle;
            }
            case CampoTipo::Opcao: {
                // O texto exibido é traduzido (rotuloOpcao); o valor
                // gravado no banco continua sendo sempre o token bruto do
                // YAML — lerValorEditor() lê de volta pelo índice
                // selecionado, nunca por getText(), senão o dado já salvo
                // mudaria de significado ao trocar de idioma.
                auto combo = std::make_unique<juce::ComboBox>();
                int idSelecionado = 0;
                for (size_t i = 0; i < campo.opcoes.size(); ++i) {
                    combo->addItem(matriz::ficha::rotuloOpcao(tipoAtual_, campo, static_cast<int>(i), campo.opcoes[i]),
                                   static_cast<int>(i) + 1);
                    if (campo.opcoes[i] == valorAtual.toStdString()) idSelecionado = static_cast<int>(i) + 1;
                }
                combo->setSelectedId(idSelecionado, juce::dontSendNotification);
                combo->onChange = [this, linha] { commitLinha(*linha); };
                return combo;
            }
            case CampoTipo::OpcaoLivre: {
                auto combo = std::make_unique<juce::ComboBox>();
                combo->setEditableText(true); // "opção livre (autocompletar + criar)" — texto digitado vira o valor direto
                std::set<std::string> jaListados;
                for (size_t i = 0; i < campo.opcoes.size(); ++i) {
                    combo->addItem(matriz::ficha::rotuloOpcao(tipoAtual_, campo, static_cast<int>(i), campo.opcoes[i]),
                                   static_cast<int>(i) + 1);
                    jaListados.insert(campo.opcoes[i]);
                }

                // Vocabulário do projeto: o que o operador já digitou neste
                // campo em QUALQUER item vira opção nas próximas vezes —
                // digitar "Agfa" uma vez basta pra ele estar na lista depois.
                // Separador antes pra ficar claro o que é da definição e o
                // que o próprio projeto acumulou.
                auto usados = projeto_.valoresUsadosNoCampo(campo.id);
                int proximoId = static_cast<int>(campo.opcoes.size()) + 1;
                bool primeiroDoProjeto = true;
                for (auto& valor : usados) {
                    if (jaListados.count(valor)) continue;
                    if (primeiroDoProjeto) {
                        combo->addSeparator();
                        primeiroDoProjeto = false;
                    }
                    combo->addItem(valor, proximoId++);
                }

                combo->setText(valorAtual, juce::dontSendNotification);
                combo->onChange = [this, linha] { commitLinha(*linha); };
                return combo;
            }
            default: {
                auto editor = std::make_unique<juce::TextEditor>();
                editor->setText(valorAtual, false);
                if (campo.tipo == CampoTipo::Data) editor->setTextToShowWhenEmpty("AAAA-MM-DD", matriz::ui::tema().textoTerciario);
                editor->onFocusLost = [this, linha] { commitLinha(*linha); };
                editor->onReturnKey = [this, linha] { commitLinha(*linha); };
                return editor;
            }
        }
    }

    juce::String lerValorEditor(const LinhaCampo& linha) const {
        if (linha.editorTabela) return linha.editorTabela->getValorJson();
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(linha.editorSimples.get()))
            return toggle->getToggleState() ? "true" : "false";
        if (auto* combo = dynamic_cast<juce::ComboBox*>(linha.editorSimples.get())) {
            if (linha.campo->tipo == CampoTipo::Opcao) {
                // addItem() mostra o rótulo traduzido — o valor gravado
                // continua sendo o token bruto do YAML, lido de volta pelo
                // índice selecionado, nunca por getText() (que devolveria
                // o texto traduzido e mudaria o dado já salvo).
                int indice = combo->getSelectedId() - 1;
                if (indice >= 0 && indice < static_cast<int>(linha.campo->opcoes.size()))
                    return linha.campo->opcoes[static_cast<size_t>(indice)];
                return {};
            }
            return combo->getText(); // OpcaoLivre: texto digitado/selecionado é o valor direto
        }
        if (auto* editor = dynamic_cast<juce::TextEditor*>(linha.editorSimples.get())) return editor->getText();
        return {};
    }

    // Só a escrita no banco + cache local de valores_ — sem tocar em cor de
    // validação, visibilidade ou relayout. Usado tanto pelo commit normal
    // (blur/Enter/onChange) quanto pelo commit forçado em limpar(), onde os
    // widgets estão prestes a ser destruídos e mexer neles seria inútil ou
    // perigoso (relayoutEExibir() dispararia sobre uma árvore que já não
    // existe mais logo em seguida).
    void salvarValorSemEfeitosColaterais(LinhaCampo& linha) {
        if (!linha.editorSimples && !linha.editorTabela) return; // linha nunca teve editor construído
        juce::String valor = lerValorEditor(linha);
        matriz::ingest::aplicarFichaEmLote(projeto_.projeto().registro(), {itemId_}, linha.nivel, linha.nivelIndice,
                                            {{linha.campo->id, valor.toStdString()}}, autorAtual());
        valores_[chaveValor(linha.nivel, linha.nivelIndice, linha.campo->id)] = valor;
    }

    void commitLinha(LinhaCampo& linha) {
        salvarValorSemEfeitosColaterais(linha);
        juce::String valor = valores_[chaveValor(linha.nivel, linha.nivelIndice, linha.campo->id)];

        // Validação é só um aviso visual — nunca apaga nem impede o campo
        // inválido de ser salvo (Parte 2.1 da correção crítica): o texto
        // digitado já foi gravado acima, exatamente como o operador
        // escreveu, mesmo se a validação falhar.
        bool valido = true;
        if (linha.campo->validacao == "isrc") valido = valor.isEmpty() || validarIsrc(valor);
        else if (linha.campo->validacao == "ean13") valido = valor.isEmpty() || validarEan13(valor);
        else if (linha.campo->validacao == "soma_100" && linha.editorTabela)
            valido = std::abs(linha.editorTabela->somaColuna("percentual") - 100.0) < 0.01;

        auto corBase = valido ? matriz::ui::tema().textoSecundario : matriz::ui::tema().perigo;
        linha.rotulo->setColour(juce::Label::textColourId, corBase);

        if (!linha.campo->afeta.empty()) recomputarEfeitosAtivos();

        atualizarVisibilidade();
        relayoutEExibir();
    }

    void recomputarEfeitosAtivos() {
        efeitosAtivos_.clear();
        for (auto* linha : linhas_)
            if (!linha->campo->afeta.empty() && valores_[chaveValor(linha->nivel, linha->nivelIndice, linha->campo->id)].isNotEmpty())
                for (auto& efeito : linha->campo->afeta) efeitosAtivos_.insert(efeito);
    }

    void atualizarVisibilidade() {
        for (auto* linha : linhas_) {
            bool visivel = true;
            if (linha->campo->visivelSe) {
                std::string chave = chaveValor(linha->nivel, linha->nivelIndice, linha->campo->visivelSe->campoId);
                juce::String valorReferenciado = valores_.count(chave) ? valores_[chave] : juce::String();
                switch (linha->campo->visivelSe->op) {
                    case matriz::ficha::VisivelSeOperador::In:
                        visivel = std::find(linha->campo->visivelSe->valores.begin(), linha->campo->visivelSe->valores.end(),
                                             valorReferenciado.toStdString()) != linha->campo->visivelSe->valores.end();
                        break;
                    case matriz::ficha::VisivelSeOperador::Igual:
                        visivel = valorReferenciado.toStdString() == linha->campo->visivelSe->valores.front();
                        break;
                    case matriz::ficha::VisivelSeOperador::Diferente:
                        visivel = valorReferenciado.toStdString() != linha->campo->visivelSe->valores.front();
                        break;
                }
            }
            linha->visivel = visivel;
            linha->rotulo->setVisible(visivel);
            if (linha->editorSimples) linha->editorSimples->setVisible(visivel);
            if (linha->editorTabela) linha->editorTabela->setVisible(visivel);
            if (linha->indicador) linha->indicador->setVisible(visivel);
            if (linha->botaoConfirmar) linha->botaoConfirmar->setVisible(visivel);
            if (linha->alerta) linha->alerta->setVisible(visivel);
        }
    }

    void relayoutEExibir() {
        if (aoRelayoutNecessario) aoRelayoutNecessario();
    }

    ProjetoAberto& projeto_;
    std::string itemId_;
    std::string tipoAtual_; // pra chaves de i18n de campo/opção (FichaI18n.h)
    std::unique_ptr<juce::Label> cabecalho_;
    std::unique_ptr<juce::Label> mensagemNaoClassificado_; // não-nulo == item sem tipo_midia válido, ver construirSeletorTipoMidia
    std::vector<std::unique_ptr<juce::TextButton>> botoesTipoNaoClassificado_;
    std::unique_ptr<juce::TextButton> botaoCancelarRecategorizar_;
    std::unique_ptr<juce::TextButton> botaoRecategorizar_;
    // Persiste através de limpar()/construirParaItem, igual adicionandoObservacao_.
    bool recategorizando_ = false;
    juce::OwnedArray<LinhaCampo> linhas_;
    std::vector<Secao> secoes_;
    std::vector<LinhaCampo*> destaque_; // origem/ano (§4.2/§4.3) — não-owning, dono é linhas_
    SecaoArquivos arquivosEsperados_;
    SecaoObservacoes observacoes_;
    // Persiste através de limpar()/construirParaItem — é o que faz o botão
    // "Nova observação" reabrir o formulário depois do reconstruir.
    bool adicionandoObservacao_ = false;
    std::map<std::string, juce::String> valores_; // chaveValor -> valor atual (cache local pra visivel_se/afeta)
    std::set<std::string> efeitosAtivos_;
    std::vector<std::unique_ptr<juce::TextButton>> botoesAdicionarFaixa_;
    int proximoIndiceFaixa_ = 0;
};

// ---------------------------------------------------------------------------
// FichaLoteConteudo — ficha em modo lote (Acréscimos §12.2, item 8). Campos
// de nível raiz apenas: níveis aninhados repetidos (ex.: faixa de um
// release) não têm uma correspondência óbvia entre itens diferentes da
// seleção — editar "faixa 3" em lote não faz sentido quando cada item pode
// ter uma quantidade de faixas diferente. Gap declarado, não escondido.
// Tabela e lista_pessoas também ficam de fora desta etapa pelo mesmo
// motivo de agregação ambígua (§12.3 completo é maior que cabe aqui).
// ---------------------------------------------------------------------------

class FichaLoteConteudo : public juce::Component {
public:
    explicit FichaLoteConteudo(ProjetoAberto& projeto) : projeto_(projeto) {}

    void mostrarSelecao(std::vector<std::string> itemIds) {
        limpar();
        itemIds_ = std::move(itemIds);
        if (itemIds_.empty()) {
            relayoutEExibir();
            return;
        }

        cabecalho_ = std::make_unique<juce::Label>();
        cabecalho_->setText(matriz::i18n::t("ficha.lote_titulo").replace("{n}", juce::String((int)itemIds_.size())),
                             juce::dontSendNotification);
        cabecalho_->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteSubtitulo, juce::Font::bold)));
        cabecalho_->setColour(juce::Label::textColourId, matriz::ui::tema().textoPrimario);
        addAndMakeVisible(*cabecalho_);

        std::set<std::string> tiposPresentes;
        bool algumNulo = false;
        for (auto& id : itemIds_) {
            auto stmt = projeto_.projeto().registro().prepare("SELECT tipo_midia FROM item WHERE id = ?");
            stmt.bind(1, matriz::db::Value::of(id));
            if (!stmt.step()) continue;
            if (stmt.columnIsNull(0)) algumNulo = true;
            else tiposPresentes.insert(stmt.columnText(0));
        }

        if (tiposPresentes.size() == 1 && !algumNulo) {
            tipoComum_ = *tiposPresentes.begin();
            try {
                construirCampos();
            } catch (const std::exception&) {
                // tipo_midia gravado não bate com nenhuma fichas/*.yaml
                // (dado inesperado, não deveria acontecer) — mesmo
                // tratamento defensivo da ficha individual: nunca lança
                // pro clique do operador, mostra a mensagem de tipo misto
                // em vez de derrubar o app.
                construirMensagemMisto();
            }
        } else if (tiposPresentes.empty() && algumNulo) {
            construirSeletorTipoMidia();
        } else if (!tiposPresentes.empty()) {
            // Tipos diferentes na seleção: ainda dá pra editar em lote os
            // campos universais (origem/ano). Qualquer uma das definições
            // presentes serve pra pegar a declaração desses dois campos —
            // são idênticos nas 19 fichas.
            tipoComum_ = *tiposPresentes.begin();
            try {
                construirCampos(/*somenteUniversais=*/true);
            } catch (const std::exception&) {
                construirMensagemMisto();
            }
        } else {
            construirMensagemMisto();
        }

        relayoutEExibir();
    }

    void relayout(int largura) {
        const auto& tk = matriz::ui::tema();
        int y = tk.espacoPainel;
        int x = tk.espacoPainel;
        int larguraUtil = largura - 2 * tk.espacoPainel;

        if (cabecalho_) {
            cabecalho_->setBounds(x, y, larguraUtil, 24);
            y += 24 + tk.espacoGrande;
        }
        if (mensagem_) {
            mensagem_->setBounds(x, y, larguraUtil, 60);
            y += 60 + tk.espacoMedio;
        }
        for (auto& b : botoesTipo_) {
            b->setBounds(x, y, larguraUtil, 26);
            y += 26 + tk.espacoPequeno;
        }
        for (auto* linha : linhas_) {
            linha->rotulo->setBounds(x, y, larguraUtil, 18);
            y += 18;
            linha->editor->setBounds(x, y, larguraUtil, 24);
            y += 24 + tk.espacoPequeno;
        }
        if (avisoCamposNaoSuportados_) {
            avisoCamposNaoSuportados_->setBounds(x, y, larguraUtil, 32);
            y += 32 + tk.espacoMedio;
        }
        if (previa_) {
            previa_->setBounds(x, y, larguraUtil, 40);
            y += 40 + tk.espacoPequeno;
        }
        if (botaoAplicar_) {
            botaoAplicar_->setBounds(x, y, 120, 28);
            if (botaoDesfazer_) botaoDesfazer_->setBounds(x + 128, y, 120, 28);
            y += 28 + tk.espacoMedio;
        }
        if (resultado_) {
            resultado_->setBounds(x, y, larguraUtil, 20);
            y += 20 + tk.espacoMedio;
        }

        setSize(largura, y + tk.espacoPainel);
    }

    std::function<void()> aoRelayoutNecessario;
    std::function<void()> aoAplicarEmLote; // ficha mudou algo que afeta contagens em outro lugar (tipo, campos)

    // Introspecção pra teste (mesmo padrão de FichaConteudo::
    // editorDoCampoParaTeste): acesso direto ao editor de um campo do modo
    // lote, pra simular edição sem depender de foco/blur real.
    juce::Component* editorDoCampoParaTeste(const std::string& campoId) {
        for (auto* linha : linhas_)
            if (linha->campo->id == campoId) return linha->editor.get();
        return nullptr;
    }
    // Idem, pro botão de aplicar tipo de mídia à seleção (§12.3).
    juce::TextButton* botaoTipoMidiaParaTeste(const std::string& tipoId) {
        auto opcoes = listarTiposMidiaDisponiveis(projeto_);
        for (size_t i = 0; i < opcoes.size() && i < botoesTipo_.size(); ++i)
            if (opcoes[i].id == tipoId) return botoesTipo_[i].get();
        return nullptr;
    }
    juce::TextButton* botaoAplicarParaTeste() { return botaoAplicar_.get(); }
    juce::TextButton* botaoDesfazerParaTeste() { return botaoDesfazer_.get(); }

private:
    struct LinhaLote {
        const Campo* campo = nullptr;
        std::unique_ptr<juce::Label> rotulo;
        std::unique_ptr<juce::Component> editor;
        bool tocado = false;
    };

    void limpar() {
        cabecalho_.reset();
        mensagem_.reset();
        botoesTipo_.clear();
        linhas_.clear();
        avisoCamposNaoSuportados_.reset();
        previa_.reset();
        botaoAplicar_.reset();
        botaoDesfazer_.reset();
        resultado_.reset();
        undoSnapshot_.clear();
        setSize(getWidth(), 0);
    }

    std::vector<const Campo*> camposRaiz(const FichaDefinition& def) const {
        std::vector<const Campo*> out;
        if (def.usaNiveis()) {
            for (auto& c : def.camposPorNivel[0].second) out.push_back(&c);
        } else {
            for (auto& g : def.grupos)
                for (auto& c : g.campos) out.push_back(&c);
        }
        return out;
    }

    void construirMensagemMisto() {
        mensagem_ = std::make_unique<juce::Label>();
        mensagem_->setText(matriz::i18n::t("ficha.lote_tipo_misto"), juce::dontSendNotification);
        mensagem_->setColour(juce::Label::textColourId, matriz::ui::tema().textoSecundario);
        mensagem_->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteCorpo)));
        addAndMakeVisible(*mensagem_);
    }

    void construirSeletorTipoMidia() {
        mensagem_ = std::make_unique<juce::Label>();
        mensagem_->setText(matriz::i18n::t("ficha.lote_escolher_tipo"), juce::dontSendNotification);
        mensagem_->setColour(juce::Label::textColourId, matriz::ui::tema().textoSecundario);
        mensagem_->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteCorpo)));
        addAndMakeVisible(*mensagem_);

        for (auto& opcao : listarTiposMidiaDisponiveis(projeto_)) {
            auto botao = std::make_unique<juce::TextButton>(opcao.rotulo);
            std::string tipoId = opcao.id;
            botao->onClick = [this, tipoId] { aplicarTipoMidia(tipoId); };
            addAndMakeVisible(*botao);
            botoesTipo_.push_back(std::move(botao));
        }
    }

    void aplicarTipoMidia(const std::string& tipo) {
        std::string agora = matriz::model::agoraIso8601();
        auto& registro = projeto_.projeto().registro();
        registro.run("BEGIN", {});
        try {
            for (auto& id : itemIds_) {
                registro.run("UPDATE item SET tipo_midia = ?, atualizado_em = ? WHERE id = ?",
                              {matriz::db::Value::of(tipo), matriz::db::Value::of(agora), matriz::db::Value::of(id)});
                aplicarOrigemPadrao(registro, id, tipo);
            }
            registro.run("COMMIT", {});
        } catch (...) {
            registro.run("ROLLBACK", {});
            throw;
        }
        if (aoAplicarEmLote) aoAplicarEmLote();
        mostrarSelecao(itemIds_); // reconstrói — agora com tipo comum, mostra os campos de verdade
    }

    struct EstadoCampo {
        bool iguais = true;
        juce::String valorComum;
    };

    EstadoCampo estadoDoCampo(const Campo& campo) const {
        EstadoCampo e;
        bool primeiro = true;
        for (auto& id : itemIds_) {
            juce::String valor = projeto_.valorCampo(id, "raiz", 0, campo.id).value_or(std::string());
            if (primeiro) {
                e.valorComum = valor;
                primeiro = false;
            } else if (valor != e.valorComum) {
                e.iguais = false;
            }
        }
        return e;
    }

    // somenteUniversais: seleção com tipos de mídia diferentes. Os campos
    // específicos de cada tipo não têm como ser editados juntos (não existem
    // nas duas fichas), mas origem e ano existem em TODAS as 19 — e o item
    // 4.3 pede o ano "editável em lote com um clique". Então em vez de não
    // oferecer nada, oferece exatamente os campos que são comuns a tudo.
    void construirCampos(bool somenteUniversais = false) {
        const FichaDefinition& def = projeto_.definicaoPara(tipoComum_);
        bool algumCampoTabela = false;

        for (auto* campo : camposRaiz(def)) {
            if (somenteUniversais && campo->id != "origem" && campo->id != "ano") continue;
            if (campo->tipo == CampoTipo::Tabela || campo->tipo == CampoTipo::ListaPessoas) {
                algumCampoTabela = true;
                continue;
            }

            EstadoCampo estado = estadoDoCampo(*campo);

            auto* linha = linhas_.add(new LinhaLote());
            linha->campo = campo;
            linha->rotulo = std::make_unique<juce::Label>();
            linha->rotulo->setText(matriz::ficha::rotuloCampo(tipoComum_, *campo), juce::dontSendNotification);
            linha->rotulo->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFontePequena)));
            linha->rotulo->setColour(juce::Label::textColourId, matriz::ui::tema().textoSecundario);
            addAndMakeVisible(*linha->rotulo);

            linha->editor = criarEditorLote(*campo, estado, linha);
            addAndMakeVisible(*linha->editor);
        }

        if (algumCampoTabela || somenteUniversais) {
            avisoCamposNaoSuportados_ = std::make_unique<juce::Label>();
            avisoCamposNaoSuportados_->setText(matriz::i18n::t(somenteUniversais ? "ficha.lote_tipo_misto_universais"
                                                                                 : "ficha.lote_campos_nao_suportados"),
                                                juce::dontSendNotification);
            avisoCamposNaoSuportados_->setColour(juce::Label::textColourId, matriz::ui::tema().textoTerciario);
            avisoCamposNaoSuportados_->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFontePequena)));
            addAndMakeVisible(*avisoCamposNaoSuportados_);
        }

        previa_ = std::make_unique<juce::Label>();
        previa_->setColour(juce::Label::textColourId, matriz::ui::tema().textoSecundario);
        previa_->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFontePequena)));
        addAndMakeVisible(*previa_);

        botaoAplicar_ = std::make_unique<juce::TextButton>(matriz::i18n::t("ficha.lote_aplicar"));
        botaoAplicar_->onClick = [this] { aplicar(); };
        addAndMakeVisible(*botaoAplicar_);

        botaoDesfazer_ = std::make_unique<juce::TextButton>(matriz::i18n::t("ficha.lote_desfazer"));
        botaoDesfazer_->onClick = [this] { desfazer(); };
        // addAndMakeVisible() força visible=true — setVisible(false) tem
        // que vir DEPOIS, senão o botão de desfazer aparece antes de
        // qualquer aplicação bem-sucedida (bug real, achado pelo harness).
        addAndMakeVisible(*botaoDesfazer_);
        botaoDesfazer_->setVisible(false);

        resultado_ = std::make_unique<juce::Label>();
        resultado_->setColour(juce::Label::textColourId, matriz::ui::tema().textoSecundario);
        resultado_->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFontePequena)));
        addAndMakeVisible(*resultado_);

        atualizarPrevia();
    }

    std::unique_ptr<juce::Component> criarEditorLote(const Campo& campo, const EstadoCampo& estado, LinhaLote* linha) {
        switch (campo.tipo) {
            case CampoTipo::Booleano: {
                auto toggle = std::make_unique<juce::ToggleButton>();
                if (estado.iguais) {
                    toggle->setToggleState(estado.valorComum == "true", juce::dontSendNotification);
                    toggle->setButtonText(estado.valorComum == "true" ? matriz::i18n::t("ficha.campo_booleano_sim")
                                                                        : matriz::i18n::t("ficha.campo_booleano_nao"));
                } else {
                    toggle->setButtonText(matriz::i18n::t("ficha.lote_valores_multiplos"));
                }
                toggle->onClick = [this, linha, t = toggle.get()] {
                    linha->tocado = true;
                    t->setButtonText(t->getToggleState() ? matriz::i18n::t("ficha.campo_booleano_sim")
                                                          : matriz::i18n::t("ficha.campo_booleano_nao"));
                    atualizarPrevia();
                };
                return toggle;
            }
            case CampoTipo::Opcao: {
                auto combo = std::make_unique<juce::ComboBox>();
                for (size_t i = 0; i < campo.opcoes.size(); ++i)
                    combo->addItem(matriz::ficha::rotuloOpcao(tipoComum_, campo, static_cast<int>(i), campo.opcoes[i]),
                                   static_cast<int>(i) + 1);
                if (estado.iguais) {
                    int idSelecionado = 0;
                    for (size_t i = 0; i < campo.opcoes.size(); ++i)
                        if (campo.opcoes[i] == estado.valorComum.toStdString()) idSelecionado = static_cast<int>(i) + 1;
                    combo->setSelectedId(idSelecionado, juce::dontSendNotification);
                } else {
                    combo->setTextWhenNothingSelected(matriz::i18n::t("ficha.lote_valores_multiplos"));
                }
                combo->onChange = [this, linha] {
                    linha->tocado = true;
                    atualizarPrevia();
                };
                return combo;
            }
            case CampoTipo::OpcaoLivre: {
                auto combo = std::make_unique<juce::ComboBox>();
                combo->setEditableText(true);
                for (size_t i = 0; i < campo.opcoes.size(); ++i)
                    combo->addItem(matriz::ficha::rotuloOpcao(tipoComum_, campo, static_cast<int>(i), campo.opcoes[i]),
                                   static_cast<int>(i) + 1);
                if (estado.iguais) combo->setText(estado.valorComum, juce::dontSendNotification);
                else combo->setTextWhenNothingSelected(matriz::i18n::t("ficha.lote_valores_multiplos"));
                combo->onChange = [this, linha] {
                    linha->tocado = true;
                    atualizarPrevia();
                };
                return combo;
            }
            default: {
                auto editor = std::make_unique<juce::TextEditor>();
                if (estado.iguais) editor->setText(estado.valorComum, false);
                else editor->setTextToShowWhenEmpty(matriz::i18n::t("ficha.lote_valores_multiplos"), matriz::ui::tema().textoTerciario);
                editor->onTextChange = [this, linha] {
                    linha->tocado = true;
                    atualizarPrevia();
                };
                return editor;
            }
        }
    }

    juce::String lerValorEditor(const LinhaLote& linha) const {
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(linha.editor.get()))
            return toggle->getToggleState() ? "true" : "false";
        if (auto* combo = dynamic_cast<juce::ComboBox*>(linha.editor.get())) {
            if (linha.campo->tipo == CampoTipo::Opcao) {
                int indice = combo->getSelectedId() - 1;
                if (indice >= 0 && indice < static_cast<int>(linha.campo->opcoes.size()))
                    return linha.campo->opcoes[static_cast<size_t>(indice)];
                return {};
            }
            return combo->getText();
        }
        if (auto* editor = dynamic_cast<juce::TextEditor*>(linha.editor.get())) return editor->getText();
        return {};
    }

    void atualizarPrevia() {
        juce::StringArray partes;
        for (auto* linha : linhas_)
            if (linha->tocado) partes.add(matriz::ficha::rotuloCampo(tipoComum_, *linha->campo) + "=" + lerValorEditor(*linha));

        if (partes.isEmpty()) {
            previa_->setText(matriz::i18n::t("ficha.lote_nenhum_campo_tocado"), juce::dontSendNotification);
            botaoAplicar_->setEnabled(false);
        } else {
            previa_->setText(matriz::i18n::t("ficha.lote_previa")
                                  .replace("{n}", juce::String((int)itemIds_.size()))
                                  .replace("{campos}", partes.joinIntoString(", ")),
                              juce::dontSendNotification);
            botaoAplicar_->setEnabled(true);
        }
    }

    void aplicar() {
        std::map<std::string, std::string> valores;
        for (auto* linha : linhas_)
            if (linha->tocado) valores[linha->campo->id] = lerValorEditor(*linha).toStdString();
        if (valores.empty()) return;

        // Snapshot do valor ANTERIOR de cada item, só pros campos tocados —
        // é o que permite desfazer em um passo (§12.2).
        undoSnapshot_.clear();
        for (auto& id : itemIds_) {
            std::map<std::string, std::string> anterior;
            for (auto& [campoId, novoValor] : valores)
                anterior[campoId] = projeto_.valorCampo(id, "raiz", 0, campoId).value_or(std::string());
            undoSnapshot_[id] = anterior;
        }

        // Um item por vez, cada um na sua própria transação (dentro de
        // aplicarFichaEmLote): falha num item não derruba os que já foram
        // gravados — "aplicação parcial não corrompe" (§12.2), nunca um
        // BEGIN/COMMIT gigante que desfaz tudo se um único item falhar.
        int sucessos = 0;
        int falhas = 0;
        for (auto& id : itemIds_) {
            try {
                matriz::ingest::aplicarFichaEmLote(projeto_.projeto().registro(), {id}, "raiz", 0, valores, autorAtual());
                ++sucessos;
            } catch (const std::exception&) {
                ++falhas;
            }
        }

        juce::String texto = matriz::i18n::t("ficha.lote_resultado").replace("{sucessos}", juce::String(sucessos));
        if (falhas > 0) texto += matriz::i18n::t("ficha.lote_resultado_falhas").replace("{falhas}", juce::String(falhas));
        resultado_->setText(texto, juce::dontSendNotification);
        botaoDesfazer_->setVisible(sucessos > 0);
        relayoutEExibir();

        if (aoAplicarEmLote) aoAplicarEmLote();
    }

    void desfazer() {
        int restaurados = 0;
        for (auto& [id, valoresAnteriores] : undoSnapshot_) {
            try {
                matriz::ingest::aplicarFichaEmLote(projeto_.projeto().registro(), {id}, "raiz", 0, valoresAnteriores, autorAtual());
                ++restaurados;
            } catch (const std::exception&) {
            }
        }
        resultado_->setText(matriz::i18n::t("ficha.lote_resultado_desfeito").replace("{n}", juce::String(restaurados)),
                             juce::dontSendNotification);
        botaoDesfazer_->setVisible(false);
        undoSnapshot_.clear();
        if (aoAplicarEmLote) aoAplicarEmLote();
    }

    void relayoutEExibir() {
        if (aoRelayoutNecessario) aoRelayoutNecessario();
    }

    ProjetoAberto& projeto_;
    std::vector<std::string> itemIds_;
    std::string tipoComum_;
    std::unique_ptr<juce::Label> cabecalho_;
    std::unique_ptr<juce::Label> mensagem_; // tipo misto OU instrução do seletor de tipo
    std::vector<std::unique_ptr<juce::TextButton>> botoesTipo_;
    juce::OwnedArray<LinhaLote> linhas_;
    std::unique_ptr<juce::Label> avisoCamposNaoSuportados_;
    std::unique_ptr<juce::Label> previa_;
    std::unique_ptr<juce::TextButton> botaoAplicar_;
    std::unique_ptr<juce::TextButton> botaoDesfazer_;
    std::unique_ptr<juce::Label> resultado_;
    std::map<std::string, std::map<std::string, std::string>> undoSnapshot_; // itemId -> {campoId -> valor anterior}
};

// ---------------------------------------------------------------------------
// FichaPanelComponent
// ---------------------------------------------------------------------------

FichaPanelComponent::FichaPanelComponent(ProjetoAberto& projeto) : projeto_(projeto) {
    conteudo_ = std::make_unique<FichaConteudo>(projeto);
    viewport_ = std::make_unique<juce::Viewport>();
    viewport_->setViewedComponent(conteudo_.get(), false);
    addAndMakeVisible(*viewport_);

    conteudo_->aoRelayoutNecessario = [this] { conteudo_->relayout(viewport_->getWidth() - viewport_->getScrollBarThickness()); };
    conteudo_->aoMudarClassificacao = [this] { if (aoAplicarEmLote) aoAplicarEmLote(); };

    metadadosOriginais_ = std::make_unique<MetadadosOriginaisComponent>(projeto);
    metadadosOriginais_->aoMudarAltura = [this] { resized(); repaint(); };
    addAndMakeVisible(*metadadosOriginais_);
}

FichaPanelComponent::~FichaPanelComponent() = default;

juce::Component* FichaPanelComponent::editorDoCampoParaTeste(const std::string& nivel, int nivelIndice,
                                                               const std::string& campoId) {
    return conteudo_->editorDoCampoParaTeste(nivel, nivelIndice, campoId);
}

juce::Component* FichaPanelComponent::editorDoCampoLoteParaTeste(const std::string& campoId) {
    return conteudoLote_ ? conteudoLote_->editorDoCampoParaTeste(campoId) : nullptr;
}

juce::TextButton* FichaPanelComponent::botaoTipoMidiaLoteParaTeste(const std::string& tipoId) {
    return conteudoLote_ ? conteudoLote_->botaoTipoMidiaParaTeste(tipoId) : nullptr;
}

juce::TextButton* FichaPanelComponent::botaoAplicarLoteParaTeste() {
    return conteudoLote_ ? conteudoLote_->botaoAplicarParaTeste() : nullptr;
}

juce::TextButton* FichaPanelComponent::botaoDesfazerLoteParaTeste() {
    return conteudoLote_ ? conteudoLote_->botaoDesfazerParaTeste() : nullptr;
}

juce::TextButton* FichaPanelComponent::botaoTipoMidiaIndividualParaTeste(const std::string& tipoId) {
    return conteudo_ ? conteudo_->botaoTipoMidiaParaTeste(tipoId) : nullptr;
}

void FichaPanelComponent::mostrarItem(const std::string& itemId) {
    itemIdAtual_ = itemId;
    modoLote_ = false;
    if (metadadosOriginais_) {
        metadadosOriginais_->mostrarItem(itemId);
        metadadosOriginais_->setVisible(!itemId.empty());
    }
    viewport_->setViewedComponent(conteudo_.get(), false);
    conteudo_->construirParaItem(itemId);
    resized();
    repaint();
}

void FichaPanelComponent::mostrarSelecao(const std::vector<std::string>& itemIds) {
    if (itemIds.size() <= 1) {
        mostrarItem(itemIds.empty() ? std::string() : itemIds.front());
        return;
    }

    itemIdAtual_.clear();
    modoLote_ = true;
    // Em lote não existe "o metadado original" — são N arquivos diferentes.
    if (metadadosOriginais_) {
        metadadosOriginais_->mostrarItem({});
        metadadosOriginais_->setVisible(false);
    }
    if (!conteudoLote_) {
        conteudoLote_ = std::make_unique<FichaLoteConteudo>(projeto_);
        conteudoLote_->aoRelayoutNecessario = [this] {
            conteudoLote_->relayout(viewport_->getWidth() - viewport_->getScrollBarThickness());
        };
        conteudoLote_->aoAplicarEmLote = [this] { if (aoAplicarEmLote) aoAplicarEmLote(); };
    }
    viewport_->setViewedComponent(conteudoLote_.get(), false);
    conteudoLote_->mostrarSelecao(itemIds);
    resized();
    repaint();
}

void FichaPanelComponent::paint(juce::Graphics& g) {
    g.fillAll(matriz::ui::tema().painel);

    // Cabeçalho da metade editável, desenhado na faixa reservada por
    // resized(). Sem ele as duas seções encostariam uma na outra e o
    // operador não teria como saber onde termina o que a máquina leu e
    // começa o que ele mesmo escreveu.
    if (metadadosOriginais_ && metadadosOriginais_->isVisible()) {
        const auto& tk = matriz::ui::tema();
        juce::Rectangle<int> faixa(0, metadadosOriginais_->getBottom(), getWidth(), kAlturaCabecalhoFicha);
        g.setColour(tk.painelAlt);
        g.fillRect(faixa);
        auto miolo = faixa.reduced(tk.espacoMedio, 0);
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::bold)));
        g.drawText(matriz::i18n::t("metadados.editaveis").toUpperCase(), miolo, juce::Justification::centredLeft);
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        g.drawText(matriz::i18n::t("metadados.editaveis_ajuda"), miolo, juce::Justification::centredRight);
    }
    if (!modoLote_ && itemIdAtual_.empty()) {
        g.setColour(matriz::ui::tema().textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteCorpo)));
        g.drawText(matriz::i18n::t("ficha.vazia"), getLocalBounds().reduced(matriz::ui::tema().espacoPainel),
                   juce::Justification::centredTop, true);
    }
}

void FichaPanelComponent::resized() {
    auto area = getLocalBounds();

    // Seção de metadado original no topo; a ficha (editável) fica com o
    // resto. Só aparece com UM item selecionado — ver mostrarSelecao().
    if (metadadosOriginais_ && metadadosOriginais_->isVisible()) {
        metadadosOriginais_->setBounds(area.removeFromTop(metadadosOriginais_->alturaDesejada()));
        area.removeFromTop(kAlturaCabecalhoFicha);
    }

    viewport_->setBounds(area);
    int largura = viewport_->getWidth() - viewport_->getScrollBarThickness();
    if (modoLote_ && conteudoLote_) conteudoLote_->relayout(largura);
    else conteudo_->relayout(largura);
}

} // namespace matriz::ui
