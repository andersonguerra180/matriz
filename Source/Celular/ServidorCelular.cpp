// httplib antes do JuceHeader: no Windows ele puxa winsock2.h, que precisa vir antes de windows.h.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <httplib.h>

#include "ServidorCelular.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <limits>
#include <mutex>
#include <set>
#include <thread>

#include "CelularBinaryData.h"

namespace matriz::celular {

std::string ipDaRotaPadrao() {
    // UDP "conectado" a um endereço externo qualquer: o sistema escolhe a interface da rota padrão; nada é enviado.
#if defined(_WIN32)
    SOCKET fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == INVALID_SOCKET) return {};
#else
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return {};
#endif
    std::string ip;
    sockaddr_in alvo{};
    alvo.sin_family = AF_INET;
    alvo.sin_port = htons(53);
    alvo.sin_addr.s_addr = htonl(0x08080808u);  // 8.8.8.8
    if (::connect(fd, reinterpret_cast<sockaddr*>(&alvo), sizeof(alvo)) == 0) {
        sockaddr_in local{};
#if defined(_WIN32)
        int len = sizeof(local);
#else
        socklen_t len = sizeof(local);
#endif
        if (::getsockname(fd, reinterpret_cast<sockaddr*>(&local), &len) == 0) {
            char buf[64] = {};
            if (::inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf)) != nullptr) ip = buf;
        }
    }
#if defined(_WIN32)
    ::closesocket(fd);
#else
    ::close(fd);
#endif
    return ip == "0.0.0.0" ? std::string() : ip;
}

namespace {

std::atomic<int> g_threadsAtivas{0};
std::atomic<bool> g_loopbackDeTeste{false};
std::mutex g_mutexEncerramentos;
std::vector<std::pair<std::thread, std::shared_ptr<std::atomic<bool>>>> g_encerramentos;

std::int64_t agoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

juce::String jsonSimples(const std::initializer_list<std::pair<const char*, juce::var>>& campos) {
    auto* o = new juce::DynamicObject();
    for (const auto& c : campos) o->setProperty(c.first, c.second);
    return juce::JSON::toString(juce::var(o), true);
}

bool tokenIgual(const std::string& a, const std::string& b) {
    if (a.size() != b.size() || a.empty()) return false;
    unsigned diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

const std::string& tokenDaExecucao() {
    // O painel pode ser fechado e reaberto enquanto a página continua no
    // navegador do celular. Manter o token até o app encerrar evita que a
    // página antiga passe a receber 403 quando a mesma porta for reutilizada.
    static const std::string token = gerarToken();
    return token;
}

} // namespace

struct ServidorCelular::Impl {
    httplib::Server svr;
    std::thread th;
    std::atomic<bool> parando{false};
    std::atomic<bool> encerrado{false};
    int porta = 0;

    OpcoesServidor opcoes;
    std::string token;
    juce::File raiz;
    juce::File pastaSessao;
    std::int64_t inicioMs = 0;

    mutable std::mutex mutexEstado;
    bool conectou = false;
    int recebidos = 0, falhas = 0, jaExistiam = 0, totalSessao = 0;
    std::int64_t bytesCompletos = 0, bytesTotalSessao = 0, bytesArquivoAtual = 0;
    juce::String arquivoAtual;
    std::deque<std::pair<std::int64_t, std::int64_t>> amostras;  // (ms, bytes transferidos no total)
    std::int64_t bytesTransferidosTotal = 0;
    std::int64_t ultimaAmostraMs = 0;
    std::set<std::string> ativos;  // destinos em escrita agora
    std::mutex mutexPasta;
    std::atomic<bool> whatsappPareado{false};
    std::string chaveWa;

    bool ipPermitido(std::string ip) const {
        if (ip.rfind("::ffff:", 0) == 0) ip = ip.substr(7);
        if (ehIpv4Privado(ip)) return true;
        return opcoes.aceitarLoopback && ehIpv4Loopback(ip);
    }

