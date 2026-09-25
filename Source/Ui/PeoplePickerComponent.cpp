#include "PeoplePickerComponent.h"
#include "ProjetoAberto.h"
#include "Tokens.h"
#include "../I18n/Strings.h"
#include "../Model/Project.h"

namespace matriz::ui {

namespace {
bool ehPt() { return matriz::i18n::localeAtivo() == "pt_BR"; }
}

PeoplePickerComponent::PeoplePickerComponent(ProjetoAberto& projeto, const std::string& itemId)
    : projeto_(projeto), itemId_(itemId) {
    const auto& tk = tema();
    bool isPt = ehPt();

    comboPeople_.setTextWhenNothingSelected(isPt ? juce::String::fromUTF8("Selecione uma pessoa da coleção...") : juce::String("Select a person in collection..."));
    comboPeople_.setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
    comboPeople_.setColour(juce::ComboBox::outlineColourId, tk.borda);
    comboPeople_.setColour(juce::ComboBox::textColourId, juce::Colours::black);
    comboPeople_.setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    comboPeople_.onChange = [this] { onComboChanged(); };
    addAndMakeVisible(comboPeople_);

    // ADD (item "ADD" — antes "Include Person"): agora pode adicionar tanto
    // uma pessoa quanto uma tag ao item; o rótulo/tooltip e o diálogo abaixo
    // (abrirDialogoIncluirPessoa) deixam essa dupla finalidade explícita.
    btnAddPerson_.setButtonText("+");
    btnAddPerson_.setTooltip(isPt ? "Adicionar uma pessoa ou uma tag (+)" : "Add a person or a tag (+)");
    btnAddPerson_.setColour(juce::TextButton::buttonColourId, tk.acento);
    btnAddPerson_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    btnAddPerson_.onClick = [this] { abrirDialogoIncluirPessoa(); };
    addAndMakeVisible(btnAddPerson_);

    btnRemovePerson_.setButtonText("-");
    btnRemovePerson_.setTooltip(isPt ? "Remover pessoa da coleção (-)" : "Remove person from collection (-)");
    btnRemovePerson_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffcc3333));
    btnRemovePerson_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    btnRemovePerson_.onClick = [this] { abrirDialogoRemoverPessoa(); };
    addAndMakeVisible(btnRemovePerson_);

    recarregarPessoas();
}

PeoplePickerComponent::~PeoplePickerComponent() = default;

void PeoplePickerComponent::recarregarPessoas() {
    bool isPt = ehPt();
    int selId = comboPeople_.getSelectedId();

    comboPeople_.clear(juce::dontSendNotification);
    comboPeople_.addItem(isPt ? juce::String::fromUTF8("+ ADICIONAR (pessoa ou tag)...") : juce::String("+ ADD (person or tag)..."), 1);
    comboPeople_.addItem(isPt ? juce::String::fromUTF8("Importar lista de nomes da coleção...") : juce::String("Import Name List from collection..."), 2);
    comboPeople_.addItem(isPt ? juce::String::fromUTF8("Remover uma pessoa da lista...") : juce::String("Remove a person from list..."), 3);
    comboPeople_.addSeparator();

    auto pessoas = projeto_.listarPessoas();
    if (pessoas.empty()) {
        comboPeople_.addItem(isPt ? juce::String::fromUTF8("(Nenhuma pessoa registrada ainda)") : juce::String("(No people registered yet)"), 9999);
        comboPeople_.setItemEnabled(9999, false);
    } else {
        int id = 10;
        for (const auto& p : pessoas) {
            comboPeople_.addItem("+  " + juce::String(p), id++);
        }
    }

    if (selId > 3 && selId != 9999) {
        comboPeople_.setSelectedId(selId, juce::dontSendNotification);
    }
}

void PeoplePickerComponent::onComboChanged() {
    int id = comboPeople_.getSelectedId();
    if (id == 1) {
        comboPeople_.setSelectedId(0, juce::dontSendNotification);
        abrirDialogoIncluirPessoa();
    } else if (id == 2) {
        comboPeople_.setSelectedId(0, juce::dontSendNotification);
        importarListaPessoasDeOutraColecao();
    } else if (id == 3) {
        comboPeople_.setSelectedId(0, juce::dontSendNotification);
        abrirDialogoRemoverPessoa();
    } else if (id >= 10) {
        juce::String texto = comboPeople_.getText().trim();
        if (texto.startsWith("+")) {
            texto = texto.substring(1).trim();
        }

        comboPeople_.setSelectedId(0, juce::dontSendNotification);

        if (texto.isNotEmpty() && onPersonAddedToTags) {
            onPersonAddedToTags(texto);
        }
    }
}

void PeoplePickerComponent::abrirDialogoIncluirPessoa() {
    // ADD (correção "Add Person or Tag", item 2): Person e Tag são o mesmo
    // campo/comportamento — sem etapa de escolha prévia, o diálogo abre
    // direto no formulário. Todo valor digitado é registrado como pessoa
    // (reaproveitável depois no combo) E aplicado como tag ao item, sempre,
    // sem ramificação.
    bool isPt = ehPt();
    auto win = std::make_shared<juce::AlertWindow>(
        isPt ? "Adicionar" : "Add",
        isPt ? juce::String::fromUTF8("Nome da pessoa ou tag para adicionar a este item:") : juce::String("Person name or tag to add to this item:"),
        juce::AlertWindow::QuestionIcon);

    win->addTextEditor("valor", "", isPt ? "Nome ou tag (ex.: Jorge)" : "Name or tag (e.g. Jorge)");
    win->addButton(isPt ? "Adicionar" : "Add", 1, juce::KeyPress(juce::KeyPress::returnKey, 0, 0));
    win->addButton(isPt ? "Cancelar" : "Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey, 0, 0));

    juce::Component::SafePointer<PeoplePickerComponent> safeThis(this);
    win->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, win](int result) {
        if (result != 1 || !safeThis) return;
        juce::String valor = win->getTextEditorContents("valor").trim();
        if (valor.isEmpty()) return;

        safeThis->projeto_.adicionarPessoa(valor.toStdString());
        safeThis->recarregarPessoas();
        if (safeThis->onPersonAddedToTags) {
            safeThis->onPersonAddedToTags(valor);
        }
    }));
}

