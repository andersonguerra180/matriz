#pragma once

#include <JuceHeader.h>

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "../Db/Database.h"

// Consolidação (item 10, §11.7) — copia byte a byte da master JÁ DENTRO DO
// PROJETO (não do arquivo original de origem — este nunca é reaberto aqui;
// P5 garante que a cópia de trabalho já é idêntica) pra dentro da estrutura
// do Acervo, renomeada pela máscara de cada pasta, e verifica depois.
//
// Escopo desta etapa, declarado — não fingido:
// - Metadado embutido (BWF/bext, iXML, EXIF write, ID3, XMP) NÃO é gravado
//   na cópia consolidada. Precisaria de escritores por formato que não
//   existem ainda (Exiv2, já uma dependência, só foi usado pra LEITURA até
//   aqui — escrever EXIF/XMP com ele é viável depois, os demais formatos
//   não têm biblioteca nenhuma no projeto). O checksum é da cópia tal como
//   sai, sem nenhum metadado a mais.
// - Destino é sempre uma pasta local. NAS/FTP/S3/nuvem (já modelados na
//   tabela `destino`) não têm cliente nenhum implementado pra consolidação
//   escrever neles.
// - "Metadado lido de volta corretamente" (§11.7 verificação) não se aplica
//   por não haver nada embutido — a verificação aqui é existência, tamanho
//   e checksum.

