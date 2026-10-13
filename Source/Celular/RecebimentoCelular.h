#pragma once

#include <JuceHeader.h>

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Lógica pura do "Receber do celular" (sem rede, sem UI): é o que o self-test exercita.

namespace matriz::celular {

struct ArquivoAnunciado {
    std::string nome;
    std::int64_t tamanho = 0;
    std::int64_t modificadoMs = 0;  // File.lastModified do navegador (ms desde 1970)
};

// (nome em minúsculas, tamanho em bytes) — a identidade de "já temos este arquivo".
using ChaveArquivo = std::pair<std::string, std::int64_t>;
using ConjuntoExistentes = std::set<ChaveArquivo>;

ChaveArquivo chaveDoArquivo(const std::string& nome, std::int64_t tamanho);

struct ResultadoVerificacao {
    std::vector<int> precisam;  // índices (na lista anunciada) que ainda precisam ser enviados
    int jaExistem = 0;          // ignorados: já estavam nos recebidos ou no projeto/backup aberto
    std::int64_t bytesPrecisam = 0;
};

// "Só o que é novo": um arquivo é novo se (nome, tamanho) não está em `existentes`. Repetidos dentro
// do próprio lote (mesmo nome e tamanho) contam uma vez.
ResultadoVerificacao verificarNovos(const std::vector<ArquivoAnunciado>& anunciados, const ConjuntoExistentes& existentes);

// Interpreta o corpo JSON do /api/check: {"files":[{"name":"..","size":123,"lm":1700000000000}, ...]}.
// Devolve false se o JSON for inválido.
bool lerAnunciados(const std::string& json, std::vector<ArquivoAnunciado>& saida);

// Todos os (nome, tamanho) das pastas de recebidos já existentes (varre recursivo, ignora *.part).
ConjuntoExistentes varrerRecebidos(const juce::File& raizRecebidos);

// --- Rede -----------------------------------------------------------------------------------
bool ehIpv4Privado(const std::string& ip);   // 10/8, 172.16/12, 192.168/16
bool ehIpv4LinkLocal(const std::string& ip); // 169.254/16
bool ehIpv4Loopback(const std::string& ip);  // 127/8

// IPv4 locais utilizáveis (sem loopback e sem link-local), 192.168 primeiro, depois 10, depois 172.16.
std::vector<std::string> enderecosLocaisUtilizaveis();
// Mesma regra de ordenação/filtro, para teste, sobre uma lista dada.
std::vector<std::string> filtrarEnderecosUtilizaveis(const std::vector<std::string>& todos);

// IPv4 da interface da rota padrão (a rede que o computador realmente usa), sem enviar nenhum pacote.
// Vazio se não houver rota. Ignora VPN/Docker/VirtualBox por construção: eles não são a rota padrão.
std::string ipDaRotaPadrao();
// O IP a usar no pareamento: o da rota padrão se for de rede local privada; senão o primeiro utilizável.
std::string escolherIpLocal();

std::string montarUrl(const std::string& ip, int porta, const std::string& token);
std::string gerarToken();  // 128 bits aleatórios, hex

// --- Disco ----------------------------------------------------------------------------------
// <Imagens do usuário>/Matriz Mini - Received from phone
juce::File pastaRaizRecebidos();
// Só para os self-tests: redireciona a pasta raiz (nunca gravar nas Imagens do usuário). File() desfaz.
void definirPastaRaizRecebidosParaTeste(const juce::File& pasta);
juce::String nomePastaSessao(juce::Time agora);  // "AAAA-MM-DD HHmm"
// Nome vindo do navegador -> nome seguro para gravar (sem separadores, sem controle, sem ".part").
juce::String nomeArquivoSeguro(const juce::String& nomeDoNavegador);
// Escolhe o nome final dentro de `pasta`: o mesmo, ou "nome (2).ext" se já existir com OUTRO tamanho.
// `jaCompleto` = true quando já existe um arquivo com este nome e exatamente `tamanho` bytes.
juce::File escolherDestino(const juce::File& pasta, const juce::String& nomeSeguro, std::int64_t tamanho, bool& jaCompleto);

} // namespace matriz::celular
