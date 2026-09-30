#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include "../Model/MergeFichas.h"
#include <string>
#include <vector>

#include "../Consolidacao/Consolidacao.h"
#include "../Consolidacao/MainEdit.h"
#include "../Ficha/FichaDefinition.h"
#include "../Model/Project.h"
#include "../Preservation/Preservation.h"

namespace matriz::vault { class ResolvedorEmLote; }

// Estado do projeto atualmente aberto na UI: dono do matriz::model::Project,
// cache de FichaDefinition por tipo de mídia (carregadas sob demanda de
// fichas/*.yaml), e as consultas que Mosaico/Ficha precisam. Um único ponto
// de acesso ao banco a partir da UI — nenhum Component fala SQL direto.

namespace matriz::consolidacao::pacote { struct Pacote; }

namespace matriz::ui {

struct ItemResumo {
    std::string id;
    std::string codigoAcervo;
    std::string titulo;
    std::string tipoMidia;
    std::string estado; // nao_digitalizado | capturado | qc_ok | alerta | publicado
    std::string atualizadoEm;
    std::string criadoEm;
    bool sincronizado = false; // halo (§11.2) — pelo menos um arquivo do item com estado_sincronizacao='sincronizado'

    // Só preenchido pra tipo_midia="release" (campos release.artista_principal
    // e release.titulo, nível raiz) — usado pra agrupar o mosaico por
    // artista/lançamento no modo Catalog (Parte 1 da correção de fluxo).
    // Vem de `item_campo`, não de `item.titulo` (que é só o nome herdado do
    // arquivo original no ingest, nunca atualizado pela ficha).
    std::optional<std::string> artistaLancamento;
    std::optional<std::string> tituloLancamento;

    // Campos universais (item 4 dos acréscimos de UI/catalogação — origem
    // digital/analógico e ano em destaque, presentes nas 19 fichas). Vêm de
    // item_campo(nivel='raiz', nivel_indice=0), como artistaLancamento
    // acima — nullopt = ainda não preenchido.
    std::optional<std::string> origem; // "Digital" | "Analógico" (valor bruto gravado no banco, não traduzido)
    std::optional<int> ano;
    // `ano` = ano do EVENT DATE (item.ano). Sem ele o item é Unknown; EXIF/data do disco não contam.
    bool anoDesconhecido() const { return !ano.has_value(); }  // sem EVENT DATE (vazio ou 0)
    std::optional<std::string> contentType;
    std::optional<std::string> collectionType;
    std::optional<std::string> subject;  // item.dc_subject (pode ter vários, separados por , ou ;)

    // Extensão (sem ponto, minúscula) do arquivo principal — usada pro
    // ícone de placeholder por categoria enquanto não há miniatura real
    // (Reorientação completa §3.3: nunca bloco cinza vazio). Vazia se o
    // item ainda não tem nenhum arquivo associado.
    std::string extensaoArquivo;
    std::string nomeOriginalArquivo;
    std::string pastaNome; // Name of the acervo_pasta folder the item belongs to
    juce::int64 tamanhoBytes = 0;
    bool offline = false;

    // Fields for detailed preview and micro-site generation
    std::optional<double> duracaoSegundos;
    std::string masterArquivoId;
    std::string caminhoRelativoArquivo;
    std::string caminhoAbsolutoOrigem;
    std::string miniaturaCaminhoRelativo;
    std::string isrc;
    std::string sourceMedia;
    std::string dataCriacao;
    std::vector<std::string> tags;
    bool marcadoPublicacao = false;
    bool marcadoZip = false;
    bool marcadoPrint = false;
    bool marcadoWatermark = false;
    bool metadadosEditados = false;
    bool marcadoRevisado = false; // atalho manual "E" (Tag as Edited) — só o operador liga/desliga
    bool pastaAtiva = true;
};

struct ConfiguracaoWatermark {
    juce::String caminhoLogo;
    float opacidade = 0.8f;
    float escala = 0.22f;
    float margem = 24.0f;
    int posicaoIdH = 1;
    float customPosX_H = 0.85f;
    float customPosY_H = 0.85f;
    int posicaoIdV = 1;
    float customPosX_V = 0.85f;
    float customPosY_V = 0.85f;

    bool valida() const {
        return caminhoLogo.isNotEmpty() && juce::File(caminhoLogo).existsAsFile();
    }
};

class ProjetoAbertoError : public std::runtime_error {
public:
    explicit ProjetoAbertoError(const std::string& message) : std::runtime_error(message) {}
};

class ProjetoAberto {
public:
    explicit ProjetoAberto(std::unique_ptr<matriz::model::Project> projeto);

    matriz::model::Project& projeto() { jassert(projeto_ != nullptr); return *projeto_; }

    static std::vector<ItemResumo> listarItensDeProjeto(matriz::db::Database& registro,
                                                        matriz::db::Database& indice,
                                                        const juce::File& pastaProjeto,
                                                        const std::map<std::string, std::string>& inMemoryRelinks = {},
                                                        // nullptr = verifica todo item em disco (coleção
                                                        // que não é a aberta); senão só re-verifica os ids
                                                        // do cache (podem ter voltado online via relink).
                                                        const std::set<std::string>* itensOffline = nullptr);
    std::vector<ItemResumo> listarItensDaColecao(const juce::File& pastaColecao) const;

    // Duplicates — "sanitizar": o operador escolheu `manterId`; `descartarId`
    // vira estado 'duplicata' (planejarConsolidacao não copia pro MAIN) e
    // NADA mais some — nem do banco, nem do disco, nem do SOURCE. O que o
    // descartado tinha de diferente é SOMADO no mantido (campo vazio é
    // preenchido; valor divergente, tags, assuntos, observações, nome e
    // localização do descartado vão pro mantido — notas sempre por append).
    // Grava evento PREMIS VALIDATION nos dois. Só banco: quem chama segura a
    // transação (lote de N pares = uma transação) e grava `linhasLog` no
    // log.md fora da message thread.
    struct ResultadoSanitizacao {
        std::string codigoMantido, codigoDescartado;
        std::string idMantido;
        int camposSomados = 0;
        int conflitos = 0;  // Fase 4: valores diferentes de verdade, perdedor no item_historico
        bool descartadoJaNoMain = false;
        juce::StringArray linhasLog;
    };
    // usarDescartado: campos de conflito em que o operador escolheu o valor do
    // descartado na tela de conflitos (vazio = vale o do mantido).
    static ResultadoSanitizacao sanitizarDuplicata(matriz::db::Database& registro, const std::string& manterId,
                                                   const std::string& descartarId,
                                                   const std::set<std::string>& usarDescartado = {});

    // Resolução de duplicatas em background (Fase 4): `corpo` roda numa
    // transação só (writeMutex + BEGIN IMMEDIATE), depois de um retrato das
    // fichas de `itensAfetados` pro Undo (desfazer devolve os dois lados como
    // estavam). aoConcluir na message thread, só se o projeto ainda existir.
    using CorpoResolucao = std::function<std::vector<ResultadoSanitizacao>(matriz::db::Database&)>;
    void resolverDuplicatasEmSegundoPlano(std::vector<std::string> itensAfetados, CorpoResolucao corpo,
                                          std::function<void(bool ok, std::vector<ResultadoSanitizacao>)> aoConcluir,
                                          const std::string& descricaoUndo = "Resolve Duplicates");
    // O que juntar os dois daria (conflitos), sem gravar — em background.
    void simularJuncaoEmSegundoPlano(const std::string& manterId, const std::string& descartarId,
                                     std::function<void(std::vector<matriz::model::merge::Conflito>)> aoConcluir);
    // Filtro "Merge conflicts" e revisão (valores perdedores no histórico).
    bool temConflitoMergePendente(const std::string& itemId) const;
    std::vector<matriz::model::merge::ConflitoPendente> conflitosMergePendentes(const std::string& itemId) const;
    void revisarConflitosMerge(const std::string& itemId, std::set<std::string> trocarHistoricoIds);

