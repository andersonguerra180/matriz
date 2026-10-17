#include "ServidorWhatsappApp.h"

#include "../App/Preferencias.h"
#include "../Celular/RecebimentoCelular.h"
#include "../I18n/Strings.h"

namespace matriz::whatsapp {

ServidorWhatsappApp& ServidorWhatsappApp::instancia() {
    static ServidorWhatsappApp s;
    return s;
}

juce::String ServidorWhatsappApp::chave() const { return chaveGuardada_.isNotEmpty() ? chaveGuardada_ : matriz::app::lerChaveWhatsapp(); }

juce::String ServidorWhatsappApp::nomeComputador() const {
    auto n = juce::SystemStats::getComputerName();
    return n.isEmpty() ? juce::String("Computer") : n;
}

juce::String ServidorWhatsappApp::ip() const { return juce::String(matriz::celular::escolherIpLocal()); }

juce::String ServidorWhatsappApp::urlDePareamento() const {
    return juce::String(kUrlAppWeb) + "?pc=" + ip() + ":" + juce::String(porta()) + "&k=" + chave();
}

bool ServidorWhatsappApp::iniciarCom(matriz::celular::OpcoesServidor op) {
    op.rotasWhatsapp = true;
    op.nomeComputador = nomeComputador();
    op.aoReceberConversa = [this](const juce::File& arquivo, juce::int64 tamanho) {
        // thread do servidor -> message thread
        juce::MessageManager::callAsync([this, arquivo, tamanho] {
            if (aoConversaRecebida) aoConversaRecebida(arquivo, tamanho);
        });
    };
    erro_.clear();
    return servidor_.iniciar(op, erro_);
}

bool ServidorWhatsappApp::iniciar() {
    if (servidor_.ativo()) return true;
    chaveGuardada_ = matriz::app::lerChaveWhatsapp();
    matriz::celular::OpcoesServidor op;
    op.portaPreferida = matriz::app::lerPortaWhatsapp();
    op.tentativasDePorta = 100;
    op.chaveWhatsapp = chaveGuardada_;
    const bool ok = iniciarCom(op);
    if (ok && servidor_.porta() != op.portaPreferida) matriz::app::gravarPortaWhatsapp(servidor_.porta());
    if (!ok) juce::Logger::writeToLog("WhatsApp: servidor local não abriu — " + erro_);
    return ok;
}

bool ServidorWhatsappApp::iniciarParaTeste(const juce::String& chaveTeste, int porta, const juce::File& pasta) {
    servidor_.parar();
    chaveGuardada_ = chaveTeste;
    matriz::celular::OpcoesServidor op;
    op.portaPreferida = porta;
    op.chaveWhatsapp = chaveTeste;
    op.aceitarLoopback = true;
    op.pastaRaizRecebidos = pasta;
    return iniciarCom(op);
}

void ServidorWhatsappApp::parar() { servidor_.pararEmSegundoPlano(); }

} // namespace matriz::whatsapp
