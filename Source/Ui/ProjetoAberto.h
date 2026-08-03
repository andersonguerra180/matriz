#pragma once

#include <JuceHeader.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../Ficha/FichaDefinition.h"
#include "../Model/Project.h"

// Estado do projeto atualmente aberto na UI: dono do matriz::model::Project,
// cache de FichaDefinition por tipo de mídia (carregadas sob demanda de
// fichas/*.yaml), e as consultas que Mosaico/Ficha precisam. Um único ponto
// de acesso ao banco a partir da UI — nenhum Component fala SQL direto.

namespace matriz::ui {

struct ItemResumo {
    std::string id;
    std::string codigoAcervo;
    std::string titulo;
    std::string tipoMidia;
    std::string estado; // nao_digitalizado | capturado | qc_ok | alerta | publicado
    std::string atualizadoEm;
    bool sincronizado = false; // halo (§11.2) — pelo menos um arquivo do item com estado_sincronizacao='sincronizado'
};

class ProjetoAbertoError : public std::runtime_error {
public:
    explicit ProjetoAbertoError(const std::string& message) : std::runtime_error(message) {}
};

class ProjetoAberto {
public:
    explicit ProjetoAberto(std::unique_ptr<matriz::model::Project> projeto);

    matriz::model::Project& projeto() { return *projeto_; }

    std::vector<ItemResumo> listarItens() const;

    // Carrega (e cacheia) a definição de ficha para `tipoMidia` a partir de
    // fichas/*.yaml. Lança ProjetoAbertoError se o tipo não tiver definição —
    // isso é um erro de dado (item com tipo_midia inválido), não algo pra
    // silenciar.
    const matriz::ficha::FichaDefinition& definicaoPara(const std::string& tipoMidia);

    // Valor atual de um campo (nível raiz, nivel_indice=0) do item, ou
    // nullopt se nunca foi preenchido.
    std::optional<std::string> valorCampo(const std::string& itemId, const std::string& nivel, int nivelIndice,
                                           const std::string& campoId) const;

    // Papéis de arquivo já presentes para o item (§6.2 arquivos_esperados).
    std::vector<std::string> papeisArquivoPresentes(const std::string& itemId) const;

    // Caminho absoluto da miniatura principal do item, se o índice já
    // processou uma (Etapa 4 escreve isso; pode não existir ainda).
    std::optional<juce::String> caminhoMiniaturaPrincipal(const std::string& itemId) const;

    // --- Sugestão de IA (P3) — índice.sugestao_campo, nunca escrito por nós,
    // só lido e, quando o operador confirma, migrado pro registro. ---
    struct SugestaoCampo {
        std::string id;
        std::string valor;
        std::optional<double> confianca;
        std::string modelo;
        std::string modeloVersao;
    };
    std::optional<SugestaoCampo> sugestaoPendente(const std::string& itemId, const std::string& nivel,
                                                   int nivelIndice, const std::string& campoId) const;

    // Migra a sugestão pro registro: item_campo (fonte='humano') +
    // item_historico (tipo_evento='confirmacao_sugestao_ia', com o modelo de
    // origem) + marca sugestao_campo.confirmado=1 no índice. Atômico dentro
    // do registro; o índice é só rastro de auditoria descartável (P2).
    void confirmarSugestao(const SugestaoCampo& sugestao, const std::string& itemId, const std::string& nivel,
                            int nivelIndice, const std::string& campoId, const std::string& autor);

private:
    std::unique_ptr<matriz::model::Project> projeto_;
    std::map<std::string, matriz::ficha::FichaDefinition> definicoesCache_;
};

} // namespace matriz::ui
