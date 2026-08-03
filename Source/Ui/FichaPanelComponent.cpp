#include "FichaPanelComponent.h"

#include "../I18n/Strings.h"
#include "../Ingest/FluxoLote.h"
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
        linhas_.clear();
        secoes_.clear();
        cabecalho_.reset();
        arquivosEsperados_.titulo.reset();
        arquivosEsperados_.linhas.clear();
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

        const FichaDefinition& def = projeto_.definicaoPara(tipoMidia);

        carregarValores(itemId, def);

        if (def.usaNiveis()) {
            construirSecao(def.rotulo.empty() ? def.tipo : def.rotulo, def.camposPorNivel[0].second, "raiz", 0);

            const std::string& nivelRepetido = def.niveis[1];
            const auto* camposFaixa = def.camposDoNivel(nivelRepetido);
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
            for (auto& grupo : def.grupos) construirSecao(grupo.rotulo, grupo.campos, "raiz", 0);
        }

        construirSecaoArquivosEsperados(itemId, def);

        atualizarVisibilidade();
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

        setSize(largura, y + tk.espacoPainel);
    }

    std::function<void()> aoRelayoutNecessario;

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

    void construirSecao(const juce::String& tituloSecao, const std::vector<Campo>& campos, const std::string& nivel,
                         int nivelIndice, bool comBotaoRemover = false) {
        Secao secao;
        secao.titulo = std::make_unique<juce::Label>();
        secao.titulo->setText(tituloSecao, juce::dontSendNotification);
        secao.titulo->setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteCorpo, juce::Font::bold)));
        secao.titulo->setColour(juce::Label::textColourId, matriz::ui::tema().textoSecundario);
        addAndMakeVisible(*secao.titulo);

        for (auto& campo : campos) secao.linhas.push_back(construirLinha(campo, nivel, nivelIndice));

        secoes_.push_back(std::move(secao));
        juce::ignoreUnused(comBotaoRemover);
    }

    LinhaCampo* construirLinha(const Campo& campo, const std::string& nivel, int nivelIndice) {
        auto* linha = linhas_.add(new LinhaCampo());
        linha->campo = &campo;
        linha->nivel = nivel;
        linha->nivelIndice = nivelIndice;

        juce::String textoRotulo = campo.rotulo + (campo.obrigatorio ? " *" : "");
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
            linha->alerta->setText(juce::String(matriz::i18n::t("ficha.alerta_rotulo")) + ": " + campo.alertaSeTrue,
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
                auto combo = std::make_unique<juce::ComboBox>();
                int idSelecionado = 0;
                for (size_t i = 0; i < campo.opcoes.size(); ++i) {
                    combo->addItem(campo.opcoes[i], static_cast<int>(i) + 1);
                    if (campo.opcoes[i] == valorAtual.toStdString()) idSelecionado = static_cast<int>(i) + 1;
                }
                combo->setSelectedId(idSelecionado, juce::dontSendNotification);
                combo->onChange = [this, linha] { commitLinha(*linha); };
                return combo;
            }
            case CampoTipo::OpcaoLivre: {
                auto combo = std::make_unique<juce::ComboBox>();
                combo->setEditableText(true); // "opção livre (autocompletar + criar)" — texto digitado vira o valor direto
                for (size_t i = 0; i < campo.opcoes.size(); ++i) combo->addItem(campo.opcoes[i], static_cast<int>(i) + 1);
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
        if (auto* combo = dynamic_cast<juce::ComboBox*>(linha.editorSimples.get())) return combo->getText();
        if (auto* editor = dynamic_cast<juce::TextEditor*>(linha.editorSimples.get())) return editor->getText();
        return {};
    }

    void commitLinha(LinhaCampo& linha) {
        juce::String valor = lerValorEditor(linha);

        bool valido = true;
        if (linha.campo->validacao == "isrc") valido = valor.isEmpty() || validarIsrc(valor);
        else if (linha.campo->validacao == "ean13") valido = valor.isEmpty() || validarEan13(valor);
        else if (linha.campo->validacao == "soma_100" && linha.editorTabela)
            valido = std::abs(linha.editorTabela->somaColuna("percentual") - 100.0) < 0.01;

        auto corBase = valido ? matriz::ui::tema().textoSecundario : matriz::ui::tema().perigo;
        linha.rotulo->setColour(juce::Label::textColourId, corBase);

        matriz::ingest::aplicarFichaEmLote(projeto_.projeto().registro(), {itemId_}, linha.nivel, linha.nivelIndice,
                                            {{linha.campo->id, valor.toStdString()}}, autorAtual());

        valores_[chaveValor(linha.nivel, linha.nivelIndice, linha.campo->id)] = valor;

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
    std::unique_ptr<juce::Label> cabecalho_;
    juce::OwnedArray<LinhaCampo> linhas_;
    std::vector<Secao> secoes_;
    SecaoArquivos arquivosEsperados_;
    std::map<std::string, juce::String> valores_; // chaveValor -> valor atual (cache local pra visivel_se/afeta)
    std::set<std::string> efeitosAtivos_;
    std::vector<std::unique_ptr<juce::TextButton>> botoesAdicionarFaixa_;
    int proximoIndiceFaixa_ = 0;
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
}

FichaPanelComponent::~FichaPanelComponent() = default;

void FichaPanelComponent::mostrarItem(const std::string& itemId) {
    itemIdAtual_ = itemId;
    conteudo_->construirParaItem(itemId);
    resized();
    repaint();
}

void FichaPanelComponent::paint(juce::Graphics& g) {
    g.fillAll(matriz::ui::tema().painel);
    if (itemIdAtual_.empty()) {
        g.setColour(matriz::ui::tema().textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(matriz::ui::tema().tamanhoFonteCorpo)));
        g.drawText(matriz::i18n::t("ficha.vazia"), getLocalBounds().reduced(matriz::ui::tema().espacoPainel),
                   juce::Justification::centredTop, true);
    }
}

void FichaPanelComponent::resized() {
    viewport_->setBounds(getLocalBounds());
    conteudo_->relayout(viewport_->getWidth() - viewport_->getScrollBarThickness());
}

} // namespace matriz::ui
