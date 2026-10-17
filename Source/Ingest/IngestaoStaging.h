#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "../Model/Project.h"

// Ingestão de pastas e arquivos soltos no projeto de staging (Make Backup).
// Mesma rotina para a interface (arrastar e soltar / seletor) e para o
// auto-teste. Garante:
//  - código de item contínuo por projeto (MAX numérico + 1, lido UMA vez por
//    ingestão e protegido por mutex: dois arrastos simultâneos nunca repetem
//    ITM-n, que violaria UNIQUE(projeto_id, codigo_acervo));
//  - pasta raiz homônima ganha sufixo " (2)", " (3)";
//  - arquivo solto vira item sem pasta (nó de arquivo na raiz do mapa);
//  - nenhuma falha some em silêncio: vai para ResultadoIngestao::falhas e para
//    juce::Logger.

namespace matriz::ingest {

struct FalhaIngestao {
    juce::String caminho;
    juce::String motivo;
};

struct OpcoesIngestao {
    std::string pastaDestinoId;                 // vazio = raiz do mapa
    std::string mapaId;                         // folder_map dono das pastas criadas (vazio = sem mapa)
    std::atomic<bool>* cancelar = nullptr;
    std::function<void(int feitos, int total)> aoProgredir;  // chamado da thread que ingere
    // Chamado (na thread que ingere) depois que o item e o arquivo foram gravados com sucesso.
    // Exceção lançada aqui NÃO desfaz o item. Usado pelo questionário "Sobre este material".
    std::function<void(const std::string& itemId)> aoItemIngerido;
    // Igual ao anterior, mas também diz de qual arquivo veio o item (usado pelas conversas do WhatsApp e pela
    // tag automática "celular"). Roda depois de aoItemIngerido.
    std::function<void(const std::string& itemId, const juce::File& arquivo)> aoArquivoIngerido;
    // Arquivos (caminho completo) que NÃO entram nesta ingestão, mesmo estando dentro de uma pasta solta:
    // são os .zip de conversas do WhatsApp, que seguem um fluxo próprio.
    std::set<juce::String> excluirCaminhos;
};

struct ResultadoIngestao {
    int arquivosIngeridos = 0;
    int pastasRaiz = 0;
    int arquivosSoltos = 0;
    bool cancelado = false;
    std::vector<std::string> pastasRaizIds;
    std::vector<std::string> itensSoltosIds;
    std::vector<std::string> itensIngeridosIds;  // todos os itens criados com sucesso (em pasta ou soltos)
    std::vector<FalhaIngestao> falhas;
};

ResultadoIngestao ingerirNoStaging(matriz::model::Project& staging, const juce::Array<juce::File>& arquivosOuPastas,
                                   const OpcoesIngestao& opcoes = {});

// Maior número N entre os códigos "ITM-N" do projeto (0 se não houver).
int maiorCodigoNumerico(matriz::model::Project& staging);

// Arquivos (itens) dentro de uma pasta, incluindo subpastas — usa acervo_item_pasta.
int contarArquivosDaPasta(matriz::model::Project& staging, const std::string& pastaId);

} // namespace matriz::ingest
