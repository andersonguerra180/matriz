#pragma once

#include <JuceHeader.h>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include "../Db/Database.h"
#include "../App/Cancelamento.h"

namespace matriz::ingest {

struct LightroomFotoMetadados {
    juce::int64 localId = 0;
    juce::File arquivoFoto;
    juce::File arquivoXmpAcompanhante;
    bool existe = false;

    juce::String titulo;
    juce::String descricao;
    juce::StringArray palavrasChave;
    juce::String direitosAutorais;
    juce::String criador;
    juce::String dataCaptura;

    std::optional<double> latitude;
    std::optional<double> longitude;
    std::optional<double> altitude;

    int rating = 0;             // 0 a 5 estrelas
    juce::String colorLabel;    // "red", "yellow", etc.
    int pick = 0;               // 1 = aprovada/pick, 0 = sem flag, -1 = rejeitada

    juce::StringArray colecoes;

    // Dados técnicos adicionais sem campo direto no MATRIZ
    juce::String camera;
    juce::String lente;
    juce::String distanciaFocal;
    juce::String abertura;
    juce::String velocidade;
    juce::String iso;
    bool editada = false;
    bool copiaVirtual = false;
    juce::String pilha;
};

struct LightroomImportResultado {
    bool sucesso = false;
    int fotosImportadas = 0;
    int fotosNaoEncontradas = 0;
    int fotosDuplicadas = 0;
    int metadadosAplicados = 0;
    int arquivosSessaoImportados = 0;
    juce::StringArray detalhesLog;
    juce::String erro;
    bool catalogoEstavaBloqueado = false;
};

class LightroomImporter {
public:
    // Valida se o arquivo é um banco SQLite com o esquema do Lightroom Classic
    static bool validarCatalogoClassic(const juce::File& lrcatArquivo, juce::String& outErro);

    // Lê os metadados e caminhos de todas as fotos contidas no catálogo
    static bool lerCatalogo(const juce::File& lrcatArquivo,
                            std::vector<LightroomFotoMetadados>& outFotos,
                            std::vector<juce::File>& outArquivosSessao,
                            bool& outEstavaBloqueado,
                            juce::String& outErro);

    // Executa a importação completa integrando com o banco de registro do MATRIZ
    static LightroomImportResultado importarCatalogo(
        const juce::File& lrcatArquivo,
        matriz::db::Database& registro,
        matriz::db::Database& indice,
        const juce::File& pastaProjeto,
        const std::string& projetoId,
        const juce::String& prefixoAcervo,
        std::shared_ptr<matriz::app::Cancelamento> cancelamento,
        std::function<void(int atual, int total, const juce::String& status)> onProgresso,
        std::function<juce::File(const juce::String& exemploEsperado, const juce::String& nomeFoto)> onPedirNovaRaiz = nullptr);

    // Formata o texto organizado para gravação no campo NOTAS (notas_livres)
    static juce::String formatarNotasTecnicas(const LightroomFotoMetadados& foto);
};

} // namespace matriz::ingest
