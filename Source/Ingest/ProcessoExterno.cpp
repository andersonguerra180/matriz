#include "ProcessoExterno.h"

namespace matriz::ingest {

std::string capturarSaidaTexto(const juce::StringArray& argumentos, const std::string& nomeFerramenta, int timeoutMs) {
    juce::ChildProcess proc;
    if (!proc.start(argumentos, juce::ChildProcess::wantStdOut))
        throw ProcessoExternoError(nomeFerramenta + " não encontrado ou não pôde ser iniciado (esperado no PATH)");

    juce::String output = proc.readAllProcessOutput();
    proc.waitForProcessToFinish(timeoutMs);
    return output.toStdString();
}

void rodarEsperandoSucesso(const juce::StringArray& argumentos, const std::string& nomeFerramenta, int timeoutMs) {
    juce::ChildProcess proc;
    if (!proc.start(argumentos, juce::ChildProcess::wantStdOut))
        throw ProcessoExternoError(nomeFerramenta + " não encontrado ou não pôde ser iniciado (esperado no PATH)");

    proc.readAllProcessOutput();
    proc.waitForProcessToFinish(timeoutMs);
    if (proc.getExitCode() != 0)
        throw ProcessoExternoError(nomeFerramenta + " terminou com código de erro " + std::to_string(proc.getExitCode()));
}

} // namespace matriz::ingest