    // Move o Project pra fora — usado só ao trocar de idioma (Preferences),
    // que reconstrói a árvore de Component inteira do zero (é o jeito mais
    // simples de garantir que toda string em tela é retraduzida sem exigir
    // um retranslateUi() em cada Component). O objeto que chamou isto fica
    // inutilizável depois — só existe pra sobreviver à troca.
    std::unique_ptr<matriz::model::Project> destacarProjeto() { return std::move(projeto_); }

    std::string destinoBackupAtivo() const { return projeto_ ? projeto_->destinoBackupAtivo() : std::string(); }
    void definirDestinoBackupAtivo(const std::string& path) { if (projeto_) projeto_->definirDestinoBackupAtivo(path); }
    void sincronizarBackupDestinoDeHistorico();

    // --- Modelo SOURCE / MAIN / CLONE (etapa 3: papéis e rótulos) ---------
    // Papel de cada versão vem de backup_destino.papel (ORIGINAL = MAIN,
    // CLONE) — identidade pelo destination_id, nunca pelo caminho. Só
    // rótulos: nada é copiado, movido ou apagado.
    struct SituacaoMain {
        enum class Tipo { Ok, SemBackup, Perguntar } tipo = Tipo::SemBackup;
        // Perguntar: versões registradas (backup_destino.id, "rótulo — caminho").
        std::vector<std::pair<std::string, juce::String>> opcoes;
    };
    // Garante no máximo um MAIN. Vários ORIGINAL e um deles é a raiz aberta
    // -> ele fica MAIN e os demais viram CLONE. Nenhum MAIN (com versões
    // registradas) ou ambíguo -> Perguntar. Pode rodar em background.
    SituacaoMain normalizarPapelMain();
    // O escolhido vira MAIN (ORIGINAL), os demais ativos viram CLONE; log.
    void definirMain(const std::string& backupDestinoId);

    // Uma linha da lista de Versões (MAIN / CLONE / SOURCE). EXPORT não entra.
    struct VersaoResumo {
        enum class Papel { Main, Clone, Source } papel = Papel::Clone;
        std::string id;              // backup_destino.id ou vault.id (SOURCE)
        juce::String rotulo;
        juce::String caminho;
        bool online = false;
        int totalItens = 0;          // MAIN/CLONE: itens com registro; SOURCE: arquivos vindos dele
        int dependentes = 0;         // SOURCE: arquivos que ainda não estão no MAIN
        bool desatualizado = false;  // CLONE
        juce::String ultimaData;     // MAIN/CLONE: último backup/sync; SOURCE: última ingestão
        // SOURCE (etapa 5): código S01…/custom; editável até o 1º arquivo dele
        // entrar no MAIN; ingestões = dias distintos em que entrou arquivo dele.
        juce::String codigo;
        bool codigoEditavel = false;
        int ingestoes = 0;
        // CLONE (etapa 7): clone bruto de um SOURCE (tabela source_clone) ou
        // espelho do MAIN; `origem` = "MAIN" ou "S01 CARD".
        bool cloneDeSource = false;
        juce::String origem;
    };
    // Lê tudo (banco + existência das pastas): chamar FORA da message thread.
    std::vector<VersaoResumo> listarVersoes();
    // Masters de itens do catálogo (fora do Intake) SEM cópia registrada no
    // MAIN — ainda dependem do SOURCE. 0 = SOURCE pode ser desconectado.
    int arquivosQueDependemDoSource();

    std::vector<ItemResumo> listarItens() const;
    // COUNT(*) no banco — nunca listarItens() (que confere no disco se cada
    // arquivo existe: com a origem no Google Drive travado, a abertura do
    // projeto congelava na message thread).
    int contarItens() const;
    std::vector<ItemResumo> listarItensEmQuarentena() const;
    // Send to Grid e Reject (Intake) entram na pilha de undo (Cmd+Z).
    void confirmarItemGrid(const std::string& itemId);
    void confirmarLoteGrid(const std::vector<std::string>& itemIds);
    // Soma o tamanho do master de cada item. Roda um SUM com função de
    // janela sobre `arquivo` inteiro — 619 ms com 5.000 itens sob carga,
    // medido. NUNCA chamar na message thread durante um lote.
    juce::int64 tamanhoTotalDosMasters() const;
    bool obterItemInfo(const std::string& itemId, std::string& titulo, std::string& tipoMidia, std::string& codigoAcervo) const;
    std::optional<ItemResumo> obterItemResumo(const std::string& itemId) const;

    struct ItemDetalhe {
        std::string id;
        std::string nome;
        std::string extensao;
        juce::int64 tamanhoBytes = 0;
    };
    std::vector<ItemDetalhe> obterDetalhesItens(const std::set<std::string>& itemIds) const;
    std::set<int> indicesExistentes(const std::string& itemId, const std::string& nivel) const;
    void atualizarTipoMidia(const std::string& itemId, const std::string& tipoMidia);
    void aplicarTipoMidiaEmLote(const std::vector<std::string>& itemIds, const std::string& tipoMidia);
    void obterTiposMidiaDosItens(const std::vector<std::string>& itemIds, std::set<std::string>& tiposPresentes, bool& algumNulo) const;

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

    // --- Capa / miniatura personalizada (item 9) ---
    //
    // Uma imagem escolhida pelo operador vira a cara do material na grade e
    // no preview, no lugar da miniatura gerada. Serve pro caso que a geração
    // automática não cobre: áudio, documento e vídeo sem quadro
    // representativo, onde a capa do disco ou a foto da fita é o único jeito
    // de reconhecer o material de relance numa grade de milhares.
    //
    // A imagem é COPIADA pra dentro do projeto e registrada como arquivo de
    // papel 'capa_frente' do item — não é um ponteiro pro arquivo original,
    // que pode sumir. Vale pra vários itens de uma vez.
    //
    // Devolve quantos itens receberam a capa.
    int definirCapa(const std::vector<std::string>& itemIds, const juce::File& imagem);

    // Tira a capa e volta pra miniatura gerada — a linha antiga do índice
    // continua lá, então basta remover a da capa pra ela voltar a valer.
    void removerCapa(const std::vector<std::string>& itemIds);

    bool temCapa(const std::string& itemId) const;

    // Vocabulário do projeto pra um campo `opcao_livre`: os valores que já
    // foram efetivamente usados em algum item deste projeto, em ordem
    // alfabética, sem repetição.
    //
    // É o que faz "digitei uma marca nova e ela passa a aparecer na lista"
    // funcionar sem inventar tabela de vocabulário nenhuma: a lista oferecida
    // é a do YAML MAIS o que o operador já digitou. Vale pra qualquer campo
    // opcao_livre (marca, formulação, ...), não só marca de fita.
    std::vector<std::string> valoresUsadosNoCampo(const std::string& campoId) const;

    // --- Undo (up to 25 levels) ---
    struct UndoEntry {
        std::string descricao;
        std::vector<std::function<void()>> acoesReversas;
    };
    void registrarUndo(const std::string& descricao, std::function<void()> acaoReversa);
    void iniciarGrupoUndo(const std::string& descricao = {});
    void finalizarGrupoUndo();
    bool desfazer();
    bool podeDesfazer() const { return !pilhaUndo_.empty(); }
    std::string descricaoUndoAtual() const;
    std::function<void()> aoMudarUndo;
    void restaurarItensParaBackup(const std::vector<std::pair<std::string, std::string>>& itensPastas);

