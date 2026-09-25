# AGENTS.md — BKR Matriz

Instruções persistentes para qualquer agente (humano ou IA) trabalhando
neste repositório. Para o estado da sessão de trabalho atual (branch,
commits recentes, o que falta), ver `HANDOFF.md` — este arquivo é sobre
regras e decisões que não mudam de sessão pra sessão.

## Regras do projeto

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
  corrigidos no commit `d837c6a` (SendToPrintDialog, ExportZipDialog,
  BatchWatermarkDialog, SyncDestinationDialog, DuplicatesWorkspaceComponent,
  RescanProgressModalDialog, ProgressoGlobal) como referência do padrão.
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
- **A message thread não deveria travar em disco/banco.** Isto é um
  objetivo a perseguir, não um fato já garantido hoje — ainda há
  chamadas síncronas de DB na message thread em vários pontos (ex.:
  `salvarMetadado` de 1 item, ver decisão abaixo). Ao tocar em código que
  já faz I/O síncrono na message thread, avaliar se dá pra mover com
  segurança; nunca piorar deliberadamente.

## Decisões que não devem ser revertidas sem entender a causa raiz

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
  indefinidamente.
- **`MainComponent::timerCallback()` só descarta `loteEmCurso_`/
  `estadoLoteAtual_` quando `finalizarUnidadeDeLote()` retorna `true`**
  (finalização de fato aconteceu) — senão um lote pode ficar sem nunca
  ser finalizado.
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
  (decisão deliberada). Não mover pra background sem antes auditar as
  ~20 chamadas em `FichaPanelComponent.cpp`, em especial `desfazer()`
  (~linha 5140), que intercala `salvarMetadado` com escrita SQL crua na
  mesma conexão logo em seguida — mover só o `salvarMetadado` pra
  background ali quebraria a ordem de gravação.
- **`BackupWorkspaceComponent`/`ConsolidacaoDialogo` mantêm
  `runDispatchLoopUntil()`** deliberadamente (padrão documentado no
  próprio código: mantém o botão Cancelar responsivo durante cópia/
  consolidação síncrona). Removê-los sem mais nada mataria essa resposta;
  o fix "correto" (mover pra thread de fundo) é uma mudança maior.
- **`crashHandler` (Main.cpp) fica desabilitado sob `MATRIZ_SANITIZER_BUILD`**
  (`__has_feature(address_sanitizer) || __has_feature(thread_sanitizer)`)
  — não interferir com os handlers do próprio sanitizer.
