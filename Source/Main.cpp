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
#include "Ui/UiSelfTest.h"

#include <csignal>
#include <dlfcn.h>
#include <execinfo.h>
#include <sys/ucontext.h>
#include <unistd.h>

namespace {

static char g_altstack[SIGSTKSZ];

void crashHandler(int sig, siginfo_t* info, void* ctx) {
    const char msg[] = "\n=== STACK OVERFLOW (signal ";
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

    const char hdr[] = ") ===\nScanning stack for return addresses:\n";
    write(STDERR_FILENO, hdr, sizeof(hdr) - 1);

    if (ctx) {
        uintptr_t fault = info ? (uintptr_t)info->si_addr : 0;
        uintptr_t scanStart = (fault + 0x1000) & ~(uintptr_t)0x7;
        uintptr_t scanEnd = scanStart + 0x40000;

        int found = 0;
        for (uintptr_t addr = scanStart; addr < scanEnd && found < 120; addr += sizeof(void*)) {
            void* val = *reinterpret_cast<void**>(addr);
            Dl_info dlinfo;
            if (!dladdr(val, &dlinfo) || !dlinfo.dli_sname)
                continue;
            const char* lib = "???";
            if (dlinfo.dli_fname) {
                const char* slash = strrchr(dlinfo.dli_fname, '/');
                lib = slash ? slash + 1 : dlinfo.dli_fname;
            }
            n = snprintf(buf, sizeof(buf), "  [%3d] %p  %s  %s\n",
                         found, val, lib, dlinfo.dli_sname);
            write(STDERR_FILENO, buf, static_cast<size_t>(n));
            ++found;
        }
    }

    const char end[] = "=== END ===\n";
    write(STDERR_FILENO, end, sizeof(end) - 1);
    _exit(128 + sig);
}

class SplashWindow : public juce::DocumentWindow, private juce::Timer {
public:
    std::function<void()> onFinished;

    SplashWindow() : juce::DocumentWindow("", juce::Colours::transparentBlack, 0) {
        auto imgData = juce::MemoryBlock(AssetsBinaryData::splash_png, AssetsBinaryData::splash_pngSize);
        auto img = juce::ImageFileFormat::loadFrom(imgData.getData(), imgData.getSize());
        if (img.isValid()) {
            int w = std::min(img.getWidth(), 900);
            float ratio = static_cast<float>(w) / static_cast<float>(img.getWidth());
            int h = static_cast<int>(img.getHeight() * ratio);
            auto* comp = new juce::ImageComponent();
            comp->setImage(img, juce::RectanglePlacement::stretchToFit);
            comp->setSize(w, h);
            setContentOwned(comp, true);
        } else {
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
    const juce::String getApplicationName() override { return "BKR Matriz"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override {
        matriz::app::inicializarPreferencias();
        matriz::diag::inicializarWatchdog();

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
        matriz::diag::instalarGuardaDeExcecao();

        matriz::i18n::carregar(matriz::app::lerLocale());

        const auto& tk = matriz::ui::tema();
        juce::LookAndFeel_V4::ColourScheme esquema{tk.fundo,        tk.painel,      tk.painel,
                                                      tk.borda,       tk.textoPrimario, tk.acento,
                                                      tk.textoSobreAcento, tk.acentoHover, tk.textoPrimario};
        lookAndFeel_ = std::make_unique<juce::LookAndFeel_V4>();
        lookAndFeel_->setColourScheme(esquema);
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

        janela_ = std::make_unique<matriz::ui::MainWindow>(matriz::i18n::t("janela_principal.titulo"));
        janela_->setVisible(false);

        splash_ = std::make_unique<SplashWindow>();
        splash_->onFinished = [this] {
            if (janela_) {
                if (auto* tela = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                    janela_->setBounds(tela->userBounds.toNearestInt());
                janela_->setVisible(true);
                janela_->toFront(true);
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
    std::unique_ptr<juce::LookAndFeel_V4> lookAndFeel_;
    std::unique_ptr<matriz::diag::MessageLoopMonitor> monitorLoop_;
    std::unique_ptr<SplashWindow> splash_;
    std::unique_ptr<matriz::ui::MainWindow> janela_;
};

} // namespace

START_JUCE_APPLICATION(MatrizApplication)
