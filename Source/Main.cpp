#include <JuceHeader.h>
#include <AssetsBinaryData.h>

#include "App/Preferencias.h"
#include "Diag/NSExceptionGuard.h"
#include "Diag/Watchdog.h"
#include "I18n/Strings.h"
#include "Ui/IngerirArquivosTest.h"
#include "Ui/MainWindow.h"
#include "Ui/ModalLoopSelfTest.h"
#include "Ui/MosaicoStressTest.h"
#include "Ui/Tokens.h"
#include "Ui/MatrizLookAndFeel.h"
#include "Ui/TrialNagDialog.h"
#include "Ui/UiSelfTest.h"

#include <csignal>
#include <dlfcn.h>
#include <execinfo.h>
#include <sys/ucontext.h>
#include <unistd.h>

// ASan/TSan instalam seus próprios handlers de SIGSEGV/SIGBUS/SIGABRT pra
// produzir os relatórios deles — instalar o crashHandler abaixo por cima
// desses builds tomaria o sinal antes do sanitizer conseguir reportar.
#if defined(__has_feature)
  #if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
    #define MATRIZ_SANITIZER_BUILD 1
  #endif
#endif
#ifndef MATRIZ_SANITIZER_BUILD
  #define MATRIZ_SANITIZER_BUILD 0
#endif

namespace {

#if !MATRIZ_SANITIZER_BUILD
static char g_altstack[SIGSTKSZ];

void crashHandler(int sig, siginfo_t* info, void* ctx) {
    const char msg[] = "\n=== CRASH (signal ";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
    char buf[512];
    int n = snprintf(buf, sizeof(buf), "%d, code=%d, fault_addr=%p",
                     sig, info ? info->si_code : -1, info ? info->si_addr : nullptr);
    write(STDERR_FILENO, buf, static_cast<size_t>(n));

    if (ctx) {
        auto* uc = static_cast<ucontext_t*>(ctx);
#if defined(__x86_64__)
        void* pc = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__rip);
        void* sp = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__rsp);
#elif defined(__aarch64__)
        void* pc = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__pc);
        void* sp = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__sp);
#else
        void* pc = nullptr;
        void* sp = nullptr;
#endif
        n = snprintf(buf, sizeof(buf), ", PC=%p, SP=%p", pc, sp);
        write(STDERR_FILENO, buf, static_cast<size_t>(n));
    }

    const char hdr[] = ") ===\nBacktrace:\n";
    write(STDERR_FILENO, hdr, sizeof(hdr) - 1);

    // backtrace()/backtrace_symbols_fd() não alocam memória (ao contrário de
    // backtrace_symbols()) — seguras dentro de um signal handler. A
    // varredura manual anterior (palavra por palavra a partir de
    // fault_addr, chamando dladdr em endereço nunca validado) podia reler
    // página não mapeada e travar o processo por 1-2s antes de sair — era
    // exatamente o "spinning wheel" do bug do TarefaGlobalModalDialog: este
    // handler reagindo a um crash de OUTRA causa (null pointer), não stack
    // overflow.
    void* frames[128];
    int frameCount = backtrace(frames, 128);
    backtrace_symbols_fd(frames, frameCount, STDERR_FILENO);

    const char end[] = "=== END ===\n";
    write(STDERR_FILENO, end, sizeof(end) - 1);

    // SA_RESETHAND (ver instalação abaixo) já devolveu a disposição deste
    // sinal para o default antes de entrarmos aqui — re-levantar agora deixa
    // o macOS (ReportCrash) gerar o .ips normalmente, em vez do _exit()
    // silencioso de antes, que não deixava rastro nenhum pro Console.app.
    raise(sig);
}
#endif // !MATRIZ_SANITIZER_BUILD

class SplashComponent : public juce::Component {
public:
    SplashComponent(const juce::Image& img, bool isTrial)
        : image_(img), isTrial_(isTrial) {}

    void paint(juce::Graphics& g) override {
        if (image_.isValid()) {
            g.drawImage(image_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
        } else {
            g.fillAll(juce::Colour(0xff18181b));
        }

        if (isTrial_) {
            auto bounds = getLocalBounds().toFloat();

            // Top-right TRIAL badge overlay
            float badgeW = 160.0f;
            float badgeH = 34.0f;
            auto badgeRect = juce::Rectangle<float>(bounds.getRight() - badgeW - 20.0f, 20.0f, badgeW, badgeH);

            g.setColour(juce::Colour(0xdd1a1a1e));
            g.fillRoundedRectangle(badgeRect, 6.0f);

            g.setColour(juce::Colour(0xffff9900));
            g.drawRoundedRectangle(badgeRect, 6.0f, 1.5f);

            g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
            g.setColour(juce::Colour(0xffffa826));
            g.drawText("TRIAL EDITION", badgeRect, juce::Justification::centred, false);

            // Bottom banner: NOT FOR SALE
            auto botRect = juce::Rectangle<float>(0.0f, bounds.getBottom() - 26.0f, bounds.getWidth(), 26.0f);
            g.setColour(juce::Colour(0xb8000000));
            g.fillRect(botRect);
            g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            g.setColour(juce::Colour(0xffffaa33));
            g.drawText("EVALUATION COPY — NOT FOR SALE", botRect, juce::Justification::centred, false);
        }
    }

private:
    juce::Image image_;
    bool isTrial_{false};
};

class SplashWindow : public juce::DocumentWindow, private juce::Timer {
public:
    std::function<void()> onFinished;

