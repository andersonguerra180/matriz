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
std::vector<int> inicioDasEntradas(const juce::String& texto) {
    std::vector<int> posicoes;
    auto ehEntrada = [&](int pos) {
        if (!texto.substring(pos).startsWith("### [")) return false;
        const juce::String ts = texto.substring(pos + 5, pos + 24);
        return ts.length() == 19 && ts[4] == '-' && ts[7] == '-' && ts[10] == ' ' && ts[13] == ':' && ts[16] == ':' &&
               texto[pos + 24] == ']';
    };
    if (ehEntrada(0)) posicoes.push_back(0);
    int i = 0;
    while ((i = texto.indexOf(i, "\n### [")) >= 0) {
        ++i;
        if (ehEntrada(i)) posicoes.push_back(i);
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
    std::lock_guard<std::mutex> lock(mutexDoLog());
    ensureInitialized();
    ensureNewestFirst();

    juce::String timestamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S");
    juce::String entry;
    entry << "### [" << timestamp << "] " << actionTitle << "\n"
          << "- **Author / Agent:** " << (author.isEmpty() ? "Operator" : author) << "\n";

    for (const auto& line : details) {
        entry << "- " << line << "\n";
    }
    entry << "\n";

    // Entrada nova no TOPO (logo antes da entrada mais recente); sem entradas ainda, vai no fim do cabeçalho.
    const juce::String texto = logFile_.loadFileAsString();
    const auto inicios = inicioDasEntradas(texto);
    if (inicios.empty()) {
        logFile_.appendText(entry);
        return;
    }
    logFile_.replaceWithText(texto.substring(0, inicios.front()) + entry + texto.substring(inicios.front()));
}

} // namespace matriz::model
