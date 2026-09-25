# HANDOFF — BKR Matriz

Estado da investigação de crash/freeze em andamento. Atualizar ao fim de
cada tarefa: branch/commit atuais, o que mudou, o que falta, decisões que
não devem ser revertidas.

## Branch atual e último commit

- Branch: `fix/crash-freeze` (criada a partir de `feature/send-to-print`
  no commit `d3b74d8`)
- Último commit: ver `git log -1 --oneline`

## O que foi corrigido nesta sessão

Commits de defeito real (não-WIP) desta sessão, mais recente primeiro:

- `e5d9f47` — `ProjetoAberto::replicarSubarvoreNoAcervo`: insere itens diretamente
  na transação ativa sem chamar `adicionarItensAPasta` (elimina erro "cannot start a
  transaction within a transaction" no selftest / Pendência Fase 2b)
- `c2f97ec` — `ProjectLoadingModalDialog`: corrige nome da tabela de 'items' para 'item'
  no warm-up de índices (Item 8)
- `dea3ec6` — `SyncEngine`: proteção do Backup contra sobrescrita de destino com
  `projetoId` divergente em `escanearEComparar` e `espelharDestinosAutomatico` (Item 7)
- `4edeffa` — `MainComponent::catalogWorkspace_->aoItemAlterado`: atualiza apenas em
  memória no mosaico para `itemId` pontual, evitando reload geral e rajada de I/O de
  disco no `backupWorkspace` (Item 6)
- `10c18ae` — Docs: atualiza HANDOFF com commits 759dbd2/7cf8da3 (Fase 3a/3b/3c) e estado do Item 4
- `7cf8da3` — `CatalogWorkspaceComponent::atualizarContagens` reutiliza
  `itensTodos_` já em memória no Mosaico (cópia na message thread antes do
  job), evita segunda `listarItens()` para o mesmo evento (Fase 3c)
- `759dbd2` — `listarItensDeProjeto` sem N+1 queries (LEFT JOIN arquivo+vault,
  statements preparados uma vez, `json_extract`/`json_valid` em SQL, offline
  via `itensOfflineCache_`); `IntakeWorkspaceComponent::recarregar` em
  background com throttle/geração/SafePointer (Fase 3a/3b)
- `8dd5709` — Batch engine do Catalog (aplicarCampoAgora/aplicarGeoAgora/
  desfazer em FichaPanelComponent) grava numa única transação (Fase 2b,
  parte 3)
- `8194f28` — Source Medium/Content/Geo Location do Intake também usam
  batch em transação única (Fase 2b, parte 2) — Source Medium é o campo
  do freeze originalmente diagnosticado
- `f2f6461` — `ProjetoAberto::salvarMetadadoEmLote()`: N itens × M campos
  numa transação, um evento amplo no fim, fora da message thread (Fase 2b)
- `03d47da` — `busca_fts_map`: DELETE na FTS por rowid em vez de
  `item_id = ? AND conteudo = ?` (varredura completa do índice) — causa
  raiz do freeze ao editar metadado (Fase 2a)
- `46053f7` — Mutex (`marcacoesMutex_`) protegendo os 4 sets de marcação
  e `inMemoryRelinkedPaths_` em `ProjetoAberto` — race confirmado sob
  TSan (Fase 1c)
- `28e3b27` — Opção `MATRIZ_TSAN` no CMake, mesmo padrão do `MATRIZ_ASAN`
- `d837c6a` — 8 UAFs de `this` bruto em `callAsync`/`Timer::callAfterDelay`
  convertidos pra `SafePointer`/cópia segura (SendToPrintDialog,
  ExportZipDialog, BatchWatermarkDialog, SyncDestinationDialog,
  DuplicatesWorkspaceComponent, RescanProgressModalDialog, ProgressoGlobal)
  (Fase 1d)
- `8f040a5` — `crashHandler` (Main.cpp) não varre memória crua num signal
  handler; usa `backtrace_symbols_fd`; desabilitado sob build com
  sanitizer (Fase 1a)
- `52df5cf` — Modal de ingest preso perto de 100% com lotes sobrepostos
  (finalização de lote / total do modal desatualizado)
- `033aacb` — Modal "Loading Catalog" preso em 0% após edição durante
  reload (`atualizarItemEmMemoria` invalidava snapshot sem completar a
  tarefa `catalog_assets`)
- `3f3fb27` — Import de nomes/tags no People Picker só pega a lista
  salva (`collection_person`), não tags soltas nem metadado
- `05d533b` — `TarefaGlobalModalDialog`: `setSize()` no construtor
  chamado antes dos labels existirem (crash no construtor)

Commits `WIP(...)` no histórico são checkpoints de trabalho que já
estava no working tree antes desta sessão (features de outra pessoa/
sessão anterior, nunca commitadas) — commitados como estavam, sem
reautoria, pra não misturar com os fixes acima. Ver mensagem de cada um
pro que cobre.

## O que está em andamento / o que falta

Plano original em 3 fases (diagnóstico de crash/freeze/lentidão):

- **Fase 1 (crash — heap corruption)**: a, c, d completos. b (ASan) coberto
  pelos self-tests automatizados (`matriz_selftest`, `matriz_ingest_selftest`,
  `matriz_analytics_selftest`, `--selftest-uitest`, `--selftest-modal-loop`
  — todos limpos sob ASan e TSan). **Falta**: o fluxo manual descrito no
  pedido original (abrir catálogo ~300 itens, batch assignment, marcações
  H/K/P/W/E rápidas, trocar filtro durante reload) nunca foi exercitado
  numa sessão real — não há automação de GUI disponível pra isso neste
  ambiente. Rodar manualmente com a build ASan (`build-asan/`) antes de
  considerar a Fase 1 fechada. Item (e) — `runDispatchLoopUntil` — só o
  uso real (ProgressoGlobal) foi removido; os ~9 usos em
  BackupWorkspaceComponent/ConsolidacaoDialogo ficam como estão, por
  decisão do usuário (ver Decisões abaixo).
- **Fase 2 (freeze de edição de metadado)**: a, b e pendência de transação aninhada
  **completos**. Selftests de UI passando 100% no teste de hierarquia.
- **Fase 3 (CPU/N+1)**: 3a, 3b, 3c **concluídas** (commits `759dbd2` e `7cf8da3`).
- **Item 4 (Freeze no fim do ingest)**: **concluído** (resolvido na Fase 3b com IntakeWorkspace em background).
- **Item 6 (Reload completo em ações simples)**: **concluído** (commit `4edeffa`).
- **Item 7 (Proteção do Backup - SyncEngine)**: **concluído** (commit `dea3ec6`).
- **Item 8 (Barras de progresso / query 'items')**: **concluído** (commit `c2f97ec`).

Fora do plano de 3 fases, pedido e depois cancelado pelo usuário nesta
sessão: feature de "relink em lote por busca em pasta" (varrer pasta,
casar por nome+tamanho+hash SHA-256/MD5). Não implementada — só
investigada (AssetRelinkEngine, resolverCaminho, schema de `arquivo`,
etc.). Se retomar, ler o pedido original na thread desta sessão antes de
desenhar — tem 7 requisitos numerados específicos.

## Decisões tomadas que não devem ser revertidas

- **`ProgressoGlobal::notificarListeners()` não chama mais
  `runDispatchLoopUntil()`** (Source/Ui/ProgressoGlobal.cpp). Causava
  reentrância confirmada sob ASan: chamado de dentro de cadeias de
  `Timer::callAfterDelay` (finalização de ingest), o loop aninhado
  reentrava no mesmo `LambdaInvoker` da pilha e causava
  heap-use-after-free por double-destruction. Não reintroduzir sem
  resolver a reentrância por outro meio.
- **`MosaicoComponent::recarregar()` sempre chama `concluirTarefa(
  "catalog_assets", ...)`**, mesmo quando a geração do snapshot ficou
  obsoleta (`atualizarItemEmMemoria()` mudou `geracaoSnapshot_` no meio
  do carregamento). Sem isso o modal "Loading Catalog" trava
  indefinidamente. Ver commit `033aacb`.
- **`MainComponent::timerCallback()` só descarta `loteEmCurso_`/
  `estadoLoteAtual_` quando `finalizarUnidadeDeLote()` retorna `true`**
  (finalização de fato aconteceu). Ver commit `52df5cf`.
- **`ProjetoAberto::marcacoesMutex_`** protege `marcadosHtml_/Zip_/
  Print_/Watermark_` e `inMemoryRelinkedPaths_` — lidos em background
  (threads MatrizSnapshot/MatrizContagens) e escritos na message thread.
  Qualquer novo leitor/escritor desses 5 membros precisa segurar o lock
  (ver comentário no membro, ProjetoAberto.h). `obterConjuntoMarcacao()`
  é privada e NÃO tranca sozinha — quem chama já precisa segurar o lock.
- **`busca_fts_map(fts_rowid, item_id, conteudo)`** espelha cada linha
  de `busca_fts` com o rowid dela. Qualquer novo gatilho ou código que
  mexa em `busca_fts` diretamente (INSERT/DELETE) precisa manter o mapa
  em sincronia (INSERT no mapa junto com `last_insert_rowid()`; DELETE
  por rowid via lookup no mapa, nunca `DELETE FROM busca_fts WHERE
  item_id = ? AND conteudo = ?` direto — isso volta a ser uma varredura
  completa do índice).
- **`salvarMetadado` de 1 item continua síncrono, na message thread**
  (decisão deliberada, não uma omissão — ver Fase 2c acima). Não mover
  pra background sem antes auditar as ~20 chamadas em
  `FichaPanelComponent.cpp`, em especial `desfazer()`
  (~linha 5140), que intercala `salvarMetadado` com escrita SQL crua na
  mesma conexão logo em seguida — mover só o `salvarMetadado` pra
  background ali quebraria a ordem de gravação.
- **`BackupWorkspaceComponent`/`ConsolidacaoDialogo` mantêm
  `runDispatchLoopUntil()`** deliberadamente (padrão documentado no
  próprio código: mantém o botão Cancelar responsivo durante cópia/
  consolidação síncrona). Removê-los sem mais nada mataria essa resposta;
  o fix "correto" (mover pra thread de fundo) é uma mudança maior, fora
  do escopo desta sessão.
- **`crashHandler` (Main.cpp) fica desabilitado sob `MATRIZ_SANITIZER_BUILD`**
  (`__has_feature(address_sanitizer) || __has_feature(thread_sanitizer)`)
  — não interferir com os handlers do próprio sanitizer.

## Regras do projeto (seguir daqui pra frente)

- **Um commit por defeito**, mensagem descrevendo causa → arquivo:linha
  → fix. Nunca misturar um fix com WIP pré-existente no mesmo commit —
  se um arquivo já tinha mudanças não commitadas antes da tarefa, separar
  em commit(s) `WIP(...)` primeiro, depois o commit do fix isolado.
- **Sem refatoração ampla, sem mudar UI, sem renomear** fora do que o
  defeito exige. Mudança mínima e cirúrgica.
- **EventBus/`juce::ListenerList` não é thread-safe.** Qualquer código
  que possa rodar em background e precise disparar
  `EventBus::dispararItemAlterado(...)` deve envolver a chamada em
  `juce::MessageManager::callAsync(...)` — nunca chamar direto de uma
  thread que não seja a message thread.
- **Callbacks assíncronos (`callAsync`/`Timer::callAfterDelay`) capturam
  `juce::Component::SafePointer<T>`, nunca `this` bruto**, quando o
  objeto pode ser destruído antes do callback disparar. Ver os 7 arquivos
  corrigidos no commit `d837c6a` como referência do padrão.
- **Sanitizers antes de features novas em área sensível.** Qualquer
  mudança que mexa em `inMemoryRelinkedPaths_`, nos sets de marcação de
  `ProjetoAberto`, ou em código de callback assíncrono deve rodar limpo
  sob ASan e TSan (`build-asan/`, `build-tsan/` — ver `MATRIZ_ASAN`/
  `MATRIZ_TSAN` no CMakeLists.txt) antes de ser considerada pronta.
- **Batch assignment de N itens = uma transação (`BEGIN IMMEDIATE`/
  `COMMIT`), nunca N transações implícitas separadas.** Ver
  `ProjetoAberto::salvarMetadadoEmLote()` como padrão pra "mesmo campo,
  mesmo valor, N itens"; onde a lógica por item é condicional demais pra
  generalizar (ex.: `FichaPanelComponent::aplicarCampoAgora`), pelo menos
  envolver o loop existente numa transação.
