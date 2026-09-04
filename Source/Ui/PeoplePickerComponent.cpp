#include "PeoplePickerComponent.h"
#include "ProjetoAberto.h"
#include "Tokens.h"

namespace matriz::ui {

PeoplePickerComponent::PeoplePickerComponent(ProjetoAberto& projeto, const std::string& itemId)
    : projeto_(projeto), itemId_(itemId) {
    const auto& tk = tema();

    comboPeople_.setTextWhenNothingSelected("Select a person in collection...");
    comboPeople_.setColour(juce::ComboBox::backgroundColourId, tk.painelAlt);
    comboPeople_.setColour(juce::ComboBox::outlineColourId, tk.borda);
    comboPeople_.setColour(juce::ComboBox::textColourId, tk.textoPrimario);
    comboPeople_.setColour(juce::ComboBox::arrowColourId, tk.textoSecundario);
    comboPeople_.onChange = [this] { onComboChanged(); };
    addAndMakeVisible(comboPeople_);

    btnAddPerson_.setButtonText("+");
    btnAddPerson_.setTooltip("Include new person in collection (+)");
    btnAddPerson_.setColour(juce::TextButton::buttonColourId, tk.acento);
    btnAddPerson_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    btnAddPerson_.onClick = [this] { abrirDialogoIncluirPessoa(); };
    addAndMakeVisible(btnAddPerson_);

    btnRemovePerson_.setButtonText("-");
    btnRemovePerson_.setTooltip("Remove person from collection (-)");
    btnRemovePerson_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffcc3333));
    btnRemovePerson_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    btnRemovePerson_.onClick = [this] { abrirDialogoRemoverPessoa(); };
    addAndMakeVisible(btnRemovePerson_);

    recarregarPessoas();
}

PeoplePickerComponent::~PeoplePickerComponent() = default;

void PeoplePickerComponent::recarregarPessoas() {
    int selId = comboPeople_.getSelectedId();

    comboPeople_.clear(juce::dontSendNotification);
    comboPeople_.addItem("+ INCLUDE A PERSON...", 1);
    comboPeople_.addItem("Import Name List from collection...", 2);
    comboPeople_.addItem("Remove a person from list...", 3);
    comboPeople_.addSeparator();

    auto pessoas = projeto_.listarPessoas();
    if (pessoas.empty()) {
        comboPeople_.addItem("(No people registered yet)", 9999);
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
    auto win = std::make_shared<juce::AlertWindow>(
        "Include Person",
        "Enter person name to register in this collection:",
        juce::AlertWindow::QuestionIcon);
    win->addTextEditor("nome", "", "Person name (e.g. Jorge):");
    win->addButton("Include", 1, juce::KeyPress(juce::KeyPress::returnKey, 0, 0));
    win->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey, 0, 0));

    juce::Component::SafePointer<PeoplePickerComponent> safeThis(this);
    win->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, win](int result) {
        if (result == 1 && safeThis) {
            juce::String nome = win->getTextEditorContents("nome").trim();
            if (nome.isNotEmpty()) {
                safeThis->projeto_.adicionarPessoa(nome.toStdString());
                safeThis->recarregarPessoas();
                if (safeThis->onPersonAddedToTags) {
                    safeThis->onPersonAddedToTags(nome);
                }
            }
        }
    }));
}

void PeoplePickerComponent::abrirDialogoRemoverPessoa() {
    auto pessoas = projeto_.listarPessoas();
    if (pessoas.empty()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            "No People Registered",
            "There are currently no people registered in this collection.",
            "OK");
        return;
    }

    juce::PopupMenu menu;
    menu.addSectionHeader("SELECT PERSON TO REMOVE (-)");
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
            sqliteFile = escolhido.getChildFile("registro.sqlite");
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
                "No valid collection database (registro.sqlite) found at the selected path.",
                "OK");
            return;
        }

        int countImported = 0;
        try {
            matriz::db::Database dbOutra(sqliteFile.getFullPathName().toStdString());
            // 1. Read from collection_person
            try {
                auto stmt = dbOutra.prepare("SELECT name FROM collection_person WHERE name IS NOT NULL AND name != ''");
                while (stmt.step()) {
                    std::string nome = stmt.columnText(0);
                    if (!nome.empty() && safeThis->projeto_.adicionarPessoa(nome)) {
                        countImported++;
                    }
                }
            } catch (...) {}

            // 2. Read distinct creators/artists
            try {
                auto stmt2 = dbOutra.prepare(
                    "SELECT DISTINCT valor FROM item_campo "
                    "WHERE campo_id IN ('dc_creator', 'artista_principal', 'artista') "
                    "AND valor IS NOT NULL AND valor != '' "
                    "AND fonte = 'humano'");
                while (stmt2.step()) {
                    std::string nome = stmt2.columnText(0);
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
            "Imported " + juce::String(countImported) + " person name(s) from collection into this collection.",
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

} // namespace matriz::ui
