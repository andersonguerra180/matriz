#pragma once

#include <string>
#include <vector>

// NOTES continua sendo um único campo de texto (item.notas_livres) — nunca
// vira tabela/coluna por seção (regra fundamental do pedido "NOTES —
// ESTRUTURA DE METADADOS E NOTAS"). O que muda é só a leitura/escrita: o
// texto é organizado em blocos "[TÍTULO]\nconteúdo", legível fora do BKR
// Matriz (SQLite bruto, CSV, backup) e reconstruível na interface como
// seções colapsáveis.

namespace matriz::model {

// Título fixo (sempre em inglês, independente do idioma da UI — assim como
// nomes de coluna — para o texto serializado continuar estável entre
// projetos/exportações) da seção automática e somente-leitura preenchida a
// partir dos metadados extraídos no ingest que não têm campo próprio na
// ficha (ver LeituraTecnicaResultado::metaUnmappedExtras).
extern const char* const kOutraMetadataTitulo;

struct SecaoNota {
    std::string titulo;
    std::string conteudo;
    // true só para a seção OTHER METADATA — a única que a UI trata como
    // somente-leitura (o dado vem da extração automática, não de digitação).
    bool automatica = false;
};

// Interpreta o texto bruto de notas_livres como uma lista de seções.
// Texto legado sem nenhum cabeçalho "[...]" vira uma única seção "NOTES"
// (nada se perde na migração). O antigo prefixo "Others (metadata):\n"
// (formato usado antes desta estrutura existir) também é reconhecido e
// convertido em uma seção OTHER METADATA automática.
std::vector<SecaoNota> parseNotasEstruturadas(const std::string& texto);

// Serializa de volta para o texto único armazenado no banco. Seções sem
// título são descartadas; conteúdo é preservado como está (sem qualquer
// tentativa de reformatação além de aparar espaços/linhas em branco nas
// bordas).
std::string serializarNotasEstruturadas(const std::vector<SecaoNota>& secoes);

} // namespace matriz::model
