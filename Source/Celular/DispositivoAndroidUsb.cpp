// WINPORT: MTP no Windows é WPD (IPortableDevice); por enquanto o módulo só existe no macOS (suportado() = false).
// Os cabeçalhos do sistema vêm antes do JuceHeader: o `using namespace juce` dele deixa o Point do MacTypes ambíguo.
#if defined(__APPLE__) && MATRIZ_COM_LIBMTP
#define MATRIZ_ANDROID_USB 1
#include <libmtp.h>
#include <IOKit/IOKitLib.h>
#else
#define MATRIZ_ANDROID_USB 0
#endif

#include "DispositivoAndroidUsb.h"

#include "../I18n/Strings.h"
#include "../Model/NomesSeguros.h"

#include <algorithm>
#include <cstdlib>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>


namespace matriz::celular {

namespace {

// Resultado de uma tarefa da thread: a chave de erro é traduzida na message thread.
template <typename F>
void naMessageThread(F&& f) {
    juce::MessageManager::callAsync(std::forward<F>(f));
}

juce::String traduzir(const char* chave) { return chave == nullptr ? juce::String() : matriz::i18n::t(chave); }

} // namespace

// ── Núcleo: fila de tarefas + uma thread que fala com a libmtp ───────────────────────────────────────

struct SessaoAndroidUsb::Nucleo {
    std::mutex mx;
    std::condition_variable cv;
    std::deque<std::function<void()>> fila;
    bool encerrar = false;
    std::atomic<bool> cancelarCopia{false};
#if MATRIZ_ANDROID_USB
    LIBMTP_mtpdevice_t* aparelho = nullptr;  // só a thread do núcleo mexe
#endif

    void enfileirar(std::function<void()> tarefa) {
        {
            std::lock_guard<std::mutex> lk(mx);
            if (encerrar) return;
            fila.push_back(std::move(tarefa));
        }
        cv.notify_one();
    }

