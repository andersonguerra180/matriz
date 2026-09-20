#pragma once

#include <JuceHeader.h>
#include <vector>

#include "BackupSyncDialog.h"
#include "ProjetoAberto.h"

namespace matriz::ui {

class BackupVersionsComponent : public juce::Component,
                                private juce::ListBoxModel {
public:
    explicit BackupVersionsComponent(ProjetoAberto& projeto);
    ~BackupVersionsComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void recarregar();

    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    juce::Component* refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component* existingComponentToUpdate) override;

private:
    void carregarVersoes();
    void renomearVersao(const BackupVersionRef& versao);
    void desvincularVersao(const BackupVersionRef& versao);

    ProjetoAberto& projeto_;
    std::vector<BackupVersionRef> versoes_;

    juce::Label lblTitulo_;
    juce::Label lblDescricao_;
    juce::ListBox listBox_;

    juce::TextButton btnSincronizar_;
    juce::TextButton btnRecuperar_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BackupVersionsComponent)
};

} // namespace matriz::ui