    // --- Unified Metadata (direct item columns) ---
    // Reads a direct column from the item table (ano, caminho_catalogo, content_type, source_media, collection_type, isrc, notas_livres)
    std::optional<std::string> lerMetadado(const std::string& itemId, const std::string& coluna) const;
    // Writes a direct column on the item table
    void salvarMetadado(const std::string& itemId, const std::string& coluna, const std::string& valor);
    // Fase 2b (freeze de edição em lote): grava N itens x M campos numa
    // ÚNICA transação (em vez de uma transação implícita por
    // UPDATE/INSERT — N*M delas hoje, via salvarMetadado em loop) e dispara
    // UM evento amplo no final (itemId vazio = "recarregar tudo", mesmo
    // idioma que MosaicoComponent::aoItemAlterado já usa) em vez de um
    // evento por item. Seguro de chamar de qualquer thread — não toca
    // Component nenhum, e o disparo do evento sempre volta pra message
    // thread via callAsync internamente (EventBus/ListenerList não é
    // thread-safe pra chamar direto de background).
    // pular (opcional): pares (itemId, coluna) que NÃO são gravados — ex.:
    // EVENT DATE só com o ano igual ao DATE CREATED original mantém o
    // dc_created daquele item. Mesma transação e mesma entrada de Undo.
    void salvarMetadadoEmLote(const std::vector<std::string>& itemIds,
                              const std::vector<std::pair<std::string, std::string>>& camposEValores,
                              const std::set<std::pair<std::string, std::string>>& pular = {});
    // Backfill silencioso do EVENT DATE (item.ano) quando ele ainda está
    // vazio. Deliberadamente NÃO passa por salvarMetadado: não marca
    // metadados_editados, não entra no Undo e não dispara evento — isto é
    // normalização de dado que faltou na ingestão, não edição do usuário.
    // Retorna true se gravou. Mesma regra do ingest: só escreve se vazio.
    bool preencherAnoPadraoSeVazio(const std::string& itemId, const std::string& ano);

    // --- Clear / Reset Metadata to Original Intake State ---
    void redefinirMetadadosItens(const std::vector<std::string>& itemIds);

    // --- Tags ---
    std::vector<std::string> lerTags(const std::string& itemId) const;
    void definirTags(const std::string& itemId, const std::vector<std::string>& tags);
    void adicionarTag(const std::string& itemId, const std::string& tag);
    void removerTag(const std::string& itemId, const std::string& tag);

    // --- People (Collection level) ---
    std::vector<std::string> listarPessoas() const;
    bool adicionarPessoa(const std::string& nome);
    bool removerPessoa(const std::string& nome);

    // Caminho absoluto da miniatura principal do item, se o índice já
    // processou uma (Etapa 4 escreve isso; pode não existir ainda).
    std::optional<juce::String> caminhoMiniaturaPrincipal(const std::string& itemId) const;

    void gerarMiniaturasFaltantes();

    // Item D.9/10 ("Reload File" / "Replace File" no menu de contexto da
    // METADATA): relê `novoCaminho` (o próprio caminho atual do master, pra
    // "reload"; um arquivo escolhido pelo operador, pra "replace") e grava
    // como uma DERIVADA nova do master do item — nunca sobrescreve o master
    // em si (trava por trigger em schema/registro.sql, P1: identidade do
    // master é preservada por integridade arquivística). Uma derivada
    // anterior do mesmo item, se houver, é substituída (mesma regra de
    // "uma por item" que definirCapa já usa). arquivoPrincipal() passa a
    // preferir essa derivada enquanto ela existir. Devolve false e preenche
    // `erro` se o item não tiver master ou o arquivo novo não puder ser lido.
    bool recarregarOuSubstituirArquivo(const std::string& itemId, const juce::File& novoCaminho, juce::String& erro);

    // "Show Recently Ingested": itens da ÚLTIMA leva promovida INTAKE -> GRID
    // (confirmarLoteGrid), lidos do banco (item.lote_grid_id) — sobrevivem a
    // fechar/reabrir. Vazio = nenhuma leva registrada. Cache em memória,
    // invalidado a cada confirmarLoteGrid.
    const std::vector<std::string>& ultimosItensIngeridos() const;

    struct ArquivoInfo {
        std::string id;
        juce::String caminhoAbsoluto;
        std::string papel;
        bool ehMaster = false;
        juce::String caracteristicasTecnicasJson;
    };

    // Arquivo "principal" do item pra fins de preview (§3.4, Reorientação
    // completa) — o master se existir, senão o primeiro arquivo cadastrado.
    // nullopt se o item ainda não tem nenhum arquivo associado (ex.:
    // ingest falhou antes de terminar a cópia).
    std::optional<ArquivoInfo> arquivoPrincipal(const std::string& itemId) const;

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

    // --- Pacote de collection (Fase 3) ---
    // Depois que os arquivos de Media/ do pacote entraram pelo INTAKE:
    // casa cada item pelo SHA-256 com o registro do matriz-pacote.json, grava
    // os dados de ficha, cria o folder map com o nome do pacote (sufixo
    // numérico se o nome já existe; nenhum outro mapa muda) e registra no
    // log. Uma transação só, sem Undo/EventBus — roda em background, quem
    // chama avisa a UI. Desfazer = o mesmo da ingestão (Reject no Intake).
    struct ResultadoIntakePacote {
        bool ok = false;
        juce::String erro;
        int entraram = 0, comDados = 0, semCorrespondencia = 0, jaExistiam = 0, registrosSemArquivo = 0;
        juce::String folderMap;
        juce::StringArray semCorrespondenciaNomes, registrosSemArquivoCaminhos;
    };
    ResultadoIntakePacote aplicarPacoteIngerido(const matriz::consolidacao::pacote::Pacote& pacote,
                                                const std::vector<std::string>& itensIngeridos);

    // --- Observações/Notes (item 9) — item_observacao, várias por item ---
    struct ItemObservacao {
        std::string id;
        std::string texto;
        std::string autor;
        std::string criadoEm;
        std::optional<int64_t> minutagemMs; // nulo pra imagem/documento e quando não informado
    };

    // Ordenadas por criado_em (mais antiga primeiro — mesma ordem em que
    // marcadores de timeline vão aparecer quando existirem, item 9.2).
    std::vector<ItemObservacao> observacoesDoItem(const std::string& itemId) const;

    // Devolve o id gerado.
    std::string adicionarObservacao(const std::string& itemId, const std::string& texto,
                                     std::optional<int64_t> minutagemMs, const std::string& autor);

    // Edita texto e/ou minutagem de uma observação existente. É por aqui que
    // a timeline grava um marcador movido ou renomeado, e a ficha grava o
    // texto editado — a MESMA linha nos dois casos (item 9.2: marcadores e
    // observações são a mesma lista, vista de dois lugares).
    void atualizarObservacao(const std::string& observacaoId, const std::string& texto,
                             std::optional<int64_t> minutagemMs);

    void removerObservacao(const std::string& observacaoId);

    // --- Árvore Origem/Acervo (Reorientação completa §5, §8.1) ---
    //
    // Um nó da árvore lateral. `id` é o id de acervo_pasta na árvore Acervo;
    // vazio nos nós sintéticos que não existem como linha própria (segmento
    // de caminho na árvore Origem, e o nó "não organizados" na raiz do
    // Acervo). `itemIds` é recursivo — já inclui os itens de todos os
    // filhos, pronto pra virar filtro da grade sem precisar caminhar a
    // árvore de novo.
    struct NoArvore {
        std::string id;
        juce::String nome;
        std::string pastaPaiId;
        int posicaoX = 0;
        int posicaoY = 0;
        bool ativo = true;
        juce::String corCustomizadaHex; // FOLDER COLOR (item 12) — "" = sem cor
        // Auto-organização: CSV de níveis (pasta com regra), "@auto" (subpasta gerada) ou "".
        juce::String regraOrganizacao;
        std::vector<NoArvore> filhos;
        std::set<std::string> itemIds;
        std::set<std::string> itemIdsDiretos;
    };

