#pragma once

#include <map>
#include <string>
#include <vector>

#include "../Db/Database.h"
#include "../Ficha/FichaDefinition.h"
#include "../Model/Project.h"

// Painel de inconsistências (§7.4): o que está incompleto ou errado depois
// do ingest. Só relata — nunca corrige sozinho (a correção é decisão
// humana, como tudo mais que toca o registro).

namespace matriz::ingest {

struct Inconsistencia {
    std::string tipo; // faixa_sem_isrc | release_sem_capa | master_lossy | sample_rate_divergente |
                       // capa_abaixo_minimo | splits_nao_somam_100 | isrc_duplicado |
                       // arquivo_orfao | checksum_divergente
    std::string itemId;
    std::string arquivoId;
    std::string descricao;
};

// Checagens que só precisam do banco de registro + das definições de ficha
// (§7.4: faixa sem ISRC, release sem capa, master em lossy, sample rate
// divergente, capa abaixo do mínimo, splits que não somam 100, ISRC
// duplicado).
std::vector<Inconsistencia> detectarInconsistenciasFicha(
    matriz::db::Database& registro, const std::map<std::string, matriz::ficha::FichaDefinition>& definicoesPorTipo);

// Checagens que precisam ler o disco (§7.4: arquivo órfão, checksum
// divergente). Percorre todo arquivo.caminho_relativo do projeto e o
// conteúdo real da pasta.
std::vector<Inconsistencia> verificarArquivosNoDisco(matriz::model::Project& projeto);

} // namespace matriz::ingest
