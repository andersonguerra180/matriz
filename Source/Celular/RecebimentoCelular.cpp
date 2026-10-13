#include "RecebimentoCelular.h"

#include <algorithm>
#include <random>

namespace matriz::celular {

ChaveArquivo chaveDoArquivo(const std::string& nome, std::int64_t tamanho) {
    return {juce::String::fromUTF8(nome.c_str()).trim().toLowerCase().toStdString(), tamanho};
}

ResultadoVerificacao verificarNovos(const std::vector<ArquivoAnunciado>& anunciados, const ConjuntoExistentes& existentes) {
    ResultadoVerificacao r;
    ConjuntoExistentes noLote;
    for (size_t i = 0; i < anunciados.size(); ++i) {
        const auto& a = anunciados[i];
        const auto k = chaveDoArquivo(a.nome, a.tamanho);
        if (a.tamanho < 0 || a.nome.empty()) continue;  // entrada inválida: ignora sem contar
        if (existentes.count(k) != 0 || !noLote.insert(k).second) {
            ++r.jaExistem;
            continue;
        }
        r.precisam.push_back(static_cast<int>(i));
        r.bytesPrecisam += a.tamanho;
    }
    return r;
}

bool lerAnunciados(const std::string& json, std::vector<ArquivoAnunciado>& saida) {
    saida.clear();
    const auto v = juce::JSON::parse(juce::String::fromUTF8(json.c_str()));
    auto* lista = v["files"].getArray();
    if (lista == nullptr) return false;
    for (const auto& e : *lista) {
        ArquivoAnunciado a;
        a.nome = e["name"].toString().toStdString();
        a.tamanho = static_cast<std::int64_t>(static_cast<juce::int64>(e["size"]));
        a.modificadoMs = static_cast<std::int64_t>(static_cast<juce::int64>(e["lm"]));
        saida.push_back(std::move(a));
    }
    return true;
}

ConjuntoExistentes varrerRecebidos(const juce::File& raiz) {
    ConjuntoExistentes out;
    if (!raiz.isDirectory()) return out;
    for (const auto& f : raiz.findChildFiles(juce::File::findFiles, true)) {
        if (f.hasFileExtension("part") || f.getFileName().startsWith(".")) continue;
        out.insert(chaveDoArquivo(f.getFileName().toStdString(), f.getSize()));
    }
    return out;
}

namespace {
bool quebrarIpv4(const std::string& ip, int (&o)[4]) {
    juce::StringArray p;
    p.addTokens(juce::String(ip), ".", "");
    if (p.size() != 4) return false;
    for (int i = 0; i < 4; ++i) {
        if (!p[i].containsOnly("0123456789") || p[i].isEmpty()) return false;
        o[i] = p[i].getIntValue();
        if (o[i] < 0 || o[i] > 255) return false;
    }
    return true;
}
} // namespace

bool ehIpv4Privado(const std::string& ip) {
    int o[4];
    if (!quebrarIpv4(ip, o)) return false;
    return o[0] == 10 || (o[0] == 172 && o[1] >= 16 && o[1] <= 31) || (o[0] == 192 && o[1] == 168);
}

bool ehIpv4LinkLocal(const std::string& ip) {
    int o[4];
    return quebrarIpv4(ip, o) && o[0] == 169 && o[1] == 254;
}

bool ehIpv4Loopback(const std::string& ip) {
    int o[4];
    return quebrarIpv4(ip, o) && o[0] == 127;
}

std::vector<std::string> filtrarEnderecosUtilizaveis(const std::vector<std::string>& todos) {
    std::vector<std::string> out;
    for (const auto& ip : todos) {
        int o[4];
        if (!quebrarIpv4(ip, o) || ehIpv4Loopback(ip) || ehIpv4LinkLocal(ip) || (o[0] == 0)) continue;
        if (std::find(out.begin(), out.end(), ip) == out.end()) out.push_back(ip);
    }
    const auto peso = [](const std::string& ip) {
        int o[4];
        quebrarIpv4(ip, o);
        if (o[0] == 192 && o[1] == 168) return 0;
        if (o[0] == 10) return 1;
        if (o[0] == 172) return 2;
        return 3;
    };
    std::stable_sort(out.begin(), out.end(), [&](const std::string& a, const std::string& b) { return peso(a) < peso(b); });
    return out;
}

std::vector<std::string> enderecosLocaisUtilizaveis() {
    std::vector<std::string> todos;
    for (const auto& a : juce::IPAddress::getAllAddresses(false)) todos.push_back(a.toString().toStdString());
    return filtrarEnderecosUtilizaveis(todos);
}

std::string escolherIpLocal() {
    const std::string rota = ipDaRotaPadrao();
    if (!rota.empty() && ehIpv4Privado(rota) && !ehIpv4LinkLocal(rota)) return rota;
    const auto todos = enderecosLocaisUtilizaveis();
    return todos.empty() ? std::string() : todos.front();
}

std::string montarUrl(const std::string& ip, int porta, const std::string& token) {
    return "http://" + ip + ":" + std::to_string(porta) + "/?t=" + token;
}

std::string gerarToken() {
    std::random_device rd;
    static const char* hex = "0123456789abcdef";
    std::string t;
    for (int i = 0; i < 16; ++i) {
        const unsigned v = rd() & 0xffu;
        t += hex[v >> 4];
        t += hex[v & 15];
    }
    return t;
}

namespace {
juce::File g_raizDeTeste;
}

void definirPastaRaizRecebidosParaTeste(const juce::File& pasta) { g_raizDeTeste = pasta; }

juce::File pastaRaizRecebidos() {
    if (g_raizDeTeste != juce::File()) return g_raizDeTeste;
    return juce::File::getSpecialLocation(juce::File::userPicturesDirectory).getChildFile("Matriz Mini - Received from phone");
}

juce::String nomePastaSessao(juce::Time agora) { return agora.formatted("%Y-%m-%d %H%M"); }

juce::String nomeArquivoSeguro(const juce::String& bruto) {
    // O navegador pode mandar um caminho ("pasta/arquivo.jpg"): fica só o último pedaço.
    juce::String nome = bruto.replaceCharacter('\\', '/').fromLastOccurrenceOf("/", false, false).trim();
    juce::String limpo;
    for (int i = 0; i < nome.length(); ++i) {
        const auto c = nome[i];
        if (c < 32 || c == 127 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') limpo += '_';
        else limpo += c;
    }
    limpo = limpo.trim().trimCharactersAtEnd(". ");
    while (limpo.startsWithChar('.')) limpo = limpo.substring(1);
    if (limpo.endsWithIgnoreCase(".part")) limpo += "_";
    if (limpo.isEmpty()) limpo = "file";
    if (limpo.getNumBytesAsUTF8() > 200) {
        const auto ext = limpo.fromLastOccurrenceOf(".", true, false);
        limpo = limpo.substring(0, 120) + (ext.length() <= 12 ? ext : juce::String());
    }
    return limpo;
}

juce::File escolherDestino(const juce::File& pasta, const juce::String& nomeSeguro, std::int64_t tamanho, bool& jaCompleto) {
    jaCompleto = false;
    juce::File alvo = pasta.getChildFile(nomeSeguro);
    if (!alvo.exists()) return alvo;
    if (alvo.getSize() == tamanho) {
        jaCompleto = true;
        return alvo;
    }
    const auto base = alvo.getFileNameWithoutExtension();
    const auto ext = alvo.getFileExtension();
    for (int n = 2; n < 10000; ++n) {
        juce::File cand = pasta.getChildFile(base + " (" + juce::String(n) + ")" + ext);
        if (!cand.exists()) return cand;
        if (cand.getSize() == tamanho) {
            jaCompleto = true;
            return cand;
        }
    }
    return alvo;
}

} // namespace matriz::celular
