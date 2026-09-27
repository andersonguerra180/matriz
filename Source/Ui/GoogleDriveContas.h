#pragma once

// Contas do Google Drive para Desktop montadas neste Mac (uma pasta
// ~/Library/CloudStorage/GoogleDrive-<email> por conta). Os botões GOOGLE
// DRIVE pegavam a PRIMEIRA pasta encontrada — com várias contas, abriam a
// conta errada. Agora: uma conta vai direto; várias, pergunta qual.

#include <JuceHeader.h>
#include <functional>
#include <vector>

#include "../App/Preferencias.h"
#include "../I18n/Strings.h"

namespace matriz::ui {

struct ContaGoogleDrive {
    juce::String rotulo;   // e-mail da conta
    juce::File pasta;      // "My Drive"/"Meu Drive" da conta (ou a raiz dela)
    bool copiaAntiga = false;  // "GoogleDrive-x (03-07-25 19:26)": sobra de reinstalação
};

inline std::vector<ContaGoogleDrive> contasGoogleDrive() {
    std::vector<ContaGoogleDrive> contas;
    const juce::File base = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/CloudStorage");
    if (base.isDirectory()) {
        auto pastas = base.findChildFiles(juce::File::findDirectories, false, "GoogleDrive-*");
        pastas.sort();
        for (const auto& p : pastas) {
            ContaGoogleDrive c;
            c.rotulo = p.getFileName().fromFirstOccurrenceOf("GoogleDrive-", false, false);
            c.copiaAntiga = c.rotulo.containsChar('(');
            c.pasta = p;
            for (const char* nome : {"My Drive", "Meu Drive"})
                if (p.getChildFile(nome).isDirectory()) { c.pasta = p.getChildFile(nome); break; }
            contas.push_back(c);
        }
    }
    // Contas atuais primeiro; cópias antigas no fim.
    std::stable_sort(contas.begin(), contas.end(),
                     [](const ContaGoogleDrive& a, const ContaGoogleDrive& b) { return !a.copiaAntiga && b.copiaAntiga; });
    if (contas.empty()) {
        const juce::File legado = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Google Drive");
        if (legado.isDirectory()) contas.push_back({"Google Drive", legado, false});
    }
    return contas;
}

// Chama aoEscolher com a pasta da conta escolhida (nada se cancelar). Sem
// nenhuma conta, avisa que o Google Drive para Desktop não foi encontrado.
inline void escolherContaGoogleDrive(juce::Component* ancora, std::function<void(const juce::File&)> aoEscolher) {
    const auto contas = contasGoogleDrive();
    if (contas.empty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Google Drive",
                                               matriz::i18n::t("gdrive.nao_encontrado"), {}, nullptr,
                                               juce::ModalCallbackFunction::create([](int) {}));
        return;
    }
    if (contas.size() == 1) {
        aoEscolher(contas.front().pasta);
        return;
    }
    const juce::String ultima = matriz::app::lerUltimaContaGoogleDrive();
    juce::PopupMenu menu;
    menu.addSectionHeader(matriz::i18n::t("gdrive.escolher_conta"));
    for (size_t i = 0; i < contas.size(); ++i)
        menu.addItem(static_cast<int>(i) + 1,
                     contas[i].copiaAntiga ? contas[i].rotulo + "  - " + matriz::i18n::t("gdrive.copia_antiga") : contas[i].rotulo,
                     true, contas[i].rotulo == ultima);
    auto opcoes = juce::PopupMenu::Options();
    if (ancora != nullptr) opcoes = opcoes.withTargetComponent(ancora);
    menu.showMenuAsync(opcoes, [contas, aoEscolher](int escolha) {
        if (escolha <= 0 || escolha > static_cast<int>(contas.size())) return;
        const auto& c = contas[static_cast<size_t>(escolha - 1)];
        matriz::app::gravarUltimaContaGoogleDrive(c.rotulo);
        aoEscolher(c.pasta);
    });
}

} // namespace matriz::ui
