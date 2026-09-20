#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>
#include <string>
#include <set>
#include "ProjetoAberto.h"

namespace matriz::ui {

class ExportZipDialog : public juce::Component {
public:
    explicit ExportZipDialog(ProjetoAberto& projeto);
    ~ExportZipDialog() override;

    static void exibirModal(ProjetoAberto& projeto);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed(const juce::KeyPress& key) override;

    std::function<void()> aoFechar;

    // Métodos utilitários públicos para portabilidade e testes unitários
    static juce::String sanitizarNomeArquivoZip(const juce::String& nomeOriginal);
    static juce::String resolverColisaoNome(const juce::String& nomeSanitizado,
                                            std::set<std::string>& nomesUsadosLower);
    static bool ehExtensaoComprimida(const juce::String& extensao);

    static constexpr juce::int64 kLimiteMaximoSeguroBytes = 3758096384LL; // 3.5 GB

private:
    void fecharDialogo();
    void escolherPastaDestino();
    void iniciarExportacao();
    void cancelarExportacao();

    ProjetoAberto& projeto_;

    // Thread de exportação em segundo plano
    class ExportThread;
    std::unique_ptr<ExportThread> threadExportacao_;

    // Componentes de UI
    std::unique_ptr<juce::Label> lblTitulo_;
    std::unique_ptr<juce::Label> lblSubtitulo_;

    // Grupo Arquivo e Destino
    std::unique_ptr<juce::GroupComponent> grpArquivo_;
    std::unique_ptr<juce::Label> lblNomeArquivo_;
    std::unique_ptr<juce::TextEditor> txtNomeArquivo_;
    std::unique_ptr<juce::Label> lblPastaDestino_;
    std::unique_ptr<juce::Label> lblCaminhoPasta_;
    std::unique_ptr<juce::TextButton> btnEscolherPasta_;
    std::unique_ptr<juce::ToggleButton> chkIncluirAssociados_;

    // Grupo Metadados
    std::unique_ptr<juce::GroupComponent> grpMetadados_;
    std::unique_ptr<juce::Label> lblMetaTitulo_;
    std::unique_ptr<juce::TextEditor> txtMetaTitulo_;
    std::unique_ptr<juce::Label> lblMetaResponsavel_;
    std::unique_ptr<juce::TextEditor> txtMetaResponsavel_;
    std::unique_ptr<juce::Label> lblMetaDescricao_;
    std::unique_ptr<juce::TextEditor> txtMetaDescricao_;
    std::unique_ptr<juce::Label> lblMetaData_;
    std::unique_ptr<juce::TextEditor> txtMetaData_;
    std::unique_ptr<juce::Label> lblMetaDireitos_;
    std::unique_ptr<juce::TextEditor> txtMetaDireitos_;

    // Progresso e Status
    std::unique_ptr<juce::ProgressBar> barraProgresso_;
    std::unique_ptr<juce::Label> lblStatusProgresso_;
    double progressoValor_ = 0.0;

    // Botões
    std::unique_ptr<juce::TextButton> btnCancelar_;
    std::unique_ptr<juce::TextButton> btnExportar_;

    juce::File pastaDestinoSelecionada_;
    bool exportando_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportZipDialog)
};

} // namespace matriz::ui
