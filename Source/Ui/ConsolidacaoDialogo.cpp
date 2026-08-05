#include "ConsolidacaoDialogo.h"

#include "../App/Cancelamento.h"
#include "../Catalogo/CatalogoProxies.h"

#include "../Consolidacao/Consolidacao.h"
#include "../Ficha/FichaI18n.h"
#include "../I18n/Strings.h"
#include "Tokens.h"

#include <JuceHeader.h>

namespace matriz::ui {

namespace {

// Lista rolável da prévia — original -> final, conflito destacado em
// vermelho (§11.6). Um Component simples pintado à mão, mesmo padrão de
// ArvoreComponent/FiltrosComponent — não precisa de mais que isso pra uma
// lista de texto.
class ListaPreviaConsolidacao : public juce::Component {
public:
    void definirPlano(const matriz::consolidacao::PlanoConsolidacao* plano) {
        plano_ = plano;
        setSize(getWidth(), calcularAltura());
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.painel);

        if (!plano_ || plano_->itens.empty()) {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFonteCorpo)));
            g.drawText(matriz::i18n::t("consolidacao.previa_vazia"), getLocalBounds(), juce::Justification::centred);
            return;
        }

        int y = 0;
        for (auto& item : plano_->itens) {
            juce::Rectangle<int> linha(0, y, getWidth(), kAlturaLinha);
            if (item.emConflito) {
                g.setColour(tk.perigo.withAlpha(0.15f));
                g.fillRect(linha);
            } else if (item.jaConsolidado) {
                g.setColour(tk.painelAlt);
                g.fillRect(linha);
            }
            g.setColour(item.emConflito ? tk.perigo : (item.jaConsolidado ? tk.textoTerciario : tk.textoPrimario));
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            juce::String texto = item.nomeOriginal + "  →  " + item.caminhoRelativoDestino;
            if (item.jaConsolidado) texto += "  (" + matriz::i18n::t("consolidacao.ja_consolidado") + ")";
            g.drawText(texto, linha.reduced(6, 2), juce::Justification::centredLeft, true);
            y += kAlturaLinha;
        }
    }

    static constexpr int kAlturaLinha = 22;

private:
    int calcularAltura() const {
        if (!plano_ || plano_->itens.empty()) return 60;
        return static_cast<int>(plano_->itens.size()) * kAlturaLinha;
    }

    const matriz::consolidacao::PlanoConsolidacao* plano_ = nullptr;
};

juce::String formatarBytes(juce::int64 bytes) {
    return juce::File::descriptionOfSizeInBytes(bytes);
}

juce::String rotuloNivel(matriz::consolidacao::NivelHierarquia n) {
    return matriz::i18n::t("hierarquia.nivel_" + matriz::consolidacao::nivelHierarquiaToString(n));
}

// Lista de níveis arrastáveis (item 5.2 — "lista de níveis arrastáveis, onde
// o operador reordena, remove ou acrescenta"). Arrastar reordena na hora;
// clique no "x" remove; botão direito num espaço vazio acrescenta um nível
// que ainda não está na lista. Cada mudança dispara aoMudar, que replaneja e
// redesenha a prévia da árvore ao lado — antes de qualquer arquivo ser
// copiado (§5.2 "prévia sempre visível, atualizando a cada mudança").
class ListaNiveisHierarquia : public juce::Component {
public:
    std::function<void()> aoMudar;

