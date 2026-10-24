# Auditoria do UNDO — 2026-10-24

Leitura de código, sem mudanças. O app tem uma pilha de Undo (`ProjetoAberto::registrarUndo`, até 100 passos). Só entra
nela o que chama `registrarUndo`/`iniciarGrupoUndo` (direta ou indiretamente, ex. `salvarMetadado`). Tudo abaixo grava no
banco **sem** entrar na pilha: o Undo não desfaz e, pior, desfaz a ação ANTERIOR que estava na pilha.

## Situação (2026-10-24, fim do dia)

Corrigidos (um commit `Fix(Undo #N)` cada): 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 18, 20, 21, 22, 23, 24, 26,
27, 28 — com testes no `--selftest-lote` ("UNDO audit").

Fora, de propósito:
- **#14** ano padrão: gravado sozinho ao abrir a ficha, "sem marcar o item como editado e sem entrar no Undo" (decisão
  registrada no código, `FichaPanelComponent`, EVENT DATE).
- **#17** mover cards no mapa Original: o Original é calculado na hora a partir dos caminhos e não tem pastas no banco
  (`acervo_pasta` vazio para ele) — mover não grava nada, não há o que desfazer.
- **#19** zerar posições: migração automática ao abrir um mapa gravado por uma versão antiga, não é ação do usuário.
- **#6 Reload File**: o Undo devolve o que o banco tinha (tamanho, cache, derivada), mas o arquivo no disco é o que ele é.

Pendentes: #25 (ingest, decisão), F (backup/configuração, decisão) e H (defeitos do mecanismo).
Decisão do usuário: Clear Metadata continua mantendo os campos Dublin Core.

Já cobertos (têm Undo): campos da ficha via `salvarMetadado`/`salvarMetadadoEmLote` (individual, lote, Intake), tags,
tipo de mídia, renomear, Send to Grid, Reject (R em lote), remover do projeto/backup, Folder Map (criar/renomear/apagar
mapa, criar/renomear/mover/apagar/mesclar pasta, ativar, cor, mover e redimensionar cards no mapa não-Original,
desconectar), auto-organização/propostas, capa do NEST, resolver duplicatas (retrato do merge), GEO LOCATION
(corrigido hoje: ficha, lote, Intake, GET em lote).

## A. METADATA / grade — edições do dia a dia (prioridade alta)

| # | Ação do usuário | Onde grava | Risco |
|---|---|---|---|
| 1 | Marcas H, K, P, W, B, R (atalho ou menu da grade) | `alternarMarcacao`, `definirMarcacao` | alto — uso constante |
| 2 | E (marcado revisado) e "limpar E" pela legenda | `alternarMarcadoRevisado`, `limparMarcadoRevisadoDe` | alto |
| 3 | **Clear Metadata (C)** | `redefinirMetadadosItens` (AcoesItem) | **crítico — apaga a ficha sem volta** |
| 4 | Other metadata em lote (menu de contexto) | `gravarOutraMetadataEmLote` | alto |
| 5 | Definir / remover capa do item | `definirCapa`, `removerCapa` | médio |
| 6 | Reload File / Replace File | `recarregarOuSubstituirArquivo` | médio (Replace troca o arquivo atual) |
| 7 | Confirmar sugestão da IA | `confirmarSugestao` | médio |
| 8 | Observações (adicionar/editar/apagar) — ficha, Audio, Timeline | `adicionar/atualizar/removerObservacao` | médio |
| 9 | Preview: Versão escolhida / Etapa de produção | `definirCampoRaiz` | médio |
| 10 | Apagar uma pessoa da lista de PESSOAS do projeto | `removerPessoa` | alto (some de todos os itens?) |
| 11 | Notas em lote: o campo "máquina" gravado junto | SQL direto em `FichaPanelComponent` (lote, notas) | baixo — as notas voltam, a "máquina" não |
| 12 | Agrupar itens em nova pasta | `agruparItensEmNovaPasta` | médio |
| 13 | Salvar / apagar coleção inteligente | `salvarColecao`, `apagarColecao` | médio (apagar não volta) |
| 14 | Ano padrão preenchido ao abrir a ficha | `preencherAnoPadraoSeVazio` | baixo (automático) |

## B. Folder Map / Structure