    void laco();
};

#if MATRIZ_ANDROID_USB

namespace {

// Duas sessões (a que está morrendo e a nova) nunca falam com a libmtp ao mesmo tempo.
std::mutex& mutexLibmtp() {
    static std::mutex m;
    return m;
}

void iniciarLibmtp() {
    static std::once_flag uma;
    std::call_once(uma, [] { LIBMTP_Init(); });
}

juce::String textoC(char* s) {  // libmtp devolve char* alocado (malloc)
    if (s == nullptr) return {};
    juce::String r = juce::String::fromUTF8(s).trim();
    std::free(s);
    return r;
}

void soltarAparelho(LIBMTP_mtpdevice_t*& aparelho) {
    if (aparelho != nullptr) LIBMTP_Release_Device(aparelho);
    aparelho = nullptr;
}

// Conteúdo de uma pasta. false = erro de comunicação (lista vazia de verdade devolve true).
bool listarPasta(LIBMTP_mtpdevice_t* aparelho, std::uint32_t memoria, std::uint32_t pasta, std::vector<EntradaAndroid>& saida) {
    LIBMTP_Clear_Errorstack(aparelho);
    LIBMTP_file_t* lista = LIBMTP_Get_Files_And_Folders(aparelho, memoria, pasta);
    const bool erro = lista == nullptr && LIBMTP_Get_Errorstack(aparelho) != nullptr;
    LIBMTP_Clear_Errorstack(aparelho);
    while (lista != nullptr) {
        LIBMTP_file_t* prox = lista->next;
        EntradaAndroid e;
        e.id = lista->item_id;
        e.memoria = lista->storage_id;
        e.nome = juce::String::fromUTF8(lista->filename != nullptr ? lista->filename : "");
        e.pasta = lista->filetype == LIBMTP_FILETYPE_FOLDER;
        e.tamanho = e.pasta ? 0 : lista->filesize;
        e.modificado = juce::Time(static_cast<juce::int64>(lista->modificationdate) * 1000);
        // Ocultos do Android (.thumbnails, .nomedia...) não interessam ao PHONE DUMP.
        if (e.nome.isNotEmpty() && !e.nome.startsWithChar('.')) saida.push_back(std::move(e));
        LIBMTP_destroy_file_t(lista);
        lista = prox;
    }
    std::sort(saida.begin(), saida.end(), [](const EntradaAndroid& a, const EntradaAndroid& b) {
        if (a.pasta != b.pasta) return a.pasta;
        return a.nome.compareNatural(b.nome) < 0;
    });
    return !erro;
}

struct DadosProgresso {
    SessaoAndroidUsb::Nucleo* nucleo = nullptr;
    std::function<void(ProgressoCopiaAndroid)>* aoProgresso = nullptr;
    ProgressoCopiaAndroid base;
    juce::uint32 ultimoAviso = 0;
};

int progressoLibmtp(std::uint64_t const enviados, std::uint64_t const, void const* const dados) {
    auto* d = static_cast<DadosProgresso*>(const_cast<void*>(dados));
    const auto agora = juce::Time::getMillisecondCounter();
    if (agora - d->ultimoAviso >= 120 && d->aoProgresso != nullptr && *d->aoProgresso) {
        d->ultimoAviso = agora;
        auto p = d->base;
        p.bytesFeitos += enviados;
        auto cb = *d->aoProgresso;
        naMessageThread([cb, p] { cb(p); });
    }
    return d->nucleo->cancelarCopia.load() ? 1 : 0;
}

} // namespace

void SessaoAndroidUsb::Nucleo::laco() {
    for (;;) {
        std::function<void()> tarefa;
        {
            std::unique_lock<std::mutex> lk(mx);
            cv.wait(lk, [this] { return encerrar || !fila.empty(); });
            if (encerrar) break;
            tarefa = std::move(fila.front());
            fila.pop_front();
        }
        std::lock_guard<std::mutex> lkMtp(mutexLibmtp());
        tarefa();
    }
    std::lock_guard<std::mutex> lkMtp(mutexLibmtp());
    soltarAparelho(aparelho);
}

bool SessaoAndroidUsb::suportado() { return true; }

#else

void SessaoAndroidUsb::Nucleo::laco() {
    for (;;) {
        std::function<void()> tarefa;
        {
            std::unique_lock<std::mutex> lk(mx);
            cv.wait(lk, [this] { return encerrar || !fila.empty(); });
            if (encerrar) break;
            tarefa = std::move(fila.front());
            fila.pop_front();
        }
        tarefa();
    }
}

bool SessaoAndroidUsb::suportado() { return false; }

#endif

SessaoAndroidUsb::SessaoAndroidUsb() : nucleo_(std::make_shared<Nucleo>()) {
    std::thread([n = nucleo_] { n->laco(); }).detach();  // a thread segura o núcleo até soltar o aparelho
}

SessaoAndroidUsb::~SessaoAndroidUsb() {
    {
        std::lock_guard<std::mutex> lk(nucleo_->mx);
        nucleo_->encerrar = true;
        nucleo_->fila.clear();
    }
    nucleo_->cancelarCopia = true;
    nucleo_->cv.notify_all();
}

void SessaoAndroidUsb::cancelar() { nucleo_->cancelarCopia = true; }

void SessaoAndroidUsb::detectar(std::function<void(bool)> aoTerminar) {
    nucleo_->enfileirar([aoTerminar] {
        bool achou = false;
#if MATRIZ_ANDROID_USB
        iniciarLibmtp();
        LIBMTP_raw_device_t* brutos = nullptr;
        int qtd = 0;
        achou = LIBMTP_Detect_Raw_Devices(&brutos, &qtd) == LIBMTP_ERROR_NONE && qtd > 0;
        std::free(brutos);
#endif
        naMessageThread([aoTerminar, achou] { if (aoTerminar) aoTerminar(achou); });
    });
}

void SessaoAndroidUsb::conectar(std::function<void(bool, AparelhoAndroid, juce::String)> aoTerminar) {
    Nucleo* n = nucleo_.get();
    n->enfileirar([n, aoTerminar] {
        AparelhoAndroid info;
        const char* chaveErro = nullptr;
#if MATRIZ_ANDROID_USB
        iniciarLibmtp();
        soltarAparelho(n->aparelho);
        LIBMTP_raw_device_t* brutos = nullptr;
        int qtd = 0;
        const auto det = LIBMTP_Detect_Raw_Devices(&brutos, &qtd);
        if (det == LIBMTP_ERROR_NO_DEVICE_ATTACHED || (det == LIBMTP_ERROR_NONE && qtd <= 0)) {
            chaveErro = "cel.usb.err.nenhum";
        } else if (det != LIBMTP_ERROR_NONE || brutos == nullptr) {
            chaveErro = "cel.usb.err.detectar";
        } else {
            LIBMTP_raw_device_t* bruto = &brutos[0];
            n->aparelho = LIBMTP_Open_Raw_Device_Uncached(bruto);
            // Mesmo truque do AFT: o Samsung 04e8:6860 às vezes só responde na segunda tentativa.
            if (n->aparelho == nullptr && bruto->device_entry.vendor_id == 0x04e8 && bruto->device_entry.product_id == 0x6860) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                n->aparelho = LIBMTP_Open_Raw_Device_Uncached(bruto);
            }
            if (n->aparelho == nullptr) chaveErro = "cel.usb.err.abrir";
        }
        std::free(brutos);

        if (n->aparelho != nullptr) {
            const juce::String fab = textoC(LIBMTP_Get_Manufacturername(n->aparelho));
            const juce::String modelo = textoC(LIBMTP_Get_Modelname(n->aparelho));
            const juce::String amigavel = textoC(LIBMTP_Get_Friendlyname(n->aparelho));
            info.nome = amigavel.isNotEmpty() ? amigavel : (modelo.startsWithIgnoreCase(fab) ? modelo : (fab + " " + modelo).trim());
            info.serial = textoC(LIBMTP_Get_Serialnumber(n->aparelho));
            LIBMTP_Get_Storage(n->aparelho, LIBMTP_STORAGE_SORTBY_NOTSORTED);
            for (auto* m = n->aparelho->storage; m != nullptr; m = m->next) {
                MemoriaAndroid mem;
                mem.id = m->id;
                mem.descricao = juce::String::fromUTF8(m->StorageDescription != nullptr ? m->StorageDescription : "").trim();
                mem.livre = m->FreeSpaceInBytes;
                mem.total = m->MaxCapacity;
                info.memorias.push_back(mem);
            }
            LIBMTP_Clear_Errorstack(n->aparelho);
            // Celular bloqueado: o Android abre a sessão MTP mas não mostra nenhuma memória.
            if (info.memorias.empty()) {
                chaveErro = "cel.usb.err.bloqueado";
                soltarAparelho(n->aparelho);
            }
        }
#else
        (void) n;
        chaveErro = "cel.usb.err.plataforma";
#endif
        const bool ok = chaveErro == nullptr;
        naMessageThread([aoTerminar, ok, info, chaveErro] { if (aoTerminar) aoTerminar(ok, info, traduzir(chaveErro)); });
    });
}

