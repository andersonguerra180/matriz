#pragma once

#include <JuceHeader.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../Ficha/FichaDefinition.h"
#include "../Model/Project.h"
#include "../Preservation/Preservation.h"

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
    std::optional<std::string> contentType;
    std::optional<std::string> collectionType;

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
                                                        const std::set<std::string>& itensOffline = {});
    std::vector<ItemResumo> listarItensDaColecao(const juce::File& pastaColecao) const;

    // Move o Project pra fora — usado só ao trocar de idioma (Preferences),
    // que reconstrói a árvore de Component inteira do zero (é o jeito mais
    // simples de garantir que toda string em tela é retraduzida sem exigir
    // um retranslateUi() em cada Component). O objeto que chamou isto fica
    // inutilizável depois — só existe pra sobreviver à troca.
    std::unique_ptr<matriz::model::Project> destacarProjeto() { return std::move(projeto_); }

    std::string destinoBackupAtivo() const { return projeto_ ? projeto_->destinoBackupAtivo() : std::string(); }
    void definirDestinoBackupAtivo(const std::string& path) { if (projeto_) projeto_->definirDestinoBackupAtivo(path); }
    void sincronizarBackupDestinoDeHistorico();

    std::vector<ItemResumo> listarItens() const;
    int contarItens() const { return static_cast<int>(listarItens().size()); }
    std::vector<ItemResumo> listarItensEmQuarentena() const;
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
    void salvarMetadadoEmLote(const std::vector<std::string>& itemIds,
                               const std::vector<std::pair<std::string, std::string>>& camposEValores);
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

    // Item C.8 ("Show Recently Ingested"): guarda em memória (não persiste
    // entre reaberturas do projeto) os item_ids do último lote importado
    // pelo IngestWizardComponent, pro botão da aba METADATA filtrar por eles.
    void definirUltimosItensIngeridos(std::vector<std::string> itemIds);
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
        std::vector<NoArvore> filhos;
        std::set<std::string> itemIds;
        std::set<std::string> itemIdsDiretos;
    };

    NoArvore arvoreOrigem(bool incluirTodos = false) const;
    NoArvore arvoreAcervo() const;
    static NoArvore podarArvore(const NoArvore& raiz, const std::set<std::string>& idsPermitidos);

    std::string criarPastaAcervo(const std::string& nome, const std::optional<std::string>& pastaPaiId);
    void renomearPastaAcervo(const std::string& pastaId, const std::string& novoNome);
    void apagarPastaAcervo(const std::string& pastaId);

    void moverPastaAcervo(const std::string& pastaId, const std::optional<std::string>& novaPastaPaiId);
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

    void adicionarItensAPasta(const std::vector<std::string>& itemIds, const std::string& pastaId);
    std::string agruparItensEmNovaPasta(const std::vector<std::string>& itemIds);

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
    // pastaPaiId vazio = raiz da BACKUP. Devolve quantos itens foram
    // vinculados (contando um item uma vez por pasta em que entrou).
    int replicarSubarvoreNoAcervo(const NoArvore& origem, const std::string& pastaPaiId, bool manterEstrutura);
    void resetarEImportarEstruturaOrigem();
    void removerItemDaPasta(const std::string& itemId, const std::string& pastaId);

    // --- Ações sobre item/seleção (menu de contexto e painel direito) ---
    //
    // NENHUMA destas apaga arquivo em disco. Nem o original na fonte, nem a
    // cópia dentro do projeto: "remover" aqui é sempre sobre o registro e o
    // plano de organização. A interface diz isso explicitamente ao operador.

    // Tira os itens de TODAS as pastas da BACKUP — eles continuam no
    // projeto, só voltam a ser "ainda sem pasta".
    void removerItensDoBackup(const std::vector<std::string>& itemIds);

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

    // Caminho absoluto de origem do arquivo principal — pra "Mostrar na
    // origem" e "Copiar caminho". nullopt se o item não tem arquivo com
    // origem registrada.
    std::optional<juce::String> caminhoDeOrigem(const std::string& itemId) const;

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
    std::set<std::string> buscarItens(const juce::String& texto) const;

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

private:
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

    std::unique_ptr<matriz::model::Project> projeto_;
    std::map<std::string, matriz::ficha::FichaDefinition> definicoesCache_;

    std::map<std::string, std::string> inMemoryRelinkedPaths_;
    bool dirty_ = false;

    std::vector<std::string> ultimosItensIngeridos_;

    static constexpr int kMaxUndo = 25;
    std::vector<UndoEntry> pilhaUndo_;
    std::optional<UndoEntry> grupoAberto_;
    bool desfazendo_ = false;
    std::set<std::string> selecionadosNoGrid_;
};

} // namespace matriz::ui