    NoArvore arvoreOrigem(bool incluirTodos = false) const;

    // Folder Maps múltiplos (Fase 1). kMapaOriginal é o único valor
    // especial: nunca é uma linha de folder_map, é computado ao vivo a
    // partir de arquivo.caminho_absoluto_origem — o que "importar/
    // atualizar estrutura da origem" fazia antes desta fase (esse botão
    // foi removido: ORIGINAL cobre o mesmo caso sem nunca ficar
    // desatualizado e sem apagar nada). Todo outro mapaId é um id real de
    // folder_map, dono de um subconjunto de acervo_pasta/acervo_item_pasta
    // totalmente independente dos demais mapas.
    static const std::string kMapaOriginal;
    struct FolderMapInfo {
        std::string id; // kMapaOriginal para o ORIGINAL
        juce::String nome;
        bool original = false;
    };
    // ORIGINAL sempre primeiro, depois os mapas do usuário em ordem.
    std::vector<FolderMapInfo> listarFolderMaps() const;
    // origem: nullopt = em branco; kMapaOriginal = cópia do ORIGINAL (é um
    // retrato do momento da cópia — não acompanha mudanças futuras da
    // SOURCE, só o ORIGINAL é ao vivo); outro id = cópia desse mapa do
    // usuário. "" em erro (nome vazio, origem inexistente).
    std::string criarFolderMap(const juce::String& nome, const std::optional<std::string>& origemMapaId);
    bool renomearFolderMap(const std::string& mapaId, const juce::String& novoNome);
    // false se mapaId for o ORIGINAL, o mapa do MAIN (Fase 2) ou não existir.
    bool apagarFolderMap(const std::string& mapaId);
    // Primeiro mapa do usuário do projeto, em ordem — usado por todo
    // consumidor que ainda não tem seletor de mapa na UI própria (ver as
    // sobrecargas sem mapaId logo abaixo). "" só é possível num projeto sem
    // nenhum folder_map, o que a migração da Fase 1 nunca deveria deixar
    // acontecer. Se ArvoreBackupComponent já chamou definirMapaAtivo() com
    // um mapa do usuário válido nesta sessão, devolve esse em vez do
    // primeiro — assim SEND TO FOLDER (AcoesItem) e as outras telas que só
    // conhecem "o mapa atual" seguem o dropdown da aba STRUCTURE sem
    // precisar saber que mapas múltiplos existem. Não persiste: cada
    // abertura do projeto volta a usar o primeiro mapa até alguém escolher
    // de novo.
    std::string mapaAtivoPadrao() const;
    // "" ou kMapaOriginal = limpa a seleção (volta a usar o primeiro mapa
    // do usuário). Chamado pelo dropdown de ArvoreBackupComponent.
    // Também persiste o mapa escolhido (mapa_ativo.txt na pasta do projeto).
    void definirMapaAtivo(const std::string& mapaId);
    // true quando o dropdown do Folder Map está no ORIGINAL (somente leitura):
    // SEND TO FOLDER fica desabilitado nesse estado.
    bool mapaAtivoEhOriginal() const { return mapaAtivoSelecionado_ == kMapaOriginal; }
    // Mapa com que o Folder Map abre: o último usado neste projeto (persistido,
    // pode ser o ORIGINAL); sem registro (1ª abertura) ou se o mapa lembrado
    // foi apagado, o ORIGINAL. Não afeta mapaAtivoPadrao(), que continua
    // devolvendo sempre um mapa editável para os mutadores compat.
    std::string mapaInicialDoFolderMap() const;
    // Mapa hoje selecionado no dropdown do Folder Map (pode ser o ORIGINAL);
    // antes de qualquer seleção nesta sessão, o mesmo que mapaInicialDoFolderMap().
    std::string mapaSelecionadoNoFolderMap() const;

    NoArvore arvoreAcervo(const std::string& mapaId) const;
    // Compat (pré Fase 1) — opera no mapaAtivoPadrao(). Só
    // ArvoreBackupComponent, dono do seletor de mapa da UI, chama a versão
    // com mapaId explícito; o resto do app (ArvoreComponent,
    // CatalogWorkspaceComponent, AcoesItem, self-tests) continua chamando
    // esta sem precisar saber que mapas múltiplos existem.
    NoArvore arvoreAcervo() const { return arvoreAcervo(mapaAtivoPadrao()); }
    static NoArvore podarArvore(const NoArvore& raiz, const std::set<std::string>& idsPermitidos);

    // SEM PASTA (NO FOLDER): itens do projeto sem nenhuma pasta no mapa
    // dado. Sempre vazio pro ORIGINAL (não existe "sem pasta" lá — S4/13).
    // Contagem por agregação (COUNT), nunca varredura por item na UI.
    std::set<std::string> itensSemPasta(const std::string& mapaId) const;
    int contarItensSemPasta(const std::string& mapaId) const;

    std::string criarPastaAcervo(const std::string& nome, const std::optional<std::string>& pastaPaiId,
                                  const std::string& mapaId);
    // Compat (pré Fase 1) — ver arvoreAcervo() sem mapaId acima.
    std::string criarPastaAcervo(const std::string& nome, const std::optional<std::string>& pastaPaiId) {
        return criarPastaAcervo(nome, pastaPaiId, mapaAtivoPadrao());
    }
    // MAPA depois que existe MAIN (etapa 5): criar pasta é livre; renomear,
    // mover ou apagar pasta que já tem arquivo no MAIN é bloqueado — estas
    // devolvem false e avisam o operador. O ORIGINAL nunca aceita nenhuma
    // destas (é computado, não tem pasta real pra editar); ver kMapaOriginal.
    bool renomearPastaAcervo(const std::string& pastaId, const std::string& novoNome);
    bool apagarPastaAcervo(const std::string& pastaId);

    bool moverPastaAcervo(const std::string& pastaId, const std::optional<std::string>& novaPastaPaiId);
    bool mainExiste() const;
    bool pastaTemArquivosNoMain(const std::string& pastaId) const;