    void definirHierarquia(matriz::consolidacao::HierarquiaBackup h) {
        hierarquia_ = std::move(h);
        setSize(getWidth(), std::max(kAlturaLinha, static_cast<int>(hierarquia_.size()) * kAlturaLinha + kAlturaLinha));
        repaint();
    }
    const matriz::consolidacao::HierarquiaBackup& hierarquia() const { return hierarquia_; }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.painel);
        for (size_t i = 0; i < hierarquia_.size(); ++i) {
            juce::Rectangle<int> linha(0, static_cast<int>(i) * kAlturaLinha, getWidth(), kAlturaLinha);
            bool arrastando = indiceArrastado_ == static_cast<int>(i);
            g.setColour(arrastando ? tk.acento.withAlpha(0.25f) : (i % 2 ? tk.painelAlt : tk.painel));
            g.fillRect(linha);
            g.setColour(tk.textoPrimario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            g.drawText(juce::String(static_cast<int>(i) + 1) + ".  " + rotuloNivel(hierarquia_[i]),
                        linha.reduced(6, 0).withTrimmedRight(24), juce::Justification::centredLeft, true);
            g.setColour(tk.textoTerciario);
            g.drawText("x", linha.removeFromRight(24), juce::Justification::centred);
        }
        juce::Rectangle<int> add(0, static_cast<int>(hierarquia_.size()) * kAlturaLinha, getWidth(), kAlturaLinha);
        g.setColour(tk.textoTerciario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena, juce::Font::italic)));
        g.drawText(matriz::i18n::t("hierarquia.acrescentar"), add.reduced(6, 0), juce::Justification::centredLeft, true);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        int indice = e.getPosition().y / kAlturaLinha;
        if (indice >= static_cast<int>(hierarquia_.size())) {
            mostrarMenuAcrescentar();
            return;
        }
        if (e.getPosition().x > getWidth() - 24) { // "x" — remover
            hierarquia_.erase(hierarquia_.begin() + indice);
            definirHierarquia(hierarquia_);
            if (aoMudar) aoMudar();
            return;
        }
        indiceArrastado_ = indice;
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (indiceArrastado_ < 0) return;
        int alvo = juce::jlimit(0, static_cast<int>(hierarquia_.size()) - 1, e.getPosition().y / kAlturaLinha);
        if (alvo == indiceArrastado_) return;
        auto nivel = hierarquia_[static_cast<size_t>(indiceArrastado_)];
        hierarquia_.erase(hierarquia_.begin() + indiceArrastado_);
        hierarquia_.insert(hierarquia_.begin() + alvo, nivel);
        indiceArrastado_ = alvo;
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (indiceArrastado_ < 0) return;
        indiceArrastado_ = -1;
        repaint();
        if (aoMudar) aoMudar();
    }

    static constexpr int kAlturaLinha = 24;

private:
    void mostrarMenuAcrescentar() {
        using N = matriz::consolidacao::NivelHierarquia;
        static const std::vector<N> todos = {N::Projeto,     N::Ano,      N::TipoMidia, N::TipoArquivo,
                                              N::Origem,      N::Artista,  N::PastaManual};
        juce::PopupMenu menu;
        int id = 1;
        std::vector<N> ofertados;
        for (auto n : todos) {
            if (std::find(hierarquia_.begin(), hierarquia_.end(), n) != hierarquia_.end()) continue;
            menu.addItem(id++, rotuloNivel(n));
            ofertados.push_back(n);
        }
        if (ofertados.empty()) return;
        menu.showMenuAsync(juce::PopupMenu::Options(), [this, ofertados](int resultado) {
            if (resultado < 1 || resultado > static_cast<int>(ofertados.size())) return;
            hierarquia_.push_back(ofertados[static_cast<size_t>(resultado - 1)]);
            definirHierarquia(hierarquia_);
            if (aoMudar) aoMudar();
        });
    }

    matriz::consolidacao::HierarquiaBackup hierarquia_;
    int indiceArrastado_ = -1;
};

