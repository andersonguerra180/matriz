#pragma once

#include <JuceHeader.h>

#include <functional>
#include <string>
#include <vector>

#include "../App/Cancelamento.h"
#include "../Consolidacao/Consolidacao.h"
#include "../Db/Database.h"

// Publicação de backup (§9) — o pacote autocontido `<Nome>.matriz/`.
//
// A diferença entre isto e a consolidação (Consolidacao.h) é o propósito:
// consolidar organiza cópias numa pasta; publicar produz um objeto de
// PRESERVAÇÃO, que precisa continuar legível daqui a décadas, num
// computador que não tem este software instalado. Daí a forma do pacote:
//
//   manifest.txt     — texto puro, caminho + SHA-256. É a fonte da verdade.
//                      Qualquer sistema consegue ler e conferir, hoje e em
//                      2050. Formato de `shasum -a 256`, verificável com
//                      `shasum -c manifest.txt` sem nenhuma ferramenta nossa.
//   manifest.sqlite  — o MESMO conteúdo indexado, só pra velocidade. Se
//                      divergir do .txt, o .txt vence.
//   catalog.sqlite   — cópia somente leitura do catálogo do arquivista: as
//                      fichas, os marcadores, a proveniência. É o
//                      conhecimento, que é o que o sistema existe pra
//                      preservar.
//   cache/           — miniaturas e formas de onda serializadas, pra o
//                      pacote poder ser navegado sem os originais montados.
//   exports/         — os arquivos originais, copiados e organizados pela
//                      máscara de metadados.
//
// Escrita segura: cada byte copiado é RELIDO do destino e o SHA-256
// recalculado antes de a publicação virar 'concluida'. Uma cópia que o
// sistema de arquivos aceitou mas gravou errado é exatamente o modo de falha
// que um backup precisa detectar na hora, não dez anos depois.

namespace matriz::publicacao {

struct ParamsPublicacao {
    juce::File pastaDestino;   // onde criar o pacote
    juce::String nomePacote;   // vira "<nomePacote>.matriz"
    std::string autor;         // assina o relatório de preservação
    std::string vaultId;       // Vault que recebe (a mídia física de destino)
    std::string nota;
    matriz::consolidacao::HierarquiaBackup hierarquia;  // vazia = a do projeto
    matriz::consolidacao::RotuloTipoMidia rotuloTipoMidia;
};

struct ResultadoPublicacao {
    std::string publicacaoId;
    juce::File pacote;
    juce::File relatorio;
    int itensGravados = 0;
    int itensPulados = 0;
    std::vector<std::string> falhas;
    bool cancelada = false;
    bool concluida = false;
};

// Chamado antes de cada item; devolver false interrompe (mesmo contrato de
// cancelamento do resto do sistema: o que já foi gravado E VERIFICADO
// continua válido).
using AoProgredir = std::function<bool(int feito, int total)>;

ResultadoPublicacao publicar(matriz::db::Database& registro, const juce::File& pastaProjeto,
                              const ParamsPublicacao& params, const AoProgredir& aoProgredir = {});

// Confere um pacote já publicado contra o próprio manifest.txt: relê cada
// arquivo listado e recalcula o SHA-256. É o que valida uma mídia de
// arquivamento anos depois, sem precisar do projeto de origem.
struct ResultadoVerificacaoPacote {
    int conferidos = 0;
    int ausentes = 0;
    int divergentes = 0;
    std::vector<std::string> problemas;
    bool integro() const { return ausentes == 0 && divergentes == 0; }
};
ResultadoVerificacaoPacote verificarPacote(const juce::File& pacote);

// true se `pasta` é (ou contém) um pacote `.matriz` publicado.
bool ehPacotePublicado(const juce::File& pasta);

} // namespace matriz::publicacao