void PeoplePickerComponent::abrirDialogoRemoverPessoa() {
    bool isPt = ehPt();
    auto pessoas = projeto_.listarPessoas();
    if (pessoas.empty()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            isPt ? "Nenhuma Pessoa Registrada" : "No People Registered",
            isPt ? juce::String::fromUTF8("Não há pessoas registradas nesta coleção no momento.")
                 : juce::String("There are currently no people registered in this collection."),
            "OK");
        return;
    }

    juce::PopupMenu menu;
    menu.addSectionHeader(isPt ? juce::String::fromUTF8("SELECIONE A PESSOA PARA REMOVER (-)") : juce::String("SELECT PERSON TO REMOVE (-)"));
    int id = 1;
    std::map<int, std::string> idParaNome;
    for (const auto& p : pessoas) {
        menu.addItem(id, "-  " + juce::String(p));
        idParaNome[id] = p;
        id++;
    }

    juce::Component::SafePointer<PeoplePickerComponent> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&btnRemovePerson_),
        [safeThis, idParaNome](int res) {
            if (!safeThis || res == 0) return;
            auto it = idParaNome.find(res);
            if (it != idParaNome.end()) {
                safeThis->projeto_.removerPessoa(it->second);
                safeThis->recarregarPessoas();
            }
        });
}

void PeoplePickerComponent::importarListaPessoasDeOutraColecao() {
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Select Collection Folder to Import Names From",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.mtz;*.bkm",
        true);

    auto flags = juce::FileBrowserComponent::openMode
               | juce::FileBrowserComponent::canSelectDirectories
               | juce::FileBrowserComponent::canSelectFiles;

    juce::Component::SafePointer<PeoplePickerComponent> safeThis(this);
    fileChooser_->launchAsync(flags, [safeThis](const juce::FileChooser& fc) {
        if (!safeThis) return;
        auto escolhido = fc.getResult();
        if (!escolhido.exists()) return;

        juce::File sqliteFile;
        if (escolhido.isDirectory()) {
            // A pasta escolhida pelo usuário é a raiz da coleção — os arquivos
            // reais (registro.sqlite) ficam dentro da subpasta Project/, não
            // direto na raiz. resolverPastaProjeto sabe achar isso (mesma
            // lógica usada para abrir um projeto normalmente).
            juce::File pastaProjeto = matriz::model::Project::resolverPastaProjeto(escolhido);
            sqliteFile = pastaProjeto.getChildFile("registro.sqlite");
            if (!sqliteFile.existsAsFile()) {
                sqliteFile = escolhido.getChildFile("registro.sqlite");
            }
            if (!sqliteFile.existsAsFile()) {
                sqliteFile = escolhido.getChildFile("catalog.sqlite");
            }
        } else {
            sqliteFile = escolhido;
        }

        if (!sqliteFile.existsAsFile()) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Collection Database Not Found",
                "No valid collection database (registro.sqlite) found at the selected path. "
                "Select the collection's root folder (the one containing the Project subfolder).",
                "OK");
            return;
        }

        int countImported = 0;
        try {
            matriz::db::Database dbOutra(sqliteFile.getFullPathName().toStdString());
            // 1. Lista real de nomes registrada na coleção de origem
            //    (coluna real é "nome", não "name" — era o bug: a query
            //    antiga sempre falhava e caía silenciosamente no fallback).
            try {
                auto stmt = dbOutra.prepare("SELECT nome FROM collection_person WHERE nome IS NOT NULL AND nome != ''");
                while (stmt.step()) {
                    std::string nome = stmt.columnText(0);
                    if (!nome.empty() && safeThis->projeto_.adicionarPessoa(nome)) {
                        countImported++;
                    }
                }
            } catch (...) {}
        } catch (...) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Error Importing Names",
                "Could not read metadata from selected collection.",
                "OK");
            return;
        }

        safeThis->recarregarPessoas();

        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            "Names Imported Successfully",
            "Imported " + juce::String(countImported) + " name(s) from collection into this collection.",
            "OK");
    });
}

void PeoplePickerComponent::paint(juce::Graphics& /*g*/) {
}

void PeoplePickerComponent::resized() {
    auto b = getLocalBounds();
    int btnW = 28;
    int gap = 4;
    btnRemovePerson_.setBounds(b.removeFromRight(btnW));
    b.removeFromRight(gap);
    btnAddPerson_.setBounds(b.removeFromRight(btnW));
    b.removeFromRight(gap);
    comboPeople_.setBounds(b);
}

void PeoplePickerComponent::lookAndFeelChanged() {
    const auto& tk = tema();
    comboPeople_.setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
    comboPeople_.setColour(juce::ComboBox::outlineColourId, tk.borda);
    comboPeople_.setColour(juce::ComboBox::textColourId, juce::Colours::black);
    comboPeople_.setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    btnAddPerson_.setColour(juce::TextButton::buttonColourId, tk.acento);
    btnAddPerson_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    btnRemovePerson_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffcc3333));
    btnRemovePerson_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    repaint();
}

} // namespace matriz::ui