    // ------------------------------------------------------------------
    // AUTO-ORGANIZAÇÃO por pasta (Folder Map). A pasta guarda uma regra (CSV de
    // blocos YEAR, MEDIA TYPE, FILE TYPE, SOURCE MEDIUM, CREATOR, CONTENT,
    // SUBJECT) e distribui os itens soltos nela — e os das subpastas AUTO dela —
    // em subpastas criadas pela regra, na ordem dos blocos (YEAR › CONTENT gera
    // "2019/Photo"). Subpastas criadas à mão ficam intocadas; subpastas AUTO
    // ficam marcadas ("@auto"), são reaproveitadas pelo nome e apagadas quando
    // esvaziam. Só no mapa do usuário (nunca no ORIGINAL) e só em pasta sem
    // arquivo no MAIN.
    // ------------------------------------------------------------------
    // Ano de um texto de data em qualquer formato ("2019", "2019-05-04", "04/05/2019"): a primeira
    // sequência de 4 dígitos entre 1800 e 2099. nullopt se não houver.
    static std::optional<int> extrairAnoDeData(const juce::String& texto);
    // Ano do EVENT DATE (coluna item.ano, a mesma que a ficha mostra). nullopt = vazio ou "0" = Unknown.
    std::optional<int> anoDoEventDate(const std::string& itemId) const;
    static bool regraEhAuto(const juce::String& regra) { return regra == "@auto"; }
    enum class StatusAutoOrg { Ok, SomenteLeitura, MapaOriginal, TemArquivosNoMain, PastaInvalida, SubpastaAuto, SemNiveis };
    struct ResultadoAutoOrg {
        StatusAutoOrg status = StatusAutoOrg::Ok;
        int itensMovidos = 0;
        int pastasCriadas = 0;
        int pastasApagadas = 0;
    };
    // Um segmento de pasta por nível pedido, por item (mesmas fontes do backup, ver
    // Consolidacao.cpp); lê tudo em lotes de consultas, nunca uma consulta por item.
    std::map<std::string, std::vector<juce::String>> segmentosDeOrganizacao(
        const std::set<std::string>& itemIds, const matriz::consolidacao::HierarquiaBackup& niveis) const;
    // Aplica a regra (CSV) na pasta e a grava. `automatico`: passada silenciosa ao
    // recarregar o Folder Map — sem entrada de desfazer e sem tocar em pasta travada.
    // Uma organização inteira = um grupo de desfazer.
    ResultadoAutoOrg aplicarAutoOrganizacao(const std::string& pastaId, const std::string& regraCsv, bool automatico = false);
    // Mantém as pastas como estão; só remove a regra e a marcação AUTO.
    void desligarAutoOrganizacao(const std::string& pastaId);
    // Passada automática: pastas com regra e itens diretos (checagem barata). Devolve
    // quantas pastas foram organizadas.
    int organizarItensSoltosDasPastasComRegra(const std::string& mapaId);
    // Fase 2 — ID do folder map gravado em projeto.backup_config_main.mapa_id
    // ("" = MAIN ainda não existe, ou foi criado por regra/estrutura original).
    // Só esse mapa fica sujeito às travas do MAIN e não pode ser apagado.
    std::string mapaDoMainId() const;
    // Nome atual do mapa do MAIN ("" se não houver).
    juce::String nomeDoMapaDoMain() const;

    // ------------------------------------------------------------------
    // MAIN EDIT MODE (Fase 4). Fora do modo o MAIN fica travado como sempre.
    // A barreira é UMA por sessão (não por operação): digitar o nome ATUAL do
    // projeto (File > Rename Project pode ter mudado). Sai sozinho ao fechar o
    // projeto ou depois de kTimeoutEdicaoMainMs sem operações.
    // ------------------------------------------------------------------
    static constexpr juce::int64 kTimeoutEdicaoMainMs = 15 * 60 * 1000;

    // ------------------------------------------------------------------
    // Clone somente leitura (Fase 5). Projeto aberto a partir de um destino com
    // papel CLONE navega, busca, vê e exporta; não edita metadado, não ingere,
    // não mexe em mapas e não faz backup. A única saída é PROMOVER A MAIN
    // (fluxo existente). Relê o papel depois da promoção.
    // ------------------------------------------------------------------
    bool somenteLeitura() const { return somenteLeitura_; }
    // Relê destination.json; true se o estado mudou (ex.: clone promovido a MAIN).
    bool reavaliarSomenteLeitura();
    // Aviso único (com trava de tempo) usado por todo guarda de escrita.
    static void avisarSomenteLeitura();
    bool editandoMain() const { return editandoMain_; }
    // MAIN existe e é este projeto (não CLONE) — condição pra oferecer o modo.
    bool podeEditarMain() const;
    // Só compara o texto com o nome atual; false = barreira não passou.
    bool nomeConfereComProjeto(const juce::String& digitado) const;
    bool entrarModoEdicaoMain(const juce::String& nomeDigitado);
    void sairModoEdicaoMain(const juce::String& motivo);
    void tocarAtividadeMain();
    // Chamado por timer da UI: se passou do timeout, sai do modo e devolve true.
    bool verificarTimeoutEdicaoMain();
    std::function<void()> aoMudarModoEdicaoMain;
    // Rename/move de pasta do mapa do MAIN com muitos arquivos roda em background; ao
    // terminar (ok ou erro) a UI do Folder Map precisa reler o mapa.
    std::function<void()> aoTerminarEdicaoPastaMain;
    static constexpr int kLimiteArquivosPastaSincrona = 300;
    // Arquivos registrados no MAIN dentro da pasta (uma consulta agregada).
    int contarArquivosDaPastaNoMain(const std::string& pastaId) const;
    // MAIN organizado por folder map (mover/pastas só nesse caso).
    bool mainUsaMapa() const { return !mapaDoMainId().empty(); }
    matriz::mainedit::ContextoMain contextoDoMain() const;

    // Operações de arquivo no MAIN: rodam numa thread de fundo (uma por vez,
    // com progresso global); `aoConcluir` volta na message thread. Só dentro do modo.
    using AoConcluirEdicaoMain = std::function<void(const matriz::mainedit::Resultado&)>;
    bool operacaoMainEmCurso() const { return operacaoMainEmCurso_.load(); }
    // Leitura em fundo pro painel do MAIN (o pool é encerrado antes do banco fechar).
    void agendarNoPoolDoMain(std::function<void()> trabalho) { poolMainEdit_.addJob(std::move(trabalho)); }
    void editarMainRenomearArquivo(const std::string& registroId, const juce::String& novoNome, AoConcluirEdicaoMain aoConcluir,
                                    bool registrarUndoDesta = true);
    void editarMainMoverArquivo(const std::string& registroId, const std::string& novaPastaId, AoConcluirEdicaoMain aoConcluir,
                                bool registrarUndoDesta = true);
    void editarMainSubstituirArquivo(const std::string& registroId, const juce::File& novaVersao, AoConcluirEdicaoMain aoConcluir);
    void editarMainDeletarArquivo(const std::string& registroId, AoConcluirEdicaoMain aoConcluir);
    void editarMainRestaurar(const std::string& quarentenaId, const juce::String& destinoAlternativoRel, AoConcluirEdicaoMain aoConcluir);
    // A barreira do nome do projeto já foi conferida pelo chamador.
    void editarMainEsvaziarQuarentena(std::function<void(const matriz::mainedit::ResultadoEsvaziar&)> aoConcluir);
    // Só de ida: gera um mapa do usuário a partir da ESTRUTURA REAL do MAIN e o
    // torna o mapa do MAIN. Devolve "" em sucesso ou a mensagem de erro.
    void converterMainParaFolderMap(std::function<void(const juce::String& erro, const juce::String& nomeDoMapa)> aoConcluir);
    // Retoma/desfaz operações interrompidas por queda (ao abrir o projeto).
    void recuperarOperacoesDoMain();
    // Mapa: item passa de uma pasta pra outra no mapa do MAIN (idempotente).
    void mapaMoverItemDoMain(const std::string& itemId, const std::string& pastaDe, const std::string& pastaPara);
    static void avisarMapaTravado(const juce::String& mensagem);
    void atualizarPosicaoPastaAcervo(const std::string& pastaId, int x, int y);
    void alternarAtivoPastaAcervo(const std::string& pastaId, bool ativo);

    // FOLDER COLOR (item 12) — overlay visual translúcido no Treemap/árvore
    // BACKUP, persistido por pasta. "" limpa a cor (volta ao padrão).
    void definirCorPastaAcervo(const std::string& pastaId, const juce::String& corArgbHex);
    juce::String lerCorPastaAcervo(const std::string& pastaId) const;

    // Histórico de cores do Folder Color (Fase 3): por PROJETO, não por
    // pasta — compartilhado entre todas as pastas do Treemap/árvore
    // BACKUP. Mais recente primeiro, no máximo 10 hex. Persistido numa
    // única coluna JSON na tabela `projeto` (linha única do banco).
    std::vector<juce::String> historicoCoresPasta() const;
    void definirHistoricoCoresPasta(const std::vector<juce::String>& coresHex);

