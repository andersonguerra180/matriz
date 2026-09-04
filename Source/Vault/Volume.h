#pragma once

#include <JuceHeader.h>

#include <string>

// Identificação física de volume (§8 — Reconciliação e Auditoria de Vaults).
//
// juce::File::getVolumeSerialNumber() retorna 0 em macOS (JUCE não implementa
// no Darwin), então não serve pra distinguir um volume do outro: todo Vault
// cairia no mesmo bucket. Aqui usamos statfs(2) pro ponto de montagem real e
// DiskArbitration pro UUID de volume — que é o identificador estável que a
// reconciliação de §8 precisa quando o disco é remontado em outro caminho.

#include "DiskIdentity.h"

namespace matriz::db {
class Database;
}

namespace matriz::vault {

struct InfoVolume {
    juce::File pontoMontagem;   // "/" , "/Volumes/BUNKER 4TB", ...
    std::string uuid;           // UUID de volume; vazio quando o SO não expõe
    std::string nome;           // rótulo pra exibição
    VolumeHardwareIdentity hardware; // Hardware physical drive info via IOKit
};

// Ponto de montagem do volume que contém `caminho`, como o sistema de
// arquivos o reporta. Cai em "/" se o caminho não puder ser resolvido.
juce::File pontoDeMontagem(const juce::File& caminho);

// Raiz do Vault: o ponto de montagem AJUSTADO pra bater com o caminho que o
// usuário e o software realmente enxergam.
//
// No macOS moderno o volume de dados é montado em "/System/Volumes/Data",
// mas o conteúdo aparece sob "/" por firmlink: statfs("/private/var/x")
// devolve "/System/Volumes/Data", enquanto o arquivo é aberto como
// "/private/var/x". Usar o ponto de montagem cru como raiz do Vault fazia o
// caminho relativo não bater com prefixo nenhum, e o registro guardava um
// caminho absoluto — que a reconciliação de §8 não consegue comparar.
juce::File raizDoVault(const juce::File& caminho);

// UUID estável do volume que contém `caminho`. String vazia quando
// indisponível (volumes de rede, imagens sem UUID) — o chamador deve então
// cair no ponto de montagem como chave.
std::string uuidDoVolume(const juce::File& caminho);

InfoVolume descreverVolume(const juce::File& caminho);

// Caminho de `arquivo` relativo à raiz do volume que o contém.
//
// Não usa juce::File::getRelativePathFrom: aquilo é textual, e com o
// caminho vindo de um link simbólico do sistema (o /var -> /private/var do
// macOS, onde vive a pasta temporária) produz coisas como
// "../../../var/folders/…". Um caminho relativo que sobe de nível não é
// relativo a nada: a reconciliação de §8 compara caminhos relativos pra
// decidir se um arquivo se mexeu, e "../.." casaria com qualquer coisa.
//
// Devolve o caminho absoluto quando o arquivo não está mesmo sob a raiz —
// honesto, e o resolvedor (Resolucao.h) sabe lidar com os dois.
std::string caminhoRelativoAoVolume(const juce::File& arquivo);

// Infere categoria de dispositivo para o ícone da UI a partir de InfoVolume (§2 Spec Storage)
std::string inferirCategoriaDispositivo(const InfoVolume& volume);

// Resolve ou cria o registro de Vault para uma pasta de destino ou arquivo ingerido,
// enriquecendo com hardware identity e respeitando categoria manual existente.
std::string obterOuCriarVaultParaDestino(matriz::db::Database& registro,
                                        const juce::File& caminhoDestino,
                                        const std::string& projetoId);

// Varre todos os volumes montados e arquivos do projeto para registrar e vincular vaults automaticamente.
void sincronizarDrivesDoProjeto(matriz::db::Database& registro, const std::string& projetoId);

} // namespace matriz::vault