namespace matriz::consolidacao {

// Organização de pastas do backup (item 5). A estrutura padrão é
// Projeto → Ano → Tipo de Mídia → Tipo de Arquivo, e o operador reordena,
// remove ou acrescenta níveis. `PastaManual` é a estrutura que ele montou à
// mão na árvore BACKUP — é o que existia como único comportamento antes
// deste item, e continua disponível como um nível entre os outros (sozinha,
// reproduz exatamente o comportamento anterior).
//
// Material sem o campo de um nível (ano desconhecido, tipo não classificado)
// vai pra uma pasta "sem <campo>" em vez de desaparecer ou travar o backup
// (§5.2 — "nunca desaparece nem trava").
// EstruturaOriginal preserves the relative folder structure from the source path.
enum class NivelHierarquia { Projeto, Ano, TipoMidia, TipoArquivo, Origem, Artista, ContentType, Subject, PastaManual, EstruturaOriginal };

std::string nivelHierarquiaToString(NivelHierarquia n);
NivelHierarquia nivelHierarquiaFromString(const std::string& s); // lança std::runtime_error se desconhecido

using HierarquiaBackup = std::vector<NivelHierarquia>;

// Projeto → Ano → Tipo de Mídia → Tipo de Arquivo (item 5.1).
HierarquiaBackup hierarquiaPadrao();

// Serialização pra projeto.hierarquia_backup (CSV). Vazio/inválido volta
// pro padrão — nunca lança na leitura, pra um valor corrompido não impedir
// de abrir o projeto.
std::string hierarquiaParaCsv(const HierarquiaBackup& h);
HierarquiaBackup hierarquiaDeCsv(const std::string& csv);

// Código de cada SOURCE (etapa 5) — SOURCE = vault de onde veio algum
// arquivo ingerido. vault.codigo gravado (custom, ou fixado na primeira cópia
// daquele SOURCE pro MAIN) prevalece; os demais recebem o próximo "S01",
// "S02"… livre, pela ordem da primeira ingestão. vault.id -> código.
std::map<std::string, std::string> codigosDeSource(matriz::db::Database& registro);
// Só letras sem acento, números e hífen; 1 a 12 caracteres.
bool codigoDeSourceValido(const juce::String& codigo);

// Fase 2 — pasta física na raiz do MAIN para itens que ficaram sem pasta no
// mapa do MAIN. Nome reservado: nenhuma pasta comum do mapa pode usá-lo
// (ArvoreBackupComponent rejeita "SEM PASTA"/"NO FOLDER", que cobre este).
inline const char* const kPastaSemPasta = "_SEM_PASTA";

// Fase 4 — registra um move feito no MAIN (arquivo ou pasta, caminhos relativos a
// Media/) na fila de CADA clone ativo (clone_move_pendente): o próximo sync do
// clone aplica como move autoritativo (o hash só confirma), nunca como apagar +
// copiar. Cadeia A->B + B->C colapsa em A->C; A->B + B->A some. Chamar dentro da
// transação que muda o registro.
void anotarMovePendentePraClones(matriz::db::Database& registro, const char* tipo, const std::string& de,
                                 const std::string& para, const std::string& sha256);

// Caminho físico (relativo a Media/) da pasta do folder map: nomes da raiz até
// a pasta, "/"-separados, como o planner monta em PastaManual. "" se pastaId
// vazio ou inexistente.
juce::String caminhoFisicoDaPasta(matriz::db::Database& registro, const std::string& pastaId);
// Segmento de pasta seguro nos dois sistemas de arquivos (mesma regra do planner).
juce::String segmentoDePastaSeguro(const juce::String& nome);

// Chave de consolidacao_registro.destino_path para a pasta Media de um destino.
std::string chaveDestino(const juce::File& destino);


struct ItemPlanejado {
    std::string itemId;
    std::string codigoAcervo;
    std::string pastaId;
    std::string arquivoId;
    juce::String nomeOriginal;             // só pro display na prévia (original vs final)
    juce::String caminhoRelativoDestino;   // relativo à raiz de destino escolhida
    juce::int64 tamanhoBytes = 0;
    bool jaConsolidado = false;            // incremental — já tem registro com o mesmo checksum, não precisa copiar de novo
    bool emConflito = false;               // caminhoRelativoDestino colide com outro item do plano
    // Fase 2 — item que está em _SEM_PASTA no MAIN e agora tem pasta no mapa
    // do MAIN: caminho atual no MAIN (a ser movido pra caminhoRelativoDestino,
    // nunca recopiado). Vazio = sem movimento.
    juce::String moverDe;
};

// Manifesto de checksums do MAIN (formato `shasum -a 256 -c` / `sha256sum -c`, rodado a partir da pasta Media):
// "<sha256>  <caminho relativo ao Media>\n" por arquivo, com o SHA-256 dos bytes ENTREGUES (consolidacao_registro).
// Lê o registro em UMA consulta e NUNCA abre arquivo (nem da SOURCE, que pode ser um placeholder de nuvem, nem do
// MAIN): item sem registro de consolidação não entra com hash inventado — vai pro fim como comentário
// "# not consolidated: <caminho>". Linhas com casamento por arquivo_id + caminho do plano, e destino_path exato >
// destino_id > legado (mesma regra de planejarConsolidacao). aoProgredir(feito,total) devolve false pra cancelar.
struct ResultadoManifestoChecksums {
    std::string texto;
    int comHash = 0;
    int semRegistro = 0;
    bool cancelado = false;
};
ResultadoManifestoChecksums gerarManifestChecksums(matriz::db::Database& registro, const juce::File& destinoMedia,
                                                   const std::vector<ItemPlanejado>& itens,
                                                   const std::function<bool(int, int)>& aoProgredir = {});

struct PlanoConsolidacao {
    std::vector<ItemPlanejado> itens;
    std::vector<juce::String> nomesEmConflito; // caminhos que aparecem em mais de um item — bloqueiam consolidar
    int itensNaoOrganizados = 0;               // §5.5 — fora do plano, só contados pro aviso
    // Fase 2 — export por folder map: itens sem pasta no mapa escolhido ficam
    // fora do plano (não existe _SEM_PASTA em export); ids pra o aviso final.
    std::set<std::string> semPastaExcluidos;
    // Fase 2 — itens a mover de _SEM_PASTA (oferta no backup seguinte).
    // Cada item aparece uma vez, com moverDe preenchido.
    std::vector<ItemPlanejado> movimentosSemPasta;
    int conflitosAutoResolvidos = 0;           // Nomes duplicados auto-resolvidos com sufixo único
    juce::int64 espacoNecessarioBytes = 0;     // soma dos itens que NÃO estão jaConsolidado
    juce::int64 espacoDisponivelBytes = 0;

