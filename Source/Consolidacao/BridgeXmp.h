#pragma once

#include <JuceHeader.h>

#include <optional>
#include <string>
#include <vector>

#include "../Db/Database.h"

// Integração Adobe Bridge — XMP nos dois sentidos, 100% offline.
//
// SAÍDA (EXPORT > Adobe Bridge): metadado do Matriz gravado SÓ na cópia exportada, no formato que o Bridge lê.
//   - formatos que aceitam XMP embutido (JPEG, PNG, TIFF, PSD, MP4/MOV, WAV): pacote XMP dentro da cópia;
//   - RAW e o resto (PDF, MP3, ...): sidecar Adobe ao lado da cópia — mesmo nome, extensão trocada por .xmp.
//   Nunca toca no MAIN nem no SOURCE.
// ENTRADA (INTAKE > Adobe Bridge): lê o XMP (embutido ou sidecar) dos arquivos de origem — somente leitura —
//   e devolve os dados pra serem aplicados nos itens recém-ingeridos.
//
// Mapeamento (só campos que já existem no Matriz): TAGS -> dc:subject; PEOPLE -> lr:hierarchicalSubject
// "Pessoas|Nome" (e o nome em dc:subject); título -> dc:title; descrição -> dc:description; GEO LOCATION ->
// Iptc4xmpCore:Location / photoshop:City / State / Country; EVENT DATE -> photoshop:DateCreated.
// P/W/K, labels e rating não entram.
namespace matriz::consolidacao::bridge {

struct DadosBridge {
    std::string titulo;
    std::string descricao;
    std::string dataCriacao;  // photoshop:DateCreated (a ficha guarda o ano do EVENT DATE)
    std::string local;        // Iptc4xmpCore:Location
    std::string cidade;
    std::string estado;
    std::string pais;
    std::vector<std::string> palavrasChave;  // dc:subject (inclui o nome das pessoas)
    std::vector<std::string> pessoas;        // "Pessoas|Nome" -> Nome

    bool vazio() const {
        return titulo.empty() && descricao.empty() && dataCriacao.empty() && local.empty() && cidade.empty() &&
               estado.empty() && pais.empty() && palavrasChave.empty() && pessoas.empty();
    }
};

// ---- SAÍDA ----
DadosBridge coletarDadosBridge(matriz::db::Database& registro, const std::string& itemId);
std::string montarPacoteXmp(const DadosBridge& dados);

// true se o formato aceita XMP embutido (pela extensão da cópia).
bool formatoAceitaXmpEmbutido(const juce::File& arquivo);
// Embute o pacote na cópia. false (arquivo intacto) se o formato/estrutura não permite.
bool embutirXmpNoArquivo(const juce::File& arquivo, const std::string& pacote);
// Sidecar Adobe: IMG_001.CR2 -> IMG_001.xmp. Nunca sobrescreve: false se o nome já existe.
bool gravarSidecarAdobe(const juce::File& arquivo, const std::string& pacote);

enum class ResultadoBridgeArquivo { Embutido, Sidecar, ColisaoSidecar, SemMetadado, Falha };
// Decide embutir x sidecar e grava. `aviso` recebe a explicação de colisão/falha (pro log do export).
ResultadoBridgeArquivo gravarXmpBridgeNaCopia(matriz::db::Database& registro, const std::string& itemId,
                                              const juce::File& copia, std::string& aviso);

// ---- ENTRADA ----
// XMP embutido e/ou sidecar (.xmp com o nome do arquivo sem extensão, ou arquivo.ext.xmp). O sidecar
// manda quando os dois existem. nullopt = nenhum XMP legível. Só lê; nunca grava nem cria arquivo.
std::optional<DadosBridge> lerXmpBridge(const juce::File& arquivo);
// Lê um pacote XMP (texto) — exposto pros testes.
DadosBridge interpretarPacoteXmp(const std::string& pacote);
// Aplica os dados a um item recém-ingerido: tags/pessoas são ADICIONADAS (mesmo nome sem diferenciar
// maiúsculas = a mesma entrada); título, descrição, local e data só quando vêm preenchidos.
void aplicarDadosBridgeAoItem(matriz::db::Database& registro, const std::string& itemId, const DadosBridge& dados);

}  // namespace matriz::consolidacao::bridge
