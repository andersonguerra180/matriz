#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <stdexcept>
#include <string>

#include "../Db/Database.h"

// Projeto portátil (P5): uma pasta no disco contendo os arquivos e o banco.
// Copiou pro HD externo, abriu em outra máquina, funciona. Esta classe é o
// único ponto de entrada para criar ou abrir essa pasta — garante que a
// estrutura (registro.sqlite, indice.sqlite, subpastas) sempre existe antes
// de qualquer outra camada tocar o projeto.

namespace matriz::model {

class ProjectError : public std::runtime_error {
public:
    explicit ProjectError(const std::string& message) : std::runtime_error(message) {}
};

enum class Modo { Preservacao, Catalogo };

std::string modoToString(Modo m);
Modo modoFromString(const std::string& s);

struct NovoProjetoParams {
    std::string nome;
    Modo modo = Modo::Preservacao;
    std::string prefixoNomenclatura;
    std::string instituicaoOuSelo;
    std::string responsavel;
    std::string isrcRegistrante; // só modo catálogo
};

struct DestinationInfo {
    int formato = 1;
    std::string destinationId;
    std::string projetoId;
    std::string papel = "ORIGINAL"; // "ORIGINAL" ou "CLONE"
    std::string rotulo;
    int64_t revisao = 1;
    std::string ultimaEdicaoUtc;
    std::string criadoEm;

    static std::optional<DestinationInfo> lerDeArquivo(const juce::File& arquivoJson);
    bool gravarEmArquivo(const juce::File& arquivoJson) const;
};

class Project {
public:
    // Cria a estrutura de pastas + registro.sqlite + indice.sqlite em
    // `pastaRaiz` (que deve estar vazia ou não existir ainda), gera
    // destination.json, inicializa Project/ e Media/ e grava a linha única de `projeto`.
    // Lança ProjectError se a pasta já contiver um projeto ou não puder ser criada.
    static std::unique_ptr<Project> criar(const juce::File& pastaRaiz, const NovoProjetoParams& params);

    // Abre uma pasta de projeto existente, aceitando a raiz de um DESTINATION,
    // a subpasta Project/, ou um diretório legado com registro.sqlite.
    static std::unique_ptr<Project> abrir(const juce::File& qualquerPasta);

    // Resolve qualquer pasta (raiz, Project/ ou legada) para a pasta que contém registro.sqlite
    static juce::File resolverPastaProjeto(const juce::File& qualquerPasta);

    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;
    Project(Project&&) = delete;
    Project& operator=(Project&&) = delete;

    const juce::File& pasta() const { return pastaProjeto_; }
    juce::File raiz() const { return pastaProjeto_.getParentDirectory(); }
    juce::File pastaMedia() const { return raiz().getChildFile("Media"); }

    matriz::db::Database& registro() {
        jassert(registro_ != nullptr);
        if (!registro_) throw ProjectError("internal: registro database is null (use-after-free?)");
        return *registro_;
    }
    matriz::db::Database& indice() {
        jassert(indice_ != nullptr);
        if (!indice_) throw ProjectError("internal: indice database is null (use-after-free?)");
        return *indice_;
    }

    // Conexão SOMENTE-LEITURA pras leituras da message thread e dos snapshots/filtros: no
    // WAL ela lê o último estado commitado sem esperar a transação da conexão de escrita
    // (que a trava de transação do Database faria esperar até o COMMIT). Escritas
    // continuam em registro()/indice().
    //
    // UMA CONEXÃO POR THREAD que lê (aberta na 1ª leitura dela): no SQLite, enquanto
    // qualquer statement está ativo numa conexão, todos os outros leitores dela
    // compartilham o mesmo snapshot — uma conexão única deixaria um snapshot de fundo em
    // andamento fazer a message thread (ou o job seguinte) ler o estado ANTIGO depois de
    // um COMMIT. Com uma por thread, cada leitura começa no estado commitado mais recente.
    // Há um teto de conexões por arquivo; acima dele (ou se não abrir), devolve a conexão
    // de ESCRITA, como sempre foi. Também devolve a de escrita quando a thread que chama
    // tem transação aberta nela (ler o que acabou de escrever, ainda sem COMMIT).
    matriz::db::Database& registroLeitura();
    matriz::db::Database& indiceLeitura();

