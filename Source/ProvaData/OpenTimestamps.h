#pragma once

#include <JuceHeader.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// OpenTimestamps (https://opentimestamps.org): prova de data ancorada na blockchain do Bitcoin, sem carteira e sem custo.
// Implementação própria do formato .ots e do protocolo dos calendários públicos, no mesmo formato do cliente oficial
// (`ots stamp` / `ots upgrade` / `ots verify`): um recibo gerado aqui passa no cliente oficial e em opentimestamps.org.
// Só o hash sai do computador. Todo acesso à rede é opcional e não bloqueante (quem chama roda em background).

namespace matriz::provadata::ots {

using Bytes = std::vector<std::uint8_t>;

Bytes sha256(const Bytes& dados);
Bytes sha256DoArquivo(const juce::File& arquivo);  // lê o arquivo em blocos
std::string hex(const Bytes& b);
Bytes deHex(const std::string& s);

// Operações do formato (tag de 1 byte; append/prepend carregam um argumento).
struct Op {
    std::uint8_t tag = 0;
    Bytes arg;
    bool binaria() const { return tag == 0xf0 || tag == 0xf1; }
    bool operator<(const Op& o) const { return tag != o.tag ? tag < o.tag : arg < o.arg; }
    bool operator==(const Op& o) const { return tag == o.tag && arg == o.arg; }
};

struct Atestacao {
    enum class Tipo { Pendente, Bitcoin, Desconhecida } tipo = Tipo::Desconhecida;
    std::array<std::uint8_t, 8> tag{};
    Bytes payload;      // bruto (reserializa sem perder nada)
    std::string uri;    // Pendente: calendário
    std::uint64_t altura = 0;  // Bitcoin: número do bloco
    bool operator<(const Atestacao& o) const { return tag != o.tag ? tag < o.tag : payload < o.payload; }
    bool operator==(const Atestacao& o) const { return tag == o.tag && payload == o.payload; }
};

struct No {
    Bytes msg;
    std::vector<Atestacao> atestacoes;
    std::vector<std::pair<Op, std::unique_ptr<No>>> ops;
};

struct Recibo {
    Bytes digestArquivo;  // SHA-256 do arquivo carimbado (o manifesto)
    No raiz;              // msg = digestArquivo
};

// Cria o recibo ainda sem calendário: digest → append(nonce) → sha256. A ponta é o que vai para os calendários.
Recibo novoRecibo(const Bytes& digestArquivo, const Bytes& nonce16);
No& ponta(Recibo& r);
const No& ponta(const Recibo& r);

Bytes serializar(const Recibo& r);
std::optional<Recibo> ler(const Bytes& dados);

// Junta a resposta de um calendário (uma árvore serializada, sem cabeçalho) na ponta do recibo.
bool anexarRespostaDeCalendario(Recibo& r, const Bytes& resposta);

struct PendenteInfo {
    std::string uri;
    Bytes commitment;
};
std::vector<PendenteInfo> pendentes(const Recibo& r);
std::optional<std::uint64_t> alturaConfirmada(const Recibo& r);  // menor bloco de uma atestação Bitcoin
bool temAtestacao(const Recibo& r);                               // pendente ou Bitcoin

// ── Rede (opcional; bloqueia: chamar em background) ───────────────────────────────────────────────
const std::vector<juce::String>& calendariosPadrao();
// Envia a ponta para os calendários. Devolve quantos responderam (≥1 = recibo provisório gravado).
int enviarParaCalendarios(Recibo& r, const std::vector<juce::String>& calendarios, int timeoutMs = 12000);
// Tenta completar os pendentes. true se algo mudou (virou Bitcoin).
bool tentarAtualizar(Recibo& r, int timeoutMs = 12000);
// Data do bloco (UTC, texto "AAAA-MM-DD HH:MM:SS") por um explorador público de blocos; vazio se não der.
juce::String dataDoBloco(std::uint64_t altura, int timeoutMs = 10000);

} // namespace matriz::provadata::ots