    SplashWindow() : juce::DocumentWindow("", juce::Colours::transparentBlack, 0) {
        auto imgData = juce::MemoryBlock(AssetsBinaryData::splash_png, AssetsBinaryData::splash_pngSize);
        auto img = juce::ImageFileFormat::loadFrom(imgData.getData(), imgData.getSize());
        bool isTrial = matriz::ui::TrialNagDialog::isTrial();

        if (img.isValid()) {
            int w = std::min(img.getWidth(), 900);
            float ratio = static_cast<float>(w) / static_cast<float>(img.getWidth());
            int h = static_cast<int>(img.getHeight() * ratio);
            auto* comp = new SplashComponent(img, isTrial);
            comp->setSize(w, h);
            setContentOwned(comp, true);
        } else {
            auto* comp = new SplashComponent(img, isTrial);
            comp->setSize(600, 340);
            setContentOwned(comp, true);
            setSize(600, 340);
        }
        setUsingNativeTitleBar(false);
        setTitleBarHeight(0);
        centreWithSize(getWidth(), getHeight());
        setDropShadowEnabled(true);
        setAlwaysOnTop(true);
        setVisible(true);
        toFront(true);
        startTimer(2200);
    }
    void closeButtonPressed() override { finish(); }
    void timerCallback() override { finish(); }

private:
    void finish() {
        stopTimer();
        setVisible(false);
        if (onFinished) {
            auto cb = std::move(onFinished);
            onFinished = nullptr;
            cb();
        }
    }
};

class MatrizApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override {
        matriz::app::inicializarPreferencias();
        matriz::diag::inicializarWatchdog();

#if !MATRIZ_SANITIZER_BUILD
        stack_t ss;
        ss.ss_sp = g_altstack;
        ss.ss_size = SIGSTKSZ;
        ss.ss_flags = 0;
        sigaltstack(&ss, nullptr);

        struct sigaction sa;
        sa.sa_sigaction = crashHandler;
        sa.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, nullptr);
        sigaction(SIGBUS, &sa, nullptr);
        sigaction(SIGABRT, &sa, nullptr);
#endif
        matriz::diag::instalarGuardaDeExcecao();
        if (commandLine.contains("--selftest-"))
            matriz::diag::desativarAppNapParaSelfTest();

        matriz::i18n::carregar(matriz::app::lerLocale());

        lookAndFeel_ = std::make_unique<matriz::ui::MatrizLookAndFeel>();
        matriz::ui::configurarLookAndFeel(*lookAndFeel_);
        juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeel_.get());

        if (commandLine.contains("--selftest-mosaico-10k")) {
            setApplicationReturnValue(matriz::ui::rodarStressTestMosaico10k());
            quit();
            return;
        }
        if (commandLine.contains("--selftest-ingerir-arquivos")) {
            monitorLoop_ = std::make_unique<matriz::diag::MessageLoopMonitor>();
            setApplicationReturnValue(matriz::ui::rodarTestIngerirArquivos());
            monitorLoop_.reset();
            quit();
            return;
        }
        if (commandLine.contains("--selftest-lightroom")) {
            monitorLoop_ = std::make_unique<matriz::diag::MessageLoopMonitor>();
            setApplicationReturnValue(matriz::ui::rodarTestLightroom());
            monitorLoop_.reset();
            quit();
            return;
        }
        if (commandLine.contains("--selftest-uitest")) {
            setApplicationReturnValue(matriz::ui::rodarUiSelfTest());
            quit();
            return;
        }
        if (commandLine.contains("--selftest-modal-loop")) {
            setApplicationReturnValue(matriz::ui::rodarModalLoopSelfTest());
            quit();
            return;
        }

        monitorLoop_ = std::make_unique<matriz::diag::MessageLoopMonitor>();

#if defined(MATRIZ_UI_APP_NAME_STRING)
        janela_ = std::make_unique<matriz::ui::MainWindow>(MATRIZ_UI_APP_NAME_STRING);
#else
        janela_ = std::make_unique<matriz::ui::MainWindow>(getApplicationName());
#endif
        janela_->setVisible(false);

        splash_ = std::make_unique<SplashWindow>();
        splash_->onFinished = [this] {
            if (janela_) {
                if (auto* tela = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                    janela_->setBounds(tela->userBounds.toNearestInt());
                janela_->setVisible(true);
                janela_->toFront(true);

                matriz::ui::TrialNagDialog::exibirSeNecessario(janela_.get());
            }
            splash_.reset();
        };
    }

    void shutdown() override {
        // O vigia morre ANTES do MessageManager: um juce::Timer destruído
        // depois disso toca numa lista de timers que já não existe.
        monitorLoop_.reset();
        splash_.reset();
        janela_.reset();
        // A janela (e qualquer Component nela) tem que morrer ANTES do
        // LookAndFeel que aponta pra ela — senão fica um ponteiro pendurado
        // no LookAndFeel::getDefaultLookAndFeel() global.
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        lookAndFeel_.reset();

        matriz::diag::fecharWatchdog();
        matriz::app::fecharPreferencias();
    }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<matriz::ui::MatrizLookAndFeel> lookAndFeel_;
    std::unique_ptr<matriz::diag::MessageLoopMonitor> monitorLoop_;
    std::unique_ptr<SplashWindow> splash_;
    std::unique_ptr<matriz::ui::MainWindow> janela_;
};

} // namespace

START_JUCE_APPLICATION(MatrizApplication)