    bool podeConsolidar() const { return nomesEmConflito.empty(); }
};

// Nunca toca em disco — só lê o banco e calcula. Sempre reexecutável (é a
// "prévia sempre visível", §11.6), inclusive pra atualizar depois de o
// operador resolver um conflito renomeando/movendo na árvore.
// Nome de pasta pra um tipo de mídia. O default devolve o próprio id
// (`fita_rolo`), que é estável mas cru; a UI passa um resolvedor que devolve
// o rótulo de exibição traduzido ("Reel tape"), como no exemplo do §5.1.
// Existe como callback, e não como include de FichaI18n aqui, porque este
// módulo não é de UI e é compilado no selftest headless, que não linka i18n.
// Canonical filename resolver: guarantees basename + existing extension = exactly one extension.
// Prevents duplicated extensions (.wav.wav, .jpg.jpg) across all modes.
juce::String resolverNomeFinalBackup(const juce::File& arquivoOrigem, const std::string& nomeBaseMascara, bool usaEstruturaOriginal);

using RotuloTipoMidia = std::function<juce::String(const std::string& tipoMidia)>;

enum class ModoPrefixoArquivo {
    Mascara = 0,  // Avalia a máscara da hierarquia/projeto (padrão da engine)
    Nenhum = 1,   // Sem prefixo: preserva o nome original do arquivo (default da UI de backup)
    Auto = 2,     // Prefixo automático do projeto (prefixo_nomenclatura)
    Custom = 3    // Prefixo customizado informado pelo usuário
};

// `hierarquia` vazia = usa o que estiver gravado em projeto.hierarquia_backup
// (ou o padrão, se não houver nada gravado).
PlanoConsolidacao planejarConsolidacao(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                        const juce::File& destino, const HierarquiaBackup& hierarquia = {},
                                        const RotuloTipoMidia& rotuloTipoMidia = {},
                                        ModoPrefixoArquivo modoPrefixo = ModoPrefixoArquivo::Mascara,
                                        const juce::String& prefixoCustomizado = {},
                                        bool autoResolverConflitos = false,
                                        bool forcarRebackup = false,
                                        // Etapa 5, só pra MAIN criado a partir desta versão:
                                        // "preservar estrutura original" ganha pasta raiz por
                                        // SOURCE (S01/…) e nome original ganha sufixo _S01.
                                        bool organizarPorSource = false,
                                        // EXPORT (etapa 6): destino volátil — ignora o que já
                                        // está registrado em algum destino (nenhum item é
                                        // "já consolidado", nenhum caminho do MAIN é herdado).
                                        bool paraExport = false,
                                        // Fase 2: folder map do usuário que alimenta PastaManual.
                                        // Vazio = sem filtro (comportamento anterior).
                                        const std::string& mapaId = {});

// Lê/grava a hierarquia escolhida pelo operador em projeto.hierarquia_backup.
HierarquiaBackup hierarquiaDoProjeto(matriz::db::Database& registro);
void gravarHierarquiaDoProjeto(matriz::db::Database& registro, const HierarquiaBackup& hierarquia);

struct ResultadoConsolidacao {
    int consolidados = 0;
    int pulados = 0; // incremental — já estavam lá, checksum batia, não foram tocados de novo
    std::vector<std::string> falhas; // "<código>: <motivo>" — nunca silencioso (§11.7 "falha vira alerta, nunca sucesso silencioso")

    // Item 10 — o operador interrompeu. O que já foi copiado E VERIFICADO
    // continua válido e registrado; retomar depois pula esses (jaConsolidado
    // no plano seguinte), sem refazer nada.
    bool cancelado = false;
    int totalPlanejado = 0; // pro resumo "X de Y processados"

