#pragma once

// Tools > Verify Source Disks: confere um disco de ORIGEM contra o MAIN, por SHA-256, sem escrever nada.
// Um arquivo do disco cai em um destes grupos: já está no MAIN (some do relatório), FALTANDO (hash em nenhum lugar)
// ou REJEITADO ANTES (hash em intake_rejeitados). Só leitura: nem o banco nem o disco são alterados.

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "../Db/Database.h"

namespace matriz::vault {

struct FaltanteFonte {
    juce::File arquivo;
    std::string sha256;
    juce::int64 tamanhoBytes = 0;
    bool rejeitadoAntes = false;
};

struct ResultadoVerificacaoFonte {
    std::vector<FaltanteFonte> faltantes;
    int totalLidos = 0;
    int totalNoMain = 0;
    int ilegiveis = 0;  // não deu para ler/hashear (disco desconectado no meio, permissão)
    bool cancelado = false;
};

// Hashes conhecidos do projeto, carregados de uma vez (message thread) para o scan rodar em background sem tocar no banco.
struct HashesConhecidos {
    std::unordered_set<std::string> noMain;
    std::unordered_set<std::string> rejeitados;
};

class VerifySource {
public:
    // Nomes dos volumes (/Volumes/<nome>) de onde veio algo do projeto, a partir de arquivo.caminho_absoluto_origem.
    static std::vector<juce::String> volumesDeOrigem(matriz::db::Database& db);

    static HashesConhecidos carregarHashes(matriz::db::Database& db);

    // Percorre `raiz` (recursivo; ignora ocultos, lixo de sistema e arquivos vazios). `aoProgredir(feitos, total, nome)`
    // devolve false para cancelar.
    static ResultadoVerificacaoFonte verificar(const juce::File& raiz, const HashesConhecidos& conhecidos,
                                               const std::atomic<bool>& cancelar,
                                               const std::function<void(int, int, const juce::String&)>& aoProgredir = {});

    static bool deveIgnorar(const juce::File& arquivo);
};

} // namespace matriz::vault
