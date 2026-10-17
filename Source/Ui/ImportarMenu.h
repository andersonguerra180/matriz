#pragma once

#include <JuceHeader.h>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "OverlayComponent.h"

// Janela IMPORT do Intake: cards quadrados de borda arredondada (mesmo desenho da escolha GALLERY/WHATSAPP),
// com sub-janelas por categoria. Um clique em card com filhos abre o nível seguinte (botão VOLTAR); um clique
// numa folha conclui com o id dela. Quem chama decide o que cada id faz (ver IntakeWorkspaceComponent).
//
// Ids das folhas (contrato com quem trata a escolha):
//   files.*   pastas e unidades      drive            Google Drive         phone   Phone Dump (Gallery/WhatsApp)
//   photos.*  fotos / Lightroom / Bridge              videos.*  audio.*  documents.*
//   other.web | other.multimedia.* | other.social.* | other.custom
namespace matriz::ui {

// `aoConcluir` roda UMA vez: id da folha escolhida, ou nullopt se o usuário cancelou.
void mostrarMenuImportar(PainelOverlay& overlay, std::function<void(std::optional<std::string>)> aoConcluir);

// Para o self-test: todos os ids de folha (inclui as "em breve") na ordem da árvore.
std::vector<std::string> idsDasFolhasDoMenuImportar();

} // namespace matriz::ui