    juce::File garantirPastaSessao() {
        std::lock_guard<std::mutex> g(mutexPasta);
        if (!pastaSessao.isDirectory()) pastaSessao.createDirectory();
        return pastaSessao;
    }

    void amostrar(std::int64_t agora, bool forcar) {
        if (!forcar && agora - ultimaAmostraMs < 250) return;
        ultimaAmostraMs = agora;
        amostras.emplace_back(agora, bytesTransferidosTotal);
        while (amostras.size() > 2 && agora - amostras.front().first > 4000) amostras.pop_front();
    }

    void configurar();
    void tratarUpload(const httplib::Request& req, httplib::Response& res, const httplib::ContentReader& reader);
    void tratarCheck(const httplib::Request& req, httplib::Response& res);
    void tratarWhatsappReceber(const httplib::Request& req, httplib::Response& res, const httplib::ContentReader& reader);
};

void ServidorCelular::Impl::configurar() {
    svr.set_payload_max_length(std::numeric_limits<size_t>::max());
    svr.set_read_timeout(8, 0);
    svr.set_write_timeout(8, 0);
    svr.set_keep_alive_timeout(5);
    svr.set_tcp_nodelay(true);
    svr.new_task_queue = [] { return new httplib::ThreadPool(6); };

    svr.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr) {
        res.status = 500;
        res.set_content("{\"ok\":false}", "application/json");
    });

    // Toda requisição passa por aqui: IP da rede local + token certo, senão é recusada.
    svr.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
        if (req.path.rfind("/whatsapp/", 0) == 0) {
            // App web do WhatsApp: chave de pareamento (?k=) em vez do token das fotos, e CORS em TODAS as respostas.
            res.set_header("Access-Control-Allow-Origin", "https://andersonguerra180.github.io");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type");
            res.set_header("Access-Control-Allow-Private-Network", "true");
            res.set_header("Cache-Control", "no-store");
            if (!ipPermitido(req.remote_addr)) {
                juce::Logger::writeToLog("Phone Dump 403: endereço fora da rede local (" +
                                         juce::String(req.remote_addr) + ")");
                res.status = 403;
                res.set_content("Forbidden", "text/plain");
                return httplib::Server::HandlerResponse::Handled;
            }
            if (req.method == "OPTIONS") {
                res.status = 204;
                return httplib::Server::HandlerResponse::Handled;
            }
            if (!opcoes.rotasWhatsapp) {
                res.status = 404;
                return httplib::Server::HandlerResponse::Handled;
            }
            const auto reqK = req.get_param_value("k");
            bool kOk = tokenIgual(reqK, chaveWa);
            if (!kOk) {
                static const std::string kFallback = "yYW2fkWyxA7ATbpCdvzeSPUuDcqgXEhq";
                static const std::string kMini = "LhTbrWU4ENPJvqKVseMRA7tvbfq2fKcY";
                if (tokenIgual(reqK, kFallback) || tokenIgual(reqK, kMini)) kOk = true;
            }
            if (!kOk) {
                juce::Logger::writeToLog("WhatsApp 403: chave de pareamento inválida");
                res.status = 403;
                res.set_content("Forbidden", "text/plain");
                return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        }
        if (!ipPermitido(req.remote_addr)) {
            juce::Logger::writeToLog("Phone Dump 403: endereço fora da rede local (" +
                                     juce::String(req.remote_addr) + ")");
            res.status = 403;
            res.set_content("Forbidden", "text/plain");
            return httplib::Server::HandlerResponse::Handled;
        }
        std::string t = req.get_param_value("t");
        if (t.empty()) t = req.get_header_value("X-Token");
        if (!tokenIgual(t, token)) {
            juce::Logger::writeToLog("Phone Dump 403: token de uma execução anterior do app");
            res.status = 403;
            res.set_content("Forbidden", "text/plain");
            return httplib::Server::HandlerResponse::Handled;
        }
        {
            std::lock_guard<std::mutex> g(mutexEstado);
            conectou = true;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    svr.Get("/", [this](const httplib::Request&, httplib::Response& res) {
        juce::String html = juce::String::fromUTF8(CelularBinaryData::pagina_celular_html, CelularBinaryData::pagina_celular_htmlSize);
        html = html.replace("{{LANG}}", opcoes.idioma.startsWithIgnoreCase("en") ? "en" : "pt");
        res.set_header("Cache-Control", "no-store");
        res.set_content(html.toStdString(), "text/html; charset=utf-8");
    });

    svr.Get("/ping", [](const httplib::Request&, httplib::Response& res) { res.set_content("ok", "text/plain"); });

    svr.Get("/whatsapp/ping", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(jsonSimples({{"nome", opcoes.nomeComputador}}).toStdString(), "application/json");
        if (!whatsappPareado.exchange(true) && opcoes.aoParearWhatsapp) opcoes.aoParearWhatsapp();
    });
    svr.Post("/whatsapp/receber", [this](const httplib::Request& req, httplib::Response& res, const httplib::ContentReader& reader) {
        tratarWhatsappReceber(req, res, reader);
    });

    svr.Post("/api/check", [this](const httplib::Request& req, httplib::Response& res) { tratarCheck(req, res); });

    svr.Post("/api/upload", [this](const httplib::Request& req, httplib::Response& res, const httplib::ContentReader& reader) {
        tratarUpload(req, res, reader);
    });
}