void SessaoAndroidUsb::listar(std::uint32_t memoria, std::uint32_t pasta,
                              std::function<void(bool, std::vector<EntradaAndroid>, juce::String)> aoTerminar) {
    Nucleo* n = nucleo_.get();
    n->enfileirar([n, memoria, pasta, aoTerminar] {
        std::vector<EntradaAndroid> itens;
        const char* chaveErro = nullptr;
#if MATRIZ_ANDROID_USB
        if (n->aparelho == nullptr) chaveErro = "cel.usb.err.desconectado";
        else if (!listarPasta(n->aparelho, memoria, pasta, itens)) chaveErro = "cel.usb.err.listar";
#else
        (void) n; (void) memoria; (void) pasta;
        chaveErro = "cel.usb.err.plataforma";
#endif
        const bool ok = chaveErro == nullptr;
        naMessageThread([aoTerminar, ok, itens = std::move(itens), chaveErro] {
            if (aoTerminar) aoTerminar(ok, itens, traduzir(chaveErro));
        });
    });
}

void SessaoAndroidUsb::expandir(std::vector<EntradaAndroid> marcados,
                                std::function<void(int, juce::String)> aoProgresso,
                                std::function<void(bool, std::vector<ItemCopiaAndroid>, juce::String)> aoTerminar) {
    Nucleo* n = nucleo_.get();
    n->cancelarCopia = false;
    n->enfileirar([n, marcados = std::move(marcados), aoProgresso, aoTerminar] {
        std::vector<ItemCopiaAndroid> saida;
        const char* chaveErro = nullptr;
#if MATRIZ_ANDROID_USB
        juce::uint32 ultimoAviso = 0;
        std::function<bool(const EntradaAndroid&, const juce::String&, int)> descer;
        descer = [&](const EntradaAndroid& e, const juce::String& prefixo, int nivel) -> bool {
            if (n->cancelarCopia.load()) return false;
            const juce::String rel = prefixo.isEmpty() ? e.nome : prefixo + "/" + e.nome;
            if (!e.pasta) {
                saida.push_back({e, rel});
                return true;
            }
            if (nivel > 64) return true;
            const auto agora = juce::Time::getMillisecondCounter();
            if (aoProgresso && (ultimoAviso == 0 || agora - ultimoAviso >= 250)) {
                ultimoAviso = agora;
                const int achados = static_cast<int>(saida.size());
                naMessageThread([aoProgresso, achados, rel] { aoProgresso(achados, rel); });
            }
            std::vector<EntradaAndroid> filhos;
            if (!listarPasta(n->aparelho, e.memoria, e.id, filhos)) return false;
            for (const auto& f : filhos)
                if (!descer(f, rel, nivel + 1)) return false;
            return true;
        };
        if (n->aparelho == nullptr) chaveErro = "cel.usb.err.desconectado";
        else
            for (const auto& m : marcados)
                if (!descer(m, {}, 0)) { chaveErro = n->cancelarCopia.load() ? "cel.usb.cancelado" : "cel.usb.err.listar"; break; }
#else
        (void) n; (void) marcados; (void) aoProgresso;
        chaveErro = "cel.usb.err.plataforma";
#endif
        const bool ok = chaveErro == nullptr;
        naMessageThread([aoTerminar, ok, saida = std::move(saida), chaveErro] {
            if (aoTerminar) aoTerminar(ok, saida, traduzir(chaveErro));
        });
    });
}

