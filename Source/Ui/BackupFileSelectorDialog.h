#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ViewModeIconButton.h"

namespace matriz::ui {

class ProjetoAberto;
class MosaicoComponent;

class BackupFileSelectorDialog : public juce::Component {
public:
    BackupFileSelectorDialog(ProjetoAberto& projeto, const std::set<std::string>& initialSelectedIds);
    ~BackupFileSelectorDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    std::function<void(const std::set<std::string>&)> aoConfirmar;
    std::function<void()> aoCancelar;

    static void exibirModal(ProjetoAberto& projeto,
                            const std::set<std::string>& initialSelectedIds,
                            juce::Component* parentComp,
                            std::function<void(const std::set<std::string>&)> callback);

private:
    struct CategoriaInfo {
        juce::String rotulo;
        std::string chave;
        int contagem = 0;
    };

    void construirSidebar();
    void atualizarContagens();
    void aplicarFiltros();
    void selecionarCategoria(int indice);
    void atualizarBotoesSidebar();
    void atualizarResumoSelecao();

    ProjetoAberto& projeto_;
    std::set<std::string> selectedIds_;

    // Header
    std::unique_ptr<juce::Label> lblTitulo_;
    std::unique_ptr<juce::TextEditor> campoBusca_;
    std::unique_ptr<juce::TextButton> btnLimparBusca_;
    std::unique_ptr<ViewModeIconButton> btnVisaoGrade_;
    std::unique_ptr<ViewModeIconButton> btnVisaoLista_;
    std::unique_ptr<juce::Slider> sliderTamanho_;
    std::unique_ptr<juce::Label> lblTamanho_;

    // Sidebar
    std::vector<CategoriaInfo> categorias_;
    std::vector<std::unique_ptr<juce::TextButton>> botoesCategorias_;
    int categoriaSelecionada_ = 0;

    std::unique_ptr<juce::Label> lblSecaoCategorias_;
    std::unique_ptr<juce::Label> lblSecaoFiltros_;
    std::unique_ptr<juce::ComboBox> comboAnos_;
    std::unique_ptr<juce::ComboBox> comboColecoes_;

    std::unique_ptr<juce::Label> lblSecaoSelecao_;
    std::unique_ptr<juce::TextButton> btnSelecionarTodos_;
    std::unique_ptr<juce::TextButton> btnDesmarcarTodos_;
    std::unique_ptr<juce::TextButton> btnInverterSelecao_;
    std::unique_ptr<juce::Label> lblContadorSelecao_;

    // Center Main Area
    std::unique_ptr<juce::Viewport> mosaicoViewport_;
    std::unique_ptr<MosaicoComponent> mosaico_;

    // Footer
    std::unique_ptr<juce::Label> lblResumo_;
    std::unique_ptr<juce::TextButton> btnCancelar_;
    std::unique_ptr<juce::TextButton> btnConfirmar_;

    bool modoVisaoGrade_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BackupFileSelectorDialog)
};

} // namespace matriz::ui