    // MOVE: tira o item de toda pasta antiga NO MESMO MAPA da pasta de
    // destino antes de pôr na nova — mapas diferentes nunca se afetam
    // (Fase 1: "editar um mapa nunca afeta outro").
    void adicionarItensAPasta(const std::vector<std::string>& itemIds, const std::string& pastaId);
    std::string agruparItensEmNovaPasta(const std::vector<std::string>& itemIds, const std::string& mapaId);
    // Compat (pré Fase 1) — ver arvoreAcervo() sem mapaId acima.
    std::string agruparItensEmNovaPasta(const std::vector<std::string>& itemIds) {
        return agruparItensEmNovaPasta(itemIds, mapaAtivoPadrao());
    }

    // S4/13 — ao contrário de adicionarItensAPasta (que MOVE: tira o item de
    // toda pasta antiga antes de pôr na nova), esta só ACRESCENTA uma
    // associação item->pasta, preservando as demais. Usada ao restaurar um
    // preset de pastas, onde um item pode legitimamente pertencer a várias
    // pastas (N:N) ao mesmo tempo.
    void adicionarItemAPastaSemRemoverOutras(const std::string& itemId, const std::string& pastaId);

    // Reencontra um item pelo código de acervo (usado ao importar um preset
    // de pastas de OUTRO projeto, onde o item_id original não existe aqui).
    // nullopt se não achar nenhum item com esse código.
    std::optional<std::string> localizarItemPorCodigo(const std::string& codigoAcervo) const;

    // Replica uma subárvore inteira da EXPLORER dentro da BACKUP.
    //
    // Esta é a operação central do software: arrastar uma pasta traz a
    // hierarquia INTEIRA junto, em todos os níveis, exatamente como está na
    // origem. Arrastar um catálogo inteiro nunca pode virar um oceano de
    // arquivos soltos — se virar, o software falhou no seu propósito.
    //
    // manterEstrutura = true  → recria cada subpasta de `origem` sob
    //                           `pastaPaiId` e prende cada item no nível
    //                           correspondente ao que ele ocupa na origem.
    // manterEstrutura = false → todos os itens da subárvore (recursivo) vão
    //                           direto pra `pastaPaiId`, sem criar subpasta.
    //
    // pastaPaiId vazio = raiz do mapa `mapaId`. Devolve quantos itens foram
    // vinculados (contando um item uma vez por pasta em que entrou).
    int replicarSubarvoreNoAcervo(const NoArvore& origem, const std::string& pastaPaiId, bool manterEstrutura,
                                   const std::string& mapaId);
    // Compat (pré Fase 1) — ver arvoreAcervo() sem mapaId acima.
    int replicarSubarvoreNoAcervo(const NoArvore& origem, const std::string& pastaPaiId, bool manterEstrutura) {
        return replicarSubarvoreNoAcervo(origem, pastaPaiId, manterEstrutura, mapaAtivoPadrao());
    }
    void removerItemDaPasta(const std::string& itemId, const std::string& pastaId);

    // --- Ações sobre item/seleção (menu de contexto e painel direito) ---
    //
    // NENHUMA destas apaga arquivo em disco. Nem o original na fonte, nem a
    // cópia dentro do projeto: "remover" aqui é sempre sobre o registro e o
    // plano de organização. A interface diz isso explicitamente ao operador.

    // Tira os itens de TODAS as pastas do mapa `mapaId` — eles continuam no
    // projeto (e em qualquer outro mapa), só voltam a ser "sem pasta" NESTE
    // mapa.
    void removerItensDoBackup(const std::vector<std::string>& itemIds, const std::string& mapaId);
    // Compat (pré Fase 1) — ver arvoreAcervo() sem mapaId acima.
    void removerItensDoBackup(const std::vector<std::string>& itemIds) {
        removerItensDoBackup(itemIds, mapaAtivoPadrao());
    }

    // Remove os itens do projeto (cascateia pra ficha, arquivo, pastas).
    // Some da grade; o arquivo de origem no disco fica intacto, e a cópia
    // dentro da pasta do projeto também — vira uma cópia órfã, que este
    // método deliberadamente não apaga.
    void removerItensDoProjeto(const std::vector<std::string>& itemIds);

    // Renomeia (item.titulo) — é o que alimenta o token {titulo} da máscara
    // de nomenclatura, ou seja, o nome que o arquivo terá no backup.
    void renomearItens(const std::vector<std::string>& itemIds, const std::string& novoTitulo);

    // Atalho "E" (Tag as Edited): marcação manual, independente de
    // metadados_editados (que é automática). Começa sempre desmarcada.
    void alternarMarcadoRevisado(const std::vector<std::string>& itemIds);
    void limparTodosMarcadosRevisado();
    // Flag E (marcado_revisado) de um item: uma query, sem disco.
    bool itemMarcadoRevisado(const std::string& itemId) const;

    // Caminho absoluto de origem do arquivo principal — pra "Mostrar na
    // origem" e "Copiar caminho". nullopt se o item não tem arquivo com
    // origem registrada.
    std::optional<juce::String> caminhoDeOrigem(const std::string& itemId) const;

    // GET EXIF (a pedido do usuário): o que a thread de fundo precisa pra
    // achar o arquivo de cada item SEM tocar neste objeto — as colunas de
    // resolução (só banco, sem disco) e um resolvedor já carregado (MAIN/
    // CLONE/origem; resolver() só usa memória + disco).
    struct AlvoArquivo {
        std::string itemId, arquivoId, localizacaoVault, caminhoRelativo, caminhoAbsolutoOrigem;
    };
    std::vector<AlvoArquivo> alvosArquivoPrincipal(const std::vector<std::string>& itemIds) const;
    std::shared_ptr<matriz::vault::ResolvedorEmLote> criarResolvedorEmLote() const;
    // Troca a seção automática [OTHER METADATA] das notas de cada item pelo
    // texto dado, numa transação só. Devolve quantos itens foram gravados.
    int gravarOutraMetadataEmLote(const std::map<std::string, std::string>& textoPorItem);
    // Expira quando este ProjetoAberto é destruído (callbacks assíncronos).
    std::weak_ptr<bool> tokenVida() const { return vivo_; }

    // Outros itens do projeto cujo conteúdo é idêntico (mesmo SHA-256 de
    // algum arquivo). Inclui o próprio item quando há duplicata, pra a grade
    // poder mostrar o conjunto inteiro lado a lado; devolve vazio quando o
    // item não tem duplicata nenhuma (ou ainda não tem checksum calculado).
    std::set<std::string> itensComMesmoConteudo(const std::string& itemId) const;

    struct ParDuplicatas {
        struct ItemInfo {
            std::string id;
            std::string codigoAcervo;
            std::string titulo;
            std::string tipoMidia;
            std::string estado;
            juce::String caminhoRelativo;
            juce::String caminhoOrigem;
            juce::int64 tamanhoBytes = 0;
        };
        std::vector<ItemInfo> itens;
        juce::String filename;
        juce::int64 tamanhoBytes = 0;
        std::string checksumSha256;
    };

    std::vector<ParDuplicatas> listarGruposDuplicados() const;
    void atualizarEstadoItem(const std::string& itemId, const std::string& novoEstado);