void SessaoAndroidUsb::copiar(std::vector<ItemCopiaAndroid> itens, juce::File destino,
                              std::function<void(ProgressoCopiaAndroid)> aoProgresso,
                              std::function<void(ResultadoCopiaAndroid)> aoTerminar) {
    Nucleo* n = nucleo_.get();
    n->enfileirar([n, itens = std::move(itens), destino, aoProgresso, aoTerminar]() mutable {
        ResultadoCopiaAndroid res;
        res.pasta = destino;
        const char* chaveErro = nullptr;
#if MATRIZ_ANDROID_USB
        ProgressoCopiaAndroid prog;
        prog.total = static_cast<int>(itens.size());
        for (const auto& it : itens) prog.bytesTotal += it.entrada.tamanho;

        if (n->aparelho == nullptr) chaveErro = "cel.usb.err.desconectado";
        else if (!destino.isDirectory() && !destino.createDirectory().wasOk()) chaveErro = "cel.usb.err.destino";

        for (size_t i = 0; chaveErro == nullptr && i < itens.size(); ++i) {
            if (n->cancelarCopia.load()) { res.cancelado = true; break; }
            const auto& it = itens[i];
            // Cada pedaço do caminho vindo do celular passa por NomesSeguros (Windows/macOS).
            juce::File alvo = destino;
            for (const auto& parte : juce::StringArray::fromTokens(it.caminhoRelativo, "/", ""))
                if (parte.isNotEmpty()) alvo = alvo.getChildFile(matriz::nomes_seguros::sanitizarComponente(parte));

            prog.arquivoAtual = it.entrada.nome;
            if (alvo.existsAsFile() && static_cast<std::uint64_t>(alvo.getSize()) == it.entrada.tamanho) {
                ++res.jaExistiam;
            } else {
                alvo.getParentDirectory().createDirectory();
                const juce::File parcial = alvo.getSiblingFile(alvo.getFileName() + ".part");
                parcial.deleteFile();
                DadosProgresso dados;
                dados.nucleo = n;
                dados.aoProgresso = &aoProgresso;
                dados.base = prog;
                LIBMTP_Clear_Errorstack(n->aparelho);
                const int rc = LIBMTP_Get_File_To_File(n->aparelho, it.entrada.id, parcial.getFullPathName().toRawUTF8(),
                                                       progressoLibmtp, &dados);
                LIBMTP_Clear_Errorstack(n->aparelho);
                if (rc == 0 && parcial.existsAsFile() && (alvo.deleteFile() || !alvo.exists()) && parcial.moveFileTo(alvo)) {
                    if (it.entrada.modificado.toMilliseconds() > 0) alvo.setLastModificationTime(it.entrada.modificado);
                    ++res.copiados;
                } else {
                    parcial.deleteFile();
                    if (n->cancelarCopia.load()) { res.cancelado = true; break; }
                    ++res.falhas;
                }
            }
            ++prog.feitos;
            prog.bytesFeitos += it.entrada.tamanho;
            if (aoProgresso) {
                auto cb = aoProgresso;
                auto p = prog;
                naMessageThread([cb, p] { cb(p); });
            }
        }
#else
        (void) n; (void) itens; (void) aoProgresso;
        chaveErro = "cel.usb.err.plataforma";
#endif
        n->cancelarCopia = false;
        naMessageThread([aoTerminar, res, chaveErro]() mutable {
            res.erro = traduzir(chaveErro);
            if (aoTerminar) aoTerminar(res);
        });
    });
}

