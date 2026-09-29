#pragma once

#include <JuceHeader.h>

#include <functional>
#include <string>
#include <vector>

#include "../Db/Database.h"

// MAIN EDIT MODE (Fase 4) — motor das operações de arquivo/pasta feitas
// DIRETO no MAIN. Sem UI: quem chama (ProjetoAberto) roda isto FORA da message
// thread, com progresso e cancelamento onde há cópia/remoção longa.
//
// Segurança de queda: cada operação grava uma linha em main_edit_journal
// (estado 'pendente') ANTES de mexer no disco e a fecha ('aplicado') na mesma
// transação que atualiza consolidacao_registro. recuperarJournal() roda ao
// abrir o projeto e retoma ou desfaz o que ficou pela metade — nunca deixa
// arquivo movido sem registro.
//
// Nada aqui escreve na SOURCE: só Media/ e _QUARENTENA/ do MAIN.
namespace matriz::mainedit {

// <raiz do MAIN>/_QUARENTENA — mesmo volume de Media/, então mover pra lá é
// instantâneo. Não confundir com os "vaults" das SOURCES (S01, S02…).
inline const char* const kPastaQuarentena = "_QUARENTENA";

struct ContextoMain {
    matriz::db::Database* registro = nullptr;
    juce::File raiz;           // raiz do MAIN: contém Media/, Project/ e _QUARENTENA/
    juce::File pastaProjeto;   // <raiz>/Project — onde mora o ProjectLog
    std::string destinoId;     // destination_id do MAIN
    bool mainUsaMapa = false;  // MAIN organizado por folder map (mover/pastas só nesse caso)
    // Único caminho que muda a pertença item->pasta do mapa do MAIN (mantém as
    // duas colunas mapa_id em sincronia). Chamado dentro da transação; deve ser
    // idempotente (a recuperação reaplica).
    std::function<void(const std::string& itemId, const std::string& pastaDe, const std::string& pastaPara)> mapaMoverItem;

    juce::File media() const { return raiz.getChildFile("Media"); }
    juce::File quarentena() const { return raiz.getChildFile(kPastaQuarentena); }
};

struct Resultado {
    bool ok = false;
    std::string erro;
    std::string idJournal;
    std::string idQuarentena;      // deletar / substituir
    juce::String deRel;            // relativo a Media/ (antes)
    juce::String paraRel;          // relativo a Media/ (depois)
    bool destinoOcupado = false;   // restaurar: o caminho original já tem algo (pedir outro destino)
};

using AoProgredirEdicao = std::function<bool(int feito, int total)>;  // false = cancelar

// --- arquivos -------------------------------------------------------------
// novoNome: só o nome (sem pasta). Sem extensão = mantém a atual. Renomeia
// também o XMP e a capa que viajam junto do arquivo.
Resultado renomearArquivo(const ContextoMain& ctx, const std::string& registroId, const juce::String& novoNome);
// Só com MAIN por folder map: move pra pasta física da pasta do mapa (o item
// muda de pasta no mapa do MAIN junto).
Resultado moverArquivo(const ContextoMain& ctx, const std::string& registroId, const std::string& novaPastaId);
// O original vai pra quarentena (motivo 'substituido') e o novo entra no mesmo
// caminho com hash novo registrado. Cópia longa: aoProgredir por arquivo.
Resultado substituirArquivo(const ContextoMain& ctx, const std::string& registroId, const juce::File& novoArquivo);
// Vai pra quarentena (motivo 'deletado'); nunca é apagado.
Resultado deletarArquivo(const ContextoMain& ctx, const std::string& registroId);

// --- pastas (só MAIN por folder map) ---------------------------------------
// A pasta do mapa já existe; cria a pasta física correspondente.
Resultado criarPastaFisica(const ContextoMain& ctx, const std::string& pastaId);
// Renomeia a pasta física e o nome no mapa, e reaponta os caminhos registrados.
Resultado renomearPasta(const ContextoMain& ctx, const std::string& pastaId, const juce::String& novoNome);
// novoPaiId vazio = raiz do mapa.
Resultado moverPasta(const ContextoMain& ctx, const std::string& pastaId, const std::string& novoPaiId);

// --- quarentena --------------------------------------------------------------
struct ItemQuarentena {
    std::string id;
    juce::String caminhoOriginal;    // relativo a Media/
    juce::String motivo;             // "substituido" | "deletado"
    juce::String criadoEm;
    juce::int64 tamanhoBytes = 0;
    std::string checksum;
};
struct ResumoQuarentena {
    int itens = 0;
    juce::int64 bytes = 0;
};
std::vector<ItemQuarentena> listarQuarentena(const ContextoMain& ctx);
ResumoQuarentena resumoQuarentena(const ContextoMain& ctx);  // uma consulta agregada
// destinoAlternativoRel vazio = caminho original. Se já houver algo lá:
// ok=false e destinoOcupado=true — o chamador pede outro destino. Nunca sobrescreve.
Resultado restaurar(const ContextoMain& ctx, const std::string& quarentenaId, const juce::String& destinoAlternativoRel = {});
// Apaga DE VERDADE os arquivos da quarentena (o chamador exige a barreira de
// digitar o nome do projeto). Registra no ProjectLog.
struct ResultadoEsvaziar {
    int apagados = 0;
    juce::int64 bytes = 0;
    std::vector<std::string> falhas;
    bool cancelado = false;
};
ResultadoEsvaziar esvaziarQuarentena(const ContextoMain& ctx, const AoProgredirEdicao& aoProgredir = {});

// --- queda / recuperação ------------------------------------------------------
struct ResultadoRecuperacao {
    int retomadas = 0;
    int desfeitas = 0;
    std::vector<std::string> conflitos;  // exigem atenção humana (nunca silencioso)
};
ResultadoRecuperacao recuperarJournal(const ContextoMain& ctx);

}  // namespace matriz::mainedit
