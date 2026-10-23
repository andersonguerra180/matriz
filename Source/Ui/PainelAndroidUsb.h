#pragma once

#include <JuceHeader.h>

#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "OverlayComponent.h"
#include "PainelReceberCelular.h"
#include "Tablet.h"
#include "../Celular/DispositivoAndroidUsb.h"
#include "../Celular/RecebimentoCelular.h"

// PHONE DUMP > USB (ANDROID): navegador do celular plugado por cabo, no lugar do Android File Transfer.
// Memória do aparelho, pastas como no Finder (duplo clique entra, ◀ sobe), checkbox por arquivo ou pasta
// (pasta = tudo dentro dela) e IMPORTAR: copia para uma pasta de sessão em "Received from phone" e devolve
// essa pasta para o Intake, igual ao QR code da GALLERY.

namespace matriz::ui {

class PainelAndroidUsb : public juce::Component, private juce::ListBoxModel {
public:
    // `existentes` (opcional) roda numa thread de fundo: (nome, tamanho) do que o projeto já tem.
    PainelAndroidUsb(std::function<matriz::celular::ConjuntoExistentes()> existentes, std::shared_ptr<RespostaReceberCelular> resposta,
                     bool comVoltar);
    ~PainelAndroidUsb() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool tratarTecla(const juce::KeyPress&);

private:
    enum class Estado { Conectando, Pronto, Erro, Preparando, Copiando };

    // ListBoxModel
    int getNumRows() override { return static_cast<int>(itens_.size()); }
    void paintListBoxItem(int linha, juce::Graphics&, int largura, int altura, bool selecionada) override;
    void listBoxItemClicked(int linha, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked(int linha, const juce::MouseEvent&) override { abrirLinha(linha); }
    void returnKeyPressed(int linha) override { abrirLinha(linha); }
    void abrirLinha(int linha);  // pasta: entra; arquivo: marca/desmarca

    void conectar();
    void abrirPasta(std::uint32_t pasta, const juce::String& nome);
    void subir();
    void recarregarPasta();
    void alternarMarca(int linha);
    void marcarTodos();
    void importar();
    void terminar(bool comIngestao, int recebidos);
    void atualizarRodape();
    bool jaNoProjeto(const matriz::celular::EntradaAndroid& e) const;
    std::uint32_t memoriaAtual() const;

    using Chave = std::pair<std::uint32_t, std::uint32_t>;  // (memória, id)
    static Chave chaveDe(const matriz::celular::EntradaAndroid& e) { return {e.memoria, e.id}; }

    std::unique_ptr<matriz::celular::SessaoAndroidUsb> sessao_;
    std::shared_ptr<RespostaReceberCelular> resposta_;
    matriz::celular::AparelhoAndroid aparelho_;
    Estado estado_ = Estado::Conectando;
    juce::String erro_;

    struct Nivel { std::uint32_t id; juce::String nome; };
    std::vector<Nivel> caminho_;                       // da raiz da memória até a pasta aberta
    std::vector<matriz::celular::EntradaAndroid> itens_;
    std::map<Chave, matriz::celular::EntradaAndroid> marcados_;
    matriz::celular::ConjuntoExistentes existentes_;
    bool listando_ = false;
    int geracaoLista_ = 0;
    double progresso_ = 0.0;
    int ignoradosNoProjeto_ = 0;

    juce::ComboBox comboMemoria_;
    juce::Label lblAparelho_, lblCaminho_, lblStatus_, lblRodape_;
    juce::ListBox lista_{"cel.usb.lista", this};
    juce::ProgressBar barra_{progresso_};
    std::unique_ptr<BotaoPilula> btnSubir_, btnTentar_, btnTodos_, btnImportar_, btnFechar_, btnVoltar_;
    juce::Rectangle<int> rcCartaoLista_, rcMensagem_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PainelAndroidUsb)
};

// Mostra o painel no overlay. `aoFechar` roda UMA vez (message thread, overlay já fechado). `voltou` = o usuário
// pediu VOLTAR (só existe quando `comVoltar`).
void mostrarAndroidUsb(PainelOverlay& overlay, std::function<matriz::celular::ConjuntoExistentes()> existentes,
                       std::function<void(RespostaReceberCelular, bool voltou)> aoFechar, bool comVoltar);

} // namespace matriz::ui
