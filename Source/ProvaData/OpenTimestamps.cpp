#include "OpenTimestamps.h"

#include <algorithm>
#include <functional>

namespace matriz::provadata::ots {

namespace {

// Cabeçalho do arquivo .ots: "\x00OpenTimestamps\x00\x00Proof\x00" + 8 bytes de assinatura.
const Bytes& cabecalho() {
    static const Bytes h = {0x00, 'O', 'p', 'e', 'n', 'T', 'i', 'm', 'e', 's', 't', 'a', 'm', 'p', 's', 0x00, 0x00, 'P', 'r', 'o', 'o', 'f', 0x00,
                            0xbf, 0x89, 0xe2, 0xe8, 0x84, 0xe8, 0x92, 0x94};
    return h;
}

constexpr std::array<std::uint8_t, 8> kTagPendente = {0x83, 0xdf, 0xe3, 0x0d, 0x2e, 0xf9, 0x0c, 0x8e};
constexpr std::array<std::uint8_t, 8> kTagBitcoin = {0x05, 0x88, 0x96, 0x0d, 0x73, 0xd7, 0x19, 0x01};
constexpr int kProfundidadeMaxima = 400;

void escreverVaruint(Bytes& o, std::uint64_t v) {
    while (v > 0x7f) {
        o.push_back(static_cast<std::uint8_t>((v & 0x7f) | 0x80));
        v >>= 7;
    }
    o.push_back(static_cast<std::uint8_t>(v));
}

void escreverVarbytes(Bytes& o, const Bytes& b) {
    escreverVaruint(o, b.size());
    o.insert(o.end(), b.begin(), b.end());
}

struct Leitor {
    const Bytes& d;
    size_t pos = 0;
    bool ok = true;
    explicit Leitor(const Bytes& dados) : d(dados) {}
    bool fim() const { return pos >= d.size(); }
    std::uint8_t byte() {
        if (pos >= d.size()) { ok = false; return 0; }
        return d[pos++];
    }
    Bytes bytes(size_t n) {
        if (n > d.size() - pos) { ok = false; return {}; }
        Bytes r(d.begin() + static_cast<long>(pos), d.begin() + static_cast<long>(pos + n));
        pos += n;
        return r;
    }
    std::uint64_t varuint() {
        std::uint64_t v = 0;
        int desloc = 0;
        while (ok) {
            const auto b = byte();
            if (!ok) break;
            v |= static_cast<std::uint64_t>(b & 0x7f) << desloc;
            if (!(b & 0x80)) return v;
            desloc += 7;
            if (desloc > 63) { ok = false; break; }
        }
        return v;
    }
    Bytes varbytes() {
        const auto n = varuint();
        if (!ok || n > d.size()) { ok = false; return {}; }
        return bytes(static_cast<size_t>(n));
    }
};

Bytes aplicar(const Op& op, const Bytes& msg, bool& conhecido) {
    conhecido = true;
    switch (op.tag) {
        case 0x08: return sha256(msg);
        case 0xf0: { Bytes r = msg; r.insert(r.end(), op.arg.begin(), op.arg.end()); return r; }
        case 0xf1: { Bytes r = op.arg; r.insert(r.end(), msg.begin(), msg.end()); return r; }
        default: conhecido = false; return {};
    }
}

bool tagDeOpValida(std::uint8_t t) {
    return t == 0x02 || t == 0x03 || t == 0x08 || t == 0x67 || t == 0xf0 || t == 0xf1 || t == 0xf2 || t == 0xf3;
}

std::unique_ptr<No> lerNo(Leitor& l, const Bytes& msg, int profundidade);

bool lerItem(Leitor& l, No& no, std::uint8_t tag, int profundidade) {
    if (tag == 0x00) {
        Atestacao a;
        const auto t = l.bytes(8);
        if (!l.ok) return false;
        std::copy(t.begin(), t.end(), a.tag.begin());
        a.payload = l.varbytes();
        if (!l.ok) return false;
        if (a.tag == kTagPendente) {
            a.tipo = Atestacao::Tipo::Pendente;
            Leitor p(a.payload);
            const auto uri = p.varbytes();
            if (!p.ok) return false;
            a.uri.assign(uri.begin(), uri.end());
        } else if (a.tag == kTagBitcoin) {
            a.tipo = Atestacao::Tipo::Bitcoin;
            Leitor p(a.payload);
            a.altura = p.varuint();
            if (!p.ok) return false;
        }
        no.atestacoes.push_back(std::move(a));
        return true;
    }
    if (!tagDeOpValida(tag)) return false;
    Op op;
    op.tag = tag;
    if (op.binaria()) {
        op.arg = l.varbytes();
        if (!l.ok || op.arg.empty()) return false;
    }
    bool conhecido = false;
    const Bytes resultado = aplicar(op, no.msg, conhecido);
    auto filho = lerNo(l, resultado, profundidade + 1);
    if (!filho) return false;
    no.ops.emplace_back(std::move(op), std::move(filho));
    return true;
}

std::unique_ptr<No> lerNo(Leitor& l, const Bytes& msg, int profundidade) {
    if (profundidade > kProfundidadeMaxima) return nullptr;
    auto no = std::make_unique<No>();
    no->msg = msg;
    auto tag = l.byte();
    while (l.ok && tag == 0xff) {
        if (!lerItem(l, *no, l.byte(), profundidade)) return nullptr;
        if (!l.ok) return nullptr;
        tag = l.byte();
    }
    if (!l.ok) return nullptr;
    if (!lerItem(l, *no, tag, profundidade)) return nullptr;
    return l.ok ? std::move(no) : nullptr;
}

void escreverAtestacao(Bytes& o, const Atestacao& a) {
    o.insert(o.end(), a.tag.begin(), a.tag.end());
    escreverVarbytes(o, a.payload);
}

void escreverOp(Bytes& o, const Op& op) {
    o.push_back(op.tag);
    if (op.binaria()) escreverVarbytes(o, op.arg);
}

void escreverNo(Bytes& o, const No& no) {
    std::vector<const Atestacao*> ats;
    for (const auto& a : no.atestacoes) ats.push_back(&a);
    std::sort(ats.begin(), ats.end(), [](const Atestacao* a, const Atestacao* b) { return *a < *b; });
    std::vector<const std::pair<Op, std::unique_ptr<No>>*> ops;
    for (const auto& p : no.ops) ops.push_back(&p);
    std::sort(ops.begin(), ops.end(), [](auto* a, auto* b) { return a->first < b->first; });

    if (ops.empty()) {
        for (size_t i = 0; i + 1 < ats.size(); ++i) {
            o.push_back(0xff);
            o.push_back(0x00);
            escreverAtestacao(o, *ats[i]);
        }
        if (!ats.empty()) {
            o.push_back(0x00);
            escreverAtestacao(o, *ats.back());
        }
        return;
    }
    for (const auto* a : ats) {
        o.push_back(0xff);
        o.push_back(0x00);
        escreverAtestacao(o, *a);
    }
    for (size_t i = 0; i < ops.size(); ++i) {
        if (i + 1 < ops.size()) o.push_back(0xff);
        escreverOp(o, ops[i]->first);
        escreverNo(o, *ops[i]->second);
    }
}

bool contemAtestacao(const No& no, Atestacao::Tipo tipo) {
    for (const auto& a : no.atestacoes) if (a.tipo == tipo) return true;
    for (const auto& p : no.ops) if (contemAtestacao(*p.second, tipo)) return true;
    return false;
}

void mesclarNo(No& alvo, No& fonte) {
    for (auto& a : fonte.atestacoes)
        if (std::find(alvo.atestacoes.begin(), alvo.atestacoes.end(), a) == alvo.atestacoes.end()) alvo.atestacoes.push_back(a);
    for (auto& p : fonte.ops) {
        bool achou = false;
        for (auto& q : alvo.ops) {
            if (q.first == p.first) { mesclarNo(*q.second, *p.second); achou = true; break; }
        }
        if (!achou) alvo.ops.emplace_back(p.first, std::move(p.second));
    }
}

void coletarNos(No& no, std::vector<No*>& out) {
    out.push_back(&no);
    for (auto& p : no.ops) coletarNos(*p.second, out);
}

void coletarMenorAltura(const No& no, std::uint64_t& menor, bool& achou) {
    for (const auto& a : no.atestacoes)
        if (a.tipo == Atestacao::Tipo::Bitcoin && (!achou || a.altura < menor)) { menor = a.altura; achou = true; }
    for (const auto& p : no.ops) coletarMenorAltura(*p.second, menor, achou);
}

// ── HTTP ────────────────────────────────────────────────────────────────────────────────────

std::optional<Bytes> http(const juce::String& endereco, const Bytes* corpo, int timeoutMs, int& status, const juce::String& accept) {
    juce::URL url(endereco);
    if (corpo != nullptr) url = url.withPOSTData(juce::MemoryBlock(corpo->data(), corpo->size()));
    juce::String cab = "Accept: " + accept + "\r\nUser-Agent: BKR-Matriz-Mini\r\n";
    if (corpo != nullptr) cab += "Content-Type: application/x-www-form-urlencoded\r\n";
    status = 0;
    auto op = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                  .withConnectionTimeoutMs(timeoutMs)
                  .withStatusCode(&status)
                  .withExtraHeaders(cab)
                  .withHttpRequestCmd(corpo != nullptr ? "POST" : "GET");
    auto stream = url.createInputStream(op);
    if (stream == nullptr) return std::nullopt;
    juce::MemoryBlock b;
    stream->readIntoMemoryBlock(b, 4 * 1024 * 1024);
    return Bytes(static_cast<const std::uint8_t*>(b.getData()), static_cast<const std::uint8_t*>(b.getData()) + b.getSize());
}

} // namespace

// ── Públicas ─────────────────────────────────────────────────────────────────────────────────

Bytes sha256(const Bytes& dados) {
    juce::SHA256 h(dados.data(), dados.size());
    const auto r = h.getRawData();
    return Bytes(static_cast<const std::uint8_t*>(r.getData()), static_cast<const std::uint8_t*>(r.getData()) + r.getSize());
}

Bytes sha256DoArquivo(const juce::File& arquivo) {
    juce::FileInputStream in(arquivo);
    if (in.failedToOpen()) return {};
    juce::SHA256 h(in);
    const auto r = h.getRawData();
    return Bytes(static_cast<const std::uint8_t*>(r.getData()), static_cast<const std::uint8_t*>(r.getData()) + r.getSize());
}

std::string hex(const Bytes& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (auto c : b) { s += d[c >> 4]; s += d[c & 15]; }
    return s;
}

Bytes deHex(const std::string& s) {
    Bytes r;
    const auto val = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1)); };
    for (size_t i = 0; i + 1 < s.size(); i += 2) {
        const int a = val(s[i]), b = val(s[i + 1]);
        if (a < 0 || b < 0) return {};
        r.push_back(static_cast<std::uint8_t>(a * 16 + b));
    }
    return r;
}