| # | Ação | Onde grava | Risco |
|---|---|---|---|
| 15 | Mover para a Trash / restaurar da Trash / extrair para o canvas | `moverItensParaTrash`, `restaurarItensDaTrash`, `extrairItensParaCanvas` | alto |
| 16 | Remover item de uma pasta (janela da pasta) | `removerItemDaPasta` | alto |
| 17 | Mover / redimensionar cards no mapa **Original** | `atualizarPosicaoPastaAcervo` (Undo só fora do Original) | médio |
| 18 | Replicar subárvore no acervo | `replicarSubarvoreNoAcervo` | médio |
| 19 | Reorganizar posições (zera x/y do mapa) | SQL direto em `ArvoreBackupComponent:1359` | médio |

## C. Intake / containers

| # | Ação | Onde grava | Risco |
|---|---|---|---|
| 20 | Ligar/desligar a marca R (item a item) | `alternarMarcaR` | médio (o "Reject" em lote tem Undo) |
| 21 | Remover do container | `removerDoContainer` | alto |
| 22 | Marcar como Phone Dump | `definirOrigemPhoneDump` | médio |
| 23 | Container/NEST: criar, trocar tipo, nome, ícone, capa, capa de frame de vídeo, remover capa, questionário | `criarContainerDeTipo`, `trocarTipoDoContainer`, `definirNome/Icone/Capa/QuestionarioDoContainer`, `removerCapaDoContainer`, `definirCapaDoContainerComFrameDeVideo`, `definirContentSeVazio` | médio |
| 24 | **Apagar mensagem da conversa do WhatsApp** | `apagarMensagemDaConversa` | **alto — apaga sem volta** |
| 25 | Ingest/importação (pacote, container e NEST da importação, "Substituir" no Intake) | `aplicarPacoteIngerido`, `criarContainerImportacao`, `criarNestDeImportacao`, SQL direto em `MainComponent:3390` | decisão: o caminho de volta hoje é "Remove from project" (que tem Undo) |

## D. Duplicates

| # | Ação | Onde grava | Risco |
|---|---|---|---|
| 26 | Restaurar duplicatas resolvidas | `restaurarDuplicatas` | médio |
| 27 | Mudar o estado de um item no painel de duplicatas | `atualizarEstadoItem` | médio |

## E. Catálogo de coleções (Catalog Hub)

| # | Ação | Onde grava |
|---|---|---|
| 28 | Linkar / desvincular / relocar coleção | `linkarColecao`, `desvincularColecao`, `relocarColecaoLink` |

## F. Backup e configuração (decisão: Undo ou só confirmação?)

Mexem em configuração/estrutura de backup, não na ficha: config do MAIN e do rascunho, destinos de backup
(ativar/desativar/mudar caminho), código do vault, "permitir sobrescrever master", definir MAIN, entrar no modo de
edição do MAIN, rebuild (destino/concluir), converter MAIN em Folder Map, sessão de impressão. Proposta: os que só
mudam configuração ganham Undo; os que mexem em arquivos no disco (rebuild, converter MAIN) ficam com confirmação
explícita, porque o Undo do banco não desfaz cópia de arquivo.

## G. Não são ação do usuário (não precisam de Undo)

Gravações internas/automáticas: fila de marcações, miniaturas, propostas de pastas, pasta Trash criada sob demanda,
histórico de autocomplete, índices técnicos (pHash, checksums da reconciliação), `salvar()`, e as chamadas internas de
funções que já têm Undo (`gravarTipoMidia`, `restaurarTiposMidia`, `sanitizarDuplicata`, `inserirItemPastaInterno`).

## H. Defeitos no próprio mecanismo de Undo

1. **A pilha só existe na memória**: fechar o projeto (ou o app) perde todo o Undo.
2. **Limite de 100 passos**: o 101º empurra o mais antigo para fora, sem aviso.
3. **Botão UNDO do lote da ficha é código morto**: `undoSnapshot_` nunca é preenchido e o botão nunca aparece.
4. **Undo vindo de outra thread** (Intake em lote, merge) entra pela message thread depois e pode cair dentro de um
   grupo aberto por outra ação naquele momento — os dois passam a ser desfeitos juntos.
5. **Undo sem nada para desfazer da ação atual desfaz a anterior** (consequência geral de tudo acima): o usuário não
   tem como saber que o passo desfeito não é o que ele acabou de fazer, exceto pelo título da janela por 1,5 s.