// Prévia da árvore resultante (item 5.2 — "prévia da árvore resultante
// sempre visível ao lado, atualizando a cada mudança"). Deriva os caminhos
// de pasta do próprio plano já calculado, então mostra a estrutura REAL que
// vai ser criada, não uma simulação em paralelo que pode divergir.
class PreviaArvoreBackup : public juce::Component {
public:
    void definirPlano(const matriz::consolidacao::PlanoConsolidacao* plano) {
        linhas_.clear();
        if (plano) {
            std::set<juce::String> pastasVistas;
            for (auto& item : plano->itens) {
                juce::StringArray partes;
                partes.addTokens(item.caminhoRelativoDestino.upToLastOccurrenceOf("/", false, false), "/", "");
                partes.removeEmptyStrings();
                juce::String acumulado;
                for (int i = 0; i < partes.size(); ++i) {
                    acumulado += (i ? "/" : "") + partes[i];
                    if (!pastasVistas.insert(acumulado).second) continue;
                    linhas_.push_back(juce::String().paddedLeft(' ', i * 3) + partes[i] + "/");
                }
            }
        }
        setSize(getWidth(), std::max(60, static_cast<int>(linhas_.size()) * kAlturaLinha));
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto& tk = tema();
        g.fillAll(tk.painel);
        if (linhas_.empty()) {
            g.setColour(tk.textoTerciario);
            g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
            g.drawText(matriz::i18n::t("consolidacao.previa_vazia"), getLocalBounds(), juce::Justification::centred);
            return;
        }
        g.setColour(tk.textoSecundario);
        g.setFont(juce::Font(juce::FontOptions(tk.tamanhoFontePequena)));
        int y = 0;
        for (auto& l : linhas_) {
            g.drawText(l, juce::Rectangle<int>(6, y, getWidth() - 6, kAlturaLinha), juce::Justification::centredLeft, true);
            y += kAlturaLinha;
        }
    }

    static constexpr int kAlturaLinha = 18;

private:
    std::vector<juce::String> linhas_;
};

// Dois Components lado a lado dentro de um customComponent do AlertWindow
// (que só empilha verticalmente).
class PainelLadoALado : public juce::Component {
public:
    PainelLadoALado(juce::Component& esquerda, juce::Component& direita) : esquerda_(esquerda), direita_(direita) {
        addAndMakeVisible(esquerda_);
        addAndMakeVisible(direita_);
        setSize(460, 200);
    }
    void resized() override {
        auto area = getLocalBounds();
        esquerda_.setBounds(area.removeFromLeft(200));
        area.removeFromLeft(8);
        direita_.setBounds(area);
    }

private:
    juce::Component& esquerda_;
    juce::Component& direita_;
};

} // namespace