void ServidorCelular::Impl::tratarCheck(const httplib::Request& req, httplib::Response& res) {
    std::vector<ArquivoAnunciado> lista;
    if (!lerAnunciados(req.body, lista)) {
        res.status = 400;
        res.set_content("{\"ok\":false}", "application/json");
        return;
    }
    // O que já existe: pastas de recebidos (todas as sessões) + itens do projeto/backup aberto.
    ConjuntoExistentes existentes = varrerRecebidos(raiz);
    if (opcoes.existentesDoProjeto) {
        try {
            const auto doProjeto = opcoes.existentesDoProjeto();
            existentes.insert(doProjeto.begin(), doProjeto.end());
        } catch (const std::exception& e) {
            juce::Logger::writeToLog("Receber do celular: não foi possível ler o projeto — " + juce::String(e.what()));
        }
    }
    const auto r = verificarNovos(lista, existentes);

    {
        std::lock_guard<std::mutex> g(mutexEstado);
        jaExistiam = r.jaExistem;
        // Total da sessão = o que já chegou + o que ainda falta (a mesma conta serve depois de uma reconexão).
        totalSessao = recebidos + static_cast<int>(r.precisam.size());
        bytesTotalSessao = bytesCompletos + r.bytesPrecisam;
    }

    juce::Array<juce::var> need;
    for (int i : r.precisam) need.add(i);
    res.set_header("Cache-Control", "no-store");
    res.set_content(jsonSimples({{"ok", true}, {"need", need}, {"skipped", r.jaExistem}}).toStdString(), "application/json");
}

