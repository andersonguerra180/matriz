#include "ProcessoExterno.h"

namespace matriz::ingest {

juce::String resolverCaminhoExecutavel(const std::string& nomeFerramenta) {
#if JUCE_WINDOWS
    juce::String nomeArquivo = juce::String(nomeFerramenta) + ".exe";
#else
    juce::String nomeArquivo = juce::String(nomeFerramenta);
#endif

    juce::File execDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();

    juce::File candidatoAoLado = execDir.getChildFile(nomeArquivo);
    if (candidatoAoLado.existsAsFile()) return candidatoAoLado.getFullPathName();

#if JUCE_MAC
    // Dentro de um .app bundle, o binário auxiliar convencionalmente fica em
    // Contents/Resources, um nível acima de Contents/MacOS (onde vive o executável).
    juce::File candidatoResources =
        execDir.getParentDirectory().getChildFile("Resources").getChildFile(nomeArquivo);
    if (candidatoResources.existsAsFile()) return candidatoResources.getFullPathName();

    for (auto* path : { "/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", "/bin" }) {
        juce::File f = juce::File(path).getChildFile(nomeArquivo);
        if (f.existsAsFile()) return f.getFullPathName();
    }
#endif

#ifdef MATRIZ_DEV_BUILD
    return juce::String(nomeFerramenta); // build de desenvolvimento: deixa o SO resolver via PATH
#else
    throw ProcessoExternoError(nomeFerramenta +
                                " não encontrado ao lado do executável (" + execDir.getFullPathName().toStdString() +
                                ") — build de produção não depende do PATH");
#endif
}

std::string capturarSaidaTexto(const std::string& nomeFerramenta, const juce::StringArray& argumentos, int timeoutMs) {
    juce::StringArray argv;
    argv.add(resolverCaminhoExecutavel(nomeFerramenta));
    argv.addArray(argumentos);

    juce::ChildProcess proc;
    if (!proc.start(argv, juce::ChildProcess::wantStdOut))
        throw ProcessoExternoError(nomeFerramenta + " could not be started: " + argv[0].toStdString());

    juce::String output = proc.readAllProcessOutput();
    proc.waitForProcessToFinish(timeoutMs);
    return output.toStdString();
}

void rodarEsperandoSucesso(const std::string& nomeFerramenta, const juce::StringArray& argumentos, int timeoutMs) {
    juce::StringArray argv;
    argv.add(resolverCaminhoExecutavel(nomeFerramenta));
    argv.addArray(argumentos);

    juce::ChildProcess proc;
    if (!proc.start(argv, juce::ChildProcess::wantStdOut))
        throw ProcessoExternoError(nomeFerramenta + " could not be started: " + argv[0].toStdString());

    proc.readAllProcessOutput();
    proc.waitForProcessToFinish(timeoutMs);
    if (proc.getExitCode() != 0)
        throw ProcessoExternoError(nomeFerramenta + " exited with error code " + std::to_string(proc.getExitCode()));
}

} // namespace matriz::ingest
