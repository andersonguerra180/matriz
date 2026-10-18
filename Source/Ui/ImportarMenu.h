#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "OverlayComponent.h"
#include "../Ingest/SobreMaterial.h"

// Janela IMPORT do Intake: cards quadrados de borda arredondada (mesmo desenho da escolha GALLERY/WHATSAPP),
// com sub-janelas por categoria. Um clique em card com filhos abre o nível seguinte (botão VOLTAR); um clique
// numa folha conclui com o id dela. Quem chama decide o que cada id faz (ver IntakeWorkspaceComponent).
//
// Ids das folhas (contrato com quem trata a escolha):
//   files.*   pastas e unidades      drive / drive.<n>   Google Drive (n = conta)   phone   Phone Dump (Gallery/WhatsApp)
//   photos.*  fotos / Lightroom / Bridge     videos.*   audio.{production,preservation,general}.*   documents.*
//   other.web | other.multimedia.* | other.social.* | other.custom
namespace matriz::ui {

// `aoConcluir` roda UMA vez: id da folha escolhida e o caminho de grupos até ela (ex.: {"audio", "audio.preservation"}),
// ou nullopt se o usuário cancelou. `caminhoInicial` abre a janela direto nesse grupo (usado pelo VOLTAR das fichas).
void mostrarMenuImportar(PainelOverlay& overlay,
                         std::function<void(std::optional<std::string>, std::vector<std::string>)> aoConcluir,
                         std::vector<std::string> caminhoInicial = {});

// Contexto que a escolha do card já implica (CONTENT, source medium, tags), aplicado a cada arquivo importado por ele.
// Nulo = o card não implica nada (ex.: "Pasta do computador", "Outro").
std::shared_ptr<const matriz::ingest::SobreMaterial> contextoDaFolhaImportar(const std::string& id);

// Soma o contexto de uma ficha ao do card (a ficha vence em CONTENT e no mesmo campo; tags, pessoas e notas somam).
std::shared_ptr<const matriz::ingest::SobreMaterial> mesclarContextosImportar(
    std::shared_ptr<const matriz::ingest::SobreMaterial> base, std::shared_ptr<const matriz::ingest::SobreMaterial> extra);

// Folhas que perguntam algo antes do seletor de arquivos (ficha de contexto). Hoje: vídeo (formato de origem).
bool folhaImportarTemFicha(const std::string& id);

// Mostra a ficha da folha. `aoConcluir(continuar, contexto)`: continuar=false = cancelou tudo; contexto nulo = o
// usuário pulou ou não respondeu nada.
// `aoVoltar` (opcional): quando existe a ficha ganha o botão VOLTAR, que chama este callback em vez de concluir.
void mostrarFichaImportar(PainelOverlay& overlay, const std::string& id,
                          std::function<void(bool, std::shared_ptr<const matriz::ingest::SobreMaterial>)> aoConcluir,
                          std::function<void()> aoVoltar = nullptr);

// Ícone com fundo branco opaco (alguns de Assets/Icons) -> o branco ligado às bordas vira transparente. Devolve a mesma
// imagem se ela já é transparente.
juce::Image tirarFundoBrancoDasBordas(const juce::Image& original);

// Para o self-test: mostra a lista de contas do Drive com contas de mentira (a máquina de teste pode não ter nenhuma).
void mostrarListaDeContasParaTeste(PainelOverlay& overlay);

// Para o self-test: todos os ids de folha (inclui as "em breve") na ordem da árvore.
std::vector<std::string> idsDasFolhasDoMenuImportar();

} // namespace matriz::ui