    // --- Busca avançada e coleções inteligentes (Acréscimos §10) ---
    //
    // Ids de item cujo código, título, algum valor de campo de ficha, ou
    // assunto contém `texto` (case-insensitive). OCR e transcrição (§10.1)
    // não existem ainda — gap declarado, não fingido: nenhum texto extraído
    // de imagem/áudio é buscável nesta etapa. String vazia devolve conjunto
    // vazio (nunca "todos", pra não confundir com "sem filtro").
    // Escopo da busca (seletor sob o campo de busca do Metadata). Todos = a
    // busca de sempre; os demais procuram o termo só naquele campo.
    enum class EscopoBusca { Todos, NomeArquivo, Criador, Assunto, Conteudo, PessoasTags, Extensao, Notas, Geo };
    std::set<std::string> buscarItens(const juce::String& texto, EscopoBusca escopo = EscopoBusca::Todos) const;

    // Contagens pra chips de filtro (§10.2 — "cada um com contagem"). Chave
    // "" no mapa de tipo de mídia = itens ainda não classificados.
    std::map<std::string, int> contagensPorTipoMidia() const;
    std::map<std::string, int> contagensPorEstado() const;
    std::map<std::string, int> contagensPorExtensao() const;
    // Chave "" = origem ainda não preenchida (campo vazio).
    std::map<std::string, int> contagensPorOrigem() const;
    std::map<std::string, int> contagensPorContentType() const;
    std::map<std::string, int> contagensPorCollectionType() const;

    struct ColecaoDisponivel {
        std::string chave;
        juce::String rotulo;
        int contagem = 0;
    };
    std::vector<ColecaoDisponivel> listarColecoesDisponiveis() const;
    std::set<std::string> itensDaColecao(const std::string& chave) const;

    // Catalog - Linked Collections (§ E.3)
    struct ColecaoLink {
        std::string id;
        juce::String caminhoProjeto;
        juce::String nome;
        juce::String grupo;
        juce::String criadoEm;
        uint64_t totalAssets = 0;
        juce::int64 totalBytes = 0;
        bool valido = false;
    };
    std::vector<ColecaoLink> listarColecoesLinkadas() const;
    bool linkarColecao(const juce::File& pastaProjeto, const juce::String& grupo = {});
    bool desvincularColecao(const std::string& linkId);
    bool relocarColecaoLink(const std::string& linkId, const juce::File& novaPastaProjeto);
    bool atualizarGrupoColecao(const std::string& linkId, const juce::String& novoGrupo);

    // Ids de item cujo campo "ano" (nível raiz) cai em [anoDe, anoAte].
    // Item sem "ano" preenchido nunca entra (faixa é sobre o que se sabe).
    std::set<std::string> itensPorFaixaAno(int anoDe, int anoAte) const;

    // Coleção inteligente guarda a DEFINIÇÃO da busca (texto + filtros),
    // nunca um resultado — reexecutada por inteiro toda vez que é aberta,
    // "se atualiza sozinha conforme o acervo muda" (§10.2).
    struct ColecaoInteligente {
        std::string id; // vazio = ainda não salva
        juce::String nome;
        juce::String buscaTexto;
        std::set<juce::String> filtrosTipoMidia;
        std::set<juce::String> filtrosEstado;
        std::set<juce::String> filtrosExtensao;
        std::set<juce::String> filtrosOrigem;         // Digital/Analógico (item 4.2)
        std::set<juce::String> filtrosContentType;    // Content filter (item 13)
        std::set<juce::String> filtrosCollectionType; // Collection filter (item 13)
        std::optional<int> anoDe, anoAte;              // faixa de ano (item 4.3); os dois nulos = sem faixa
    };
    std::vector<ColecaoInteligente> listarColecoes() const;

    // -----------------------------------------------------------------
    // Vaults (§8) e coleções EMBUTIDAS (§10). As embutidas não são salvas
    // pelo operador: são views SQL do schema (colecao_embutida), que se
    // atualizam sozinhas a cada consulta — daí não haver id de banco nem
    // parâmetros pra editar, só a chave.
    // -----------------------------------------------------------------
    struct VaultResumo {
        std::string id;
        juce::String nome;
        juce::String localizacao;
        bool online = false;
        int totalItens = 0;
    };
    std::vector<VaultResumo> listarVaults() const;

    struct ColecaoEmbutida {
        std::string chave;   // "clipping", "ausentes", ...
        juce::String rotulo; // já traduzido
        int contagem = 0;
    };
    std::vector<ColecaoEmbutida> listarColecoesEmbutidas() const;
    std::set<std::string> itensDaColecaoEmbutida(const std::string& chave) const;

    // Reavalia quais Vaults estão montados agora e devolve os que acabaram
    // de ficar online — o gatilho da varredura de §8. Barato: só compara
    // caminho e UUID, não percorre volume nenhum.
    std::vector<std::string> reavaliarVaults();
    // colecao.id vazio cria uma nova; preenchido atualiza a existente.
    // Devolve o id (novo ou o mesmo).
    std::string salvarColecao(const ColecaoInteligente& colecao);
    void apagarColecao(const std::string& id);

    // -----------------------------------------------------------------
    // Camada de Preservação Digital (OAIS / PREMIS / FAIR)
    // -----------------------------------------------------------------

    // Preservation status calculado da view asset_preservation_status.
    preservation::PreservationStatus obterPreservationStatus(const std::string& itemId) const;

    // Lista de eventos PREMIS do item, cronológica inversa.
    std::vector<preservation::EventoPreservacao> listarEventosPreservacao(const std::string& itemId) const;

    // Direitos registrados. nullopt se nenhuma linha existe ainda.
    std::optional<preservation::DireitosPreservacao> obterDireitos(const std::string& itemId) const;
    void salvarDireitos(const std::string& itemId, const preservation::DireitosPreservacao& d);

    // Verifica fixity em thread background.
    // callback é chamado de volta na message thread com o resultado.
    void verificarFixityAsync(const std::string& arquivoId,
                              const std::string& caminhoAbsoluto,
                              std::function<void(preservation::ResultadoFixity)> callback);

    // Exporta metadados completos do item como JSON (DIP / FAIR).
    juce::String exportarPreservacaoJson(const std::string& itemId) const;

    // Exporta vários items como CSV (uma linha de cabeçalho + uma por item).
    juce::String exportarPreservacaoCsv(const std::vector<std::string>& itemIds) const;

    // Exporta todos os dados dos arquivos incluindo geolocalização como Full CSV.
    juce::String exportarFullCsv(const std::vector<std::string>& itemIds) const;

    // Exporta pacote completo BKR Full CSV (BKR_FULL.csv, BKR_FULL.schema.json, manifest.json) com validação.
    bool exportarFullCsvPacote(const std::vector<std::string>& itemIds, const juce::File& destLocation, juce::String& errorOut) const;

    // Exporta planilha XLS com cabeçalho de projeto e metadados Dublin Core.
    juce::String exportarXlsXml(const std::vector<std::string>& itemIds) const;

    // Exporta CSV com cabeçalhos Dublin Core (dc.identifier, dc.title, ...).
    juce::String exportarDublinCoreCsv(const std::vector<std::string>& itemIds) const;

    // Exporta manifesto de checksums em formato sha256sum -c (.txt legível).
    juce::String exportarFixityManifest(const std::vector<std::string>& itemIds,
                                         const std::string& algoritmo = "sha256") const;

    const std::set<std::string>& obterItensSelecionadosNoGrid() const { return selecionadosNoGrid_; }
    void definirItensSelecionadosNoGrid(const std::set<std::string>& selecionados) { selecionadosNoGrid_ = selecionados; }

    // Tipos de marcação volátil de sessão (em memória, sem persistência em banco)
    enum class TipoMarcacao {
        Html,
        Zip,
        Print,
        Watermark
    };

