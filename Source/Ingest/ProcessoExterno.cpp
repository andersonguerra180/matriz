#include "ProcessoExterno.h"

#include <thread>
#include <mutex>
#include <atomic>

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

namespace {

juce::StringArray prepararArgumentos(const std::string& nomeFerramenta, const juce::StringArray& argumentos) {
    juce::StringArray argv;
    argv.add(resolverCaminhoExecutavel(nomeFerramenta));
    if (nomeFerramenta == "ffmpeg" || nomeFerramenta == "ffprobe") {
        if (!argumentos.contains("-hide_banner")) argv.add("-hide_banner");
        if (!argumentos.contains("-loglevel")) { argv.add("-loglevel"); argv.add("error"); }
        if (nomeFerramenta == "ffmpeg") {
            if (!argumentos.contains("-y")) argv.add("-y");
            if (!argumentos.contains("-nostdin")) argv.add("-nostdin");
        }
    }
    argv.addArray(argumentos);
    return argv;
}

std::string executarProcesso(const std::string& nomeFerramenta, const juce::StringArray& argumentos,
                            int timeoutInatividadeMs, matriz::app::CancelamentoPtr cancelamento,
                            bool exigirSucesso) {
    auto argv = prepararArgumentos(nomeFerramenta, argumentos);

    juce::ChildProcess proc;
    if (!proc.start(argv, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        throw ProcessoExternoError(nomeFerramenta + " could not be started: " + argv[0].toStdString());

    const int timeoutInat = (timeoutInatividadeMs > 0) ? timeoutInatividadeMs : 300000;
    std::string output;
    std::mutex outputMutex;
    std::atomic<uint32_t> ultimaAtividade{juce::Time::getMillisecondCounter()};
    std::atomic<bool> leitorConcluido{false};

    std::thread leitorThread([&] {
        char buffer[4096];
        while (true) {
            int lidos = proc.readProcessOutput(buffer, sizeof(buffer));
            if (lidos <= 0) break;
            {
                std::lock_guard<std::mutex> lock(outputMutex);
                output.append(buffer, static_cast<size_t>(lidos));
            }
            ultimaAtividade.store(juce::Time::getMillisecondCounter());
        }
        leitorConcluido.store(true);
    });

    bool cancelado = false;
    bool inatividadeEstourada = false;

    while (!leitorConcluido.load() && proc.isRunning()) {
        if (cancelamento != nullptr && cancelamento->pedido()) {
            cancelado = true;
            proc.kill();
            break;
        }

        auto agora = juce::Time::getMillisecondCounter();
        auto ultima = ultimaAtividade.load();
        if (agora >= ultima && (agora - ultima) > static_cast<juce::uint32>(timeoutInat)) {
            inatividadeEstourada = true;
            proc.kill();
            break;
        }

        juce::Thread::sleep(20);
    }

    if (leitorThread.joinable())
        leitorThread.join();

    if (cancelado || (cancelamento != nullptr && cancelamento->pedido())) {
        throw ProcessoExternoError(nomeFerramenta + " cancelled by user");
    }

    if (inatividadeEstourada) {
        throw ProcessoExternoError(nomeFerramenta + " timed out due to inactivity (" + std::to_string(timeoutInat) + " ms without output)");
    }

    if (exigirSucesso && proc.getExitCode() != 0) {
        std::string errOut;
        {
            std::lock_guard<std::mutex> lock(outputMutex);
            errOut = output;
        }
        throw ProcessoExternoError(nomeFerramenta + " exited with error code " + std::to_string(proc.getExitCode()) +
                                   (errOut.empty() ? "" : ": " + errOut));
    }

    std::lock_guard<std::mutex> lock(outputMutex);
    return output;
}

} // namespace

std::string capturarSaidaTexto(const std::string& nomeFerramenta, const juce::StringArray& argumentos,
                                int timeoutInatividadeMs, matriz::app::CancelamentoPtr cancelamento) {
    return executarProcesso(nomeFerramenta, argumentos, timeoutInatividadeMs, cancelamento, false);
}

void rodarEsperandoSucesso(const std::string& nomeFerramenta, const juce::StringArray& argumentos,
                            int timeoutInatividadeMs, matriz::app::CancelamentoPtr cancelamento) {
    executarProcesso(nomeFerramenta, argumentos, timeoutInatividadeMs, cancelamento, true);
}

} // namespace matriz::ingest