Recibo novoRecibo(const Bytes& digestArquivo, const Bytes& nonce) {
    Recibo r;
    r.digestArquivo = digestArquivo;
    r.raiz.msg = digestArquivo;
    Op append{0xf0, nonce};
    auto apendado = std::make_unique<No>();
    apendado->msg = digestArquivo;
    apendado->msg.insert(apendado->msg.end(), nonce.begin(), nonce.end());
    Op sha{0x08, {}};
    auto topo = std::make_unique<No>();
    topo->msg = sha256(apendado->msg);
    apendado->ops.emplace_back(sha, std::move(topo));
    r.raiz.ops.emplace_back(append, std::move(apendado));
    return r;
}

No& ponta(Recibo& r) { return *r.raiz.ops.front().second->ops.front().second; }
const No& ponta(const Recibo& r) { return *r.raiz.ops.front().second->ops.front().second; }

Bytes serializar(const Recibo& r) {
    Bytes o = cabecalho();
    escreverVaruint(o, 1);  // versão do arquivo
    o.push_back(0x08);      // operação do hash do arquivo: SHA-256
    o.insert(o.end(), r.digestArquivo.begin(), r.digestArquivo.end());
    escreverNo(o, r.raiz);
    return o;
}

std::optional<Recibo> ler(const Bytes& dados) {
    const auto& h = cabecalho();
    if (dados.size() < h.size() + 3 || !std::equal(h.begin(), h.end(), dados.begin())) return std::nullopt;
    Leitor l(dados);
    l.pos = h.size();
    if (l.varuint() != 1 || !l.ok) return std::nullopt;
    if (l.byte() != 0x08) return std::nullopt;  // só SHA-256 (o que o Matriz e o cliente oficial usam por padrão)
    Recibo r;
    r.digestArquivo = l.bytes(32);
    if (!l.ok) return std::nullopt;
    auto raiz = lerNo(l, r.digestArquivo, 0);
    if (!raiz) return std::nullopt;
    r.raiz = std::move(*raiz);
    return r;
}