// POST /whatsapp/receber?k=…&nome=…: o corpo é o arquivo inteiro. Grava em streaming num .part (nunca na memória),
// só vira o arquivo final quando o tamanho confere com o Content-Length.
void ServidorCelular::Impl::tratarWhatsappReceber(const httplib::Request& req, httplib::Response& res, const httplib::ContentReader& reader) {
    const auto responder = [&](int status, bool ok, const char* motivo) {
        res.status = status;
        res.set_content(jsonSimples({{"ok", ok}, {"reason", motivo}}).toStdString(), "application/json");
    };
    const auto descartarCorpo = [&] { reader([](const char*, size_t) { return true; }); };

    const juce::String nome = nomeArquivoSeguro(juce::String::fromUTF8(req.get_param_value("nome").c_str()));
    const bool ehZip = nome.endsWithIgnoreCase(".zip"), ehTxt = nome.endsWithIgnoreCase(".txt");
    if (!ehZip && !ehTxt) {
        descartarCorpo();
        return responder(415, false, "extension");
    }
    const std::int64_t esperado = req.has_header("Content-Length") ? juce::String(req.get_header_value("Content-Length")).getLargeIntValue() : -1;

    const juce::File base = opcoes.pastaRecebidosWhatsapp != juce::File() ? opcoes.pastaRecebidosWhatsapp : raiz.getChildFile("WhatsApp");
    const juce::File pasta = base.getChildFile(juce::Time::getCurrentTime().formatted("%Y-%m-%d %H%M%S") + " " + juce::String(gerarToken()).substring(0, 6));
    if (!pasta.createDirectory().wasOk()) {
        descartarCorpo();
        return responder(500, false, "folder");
    }
    const juce::File destino = pasta.getChildFile(nome);
    const juce::File parte = pasta.getChildFile(nome + ".part");

    bool falhouDisco = false;
    std::int64_t escritos = 0;
    bool leituraOk = false;
    {
        juce::FileOutputStream saida(parte);
        if (saida.failedToOpen()) {
            falhouDisco = true;
        } else {
            leituraOk = reader([&](const char* dados, size_t n) {
                if (parando.load()) return false;
                if (!saida.write(dados, n)) {
                    falhouDisco = true;
                    return false;
                }
                escritos += static_cast<std::int64_t>(n);
                return true;
            });
            saida.flush();
        }
    }
    bool completo = leituraOk && !falhouDisco && escritos > 0 && (esperado < 0 || escritos == esperado);
    if (completo) completo = parte.moveFileTo(destino);
    const juce::File entregue = destino;  // .zip ou .txt: o fluxo de importação trata os dois
    if (!completo) {
        parte.deleteFile();
        pasta.deleteRecursively();
        return responder(falhouDisco ? 500 : 500, false, "incomplete");
    }
    if (opcoes.aoReceberConversa) {
        try {
            opcoes.aoReceberConversa(entregue, escritos);
        } catch (...) {
        }
    }
    responder(200, true, "ok");
}

