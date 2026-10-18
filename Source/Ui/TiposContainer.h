#pragma once

#include <JuceHeader.h>

#include <functional>
#include <string>
#include <vector>

#include "OverlayComponent.h"

// "Group as Project Container": os tipos de container que o usuario escolhe DEPOIS da importacao (INTAKE, METADATA, FOLDER MAP).
// Os tipos com player proprio (origemPlayer nao vazia) geram o mesmo container que o card do IMPORT; os demais abrem o preview
// normal com a lista dos membros. O tipo fica em nest.tipo_container; nest.origem diz qual visual/player vale.

namespace matriz::ui {

class ProjetoAberto;

enum class CategoriaContainer { Video, Audio, Image, Docs, Outro };
// Lista de CONTENT (collection_type) a que o valor pertence: so os arquivos dessa categoria de midia o recebem.
enum class CategoriaConteudo { Nenhuma, Video, Audio, Image, Docs };

struct TipoContainer {
    const char* id;              // ex.: "audio.album"; os tipos com player usam o id do card do IMPORT ("videos.youtube"...)
    CategoriaContainer categoria;
    const char* chaveRotulo;     // chave i18n
    const char* origemPlayer;    // "" = sem player; senao a origem do container do IMPORT (youtube, production, ddp, vinyl, tape)
    const char* content;         // CONTENT gravado nos membros; "" = nao grava (ou vem da ficha, nos tipos com player)
    CategoriaConteudo categoriaConteudo;
    const char* icone;           // recurso embutido (ci_*_png)
    bool temPlayer() const { return origemPlayer[0] != '\0'; }
};

// Origem de nest dos containers sem player.
inline constexpr const char* kOrigemContainerGenerico = "projeto";

const std::vector<TipoContainer>& tiposDeContainer();
const TipoContainer* tipoDeContainer(const std::string& id);
// Tipo do container ja existente: nest.tipo_container; vazio (container do IMPORT antigo) = o tipo da origem.
const TipoContainer* tipoDeContainerDaOrigem(const std::string& tipoGuardado, const std::string& origem);
// Origem de nest que o tipo grava.
std::string origemDoTipoContainer(const TipoContainer& tipo);
juce::String rotuloDoTipoContainer(const TipoContainer& tipo);
juce::String rotuloDaCategoriaContainer(CategoriaContainer c);

// Categoria dominante entre os itens (a que mais aparece na selecao); empate: a ordem Video, Audio, Image, Docs.
CategoriaContainer categoriaDominanteDosItens(ProjetoAberto& projeto, const std::vector<std::string>& itemIds);
// Nome da pasta em comum dos arquivos ("" se nao ha).
juce::String nomeDaPastaEmComum(ProjetoAberto& projeto, const std::vector<std::string>& itemIds);
// Nome padrao: a pasta em comum dos arquivos ou, sem ela, "<tipo> <data>".
juce::String nomePadraoDoContainer(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, const juce::String& rotuloDoTipo);

// Grava o CONTENT do tipo nos membros: so nos arquivos cuja categoria de midia tem o valor na lista de CONTENT e so onde esta vazio.
// `contentDaFicha` (nao vazio) vale no lugar de tipo.content (tipos com player: o CONTENT do card do IMPORT). Fora da message thread.
int aplicarContentDoTipo(ProjetoAberto& projeto, const std::vector<std::string>& membros, const TipoContainer& tipo,
                         const std::string& contentDaFicha = {});

// Onde o dialogo de tipo (e a ficha dos tipos com player) aparece: cada tela tem o seu PainelOverlay.
//   abrir()   devolve o overlay para usar agora (nullptr = ja ha um overlay aberto: o pedido e ignorado);
//   fechar()  roda quando o fluxo termina (a tela solta o overlay que criou);
//   aoMudarDados() roda na message thread depois de gravar (a tela recarrega o que mostra).
struct HospedeOverlay {
    std::function<PainelOverlay*()> abrir;
    std::function<void()> fechar;
    std::function<void()> aoMudarDados;
};

// Itens do botao direito nas tres telas: "Group as Project Container...", "Change Container Type..." e "Ungroup Container".
// Group so vale com 2+ arquivos e nenhum ja em nest/container (senao fica desabilitado com o aviso logo abaixo).
void adicionarMenuProjectContainer(juce::PopupMenu& menu, ProjetoAberto& projeto, const std::vector<std::string>& itemIds, HospedeOverlay hospede);

void agruparComoProjectContainer(ProjetoAberto& projeto, std::vector<std::string> itemIds, HospedeOverlay hospede);
void mudarTipoDoContainer(ProjetoAberto& projeto, const std::string& nestId, HospedeOverlay hospede);
// Desfaz os CONTAINERS a que os itens pertencem (os arquivos voltam soltos; metadados intactos). Nests comuns ficam.
void desagruparContainer(ProjetoAberto& projeto, const std::vector<std::string>& itemIds, HospedeOverlay hospede);

} // namespace matriz::ui
