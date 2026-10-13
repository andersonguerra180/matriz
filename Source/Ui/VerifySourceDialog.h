#pragma once

// Tools > Verify Source Disks. Pede um disco de origem por vez, lê (só leitura), compara por SHA-256 com o MAIN e
// mostra num mini grid o que NÃO está lá (FALTANDO) e o que o usuário rejeitou no Intake (REJEITADO ANTES). Os marcados
// vão para o Intake pelo mesmo caminho do arrastar; só então o dialog libera o disco e pede o próximo.

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <vector>

#include "../Vault/VerifySource.h"

namespace matriz::ui {

class VerifySourceDialog : public juce::Component, private juce::ListBoxModel, private juce::Timer {
public:
    struct Ganchos {
        std::vector<juce::String> volumes;  // discos de origem conhecidos da coleção, na ordem em que serão pedidos
        std::function<matriz::vault::HashesConhecidos()> carregarHashes;  // message thread; vazio se o projeto fechou
        std::function<void(const juce::Array<juce::File>&)> ingerir;      // mesmo caminho do arrastar para o Intake
        std::function<bool()> ingestOcupado;                              // lote ainda rodando / resolvendo duplicatas
        std::function<juce::File()> pastaRelatorios;                      // onde salvar o relatório; File() = não salvar
    };

    explicit VerifySourceDialog(Ganchos ganchos);
    ~VerifySourceDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    static void exibirModal(Ganchos ganchos);

private:
    enum class Estado { Pedir, Escaneando, Resultado, Ingerindo, Ingerido, Fim };

    struct EstadoScan {
        std::atomic<bool> cancelar{false};
        std::atomic<bool> pronto{false};
        std::atomic<int> feitos{0};
        std::atomic<int> total{0};
        juce::CriticalSection lock;
        juce::String arquivoAtual;
        matriz::vault::ResultadoVerificacaoFonte resultado;
        matriz::vault::HashesConhecidos hashes;
        juce::File raiz;
    };

    struct Linha {
        matriz::vault::FaltanteFonte f;
        bool marcado = false;
        bool ingerido = false;
    };

    class Cabecalho;

    void timerCallback() override;
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selecionada) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;

    void aplicarEstado(Estado e);
    void atualizarTextos();
    void pedirProximoDisco();
    void escolherPasta();
    void iniciarScan(const juce::File& raiz, const juce::String& nome);
    void concluirScan();
    void reconstruirVisiveis();
    int contarMarcados() const;
    void marcar(bool soFaltando, bool valor);
    void ingerirMarcados();
    void proximoDisco();
    void finalizar();
    void anexarRelatorioDoDisco();
    juce::File salvarRelatorio();
    void fechar();
    void cancelarScan();

    Ganchos ganchos_;
    Estado estado_ = Estado::Pedir;
    size_t indiceDisco_ = 0;
    juce::String nomeAtual_;
    juce::String avisoPedir_;
    std::shared_ptr<EstadoScan> scan_;
    int discosVerificados_ = 0, faltandoTotal_ = 0, ingeridosTotal_ = 0, ingeridosDoDisco_ = 0, ociosos_ = 0;
    int lidosDoDisco_ = 0, noMainDoDisco_ = 0, ilegiveisDoDisco_ = 0;
    std::vector<Linha> linhas_;
    std::vector<int> visiveis_;
    int filtro_ = 0;  // 0 todos, 1 faltando, 2 rejeitados antes
    juce::String relatorio_;
    bool relatorioSalvo_ = false;
    bool discoPendente_ = false;  // há resultado de disco ainda não anexado ao relatório
    juce::File arquivoRelatorio_;

    juce::Label lblInfo_;
    double progresso_ = 0.0;
    juce::ProgressBar barra_;
    std::unique_ptr<Cabecalho> cabecalho_;
    juce::ListBox lista_;
    juce::TextButton filtroTodos_, filtroFaltando_, filtroRejeitado_;
    juce::TextButton btnMarcarFalt_, btnMarcarTodos_, btnDesmarcar_;
    juce::TextButton btnA_, btnB_, btnC_, btnFechar_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VerifySourceDialog)
};

} // namespace matriz::ui
