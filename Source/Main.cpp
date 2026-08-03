#include <JuceHeader.h>

#include "I18n/Strings.h"
#include "Ui/IngerirArquivosTest.h"
#include "Ui/MainWindow.h"
#include "Ui/MosaicoStressTest.h"

namespace {

class MatrizApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "MATRIZ"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override {
        matriz::i18n::carregar("pt_BR");

        // Modo oculto de verificação headless (B.2): `MATRIZ
        // --selftest-mosaico-10k` roda o benchmark de virtualização do
        // mosaico e sai, sem abrir janela nenhuma.
        if (commandLine.contains("--selftest-mosaico-10k")) {
            setApplicationReturnValue(matriz::ui::rodarStressTestMosaico10k());
            quit();
            return;
        }
        if (commandLine.contains("--selftest-ingerir-arquivos")) {
            setApplicationReturnValue(matriz::ui::rodarTestIngerirArquivos());
            quit();
            return;
        }

        janela_ = std::make_unique<matriz::ui::MainWindow>(matriz::i18n::t("janela_principal.titulo"));
    }

    void shutdown() override { janela_.reset(); }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<matriz::ui::MainWindow> janela_;
};

} // namespace

START_JUCE_APPLICATION(MatrizApplication)
