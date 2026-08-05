#include <JuceHeader.h>

#include "App/Preferencias.h"
#include "I18n/Strings.h"
#include "Ui/IngerirArquivosTest.h"
#include "Ui/MainWindow.h"
#include "Ui/MosaicoStressTest.h"
#include "Ui/Tokens.h"
#include "Ui/UiSelfTest.h"

namespace {

class MatrizApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "BKR Matriz"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override {
        matriz::i18n::carregar(matriz::app::lerLocale());

        // LookAndFeel global a partir dos MESMOS tokens que os Components
        // customizados usam (Tokens.h) — sem isto, todo widget nativo não
        // pintado à mão (TextEditor, TextButton, ComboBox, PopupMenu,
        // AlertWindow) cai no esquema escuro padrão do LookAndFeel_V4,
        // mesmo com o tema claro dos painéis customizados: caixa de busca e
        // botões de aba ficavam pretos no meio de um fundo cinza claro.
        // Setado antes de qualquer coisa, inclusive dos modos de selftest,
        // pra as capturas de tela do harness também saírem coerentes.
        const auto& tk = matriz::ui::tema();
        juce::LookAndFeel_V4::ColourScheme esquema{tk.fundo,        tk.painel,      tk.painel,
                                                      tk.borda,       tk.textoPrimario, tk.acento,
                                                      tk.textoSobreAcento, tk.acentoHover, tk.textoPrimario};
        lookAndFeel_ = std::make_unique<juce::LookAndFeel_V4>();
        lookAndFeel_->setColourScheme(esquema);
        juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeel_.get());

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
        // Harness de UI headless (correção crítica, Parte 1): renderiza
        // telas pra PNG em test-output/ e dirige interação sintética real
        // (foco de teclado, drop de arquivo) via peer off-screen.
        if (commandLine.contains("--selftest-uitest")) {
            setApplicationReturnValue(matriz::ui::rodarUiSelfTest());
            quit();
            return;
        }

        janela_ = std::make_unique<matriz::ui::MainWindow>(matriz::i18n::t("janela_principal.titulo"));
    }

    void shutdown() override {
        janela_.reset();
        // A janela (e qualquer Component nela) tem que morrer ANTES do
        // LookAndFeel que aponta pra ela — senão fica um ponteiro pendurado
        // no LookAndFeel::getDefaultLookAndFeel() global.
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        lookAndFeel_.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<juce::LookAndFeel_V4> lookAndFeel_;
    std::unique_ptr<matriz::ui::MainWindow> janela_;
};

} // namespace

START_JUCE_APPLICATION(MatrizApplication)
