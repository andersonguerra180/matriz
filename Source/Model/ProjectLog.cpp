#include "ProjectLog.h"
#include "Project.h"
#include <mutex>
#include <vector>

namespace matriz::model {

namespace {

// appendEntry() agora lê-e-reescreve o arquivo (entrada nova no TOPO), e várias threads (backup, ingest, UI)
// gravam no mesmo log.md: sem trava uma entrada sobrescreveria a outra.
std::mutex& mutexDoLog() {
    static std::mutex m;
    return m;
}

// Posições das entradas automáticas: "### [AAAA-MM-DD HH:MM:SS] " no começo de linha.
// Uma passada só pelo UTF-8: substring(pos)/indexOf(pos)/texto[pos] andam do começo do texto a cada chamada, e num
// log.md de MBs isso levava segundos por entrada (Locate Offline em lote parava "em 100%" gravando o log).
std::vector<int> inicioDasEntradas(const juce::String& texto) {
    std::vector<int> posicoes;
    auto ehEntrada = [](juce::CharPointer_UTF8 p) {
        static const char* const molde = "### [0000-00-00 00:00:00]";  // '0' = qualquer caractere
        for (int k = 0; molde[k] != 0; ++k) {
            const juce::juce_wchar c = p.getAndAdvance();
            if (molde[k] == '0' ? c == 0 : c != static_cast<juce::juce_wchar>(molde[k])) return false;
        }
        return true;
    };
    auto p = texto.getCharPointer();
    bool inicioDeLinha = true;
    for (int i = 0; !p.isEmpty(); ++i) {
        if (inicioDeLinha && *p == '#' && ehEntrada(p)) posicoes.push_back(i);
        inicioDeLinha = (p.getAndAdvance() == '\n');
    }
    return posicoes;
}

} // namespace

ProjectLog::ProjectLog(const juce::File& projectRootFolder)
    : projectFolder_(projectRootFolder),
      logFile_(projectRootFolder.getChildFile("log.md")) {
}

juce::File ProjectLog::getLogFile() const {
    return logFile_;
}

bool ProjectLog::exists() const {
    return logFile_.existsAsFile();
}

void ProjectLog::ensureInitialized() {
    if (!projectFolder_.isDirectory()) return;

    if (!logFile_.existsAsFile()) {
        juce::String header;
        header << "# BKR Matriz — Project Log\n\n"
               << "> Project root: `" << projectFolder_.getFullPathName() << "`\n"
               << "> Created: `" << juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S") << "`\n\n"
               << "---\n\n"
               << "## Operational Log & Notes\n\n"
               << "_Newest entries first._\n\n";
        logFile_.replaceWithText(header);
    }
}

// log.md nasceu em ordem cronológica (mais antigo em cima). Agora o mais recente fica no topo; um log antigo é
// invertido uma única vez (a ordem é deduzida dos horários da primeira e da última entrada, então rodar de
// novo num log já invertido não faz nada). Guarda a cópia original em log.antes-inversao.md. Chamar com o
// mutex do log seguro.
void ProjectLog::ensureNewestFirst() {
    if (!logFile_.existsAsFile()) return;
    const juce::String texto = logFile_.loadFileAsString();
    const auto inicios = inicioDasEntradas(texto);
    if (inicios.size() < 2) return;
    auto horario = [&](int pos) { return texto.substring(pos + 5, pos + 24); };
    if (horario(inicios.front()).compareNatural(horario(inicios.back())) >= 0) return;  // já mais recente em cima

    const juce::File copia = logFile_.getSiblingFile("log.antes-inversao.md");
    if (!copia.existsAsFile()) logFile_.copyFileTo(copia);

    juce::String novo = texto.substring(0, inicios.front());
    for (int i = static_cast<int>(inicios.size()) - 1; i >= 0; --i) {
        const int fim = (i + 1 < static_cast<int>(inicios.size())) ? inicios[static_cast<size_t>(i) + 1] : texto.length();
        juce::String bloco = texto.substring(inicios[static_cast<size_t>(i)], fim);
        if (!bloco.endsWith("\n\n")) bloco = bloco.trimEnd() + "\n\n";
        novo << bloco;
    }
    logFile_.replaceWithText(novo);
}

juce::String ProjectLog::readContent() {
    std::lock_guard<std::mutex> lock(mutexDoLog());
    ensureInitialized();
    ensureNewestFirst();
    return logFile_.loadFileAsString();
}

bool ProjectLog::saveContent(const juce::String& newContent) {
    std::lock_guard<std::mutex> lock(mutexDoLog());
    if (!projectFolder_.isDirectory()) return false;
    return logFile_.replaceWithText(newContent);
}

void ProjectLog::appendEntry(const juce::String& actionTitle,
                             const juce::StringArray& details,
                             const juce::String& author) {
    appendEntries({{actionTitle, details}}, author);
}

void ProjectLog::appendEntries(const std::vector<Entry>& entries, const juce::String& author) {
    if (entries.empty()) return;
    std::lock_guard<std::mutex> lock(mutexDoLog());
    ensureInitialized();
    ensureNewestFirst();

    juce::String timestamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S");
    juce::String bloco;
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        juce::String entry;
        entry << "### [" << timestamp << "] " << it->actionTitle << "\n"
              << "- **Author / Agent:** " << (author.isEmpty() ? "Operator" : author) << "\n";

        for (const auto& line : it->details) {
            entry << "- " << line << "\n";
        }
        entry << "\n";
        bloco << entry;
    }

    // Entrada nova no TOPO (logo antes da entrada mais recente); sem entradas ainda, vai no fim do cabeçalho.
    const juce::String texto = logFile_.loadFileAsString();
    const auto inicios = inicioDasEntradas(texto);
    if (inicios.empty()) {
        logFile_.appendText(bloco);
        return;
    }
    logFile_.replaceWithText(texto.substring(0, inicios.front()) + bloco + texto.substring(inicios.front()));
}

} // namespace matriz::model