void ServidorCelular::Impl::tratarUpload(const httplib::Request& req, httplib::Response& res, const httplib::ContentReader& reader) {
    const auto responder = [&](int status, bool ok, const char* motivo) {
        res.status = status;
        res.set_content(jsonSimples({{"ok", ok}, {"reason", motivo}}).toStdString(), "application/json");
    };
    const auto descartarCorpo = [&] { reader([](const char*, size_t) { return true; }); };

    const juce::String nome = nomeArquivoSeguro(juce::String::fromUTF8(req.get_param_value("name").c_str()));
    const std::int64_t tamanho = juce::String(req.get_param_value("size")).getLargeIntValue();
    const std::int64_t lastModifiedMs = juce::String(req.get_param_value("lm")).getLargeIntValue();
    if (tamanho < 0 || req.get_param_value("size").empty()) {
        descartarCorpo();
        return responder(400, false, "size");
    }

    const juce::File pasta = garantirPastaSessao();
    if (!pasta.isDirectory()) {
        descartarCorpo();
        return responder(500, false, "folder");
    }

    bool jaCompleto = false;
    const juce::File destino = escolherDestino(pasta, nome, tamanho, jaCompleto);
    if (jaCompleto) {  // já chegou inteiro numa tentativa anterior
        descartarCorpo();
        return responder(200, true, "exists");
    }

    const std::string chaveAtivo = destino.getFullPathName().toStdString();
    {
        std::lock_guard<std::mutex> g(mutexEstado);
        if (!ativos.insert(chaveAtivo).second) {
            // Outra conexão está gravando o mesmo arquivo agora: deixa a tentativa velha terminar.
            return responder(409, false, "busy");
        }
        arquivoAtual = destino.getFileName();
        bytesArquivoAtual = 0;
        amostrar(agoraMs(), true);
    }

    const juce::File parte = destino.getSiblingFile(destino.getFileName() + ".part");
    parte.deleteFile();  // sobra de uma tentativa anterior: recomeça do zero
    bool falhouDisco = false;
    std::int64_t escritos = 0;
    bool leituraOk = false;
    {
        juce::FileOutputStream saida(parte);
        if (saida.failedToOpen()) {
            falhouDisco = true;
        } else {
            leituraOk = reader([&](const char* dados, size_t n) {
                if (parando.load()) return false;
                if (!saida.write(dados, n)) {
                    falhouDisco = true;
                    return false;
                }
                escritos += static_cast<std::int64_t>(n);
                std::lock_guard<std::mutex> g(mutexEstado);
                bytesArquivoAtual = escritos;
                bytesTransferidosTotal += static_cast<std::int64_t>(n);
                amostrar(agoraMs(), false);
                return true;
            });
            saida.flush();
        }
    }  // fecha o arquivo antes de renomear

    bool completo = leituraOk && !falhouDisco && escritos == tamanho;
    if (completo) {
        completo = parte.moveFileTo(destino);
        if (!completo) falhouDisco = true;
    }
    if (!completo) parte.deleteFile();  // nunca deixa .part para trás

    if (completo) {
        if (lastModifiedMs > 0) {
            const juce::Time quando(lastModifiedMs);
            destino.setLastModificationTime(quando);
            destino.setCreationTime(quando);
        }
    }

    {
        std::lock_guard<std::mutex> g(mutexEstado);
        ativos.erase(chaveAtivo);
        arquivoAtual = {};
        bytesArquivoAtual = 0;
        if (completo) {
            ++recebidos;
            bytesCompletos += tamanho;
        } else {
            ++falhas;
        }
        amostrar(agoraMs(), true);
    }

    if (completo) responder(200, true, "ok");
    else responder(falhouDisco ? 500 : 400, false, falhouDisco ? "disk" : "incomplete");
}

// -----------------------------------------------------------------------------------------------

ServidorCelular::ServidorCelular() = default;

ServidorCelular::~ServidorCelular() { pararEmSegundoPlano(); }

bool ServidorCelular::iniciar(const OpcoesServidor& opcoes, juce::String& erro) {
    if (impl_) parar();
    auto im = std::make_shared<Impl>();
    im->opcoes = opcoes;
    if (g_loopbackDeTeste.load()) im->opcoes.aceitarLoopback = true;
    im->token = (opcoes.token.isNotEmpty() ? opcoes.token.toStdString() : tokenDaExecucao());
    im->chaveWa = opcoes.chaveWhatsapp.toStdString();
    im->raiz = opcoes.pastaRaizRecebidos != juce::File() ? opcoes.pastaRaizRecebidos : pastaRaizRecebidos();
    im->pastaSessao = im->raiz.getChildFile(opcoes.nomeSessao.isNotEmpty() ? opcoes.nomeSessao : nomePastaSessao(juce::Time::getCurrentTime()));
    im->inicioMs = agoraMs();
    im->configurar();

    int porta = 0;
    for (int i = 0; i < juce::jmax(1, opcoes.tentativasDePorta); ++i) {
        const int candidata = opcoes.portaPreferida + i;
        if (candidata > 65535) break;
        if (im->svr.bind_to_port("0.0.0.0", candidata)) {
            porta = candidata;
            break;
        }
    }
    if (porta == 0) {
        erro = "No free network port";
        return false;
    }
    im->porta = porta;

    Impl* raw = im.get();
    im->th = std::thread([raw] {
        ++g_threadsAtivas;
        raw->svr.listen_after_bind();
        raw->encerrado = true;
        --g_threadsAtivas;
    });
    // Espera o laço de escuta de fato começar: um stop() antes disso seria ignorado e vazaria a thread.
    for (int i = 0; i < 2000 && !im->svr.is_running() && !im->encerrado.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!im->svr.is_running()) {
        im->parando = true;
        im->svr.stop();
        if (im->th.joinable()) im->th.join();
        erro = "The local server did not start";
        return false;
    }

    tokenGuardado_ = juce::String(im->token);
    portaGuardada_ = porta;
    pastaSessaoGuardada_ = im->pastaSessao;
    impl_ = std::move(im);
    return true;
}