    // Item 8.3 — quantas cópias receberam os marcadores como metadado
    // embutido (cue/adtl/iXML). Só WAV nesta etapa; ver MetadadoEmbutido.h.
    int arquivosComMarcadorEmbutido = 0;
};

// Chamado ANTES de cada arquivo, com quantos já foram processados e o total.
// Devolver false interrompe: o arquivo em curso é o último, e nada do que já
// foi feito é revertido.
//
// É também o gancho de progresso — não existe versão "só progresso" ou "só
// cancelamento", justamente pra não haver operação longa que reporte
// andamento sem oferecer saída (item 10: cancelar SEM EXCEÇÃO).
using AoProgredir = std::function<bool(int feito, int total)>;

// Copia cada item do plano (que não esteja em conflito nem já consolidado)
// pro destino, verifica depois (existe, tamanho bate, checksum bate), e
// registra em consolidacao_registro. Falha num item não aborta os outros —
// mesma resiliência por-item já usada na ficha em lote (item 8).
//
// Modelo SOURCE/MAIN (etapa 5): a cópia sai com os MESMOS bytes do
// original. `embutirNaCopia` (metadados + marcadores dentro do arquivo) só
// pode ser true no PRIMEIRO backup, antes de o MAIN existir — nunca em
// "Adicionar ao MAIN". `itensMarcadosWatermark` não é mais passado pelo
// backup (marca d'água só em EXPORT); fica pra quem copia pra fora do MAIN.
ResultadoConsolidacao executarConsolidacao(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                            const juce::File& destino, const PlanoConsolidacao& plano,
                                            const AoProgredir& aoProgredir = {},
                                            const std::set<std::string>& itensMarcadosWatermark = {},
                                            bool embutirNaCopia = false);

// Fase 2 — move (nunca recopia) os itens de _SEM_PASTA pra pasta nova do mapa
// do MAIN, dentro do MAIN. Não sobrescreve: destino já existente vira falha.
// Atualiza consolidacao_registro (pasta_id + caminho) e o ProjectLog. Cada
// item aparece uma vez em `movimentos` (ItemPlanejado::moverDe preenchido).
struct ResultadoMovimentos {
    int movidos = 0;
    std::vector<std::string> falhas;
    bool cancelado = false;
};
ResultadoMovimentos executarMovimentosSemPasta(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                                const juce::File& destino,
                                                const std::vector<ItemPlanejado>& movimentos,
                                                const AoProgredir& aoProgredir = {});

// EXPORT (etapa 6): recorte volátil do MAIN. Copia cada item do plano (feito
// com paraExport = true) pra `destinoExport`, SEMPRE a partir da cópia no
// MAIN/CLONE — item que só existe no SOURCE é pulado e contado. Na cópia:
// marca d'água nos ids pedidos, metadados/marcadores embutidos se
// `embutir`. Não grava consolidacao_registro nem backup_destino: não vira
// versão, não sincroniza, não conta como proteção. Só o ProjectLog registra.
struct ResultadoExport {
    int copiados = 0;
    int foraDoMain = 0;       // ainda não entraram no MAIN: não exportados
    int comMarcaDagua = 0;
    std::vector<std::string> falhas;
    bool cancelado = false;
};
ResultadoExport executarExport(matriz::db::Database& registro, const juce::File& pastaProjeto,
                               const juce::File& destinoExport, const PlanoConsolidacao& plano,
                               const AoProgredir& aoProgredir, const std::set<std::string>& itensComMarcaDagua,
                               bool embutir);

// SEM USO desde a etapa 5 (arquivo no MAIN nunca é renomeado); mantida só
// para referência — não chamar a partir do fluxo de backup.
//
// Depois que o título de um item muda (ProjetoAberto::renomearItens), o nome
// físico do arquivo já consolidado no(s) backup(s) ativo(s) fica desatualizado
// — a máscara de nomenclatura ("{codigo}-{seq:03}-{titulo}") incorpora o
// título. Esta função sincroniza: pra cada backup_destino ativo e montado
// agora, se o arquivo antigo existir, renomeia (move, nunca recopia) pro nome
// que a máscara produziria com o título novo, sem mudar de pasta.
//
// Nunca sobrescreve: se já existir um arquivo com o nome novo, pula e
// registra no ProjectLog. Backups cuja hierarquia é "estrutura original"
// preservam o nome do arquivo master e não são tocados (o título nunca fez
// parte desse nome). Destino que não está montado agora é só ignorado — sem
// fila de pendência; fica alinhado da próxima vez que rodar o Backup manual.
void sincronizarNomeDeBackupAposRenomear(matriz::db::Database& registro, const juce::File& pastaProjeto,
                                          const std::string& itemId, const std::string& tituloAntigo,
                                          const std::string& tituloNovo);

} // namespace matriz::consolidacao
