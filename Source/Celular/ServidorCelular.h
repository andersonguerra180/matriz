#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <string>

#include "RecebimentoCelular.h"

// Servidor HTTP embutido do "Receber do celular". Só existe enquanto o painel está aberto.
// Roda em thread de fundo (a message thread nunca toca disco nem rede). O celular só usa o
// navegador: GET / devolve uma página única embutida no binário; os arquivos chegam em streaming
// (um por vez) para <nome>.part e só viram o nome final quando o tamanho confere.

namespace matriz::celular {

struct EstadoRecebimento {
    bool ativo = false;
    int porta = 0;
    bool algumCelularConectou = false;
    juce::int64 msDesdeInicio = 0;
    int totalArquivos = 0;      // recebidos + ainda por receber (sessão)
    int recebidos = 0;
    int falhas = 0;
    int jaExistiam = 0;         // ignorados no último lote (já estavam no backup/recebidos)
    juce::int64 bytesTotal = 0;
    juce::int64 bytesRecebidos = 0;
    double bytesPorSegundo = 0.0;
    juce::String arquivoAtual;  // vazio = nenhum em andamento
};

struct OpcoesServidor {
    int portaPreferida = 8765;       // se ocupada, tenta as próximas
    int tentativasDePorta = 50;
    juce::String token;              // vazio = gera um novo
    juce::File pastaRaizRecebidos;   // vazio = <Imagens>/Matriz Mini - Received from phone
    juce::String nomeSessao;         // vazio = "AAAA-MM-DD HHmm" de agora
    juce::String idioma = "pt";      // "pt" ou "en" (a página do celular)
    bool aceitarLoopback = false;    // só para os self-tests
    // Chamado em thread do servidor a cada verificação: o que o projeto/backup aberto já tem (nome + tamanho).
    std::function<ConjuntoExistentes()> existentesDoProjeto;

    // Conversas do WhatsApp pelo Wi-Fi (app web instalado no celular): rotas /whatsapp/ping e /whatsapp/receber,
    // protegidas pela chave de pareamento (?k=) e com os cabeçalhos CORS que o Chrome do celular exige.
    bool rotasWhatsapp = false;
    juce::String chaveWhatsapp;
    juce::String nomeComputador;
    juce::File pastaRecebidosWhatsapp;  // vazio = <pasta de recebidos>/WhatsApp
    // Chamados em thread do servidor. `arquivo` = o .zip recebido (um .txt vira um zip com o .txt dentro).
    std::function<void(const juce::File& arquivo, juce::int64 tamanho)> aoReceberConversa;
    std::function<void()> aoParearWhatsapp;  // primeiro ping com a chave certa
};

class ServidorCelular {
public:
    ServidorCelular();
    ~ServidorCelular();  // fecha o socket na hora; a thread é juntada em segundo plano

    bool iniciar(const OpcoesServidor& opcoes, juce::String& erro);
    // Síncrono: devolve quando a thread terminou.
    void parar();
    // Fecha a porta imediatamente e junta a thread em segundo plano (não trava a interface).
    void pararEmSegundoPlano();

    bool ativo() const;
    int porta() const;
    juce::String token() const;
    EstadoRecebimento estado() const;
    bool whatsappPareado() const;
    // Pasta desta sessão (criada só quando chega o primeiro arquivo).
    juce::File pastaSessao() const;
    bool pastaSessaoExiste() const;

    // Só para os self-tests: aceita 127.0.0.1 em qualquer servidor (o app real só aceita IPs privados).
    static void permitirLoopbackParaTeste(bool permitir);
    static bool loopbackPermitidoParaTeste();

    // Espera o fim das threads de encerramento em segundo plano (self-test e saída do app).
    static void aguardarEncerramentos();
    // Threads de servidor vivas agora (escuta + encerramentos pendentes). 0 = nada vazou.
    static int threadsAtivas();

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    juce::File pastaSessaoGuardada_;
    juce::String tokenGuardado_;
    int portaGuardada_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ServidorCelular)
};

} // namespace matriz::celular