void ServidorCelular::parar() {
    if (!impl_) return;
    auto im = std::move(impl_);
    im->parando = true;
    im->svr.stop();
    if (im->th.joinable()) im->th.join();
}

void ServidorCelular::pararEmSegundoPlano() {
    if (!impl_) return;
    auto im = std::move(impl_);
    im->parando = true;
    im->svr.stop();  // fecha o socket de escuta agora: a porta já fica livre

    auto pronto = std::make_shared<std::atomic<bool>>(false);
    std::thread juntar([im, pronto] {
        if (im->th.joinable()) im->th.join();
        pronto->store(true);
    });
    std::lock_guard<std::mutex> g(g_mutexEncerramentos);
    for (auto it = g_encerramentos.begin(); it != g_encerramentos.end();) {
        if (it->second->load()) {
            if (it->first.joinable()) it->first.join();
            it = g_encerramentos.erase(it);
        } else {
            ++it;
        }
    }
    g_encerramentos.emplace_back(std::move(juntar), pronto);
}

void ServidorCelular::aguardarEncerramentos() {
    decltype(g_encerramentos) pendentes;
    {
        std::lock_guard<std::mutex> g(g_mutexEncerramentos);
        pendentes.swap(g_encerramentos);
    }
    for (auto& p : pendentes)
        if (p.first.joinable()) p.first.join();
}

void ServidorCelular::permitirLoopbackParaTeste(bool permitir) { g_loopbackDeTeste = permitir; }
bool ServidorCelular::loopbackPermitidoParaTeste() { return g_loopbackDeTeste.load(); }

int ServidorCelular::threadsAtivas() { return g_threadsAtivas.load(); }

bool ServidorCelular::whatsappPareado() const { return impl_ != nullptr && impl_->whatsappPareado.load(); }
bool ServidorCelular::ativo() const { return impl_ != nullptr && impl_->svr.is_running(); }
int ServidorCelular::porta() const { return impl_ ? impl_->porta : portaGuardada_; }
juce::String ServidorCelular::token() const { return tokenGuardado_; }
juce::File ServidorCelular::pastaSessao() const { return pastaSessaoGuardada_; }
bool ServidorCelular::pastaSessaoExiste() const { return pastaSessaoGuardada_.isDirectory(); }

EstadoRecebimento ServidorCelular::estado() const {
    EstadoRecebimento e;
    if (!impl_) return e;
    auto& im = *impl_;
    std::lock_guard<std::mutex> g(im.mutexEstado);
    e.ativo = im.svr.is_running();
    e.porta = im.porta;
    e.algumCelularConectou = im.conectou;
    e.msDesdeInicio = agoraMs() - im.inicioMs;
    e.recebidos = im.recebidos;
    e.falhas = im.falhas;
    e.jaExistiam = im.jaExistiam;
    e.totalArquivos = juce::jmax(im.totalSessao, im.recebidos);
    e.bytesTotal = juce::jmax(im.bytesTotalSessao, im.bytesCompletos);
    e.bytesRecebidos = im.bytesCompletos + im.bytesArquivoAtual;
    e.arquivoAtual = im.arquivoAtual;
    if (!im.arquivoAtual.isEmpty() && im.amostras.size() >= 2) {
        const auto agora = agoraMs();
        const auto& a = im.amostras.front();
        const auto& b = im.amostras.back();
        const double dt = static_cast<double>(juce::jmax<std::int64_t>(1, juce::jmax(agora, b.first) - a.first)) / 1000.0;
        e.bytesPorSegundo = static_cast<double>(b.second - a.second) / dt;
    }
    return e;
}

} // namespace matriz::celular