// ── Aviso de USB plugado ────────────────────────────────────────────────────────────────────────

#if MATRIZ_ANDROID_USB

struct AvisoUsbPlugado::Impl : private juce::Timer {
    std::function<void()> aoPlugar;
    IONotificationPortRef porta = nullptr;
    io_iterator_t iterador = 0;

    explicit Impl(std::function<void()> cb) : aoPlugar(std::move(cb)) {
        porta = IONotificationPortCreate(MACH_PORT_NULL);
        if (porta == nullptr) return;
        CFRunLoopAddSource(CFRunLoopGetMain(), IONotificationPortGetRunLoopSource(porta), kCFRunLoopCommonModes);
        // Só a chegada de um aparelho USB (sem abrir nada): IOUSBHostDevice existe desde o macOS 10.11.
        if (IOServiceAddMatchingNotification(porta, kIOFirstMatchNotification, IOServiceMatching("IOUSBHostDevice"),
                                             &Impl::chegou, this, &iterador) == KERN_SUCCESS)
            esvaziar(iterador);  // arma a notificação; o que já estava plugado não conta
    }

    ~Impl() override {
        stopTimer();
        if (iterador != 0) IOObjectRelease(iterador);
        if (porta != nullptr) {
            CFRunLoopRemoveSource(CFRunLoopGetMain(), IONotificationPortGetRunLoopSource(porta), kCFRunLoopCommonModes);
            IONotificationPortDestroy(porta);
        }
    }

    static int esvaziar(io_iterator_t it) {
        int n = 0;
        while (io_object_t s = IOIteratorNext(it)) { IOObjectRelease(s); ++n; }
        return n;
    }

    static void chegou(void* refcon, io_iterator_t it) {  // run loop principal = message thread
        auto* self = static_cast<Impl*>(refcon);
        if (esvaziar(it) > 0) self->startTimer(1500);  // reinicia: várias chegadas seguidas viram um aviso só
    }

    void timerCallback() override {
        stopTimer();
        if (aoPlugar) aoPlugar();
    }
};

#else

struct AvisoUsbPlugado::Impl {  // WINPORT: WM_DEVICECHANGE / RegisterDeviceNotification
    explicit Impl(std::function<void()>) {}
};

#endif

AvisoUsbPlugado::AvisoUsbPlugado(std::function<void()> aoPlugar) : impl_(std::make_unique<Impl>(std::move(aoPlugar))) {}
AvisoUsbPlugado::~AvisoUsbPlugado() = default;

} // namespace matriz::celular