bool anexarRespostaDeCalendario(Recibo& r, const Bytes& resposta) {
    if (r.raiz.ops.empty()) return false;
    Leitor l(resposta);
    auto no = lerNo(l, ponta(r).msg, 0);
    if (!no) return false;
    mesclarNo(ponta(r), *no);
    return true;
}

std::vector<PendenteInfo> pendentes(const Recibo& r) {
    std::vector<PendenteInfo> out;
    std::function<void(const No&)> varrer = [&](const No& no) {
        for (const auto& a : no.atestacoes)
            if (a.tipo == Atestacao::Tipo::Pendente) out.push_back({a.uri, no.msg});
        for (const auto& p : no.ops) varrer(*p.second);
    };
    varrer(r.raiz);
    return out;
}

std::optional<std::uint64_t> alturaConfirmada(const Recibo& r) {
    std::uint64_t menor = 0;
    bool achou = false;
    coletarMenorAltura(r.raiz, menor, achou);
    return achou ? std::optional<std::uint64_t>(menor) : std::nullopt;
}

bool temAtestacao(const Recibo& r) { return contemAtestacao(r.raiz, Atestacao::Tipo::Pendente) || contemAtestacao(r.raiz, Atestacao::Tipo::Bitcoin); }

const std::vector<juce::String>& calendariosPadrao() {
    static const std::vector<juce::String> c = {"https://alice.btc.calendar.opentimestamps.org", "https://bob.btc.calendar.opentimestamps.org",
                                                "https://finney.calendar.eternitywall.com"};
    return c;
}