std::shared_ptr<juce::AlertWindow> mostrarDialogoConsolidacao(ProjetoAberto& projeto, std::function<void()> aoFechar) {
    struct EstadoVivo {
        std::shared_ptr<juce::AlertWindow> janela;
        std::shared_ptr<juce::Label> labelDestino;
        std::shared_ptr<juce::TextButton> botaoEscolherDestino;
        std::shared_ptr<juce::Viewport> viewportPrevia;
        std::shared_ptr<ListaPreviaConsolidacao> listaPrevia;
        std::shared_ptr<juce::Label> labelHierarquia;
        std::shared_ptr<ListaNiveisHierarquia> listaNiveis;
        std::shared_ptr<juce::Viewport> viewportArvore;
        std::shared_ptr<PreviaArvoreBackup> previaArvore;
        std::shared_ptr<PainelLadoALado> painelHierarquia;
        std::shared_ptr<juce::Label> labelAvisos;
        std::shared_ptr<juce::Label> labelResultado;
        std::shared_ptr<juce::TextButton> botaoConsolidar;
        std::shared_ptr<juce::TextButton> botaoCancelar;
        std::shared_ptr<juce::ToggleButton> caixaCatalogo;
        std::shared_ptr<juce::Label> labelCatalogo;
        juce::File destino;
        matriz::consolidacao::PlanoConsolidacao plano;
        bool consolidando = false;
        matriz::app::CancelamentoPtr cancelamento = matriz::app::novoCancelamento();
    };
    auto vivo = std::make_shared<EstadoVivo>();

    vivo->janela = std::make_shared<juce::AlertWindow>(matriz::i18n::t("consolidacao.titulo"), juce::String(),
                                                         juce::MessageBoxIconType::NoIcon);

    vivo->labelDestino = std::make_shared<juce::Label>();
    vivo->labelDestino->setText(matriz::i18n::t("consolidacao.destino_nao_escolhido"), juce::dontSendNotification);
    vivo->labelDestino->setColour(juce::Label::textColourId, tema().textoSecundario);
    vivo->labelDestino->setSize(460, 22);
    vivo->janela->addCustomComponent(vivo->labelDestino.get());

    vivo->botaoEscolherDestino = std::make_shared<juce::TextButton>(matriz::i18n::t("consolidacao.escolher_destino"));
    // Button(name) também define Component::getName() — AlertWindow desenha
    // o nome de todo customComponent como legenda acima dele (ver
    // juce_AlertWindow.cpp, drawFittedText(c->getName(), ...)), duplicando
    // visualmente o texto do próprio botão. Limpa o nome pra não afetar o
    // texto exibido (setButtonText/getButtonText são independentes disto).
    vivo->botaoEscolherDestino->setName("");
    vivo->botaoEscolherDestino->setSize(200, 28);
    vivo->janela->addCustomComponent(vivo->botaoEscolherDestino.get());

    // Hierarquia de pastas do backup (item 5.2): níveis arrastáveis à
    // esquerda, prévia da árvore resultante à direita, lado a lado.
    vivo->labelHierarquia = std::make_shared<juce::Label>();
    vivo->labelHierarquia->setText(matriz::i18n::t("hierarquia.titulo"), juce::dontSendNotification);
    vivo->labelHierarquia->setColour(juce::Label::textColourId, tema().textoSecundario);
    vivo->labelHierarquia->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena, juce::Font::bold)));
    vivo->labelHierarquia->setSize(460, 20);
    vivo->janela->addCustomComponent(vivo->labelHierarquia.get());

    vivo->listaNiveis = std::make_shared<ListaNiveisHierarquia>();
    vivo->listaNiveis->setSize(200, 200);
    vivo->listaNiveis->definirHierarquia(matriz::consolidacao::hierarquiaDoProjeto(projeto.projeto().registro()));

    vivo->previaArvore = std::make_shared<PreviaArvoreBackup>();
    vivo->previaArvore->setSize(252, 200);
    vivo->viewportArvore = std::make_shared<juce::Viewport>();
    vivo->viewportArvore->setViewedComponent(vivo->previaArvore.get(), false);

    // AlertWindow empilha customComponents verticalmente, então o layout
    // horizontal (níveis à esquerda, árvore à direita) precisa vir de um
    // contêiner próprio. Vive em EstadoVivo, junto com todo o resto do
    // diálogo — o AlertWindow não assume posse de customComponent nenhum.
    vivo->painelHierarquia = std::make_shared<PainelLadoALado>(*vivo->listaNiveis, *vivo->viewportArvore);
    vivo->janela->addCustomComponent(vivo->painelHierarquia.get());

    vivo->listaPrevia = std::make_shared<ListaPreviaConsolidacao>();
    vivo->listaPrevia->setSize(460, 60);
    vivo->viewportPrevia = std::make_shared<juce::Viewport>();
    vivo->viewportPrevia->setViewedComponent(vivo->listaPrevia.get(), false);
    vivo->viewportPrevia->setSize(460, 220);
    vivo->janela->addCustomComponent(vivo->viewportPrevia.get());

    vivo->labelAvisos = std::make_shared<juce::Label>();
    vivo->labelAvisos->setText("", juce::dontSendNotification);
    vivo->labelAvisos->setColour(juce::Label::textColourId, tema().textoSecundario);
    vivo->labelAvisos->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
    vivo->labelAvisos->setSize(460, 60);
    vivo->janela->addCustomComponent(vivo->labelAvisos.get());

    vivo->botaoConsolidar = std::make_shared<juce::TextButton>(matriz::i18n::t("consolidacao.consolidar"));
    vivo->botaoConsolidar->setName(""); // ver nota em botaoEscolherDestino acima
    vivo->botaoConsolidar->setSize(200, 30);
    vivo->botaoConsolidar->setEnabled(false); // só habilita com destino escolhido e plano sem conflito
    vivo->janela->addCustomComponent(vivo->botaoConsolidar.get());

    vivo->botaoCancelar = std::make_shared<juce::TextButton>(matriz::i18n::t("ingest.cancelar"));
    vivo->botaoCancelar->setName(""); // ver nota em botaoEscolherDestino acima
    vivo->botaoCancelar->setSize(200, 26);
    vivo->botaoCancelar->setColour(juce::TextButton::textColourOffId, tema().perigo);
    // Só aparece durante a gravação — botão de cancelar visível com nada
    // rodando é ruído, mas ele TEM que estar lá no instante em que começa.
    vivo->botaoCancelar->setVisible(false);
    vivo->botaoCancelar->onClick = [vivo] {
        vivo->cancelamento->pedir();
        vivo->botaoCancelar->setEnabled(false);
    };
    vivo->janela->addCustomComponent(vivo->botaoCancelar.get());

    // Catálogo de proxies (item 11): ligado por padrão. É barato (fração do
    // tamanho do acervo, mostrada no próprio rótulo) e é o que permite
    // consultar o backup depois sem o HD original conectado — desligar tem
    // que ser uma escolha consciente, não o caminho de menor resistência.
    vivo->caixaCatalogo = std::make_shared<juce::ToggleButton>(matriz::i18n::t("catalogo.gerar"));
    vivo->caixaCatalogo->setName("");
    vivo->caixaCatalogo->setToggleState(true, juce::dontSendNotification);
    vivo->caixaCatalogo->setSize(460, 24);
    vivo->janela->addCustomComponent(vivo->caixaCatalogo.get());

    vivo->labelCatalogo = std::make_shared<juce::Label>();
    vivo->labelCatalogo->setFont(juce::Font(juce::FontOptions(tema().tamanhoFontePequena)));
    vivo->labelCatalogo->setColour(juce::Label::textColourId, tema().textoTerciario);
    vivo->labelCatalogo->setSize(460, 34);
    vivo->janela->addCustomComponent(vivo->labelCatalogo.get());

    vivo->labelResultado = std::make_shared<juce::Label>();
    vivo->labelResultado->setText("", juce::dontSendNotification);
    vivo->labelResultado->setColour(juce::Label::textColourId, tema().textoSecundario);
    vivo->labelResultado->setSize(460, 40);
    vivo->janela->addCustomComponent(vivo->labelResultado.get());

    vivo->janela->addButton(matriz::i18n::t("consolidacao.fechar"), 1);

    auto atualizarPlano = [vivo, &projeto] {
        vivo->plano = matriz::consolidacao::planejarConsolidacao(
            projeto.projeto().registro(), projeto.projeto().pasta(), vivo->destino, vivo->listaNiveis->hierarquia(),
            [&projeto](const std::string& tipo) -> juce::String {
                try {
                    const auto& def = projeto.definicaoPara(tipo);
                    return matriz::ficha::rotuloTipo(tipo, def.rotulo.empty() ? tipo : def.rotulo);
                } catch (const std::exception&) {
                    return juce::String(tipo); // tipo sem ficha — nome de pasta cru é melhor que nenhum
                }
            });
        vivo->listaPrevia->definirPlano(&vivo->plano);
        vivo->previaArvore->definirPlano(&vivo->plano);

        juce::StringArray avisos;
        if (vivo->plano.itensNaoOrganizados > 0)
            avisos.add(matriz::i18n::t("consolidacao.aviso_nao_organizados").replace("{n}", juce::String(vivo->plano.itensNaoOrganizados)));
        if (!vivo->plano.nomesEmConflito.empty())
            avisos.add(matriz::i18n::t("consolidacao.aviso_conflitos").replace("{n}", juce::String((int)vivo->plano.nomesEmConflito.size())));
        avisos.add(matriz::i18n::t("consolidacao.aviso_espaco")
                       .replace("{necessario}", formatarBytes(vivo->plano.espacoNecessarioBytes))
                       .replace("{disponivel}", formatarBytes(vivo->plano.espacoDisponivelBytes)));
        vivo->labelAvisos->setColour(juce::Label::textColourId,
                                       !vivo->plano.nomesEmConflito.empty() ? tema().perigo : tema().textoSecundario);
        vivo->labelAvisos->setText(avisos.joinIntoString("\n"), juce::dontSendNotification);

        // Tamanho do catálogo ANTES de gerar (§11) — número real, somando os
        // proxies que já existem, não estimativa por heurística.
        auto estimativa = matriz::catalogo::estimar(projeto.projeto().registro(), projeto.projeto().indice(),
                                                     projeto.projeto().pasta());
        vivo->labelCatalogo->setText(
            matriz::i18n::t("catalogo.gerar_ajuda").replace("{tamanho}", formatarBytes(estimativa.tamanhoBytes)),
            juce::dontSendNotification);

        bool podeConsolidar = vivo->destino != juce::File() && vivo->plano.podeConsolidar() && !vivo->plano.itens.empty();
        vivo->botaoConsolidar->setEnabled(podeConsolidar && !vivo->consolidando);
    };

    // Reordenar/remover/acrescentar um nível replaneja e redesenha a árvore
    // na hora, e grava a escolha no projeto (§5.2 — a hierarquia é do
    // projeto, não de uma sessão do diálogo).
    vivo->listaNiveis->aoMudar = [vivo, &projeto, atualizarPlano] {
        matriz::consolidacao::gravarHierarquiaDoProjeto(projeto.projeto().registro(), vivo->listaNiveis->hierarquia());
        atualizarPlano();
    };

    vivo->botaoEscolherDestino->onClick = [vivo, atualizarPlano] {
        auto chooser = std::make_shared<juce::FileChooser>(matriz::i18n::t("consolidacao.escolher_destino"));
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [vivo, atualizarPlano, chooser](const juce::FileChooser& fc) {
                                  juce::File pasta = fc.getResult();
                                  if (pasta == juce::File()) return;
                                  vivo->destino = pasta;
                                  vivo->labelDestino->setText(pasta.getFullPathName(), juce::dontSendNotification);
                                  atualizarPlano();
                              });
    };

    vivo->botaoConsolidar->onClick = [vivo, &projeto] {
        if (vivo->consolidando) return;
        vivo->consolidando = true;
        vivo->botaoConsolidar->setEnabled(false);
        vivo->labelResultado->setColour(juce::Label::textColourId, tema().textoSecundario);
        vivo->labelResultado->setText(matriz::i18n::t("consolidacao.consolidando"), juce::dontSendNotification);
        vivo->labelResultado->repaint();
        vivo->cancelamento->rearmar();
        vivo->botaoCancelar->setEnabled(true);
        vivo->botaoCancelar->setVisible(true);

        // Roda na thread de mensagens, de propósito — ao contrário do
        // ingest (§2, item 2/3), este diálogo já é MODAL (enterModalState),
        // o operador não pode fazer mais nada enquanto está aberto de
        // qualquer forma. Uma thread separada aqui escreveria em
        // projeto.registro() depois de o operador poder fechar o projeto
        // (o diálogo não impede isso do lado de fora), correndo o mesmo
        // risco de ponteiro pendurado que já causou um crash real nesta
        // sessão — não vale o ganho de não travar um modal que já trava
        // tudo mesmo. Consolidações muito grandes deixam este diálogo sem
        // responder até terminar; limitação conhecida, não escondida.
        // Progresso + cancelamento entre arquivos. O runDispatchLoopUntil é
        // o que permite o clique em "Cancelar" chegar até aqui sem mover a
        // cópia pra outra thread — decisão preservada de propósito (ver nota
        // acima). A janela é modal, então o loop bombeado só entrega evento
        // pros botões DESTE diálogo, não pro resto do app.
        auto resultado = matriz::consolidacao::executarConsolidacao(
            projeto.projeto().registro(), projeto.projeto().pasta(), vivo->destino, vivo->plano,
            [vivo](int feito, int total) {
                vivo->labelResultado->setText(matriz::i18n::t("consolidacao.progresso")
                                                   .replace("{feito}", juce::String(feito))
                                                   .replace("{total}", juce::String(total)),
                                               juce::dontSendNotification);
                juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
                return !vivo->cancelamento->pedido();
            });
        vivo->consolidando = false;
        vivo->botaoCancelar->setVisible(false);

        if (resultado.cancelado) {
            vivo->labelResultado->setColour(juce::Label::textColourId, tema().alerta);
            vivo->labelResultado->setText(
                matriz::i18n::t("consolidacao.cancelado")
                    .replace("{feito}", juce::String(resultado.consolidados + resultado.pulados))
                    .replace("{total}", juce::String(resultado.totalPlanejado)),
                juce::dontSendNotification);
            vivo->botaoConsolidar->setEnabled(true);
            return;
        }

        juce::String texto = matriz::i18n::t("consolidacao.resultado")
                                  .replace("{consolidados}", juce::String(resultado.consolidados))
                                  .replace("{pulados}", juce::String(resultado.pulados));
        if (!resultado.falhas.empty()) {
            texto += " · " +
                     matriz::i18n::t("consolidacao.resultado_falhas").replace("{n}", juce::String((int)resultado.falhas.size()));
            vivo->labelResultado->setColour(juce::Label::textColourId, tema().perigo);
        } else {
            vivo->labelResultado->setColour(juce::Label::textColourId, tema().estadoQcOk);
        }
        vivo->labelResultado->setText(texto, juce::dontSendNotification);

        // Catálogo depois da cópia: ele registra ONDE cada arquivo ficou no
        // backup, então precisa da consolidação já gravada pra ter o que
        // apontar.
        if (vivo->caixaCatalogo->getToggleState()) {
            vivo->cancelamento->rearmar();
            vivo->botaoCancelar->setEnabled(true);
            vivo->botaoCancelar->setVisible(true);
            auto resCatalogo = matriz::catalogo::gerar(
                projeto.projeto().registro(), projeto.projeto().indice(), projeto.projeto().pasta(), vivo->destino,
                [vivo](int feito, int total) {
                    vivo->labelResultado->setText(matriz::i18n::t("catalogo.gerando")
                                                       .replace("{feito}", juce::String(feito))
                                                       .replace("{total}", juce::String(total)),
                                                   juce::dontSendNotification);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
                    return !vivo->cancelamento->pedido();
                });
            vivo->botaoCancelar->setVisible(false);

            juce::String textoCatalogo =
                resCatalogo.cancelado
                    ? matriz::i18n::t("catalogo.cancelado")
                          .replace("{feito}", juce::String(resCatalogo.gravados))
                          .replace("{total}", juce::String(resCatalogo.totalPlanejado))
                    : matriz::i18n::t("catalogo.gerado").replace("{n}", juce::String(resCatalogo.gravados));
            vivo->labelResultado->setText(texto + "\n" + textoCatalogo, juce::dontSendNotification);
        }

        vivo->botaoConsolidar->setEnabled(true);
    };

    atualizarPlano();

    vivo->janela->enterModalState(true, juce::ModalCallbackFunction::create([vivo, aoConcluir = std::move(aoFechar)](int) {
                                        if (aoConcluir) aoConcluir();
                                    }));
    return vivo->janela;
}

} // namespace matriz::ui
