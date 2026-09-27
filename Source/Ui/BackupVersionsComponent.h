#pragma once

#include <JuceHeader.h>
#include <vector>

#include "BackupSyncDialog.h"
#include "ProjetoAberto.h"
#include "../Sync/SyncEngine.h"

namespace matriz::ui {

class BackupVersionsComponent : public juce::Component,
                                private juce::ListBoxModel {
public:
    explicit BackupVersionsComponent(ProjetoAberto& projeto);
    ~BackupVersionsComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void recarregar();

    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    juce::Component* refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component* existingComponentToUpdate) override;

private:
    void carregarVersoes();
    void aplicarVersoes(std::vector<ProjetoAberto::VersaoResumo> linhas);
    void renomearVersao(const ProjetoAberto::VersaoResumo& versao);
    void desvincularVersao(const ProjetoAberto::VersaoResumo& versao);
    // Etapa 7
    void clonar(const ProjetoAberto::VersaoResumo& versao);
    void sincronizarClone(const ProjetoAberto::VersaoResumo& versao);
    void confirmarSincronizacao(const ProjetoAberto::VersaoResumo& versao, int adicoes, const juce::StringArray& remocoes,
                                const juce::String& erro);
    void promoverAMain(const ProjetoAberto::VersaoResumo& versao);
    void rodarAcao(const juce::String& titulo,
                   std::function<juce::String(matriz::sync::CallbackProgressoSync, matriz::app::CancelamentoPtr)> trabalho);

    ProjetoAberto& projeto_;
    // Linhas exibidas (MAIN no topo, CLONEs, SOURCEs) e, separado, só os
    // destinos (MAIN/CLONE) — é o que os diálogos de sincronizar/recuperar usam.
    std::vector<ProjetoAberto::VersaoResumo> linhas_;
    std::vector<BackupVersionRef> versoes_;
    int geracaoCarga_ = 0;
    juce::ThreadPool poolCarga_{1};
    juce::ThreadPool poolAcoes_{1};
    matriz::app::CancelamentoPtr cancelamento_ = std::make_shared<matriz::app::Cancelamento>();
    bool acaoEmCurso_ = false;
    std::unique_ptr<juce::FileChooser> chooser_;

    juce::Label lblTitulo_;
    juce::Label lblDescricao_;
    juce::ListBox listBox_;

    juce::TextButton btnSincronizar_;
    juce::TextButton btnRecuperar_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BackupVersionsComponent)
};

} // namespace matriz::ui