    // Operações sobre conjuntos de marcação de sessão
    void alternarMarcacao(TipoMarcacao tipo, const std::vector<std::string>& itemIds);
    void definirMarcacao(TipoMarcacao tipo, const std::vector<std::string>& itemIds, bool marcado);
    bool contemMarcacao(TipoMarcacao tipo, const std::string& itemId) const;
    size_t contarMarcacoes(TipoMarcacao tipo) const;
    void limparMarcacoes(TipoMarcacao tipo);
    void limparTodasMarcacoes();
    std::vector<std::string> idsMarcados(TipoMarcacao tipo) const;
    void transferirMarcacoes(const std::string& oldItemId, const std::string& newItemId);

    // Aliases funcionais
    void alternar(TipoMarcacao tipo, const std::vector<std::string>& itemIds) { alternarMarcacao(tipo, itemIds); }
    bool contem(TipoMarcacao tipo, const std::string& itemId) const { return contemMarcacao(tipo, itemId); }
    size_t contar(TipoMarcacao tipo) const { return contarMarcacoes(tipo); }
    void limpar(TipoMarcacao tipo) { limparMarcacoes(tipo); }
    std::vector<std::string> idsDe(TipoMarcacao tipo) const { return idsMarcados(tipo); }

    // Marcação para Publicação (compatibilidade: direcionado à lista Html em memória)
    void alternarPublicacaoItens(const std::vector<std::string>& itemIds) { alternarMarcacao(TipoMarcacao::Html, itemIds); }
    void definirPublicacaoItens(const std::vector<std::string>& itemIds, bool marcado) { definirMarcacao(TipoMarcacao::Html, itemIds, marcado); }
    bool itemMarcadoPublicacao(const std::string& itemId) const { return contemMarcacao(TipoMarcacao::Html, itemId); }

    // Marca d'água: persistência das configurações do projeto
    ConfiguracaoWatermark obterConfiguracaoWatermark() const;
    void salvarConfiguracaoWatermark(const ConfiguracaoWatermark& cfg);
    static ConfiguracaoWatermark carregarConfiguracaoWatermarkDePasta(const juce::File& pastaProjeto);

    // In-memory relinking and two-stage persistence
    bool isDirty() const { return dirty_; }
    void setDirty(bool d) { dirty_ = d; }
    // Cópia (não referência): marcadosHtml_/Zip_/Print_/Watermark_ e
    // inMemoryRelinkedPaths_ são lidos por listarItens()/
    // listarItensDaColecao()/listarItensEmQuarentena() nas threads de
    // background MatrizSnapshot/MatrizContagens enquanto a message thread
    // escreve via alternarMarcacao()/aplicarRelinkEmMemoria()/etc — ver
    // marcacoesMutex_. Uma referência devolvida aqui sobreviveria ao escopo
    // do lock e voltaria a ser uma leitura desprotegida.
    std::map<std::string, std::string> inMemoryRelinkedPaths() const {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        return inMemoryRelinkedPaths_;
    }
    void aplicarRelinkEmMemoria(const std::string& arquivoId, const std::string& newPath);
    void aplicarBatchRelinkEmMemoria(const std::map<std::string, std::string>& newPaths);
    void salvar();
    void descartarAlteracoesEmMemoria();
    std::optional<juce::File> resolverArquivoComMemoria(const std::string& arquivoId) const;

    void definirItensOffline(const std::set<std::string>& offlineIds) {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        itensOfflineCache_ = offlineIds;
    }
    std::set<std::string> obterItensOffline() const {
        std::lock_guard<std::mutex> lock(marcacoesMutex_);
        return itensOfflineCache_;
    }

    std::recursive_mutex& writeMutex() { return projeto_->writeMutex(); }

private:
    // Folder Maps múltiplos (Fase 1) — único caminho de código que insere
    // em acervo_item_pasta: resolve o mapa_id da pasta e grava as duas
    // colunas juntas, sempre em sincronia (self-test dedicado cobra isto
    // depois de mover pasta, duplicar mapa e importar mapa). Chame dentro
    // de uma transação já aberta pelo chamador — não abre a própria.
    void inserirItemPastaInterno(const std::string& itemId, const std::string& pastaId, const std::string& agora);
    // mapa_id de uma pasta existente ("" se não achar — chamador decide o
    // que fazer, normalmente tratar como erro silencioso como o resto do
    // arquivo já faz).
    std::string mapaIdDaPasta(const std::string& pastaId) const;
    // Ver definirMapaAtivo()/mapaAtivoPadrao() acima.
    std::string mapaAtivoSelecionado_;

    std::set<std::string>& obterConjuntoMarcacao(TipoMarcacao tipo);
    const std::set<std::string>& obterConjuntoMarcacao(TipoMarcacao tipo) const;

    // Protege os 4 sets de marcação, itensOfflineCache_ e inMemoryRelinkedPaths_ abaixo: lidos
    // por listarItens()/listarItensDaColecao()/listarItensEmQuarentena() nas
    // threads de background MatrizSnapshot/MatrizContagens (ver
    // MosaicoComponent::recarregar(), FiltrosComponent) enquanto a message
    // thread escreve via alternarMarcacao()/definirMarcacao()/
    // limparMarcacoes()/transferirMarcacoes()/aplicarRelinkEmMemoria()/
    // aplicarBatchRelinkEmMemoria()/salvar()/descartarAlteracoesEmMemoria() —
    // std::set/std::map não são thread-safe pra leitura concorrente com
    // escrita, era um data race real (confirmado sob TSan).
    mutable std::mutex marcacoesMutex_;
    std::set<std::string> marcadosHtml_;
    std::set<std::string> marcadosZip_;
    std::set<std::string> marcadosPrint_;
    std::set<std::string> marcadosWatermark_;
    std::set<std::string> itensOfflineCache_;
    // Só existe pra ser observado por weak_ptr em callbacks assíncronos
    // (ex.: Undo do salvarMetadadoEmLote registrado via callAsync): expira
    // quando este ProjetoAberto é destruído.
    std::shared_ptr<bool> vivo_ = std::make_shared<bool>(true);

    std::unique_ptr<matriz::model::Project> projeto_;
    std::map<std::string, matriz::ficha::FichaDefinition> definicoesCache_;

    // MAIN EDIT MODE. O pool vem DEPOIS de projeto_: é destruído antes dele
    // (o job em curso ainda usa o banco) — não reordenar.
    bool editandoMain_ = false;
    bool somenteLeitura_ = false;
    bool organizandoAuto_ = false;  // guarda contra recursão da passada automática de auto-organização
    juce::int64 ultimaAtividadeMainMs_ = 0;
    std::atomic<bool> operacaoMainEmCurso_{false};
    void executarEdicaoMain(const juce::String& titulo, std::function<matriz::mainedit::Resultado()> trabalho,
                            AoConcluirEdicaoMain aoConcluir);
    juce::ThreadPool poolMainEdit_{1};
    // Resolução de duplicatas/merge (Fase 4). Também depois de projeto_.
    juce::ThreadPool poolMerge_{1};
    void restaurarRetratoMerge(const matriz::model::merge::Retrato& retrato);

    std::map<std::string, std::string> inMemoryRelinkedPaths_;
    bool dirty_ = false;

    mutable std::vector<std::string> ultimosItensIngeridos_;
    mutable bool ultimosItensIngeridosValido_ = false;

    static constexpr int kMaxUndo = 25;
    std::set<std::string> buscarItensNoEscopo(const juce::String& termo, EscopoBusca escopo) const;
    void registrarUndoEnvioAoGrid(const std::vector<std::string>& itemIds);
    std::vector<UndoEntry> pilhaUndo_;
    std::optional<UndoEntry> grupoAberto_;
    bool desfazendo_ = false;
    std::set<std::string> selecionadosNoGrid_;
};

} // namespace matriz::ui