    std::string projetoId() const { return projetoId_; }
    int64_t revisao() const { return destinationInfo_.revisao; }
    std::string destinationId() const { return destinationInfo_.destinationId; }
    std::string papel() const { return destinationInfo_.papel; }
    const DestinationInfo& destinationInfo() const { return destinationInfo_; }
    // Relê o papel gravado em destination.json (depois de "promover a MAIN" o papel
    // muda no arquivo mas este objeto continua com o que leu ao abrir).
    void recarregarPapel();

    // Se houve alterações no banco de registro, incrementa a revisão,
    // atualiza destination.json atomicamente e zera a flag de dirty.
    void confirmarRevisao();

    // `nome` é lido direto de `projeto` (linha única): não é editável em
    // nenhuma tela hoje, mas ler ao vivo custa uma linha e evita dúvida
    // sobre staleness se isso mudar.
    std::string nome();
    // Rename Project (item 5, correção METADATA 2026-09-28): só o rótulo em
    // `projeto.nome` muda. Nunca renomeia a pasta do projeto, nenhum
    // arquivo, prefixo de nomenclatura ou código de SOURCE — nenhum desses
    // é derivado de `nome` depois da criação (ver AGENTS.md). Falha se
    // `novoNome` vier vazio (validação fica pra quem chama, com o texto já
    // aparado).
    void renomear(const std::string& novoNome);
    std::string destinoBackupAtivo();
    void definirDestinoBackupAtivo(const std::string& path);
    // O modo é decidido na criação do projeto e nunca muda — lê do banco
    // UMA vez, na abertura, e devolve o valor guardado.
    Modo modo() const { return modo_; }

    // Mutex unificado de transações e escritas no banco de registro
    std::recursive_mutex& writeMutex() { return writeMutex_; }

private:
    Project(juce::File pastaProjeto, std::unique_ptr<matriz::db::Database> registro,
            std::unique_ptr<matriz::db::Database> indice, std::string projetoId,
            DestinationInfo destinationInfo = {});

    juce::File pastaProjeto_;
    std::unique_ptr<matriz::db::Database> registro_;
    std::unique_ptr<matriz::db::Database> indice_;
    // Conexões de leitura: declaradas DEPOIS das de escrita, então são fechadas antes
    // delas (a última a fechar é a que faz o checkpoint do WAL).
    struct ConexoesLeitura {
        std::mutex mutex;
        std::unordered_map<std::thread::id, std::unique_ptr<matriz::db::Database>> porThread;
        bool falhou = false;
    };
    matriz::db::Database& leituraDe(matriz::db::Database& escrita, ConexoesLeitura& conexoes, const char* arquivo);
    ConexoesLeitura registroLeitura_, indiceLeitura_;
    std::string projetoId_;
    Modo modo_ = Modo::Preservacao;
    DestinationInfo destinationInfo_;
    std::recursive_mutex writeMutex_;
};

// Timestamp ISO-8601 UTC ("YYYY-MM-DDTHH:MM:SSZ"), a convenção de todo
// timestamp gravado em registro.sqlite/indice.sqlite.
std::string agoraIso8601();

// UUID v4 em minúsculas, a convenção de todo id gravado no banco.
std::string novoUuid();

// Normaliza qualquer caminho para a raiz do destino de backup.
// Se o nome for "Media" ou "Project", sobe para o pai. Se o caminho estiver dentro
// de uma Media ou Project, sobe até a raiz que contém destination.json.
juce::File normalizarParaRaizDestino(const juce::File& f);

// Higieniza rigorosamente a estrutura de qualquer destino de backup ou projeto.
// Garante que Media/ contenha EXCLUSIVAMENTE arquivos de mídia do acervo,
// movendo qualquer log, _lixeira, Project, relatórios, catálogo ou banco para Project/,
// e limpando pastas aninhadas indevidas na raiz ou em Media/.
void sanitizarEstruturaDestino(const juce::File& f);

} // namespace matriz::model

