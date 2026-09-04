#pragma once

#include <JuceHeader.h>
#include <functional>
#include <string>
#include <vector>

namespace matriz::ui {

class ProjetoAberto;

class PeoplePickerComponent : public juce::Component {
public:
    PeoplePickerComponent(ProjetoAberto& projeto, const std::string& itemId = {});
    ~PeoplePickerComponent() override;

    void setItemId(const std::string& itemId) { itemId_ = itemId; }

    void recarregarPessoas();
    void abrirDialogoIncluirPessoa();
    void abrirDialogoRemoverPessoa();
    void importarListaPessoasDeOutraColecao();

    std::function<void(const juce::String& nomePessoa)> onPersonAddedToTags;

    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::ComboBox& getComboBoxForTest() { return comboPeople_; }
    juce::TextButton& getAddButtonForTest() { return btnAddPerson_; }
    juce::TextButton& getRemoveButtonForTest() { return btnRemovePerson_; }

private:
    ProjetoAberto& projeto_;
    std::string itemId_;

    juce::ComboBox comboPeople_;
    juce::TextButton btnAddPerson_;
    juce::TextButton btnRemovePerson_;
    std::unique_ptr<juce::FileChooser> fileChooser_;

    void onComboChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PeoplePickerComponent)
};

} // namespace matriz::ui
