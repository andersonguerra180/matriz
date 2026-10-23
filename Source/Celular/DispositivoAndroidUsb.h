#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// PHONE DUMP > USB (Android): acesso a um celular Android plugado por USB em modo "Transferência de arquivos" (MTP).
//
// Mesmo desenho do Android File Transfer (engenharia reversa do AFT 1.0.507: libmtp 1.1.13 + libusb):
// abre o aparelho sem cache (LIBMTP_Open_Raw_Device_Uncached), timeout de 20 s, uma segunda tentativa 2 s depois
// no Samsung 04e8:6860, e lista cada pasta só quando ela é aberta (LIBMTP_Get_Files_And_Folders).
//
// Toda chamada à libmtp roda numa única thread de fundo, uma por vez (a libmtp não é thread-safe por aparelho).
// Os callbacks de resultado chegam na message thread (callAsync). Quem passa callback que toca componente captura
// SafePointer — a sessão pode morrer antes do callback.

namespace matriz::celular {

struct MemoriaAndroid {
    std::uint32_t id = 0;
    juce::String descricao;          // "Internal shared storage", "SD card"...
    std::uint64_t livre = 0, total = 0;
};

struct AparelhoAndroid {
    juce::String nome;               // "Samsung Galaxy S21" (fabricante + modelo, ou o nome amigável)
    juce::String serial;
    std::vector<MemoriaAndroid> memorias;
};

struct EntradaAndroid {
    std::uint32_t id = 0;
    std::uint32_t memoria = 0;
    juce::String nome;
    bool pasta = false;
    std::uint64_t tamanho = 0;
    juce::Time modificado;
};

// Um arquivo a copiar e o caminho relativo dele dentro da pasta de destino (ex.: "Camera/IMG_0001.jpg").
struct ItemCopiaAndroid {
    EntradaAndroid entrada;
    juce::String caminhoRelativo;
};

struct ProgressoCopiaAndroid {
    int feitos = 0, total = 0;
    std::uint64_t bytesFeitos = 0, bytesTotal = 0;
    juce::String arquivoAtual;
};

struct ResultadoCopiaAndroid {
    bool cancelado = false;
    int copiados = 0, jaExistiam = 0, falhas = 0;
    juce::File pasta;
    juce::String erro;
};

class SessaoAndroidUsb {
public:
    SessaoAndroidUsb();
    // Não espera a thread: pede o cancelamento e ela termina (e solta o aparelho) sozinha.
    ~SessaoAndroidUsb();

    // false nesta plataforma (o módulo só existe no macOS por enquanto).
    static bool suportado();

    // Só procura (não abre): há algum Android em MTP plugado? Usado pelo aviso de USB plugado.
    void detectar(std::function<void(bool achou)> aoTerminar);

    // Procura um Android em MTP e abre o primeiro. `erro` vem traduzido quando não deu.
    void conectar(std::function<void(bool ok, AparelhoAndroid, juce::String erro)> aoTerminar);

    // Conteúdo de uma pasta (pasta = 0xffffffff para a raiz da memória). Pastas primeiro, depois por nome.
    void listar(std::uint32_t memoria, std::uint32_t pasta,
                std::function<void(bool ok, std::vector<EntradaAndroid>, juce::String erro)> aoTerminar);

    // Expande as pastas marcadas (recursivo) em arquivos, com o caminho relativo a partir da pasta marcada.
    void expandir(std::vector<EntradaAndroid> marcados,
                  std::function<void(bool ok, std::vector<ItemCopiaAndroid>, juce::String erro)> aoTerminar);

    // Copia para `destino` (criada se faltar). Arquivo que já existe com o mesmo tamanho é pulado.
    void copiar(std::vector<ItemCopiaAndroid> itens, juce::File destino,
                std::function<void(ProgressoCopiaAndroid)> aoProgresso,
                std::function<void(ResultadoCopiaAndroid)> aoTerminar);

    // Interrompe a cópia em andamento (o arquivo pela metade é apagado).
    void cancelar();

    struct Nucleo;

private:
    std::shared_ptr<Nucleo> nucleo_;
};

// Aviso de "um aparelho USB foi plugado" (macOS: notificação do IOKit, como o agente do AFT). Não faz I/O no aparelho:
// quem recebe decide se procura um Android (SessaoAndroidUsb::conectar). Chega na message thread, já com um atraso
// de ~1,5 s (o celular reenumera ao trocar para "Transferência de arquivos") e agrupado.
class AvisoUsbPlugado {
public:
    explicit AvisoUsbPlugado(std::function<void()> aoPlugar);
    ~AvisoUsbPlugado();

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace matriz::celular
