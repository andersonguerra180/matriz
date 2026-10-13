#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>

#include "../Celular/ServidorCelular.h"

// Servidor local do app para receber conversas do WhatsApp pelo Wi-Fi. Fica ativo enquanto o Matriz Mini estiver
// aberto (não só na tela IMPORTAR WHATSAPP). É o mesmo ServidorCelular das fotos, com as rotas /whatsapp/ ligadas:
// as fotos só abrem o seu servidor enquanto o painel está aberto, então este é o servidor "do app".

namespace matriz::whatsapp {

// Endereço do app web instalável no celular (único lugar do código onde ele aparece).
inline const char* const kUrlAppWeb = "https://andersonguerra180.github.io/matriz-mini/";

class ServidorWhatsappApp {
public:
    static ServidorWhatsappApp& instancia();

    // Abre a porta (a gravada nas preferências; se ocupada, outra livre, que passa a ser a gravada).
    bool iniciar();
    void parar();

    bool ativo() const { return servidor_.ativo(); }
    int porta() const { return servidor_.porta(); }
    const juce::String& erro() const { return erro_; }
    bool pareado() const { return servidor_.whatsappPareado(); }
    juce::String chave() const;
    juce::String nomeComputador() const;
    // IPv4 da rede local (rota padrão); vazio = sem rede.
    juce::String ip() const;
    // https://…/matriz-mini/?pc=<IP>:<PORTA>&k=<CHAVE>
    juce::String urlDePareamento() const;

    // Chamado na message thread quando chega uma conversa completa.
    std::function<void(const juce::File& arquivo, juce::int64 tamanho)> aoConversaRecebida;

    // Para o self-test: sobe com chave/porta dadas, aceitando loopback, sem tocar nas preferências.
    bool iniciarParaTeste(const juce::String& chave, int porta, const juce::File& pasta);

private:
    ServidorWhatsappApp() = default;
    bool iniciarCom(matriz::celular::OpcoesServidor opcoes);

    matriz::celular::ServidorCelular servidor_;
    juce::String erro_;
    juce::String chaveGuardada_;
};

} // namespace matriz::whatsapp
