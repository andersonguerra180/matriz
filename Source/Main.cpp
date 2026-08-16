#include <JuceHeader.h>

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
            // Medida independente da que o próprio teste faz: se as duas
            // discordarem, o problema está na medição, não no software.
            monitorLoop_ = std::make_unique<matriz::diag::MessageLoopMonitor>();
            setApplicationReturnValue(matriz::ui::rodarTestIngerirArquivos());
            monitorLoop_.reset();
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
        // Teste de estresse de diálogo (§3, critério 21): abre e descarta os
        // overlays 500 vezes seguidas bombeando mensagens, pra rodar sob
        // AddressSanitizer/Guard Malloc atrás de acesso a memória liberada.
        if (commandLine.contains("--selftest-modal-loop")) {
            setApplicationReturnValue(matriz::ui::rodarModalLoopSelfTest());
            quit();
            return;
        }

        // Vigia da message thread (§0): a partir daqui, qualquer callback
        // acima de 16 ms e qualquer travamento do loop acima de 300 ms vão
        // parar em ~/Library/Logs/MATRIZ/perf.log com a call stack. Fora dos
        // modos de selftest — ali o processo é headless e curto, e o log só
        // somaria ruído ao que o harness já mede.
        monitorLoop_ = std::make_unique<matriz::diag::MessageLoopMonitor>();

        janela_ = std::make_unique<matriz::ui::MainWindow>(matriz::i18n::t("janela_principal.titulo"));
    }

    void shutdown() override {
        // O vigia morre ANTES do MessageManager: um juce::Timer destruído
        // depois disso toca numa lista de timers que já não existe.
        monitorLoop_.reset();
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
    std::unique_ptr<matriz::ui::MainWindow> janela_;
};

} // namespace

START_JUCE_APPLICATION(MatrizApplication)