int enviarParaCalendarios(Recibo& r, const std::vector<juce::String>& calendarios, int timeoutMs) {
    int respondeu = 0;
    const Bytes digest = ponta(r).msg;
    for (const auto& cal : calendarios) {
        int status = 0;
        const auto resp = http(cal + "/digest", &digest, timeoutMs, status, "application/vnd.opentimestamps.v1");
        if (!resp || status != 200 || resp->empty()) continue;
        if (anexarRespostaDeCalendario(r, *resp)) ++respondeu;
    }
    return respondeu;
}

bool tentarAtualizar(Recibo& r, int timeoutMs) {
    bool mudou = false;
    for (const auto& p : pendentes(r)) {
        if (p.uri.empty()) continue;
        int status = 0;
        const auto resp = http(juce::String(p.uri) + "/timestamp/" + juce::String(hex(p.commitment)), nullptr, timeoutMs, status, "application/vnd.opentimestamps.v1");
        if (!resp || status != 200 || resp->empty()) continue;  // 404 = ainda pendente
        Leitor l(*resp);
        auto novo = lerNo(l, p.commitment, 0);
        if (!novo || !contemAtestacao(*novo, Atestacao::Tipo::Bitcoin)) continue;
        std::vector<No*> nos;
        coletarNos(r.raiz, nos);
        for (auto* no : nos) {
            if (no->msg != p.commitment) continue;
            mesclarNo(*no, *novo);
            no->atestacoes.erase(std::remove_if(no->atestacoes.begin(), no->atestacoes.end(),
                                                [&](const Atestacao& a) { return a.tipo == Atestacao::Tipo::Pendente && a.uri == p.uri; }),
                                 no->atestacoes.end());
            mudou = true;
            break;
        }
    }
    return mudou;
}

juce::String dataDoBloco(std::uint64_t altura, int timeoutMs) {
    for (const char* base : {"https://blockstream.info/api", "https://mempool.space/api"}) {
        int status = 0;
        const auto h = http(juce::String(base) + "/block-height/" + juce::String(static_cast<juce::int64>(altura)), nullptr, timeoutMs, status, "*/*");
        if (!h || status != 200) continue;
        const juce::String hash = juce::String::fromUTF8(reinterpret_cast<const char*>(h->data()), static_cast<int>(h->size())).trim();
        if (hash.length() != 64) continue;
        const auto b = http(juce::String(base) + "/block/" + hash, nullptr, timeoutMs, status, "application/json");
        if (!b || status != 200) continue;
        const auto json = juce::JSON::parse(juce::String::fromUTF8(reinterpret_cast<const char*>(b->data()), static_cast<int>(b->size())));
        const juce::int64 ts = static_cast<juce::int64>(json["timestamp"]);
        if (ts > 0) {
            const auto iso = juce::Time(ts * 1000).toISO8601(true);  // 2026-10-10T12:00:00.000Z
            return iso.substring(0, 10) + " " + iso.substring(11, 19) + " UTC";
        }
    }
    return {};
}

} // namespace matriz::provadata::ots
