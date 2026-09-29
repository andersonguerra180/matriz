# HANDOFF — BKR Matriz

Estado da investigação de crash/freeze em andamento. Atualizar ao fim de
cada tarefa: branch/commit atuais, o que mudou, o que falta, decisões que
não devem ser revertidas.

## Branch atual e último commit

- Branch de trabalho: `fix/crash-freeze` (criada a partir de
  `feature/send-to-print` no commit `d3b74d8`).
- **Integrada na `main` em 2026-09-26 por fast-forward** (a `main` estava em
  `c43ce67`, ancestral direto — sem merge commit). Inclui também os commits
  `WIP(...)` herdados da `feature/send-to-print`. Novas correções: continuar na
  `fix/crash-freeze` (ou branch nova a partir da `main`) e integrar do mesmo jeito.
- Último commit: ver `git log -1 --oneline`.

## Baseline dos self-tests (não confundir com regressão)

- `--selftest-lote`: verde (ASan/TSan/Release).
- `--selftest-ingerir-arquivos`: pré-existentes — audio/image com `tipo_midia=NULL`;
  "ingerir de novo soma mais um item (3)"; "pasta expande recursivamente (4)";
  "re-arrastar ignorou os do Intake" (15 esperado, 18–22 obtido, não-determinístico);
  "catalog mode shows the inconsistency panel"; "5.000 itens na grade durante o
  processamento" (0); sob sanitizer o lote de 5.000 bate no teto de 600 s do teste
  (≈3.800 códigos) e "processed stays valid after cancelling" falha junto. O freeze
  >1 s do lote foi corrigido.
- `matriz_ingest_selftest`: "an origin level with no value becomes No origin"
  (rótulo virou "No source medium" em `e9fdbc4`).
- `--selftest-uitest`: 41 FAIL em `docs/uitest_fails_2026-09-26.txt`; trava no
  teardown final (matar depois da última linha "ETAPA 3").
- Rodar sempre gravando em arquivo (`script -q <arq> <bin> --selftest-...`), nunca
  pipe direto pra grep; nesta máquina (i7 dual-core) cada suíte leva 10–25 min sob
  sanitizer. lldb anexa com Developer Mode ligado; senão, `sample`.

## O que foi corrigido nesta sessão

### Modelo SOURCE / MAIN / CLONE / EXPORT (em andamento, uma etapa por vez)

Especificação e respostas do usuário na conversa de 2026-09-26. Decisões:
MAIN identificado por destination_id (papel ORIGINAL), nunca por caminho;
marca d'água só em EXPORT; ao fim de "Adicionar ao MAIN" perguntar
"Sincronizar clones agora?" (adições diretas, remoções sempre pendentes com
confirmação); duplicatas descartadas vão pra `_lixeira` do MAIN (reversível,
"Esvaziar lixeira", fora do catálogo e dos clones) sem perder proveniência;
SOURCE por volume com registro de cada ingestão (volume reaparecendo com
conteúdo diferente = SOURCE novo); remover o checkbox por destino (fbe2685)
na etapa 4; MAIN organizado por SOURCE (pasta raiz fixa por SOURCE, código
S01…), organização por ano/evento só no EXPORT.

- **Etapa 1 (auditoria)** — feita.
- **Etapa 2 (resolvedor)** — `05ba14a`: `resolverArquivo(...,
  Preferencia)` MAIN → CLONE → SOURCE → origem → legado;
  `ResolvedorEmLote`; `consolidacao_registro.destino_id`; consumidores de
  leitura no MAIN, de ORIGEM mantidos (nome original, mtime, tamanho,
  painel de inconsistências). Teste `1303a06`. `e2ec2b0`: SUBSTITUIR do
  Intake não move mais o original do SOURCE.
- **Etapa 3 (papéis/versões)** — `ee3cbcd`: `normalizarPapelMain`/
  `definirMain`/`listarVersoes` em ProjetoAberto; pergunta única ao abrir
  quando não há MAIN claro; lista de Versões com selo MAIN/CLONE/SOURCE e
  status; carga em background. SOURCE = tabela `vault` (volume). O registro
  de cada ingestão por SOURCE e "volume reaparecendo com outro conteúdo =
  SOURCE novo" ficam para a etapa 4 (entrada de fonte nova).
- **Etapa 4** — MAIN = sempre a pasta do projeto; backup só com a linha
  MAIN destacada; FAZER BACKUP → ADICIONAR AO MAIN; arquivo já copiado
  mantém o caminho registrado (nada no MAIN é renomeado/movido; cópia sumida
  volta no mesmo caminho); aviso de SOURCE liberado/dependentes; checkbox
  por destino removido; ao fim, "Sincronizar clones agora?" só com adições.
  Decisões do usuário (2026-09-26): estrutura/nomes livres no 1º backup e
  travados depois; pasta raiz por SOURCE só no modo "preservar estrutura
  original"; sufixo `_S01` (auto ou custom, único, caracteres seguros,
  editável até o 1º backup daquele SOURCE) só quando a máscara mantém os
  nomes originais. → etapa 5 (junto das travas).
- Pendências conhecidas para as próximas etapas: `executarConsolidacao`
  embute metadados/marcadores/marca d'água em toda cópia (etapa 5/6);
  `embutirMetadadosNoBackup` reescreve a Media inteira (etapa 5);
  espelhamento automático move pra `_lixeira` do clone sem confirmar
  (etapa 7); `sincronizarNomeDeBackupAposRenomear` renomeia no backup
  (etapa 5).
- **Duplicates "sanitizar"** (feito antes da etapa 5, a pedido do usuário;
  substitui a etapa 9 original) — validar um par e escolher um lado:
  o outro vira estado `'duplicata'` (não entra no ADICIONAR AO MAIN), NADA
  é apagado (banco, disco, SOURCE), o arquivo continua só no SOURCE. O que
  o descartado tinha de diferente é somado no mantido (campo vazio
  preenchido; divergentes, nome, localização e notas do descartado por
  append nas notas; união de tags/assuntos; observações copiadas). Evento
  PREMIS VALIDATION + entrada "Duplicates Resolved" no log.md. Notas nunca
  mais são sobrescritas (Keep Both/Dismiss também fazem append). Keep Both
  não tira ninguém do backup. Rotina: `ProjetoAberto::sanitizarDuplicata`.
  Em aberto (usuário não respondeu): descartado que JÁ tem cópia no MAIN
  — hoje fica intocada e isso vai pro log.
- **Etapa 5** (`81988e0`, `23b5c7f`, `a93dcb7`) — cópia pro MAIN byte a byte
  (`executarConsolidacao(..., embutirNaCopia)` só no 1º backup; marca d'água
  nunca no MAIN; o passe `embutirMetadadosNoBackup` que reescrevia a Media
  inteira saiu). `projeto.backup_config_main` guarda estrutura/nomes/prefixo/
  `por_source` do 1º backup e a Configuração fica travada ("Set in the first
  backup"); embed e "Forçar backup completo" somem com MAIN. MAPA: renomear/
  mover/apagar pasta com arquivo no MAIN e importar estrutura/carregar preset
  bloqueados (criar pasta livre). Renomear item não renomeia mais no MAIN.
  SOURCE: `vault.codigo` (S01… pela 1ª ingestão ou custom — letras sem acento,
  números, hífen, único; editável na linha SOURCE até o 1º arquivo dele entrar
  no MAIN, depois fixo); `organizarPorSource` (só MAIN criado a partir desta
  versão): estrutura original ganha pasta raiz S01/, nome original ganha
  sufixo `_S01`. Ingestões por SOURCE = dias distintos (linha SOURCE). Volume
  que reaparece sem nenhum dos arquivos já ingeridos = vault antigo aposentado
  + SOURCE novo (1x por vault por sessão, sob mutex).
- **Etapa 6** (`142b0e8`, `e326fae`) — EXPORT...: recorte volátil sempre a partir
  do MAIN/CLONE (item só no SOURCE é pulado e contado), estrutura/nomes/embed/
  marca d'água livres, nada registrado (não é versão nem proteção), só log.
  "Exportar planilha de metadados".
- **Fix `a9a5e1d`**: AlertWindow/showMessageBoxAsync com callback nulo roda
  `runModalLoop` síncrono (JUCE_MODAL_LOOPS_PERMITTED) — código novo sempre
  passa `ModalCallbackFunction::create([](int){})`. O padrão antigo com nullptr
  continua espalhado pelo app (não mexido).
- **Etapa 7** (`9580012`, `cfe4a19`) — Versões: CLONAR (MAIN: destino CLONE +
  Media/Project; SOURCE: cópia bruta + `checksums.sha256`, tabela
  `source_clone`), SINCRONIZAR clone (origem fixa; remoções só confirmadas, pra
  `_lixeira` do clone), PROMOVER A MAIN (papéis + consolidacao_registro no banco
  aberto E no do clone + destination.json; depois abrir o projeto pelo clone),
  aviso "Clone X desatualizado desde DD/MM" ao abrir.
- **Etapa 8** (`0b9186a`, `5246870`) — sidecars `arquivo.ext.xmp` (XMP padrão
  RDF/XML escrito à mão: o Exiv2 da build tem `EXIV2_ENABLE_XMP OFF`).
  ATUALIZAR SIDECARS com MAIN; `sidecar_registro` (SHA-256) — editado por fora
  ou não escrito pelo Matriz nunca é sobrescrito em silêncio (Importar /
  Sobrescrever / Deixar). Sincronizar clone atualiza antes; EXPORT grava
  sidecar onde o formato não aceita embed. Ingest lê o .xmp do cliente
  (`IMG.xmp`/`IMG.CR2.xmp`) e não o transforma em item.
  ATENÇÃO: com XMP desligado no Exiv2, o embed `Xmp.dc.*` antigo nunca gravou
  nada; só o EXIF (ImageDescription/Artist) funciona. `embutirMetadadosEmItens`
  (sem chamador) gravaria no ORIGINAL — não usar.
- **Etapa 9** (`c7ec88b`, `32c8efa`) — Duplicates: critérios rápidos "manter a
  ingestão mais recente" / "manter a primeira no backup"; sem backup ou empate
  = decisão manual sinalizada. Resolução = sanitizar.
- **Etapa 10** (`c348dfd`, `84be30f`) — INTAKE: Enter = APPLY nos popups de lote,
  Esc fecha, autocomplete confirma a sugestão no 1º Enter, vazio não aplica,
  CONTENT só depois de uma escolha.
- uitest (2026-09-26, após etapa 6): mesma baseline de 41 FAIL; "within parent
  bounds …/13|14/18/0" aparecem como /15|16/ (índice de filho). "Custom
  prefix" adaptado (planeja como export).
- Self-test ASan: o binário atual é
  `build-asan/matriz_artefacts/Debug/BKR Matriz.app` — o
  `build-asan/matriz_artefacts/BKR Matriz.app` (sem Debug/) é de agosto,
  não tem `--selftest-lote` e fica parado no loop se rodado.

### Sessão 2026-09-28 (abertura travada, banco de 965 MB)

- Abrir projeto travava: `contarItens()` era `listarItens().size()` (stat de
  cada arquivo na message thread; origem no Google Drive preso). Agora COUNT.
- "2011 a 2013": registro.sqlite 965 MB -> 166 MB com
  `--compactar-registro` (EXIF inteiro + miniatura em blob; arquivo também
  estava fragmentado no HD: 12 MB/s x 78 MB/s). Original guardado em
  `Project/registro.antes-compactacao-20260928-065819.sqlite`; 11.890
  itens/arquivos, quick_check ok, as 741 notas de usuário idênticas.
  Os outros projetos (ex.: "2022 a 2026") ainda não foram compactados.
- Novo: GET EXIF (ficha, ao lado de + ADD NOTE; menu da grade).

### Sessão 2026-09-27 (pós-etapa 10: listas de pedidos do usuário)

Tudo em `fix/crash-freeze`, local (não pushado). Principais pontos:
- **Metadata:** LISTA igual à do INTAKE, paginada (50/100/200/500),
  ordenação por coluna vale pra lista INTEIRA (vira um grupo só "All
  files"), "Buscar em" (escopo da busca), seleção só soma com Cmd.
  Pasta vinda do Folder Map ("Show content in grid") fica em
  `CatalogWorkspaceComponent::filtroHerdadoIds_` e sobrevive a
  `aplicarFiltrosAdicionais()` (salvar um campo); cai com HOME/busca/outra
  categoria.
- **Autocomplete da ficha:** clicar numa sugestão derrubava o app
  (referência pendurada à PopupRow destruída) — a linha copia valor e
  callback antes; SafePointer no editor.
- **Áudio:** um só `AudioDeviceManager` do app
  (`Source/Audio/DispositivoAudioApp`), aberto com a escolha salva em
  Preferences > Audio Device (`audio_dispositivo` nas preferências).
  Timeline, Preview e AudioWorkspace usam esse. Antes cada um abria a
  saída PADRÃO do macOS (aqui: HDMI da TV).
- **Vídeo .mov:** `Source/Audio/FormatoAudioQuickTime` lê o áudio via
  ExtAudioFileOpenURL (o CoreAudioFormat do JUCE abre por callbacks e o
  CoreAudio recusa QuickTime assim). Fixture: `tools/fixtures/video_aac.mov`.
  Sync vídeo x timeline: tolerância 350 ms tocando, 40 ms parado.
- **Backup:** PUBLISH/EXPORT/planilha/SYNC travados sem MAIN.
- **Storage:** selos MAIN/CLONE/SOURCE nos cards; Google Drive pergunta a
  conta (`GoogleDriveContas.h`).
- **Duplicates:** sub-card 168 px com miniatura na altura toda
  (`kAlturaSubCard`).
- Snapshots pra conferir a olho: `test-output/metadata_lista.png`,
  `test-output/duplicatas_card.png`.

### Sessão 2026-09-26 (EVENT DATE x DATE CREATED)

- `05ce298` — batch EVENT DATE do Intake: só o ano e igual ao ano do
  DATE CREATED original -> dc_created mantido; senão atualiza (como antes).
  `salvarMetadadoEmLote(..., pular)` pula pares (item, coluna) na mesma
  transação/Undo. Teste no --selftest-lote. Ainda não está na `main`.

### Sessão 2026-09-26 (Geo Location do Intake)

- `c5404f9` — popup GEO LOCATION do Intake: "★ Add to Favorites" (mesmo
  GeoFavoritosRepository da ficha) e autocomplete "Used locations in this
  project" (asset_geolocation, mais usadas primeiro; preenche os 5 campos).
- `50a8d5d` — corrida no E: snapshot em voo lido antes do E trazia a flag
  antiga; agora pede recarregar após ele.
- `be66bee` — testes. Estes 3 commits ainda NÃO estão na `main` do GitHub.

### Sessão 2026-09-26 (miniatura no Intake)

- `107ba6a` — JPG recém-ingerido sem miniatura no grid do Intake: o cache
  negativo `noThumbnail_` nunca era limpo e o card chega antes da
  miniatura (exposto pelo recarregar em background de 759dbd2). Agora é
  esquecido a cada snapshot do Intake. Teste em `6ddd9ec`.

### Sessão 2026-09-26 (SUBJECT + Show Recently Ingested)

- `c1fd7e6` — dropdown SUBJECT no card CONTENT TYPE (METADATA).
  `ItemResumo::subject` = item.dc_subject (lido no snapshot); valores com
  , ou ; viram subjects separados.
- `bdb1722` — "Show Recently Ingested": leva persistida em
  `item.lote_grid_id` (confirmarLoteGrid); `ultimosItensIngeridos()` lê a
  leva mais recente do banco; filtro espera o snapshot conter a leva; leva
  vazia = grade vazia + "No recent intake batch"; contagens restritas.
  O estado LIGADO do botão não é persistido — ao reabrir o projeto, ligar
  de novo mostra a mesma leva.
- `9b51f6f` — testes no --selftest-lote.
- `c585694` — **crash** ao fechar/trocar projeto: jobs de `poolVaults_`
  com `ProjetoAberto*` cru sobreviviam ao `removeAllJobs(true, 2000)` e
  usavam o banco já fechado (SIGSEGV em sqlite3_prepare_v2 no Release).
  Agora `esperarJobsDeVaults()` espera de verdade. Candidato forte a uma
  das fontes da heap corruption original (.ips).

### Sessão 2026-09-26 (lista de 7 correções)

- `81dd172` 1 — popup Source Medium do Intake sem "(BATCH)".
- `30dbeac` 2 — contagens da sidebar do Catalog: recontar em
  limparTodosOsFiltros/aplicarFiltroAno/definirSelecaoItens e quando
  `MosaicoComponent::versaoSnapshot()` muda.
- `fbe2685` 3 — Backup: checkbox por destino (sessão). ATENÇÃO: o
  espelhamento MAIN→clones é espelho real (faltou no MAIN → `_lixeira` do
  clone); com MAIN desmarcado NÃO espelha e consolida direto em cada
  destino marcado. Não há teste automático — testar à mão.
- `699a571` 4 — aba STRUCTURE abre no FOLDER MAP (catálogo: Storage/SpaceMap).
- `c3b4e48` 5 — tecla E: `obterItemResumo` não preenchia marcadoRevisado;
  branch próprio no Mosaico; selo E na float window + EventBus.
- `d24ec6f` 6 — `somenteBarra` em ProgressoGlobal: catalog_assets/
  catalog_view/hub_open só na barra inferior.
- `ad21f82` 7 — `consolidacao_registro.destino_path` era gravado SEMPRE
  vazio; agora grava a pasta Media do destino e o jaConsolidado filtra por
  ela (fallback: registros legados vazios contam pra qualquer destino).
  Clique num destino recalcula o plano antes do scan.
- `c67c6b3` testes (--selftest-lote: E e contagens; ingest selftest: por destino).
- matriz_ingest_selftest: FAIL pré-existente "an origin level with no value
  becomes \"No origin\"" (rótulo virou "No source medium" em e9fdbc4).

### Sessão 2026-09-26 (regressão "Source Medium em lote só muda 1 item")

- `63575c9` — **Causa**: botão "Select All" do Catalog só disparava
  `aoMudarSelecao`; a ficha continuava em modo individual no item clicado
  antes → qualquer campo "em lote" gravava só nele. Agora chama
  `selecionarItem({})` (ficha em lote), como o Cmd+A. Os suspeitos
  f2f6461/4edeffa/8dd5709 foram testados e gravam N itens corretamente.
- `ee52cd4` — `--selftest-lote`: 12 itens no Catalog (clique + Select All;
  Creator/Subject/Event Date/Content/Source Medium/Geo; Cmd+Z) e no Intake
  (mesmos campos; desfazer). Confere banco, tela sem reabrir e undo.
  ASan e TSan: ALL TESTS PASSED, 0 races.
- `e28d905` — Undo do `salvarMetadadoEmLote` era registrado na thread de
  background do Intake (race em `pilhaUndo_` + `aoMudarUndo` fora da
  message thread). Agora via callAsync + `vivo_` (weak_ptr).
- Nota: no lote do Catalog o botão "Undo" próprio (`botaoDesfazer_`) nunca
  aparece no modo tempo-real; o desfazer é o Cmd+Z global. O check do
  uitest "the Undo button appears after a successful apply" está obsoleto.

### Sessão 2026-09-25/26 (Claude — estabilização, pedido em 7 itens)

Mais recente primeiro:

- `738dcf6` — fase de checagem de duplicatas (job antes do lote) agora
  conta em `ingestEmAndamento()`; cancelar nessa fase descarta o lote.
  Antes: fechar projeto permitido, cancelar ignorado, lote de 5.000 do
  selftest "terminava" com 0 voltas.
- `d969099` — **Item 6a**: `fichaPanel_->aoMudar` → contagens com
  debounce de 500 ms.
- `c5fad24` — teste de lotes sobrepostos conta só modais do próprio bloco.
- `81fceb5` — SendToPrintDialog: job de carregamento esperava
  MessageManagerLock enquanto o destrutor (message thread) esperava o pool
  → JUCE matava a thread à força. Agora `MessageManagerLock(ThreadPoolJob*)`.
- `af2be71` — selftests headless desligam App Nap (causa do "uitest para
  em ~5 min de CPU": processo rebaixado a PRI 4/darwin-bg; NÃO era lock).
- `5cfa222` — backfill de `tamanho_bytes` (ProjetoAberto ctor) fazia
  `getSize()` de cada arquivo DENTRO da transação (SQLITE_BUSY na conexão
  principal) e sem ROLLBACK.
- `c1b40e3` — **Item 3**: trava de conexão no `Database`: toda chamada
  passa por `Database::Trava`; em transação, a thread dona retém a trava
  até COMMIT/ROLLBACK (outras threads esperam, não são absorvidas). Rede
  de segurança: espera máx. 60 s → loga `[db] trava da conexao nao obtida`
  em stderr e segue. Ficha: `commitOuReverter()` relê itens após ROLLBACK.
- `8bc40a7` — **Item 2d**: teto de 60 s da finalização loga no perf.log.
- `2f75e39` — **Item 2c**: thread órfã da miniatura só escreve no índice
  sob `PermissaoEscrita` não revogada; timeout/skip revoga.
- `2d89cc3` — **Item 2a/b**: causa do "560 of 586": `expandirArquivosAsync`
  sobrescrevia `ingestModalDialog_` (modal anterior ficava órfão pra
  sempre); `ajustarTotalArquivos` usava jmax; lote sobreposto descartava o
  `EstadoLote` do anterior. Teste novo em IngerirArquivosTest (A=20, B=300
  durante a finalização de A). (2a — timer zerando loteEmCurso_ — já
  estava corrigido em 52df5cf.)
- `cdf4d71` — **Passo 0b (7cf8da3) + Item 5**: contagens/filtros do Catalog
  esperam o snapshot (sem `listarItens()` na message thread / sem 2ª
  query) e são refeitos quando ele chega (`aoMudarConteudoVisivel`).
- `dd95cf4` — **Passo 0b (759dbd2)**: regressão — offline vinha só do
  cache (preenchido 1x, após o 1º snapshot, nunca após relink, aplicado a
  outra coleção) e sumiu o fallback de ano pela data do arquivo.
- `e413066`, `7cce32d` — **Passo 0a**: WIP do Antigravity revisado e
  commitado (writeMutex único do Project; PainelInconsistencias espera o
  job no destrutor).

### Sessões anteriores

Commits de defeito real (não-WIP), mais recente primeiro:

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

### Pendências da sessão 2026-09-25/26

- Build Release universal (x86_64+arm64) OK em 2026-09-26:
  `build-release/matriz_artefacts/Release/BKR Matriz.app` (o cache de
  `build-release/` estava com CMAKE_BUILD_TYPE vazio = sem otimização;
  reconfigurado com `-DCMAKE_BUILD_TYPE=Release`). Não assinado/notarizado.

- **Item 1 (uitest sob ASan)**: sem erros de ASan; App Nap resolvido.
  AINDA TRAVA no teardown final (`UiSelfTest.cpp:3305`, fim do `try`):
  main thread presa em `~MosaicoComponent` → `~ThreadPool` →
  `waitForThreadToExit(500)` que nunca estoura, com TODAS as threads de
  pool ociosas em `wait(500)` e nenhuma esperando DB/transação. Não
  explicado (lldb não anexa: Developer Mode desligado). Rodar com teto de
  tempo; os checks já saíram todos antes disso.
- **uitest: 41 FAIL** idênticas em 2 rodadas (lista em
  `$TMPDIR/uitest_asan_run3.txt` na máquina). Baseline pré-sessão ainda
  não medida — muitas são cascata de "flow 1: a dropped file shows up in
  the grid" (ingest em 30 s). Comparar com build-tsan (binário antigo)
  antes de concluir se alguma é regressão.
- **Lista das 41 FAIL do uitest**: `docs/uitest_fails_2026-09-26.txt`.
  Idênticas em ASan e TSan. "flow 1" e "maquina" falham também no
  binário Debug de 25/09 20:29 (antes dos commits de ingest/DB) →
  pré-existentes; a maioria das outras é cascata do `pdfId` do flow 1.
  O uitest NÃO instancia `CatalogWorkspaceComponent` (item 5/6a não são
  cobertos por ele).
- **Item 4 (TSan)**: `--selftest-ingerir-arquivos` e `--selftest-uitest`
  sob TSan: **0 data races** (ingest 5.000 + cancelamento + lotes
  sobrepostos; batch assignment/atalhos, marcações, reload de mosaico no
  uitest). O fluxo MANUAL pedido (catálogo ~300, batch assignment,
  H/K/P/W/E rápidas, trocar filtro durante reload, ingest) continua não
  exercitado — sem automação de GUI nativa aqui. Rodar à mão com
  `build-tsan/` e anotar races.
- **Selftest ingest (TSan, 2026-09-26)**: 9 FAIL — baseline 1–7 da
  memória; #8 (freeze >1 s) CORRIGIDO (pior latência 75 ms, 54k voltas);
  5.000 bate no teto de 600 s do próprio teste sob TSan (3.769 códigos),
  e daí falha "processed stays valid after cancelling".
- **Item 6b (auditoria)**: só inventário até agora — 33
  `mosaico->recarregar()`, 27 `atualizarContagens()`, 33
  `listarItens*()`. Revisados: `CatalogWorkspace:520/532/538` (limpar
  TODAS as marcações H/K/P/W/E → reload justificado, manter). Resto não
  auditado.
- **Item 6c** (progresso real em catalog_assets/catalog_view/hub_open,
  callback por etapa em Project::abrir, setStatus via callAsync): não feito.
- `expandirArquivosAsync` ainda faz `SELECT DISTINCT caminho_absoluto_origem`
  NA message thread (pasta grande = freeze curto). Não mexido.

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
- **Trava de conexão do `Database` (c1b40e3)**: não chamar código que
  espere outra thread que use o MESMO banco enquanto houver BEGIN aberto
  (ex.: `removeAllJobs(true, ...)` de um pool que lê o registro) — vira
  espera de até 60 s. Todo BEGIN precisa de COMMIT/ROLLBACK em todos os
  caminhos (senão as outras threads esperam o teto). Ordem de lock:
  `Project::writeMutex()` → trava da conexão, nunca o contrário.
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

## Folder Maps — Fases 2 a 6 (branch `feat/folder-maps-fases-2-6`, 2026-09-29)

Uma fase por commit (Fase 1 já estava em `3507648`). Decisões que não mudam sem
entender a causa:

- **`backup_config_main.mapa_id`** guarda o ID do mapa do MAIN (nunca o nome).
  Só esse mapa fica sujeito às travas do MAIN e não pode ser apagado; os outros
  mapas do usuário são sempre livres (`ProjetoAberto::mapaDoMainId`).
- **`_SEM_PASTA`** só existe com o MAIN em folder map (planner:
  `planejarConsolidacao(..., mapaId)`); item que ganha pasta depois é OFERECIDO
  pra mover no backup seguinte (`executarMovimentosSemPasta`), nunca recopiado.
  Export por mapa exclui e conta os sem pasta.
- **`acervo_item_pasta.mapa_id`**: único caminho que escreve é
  `ProjetoAberto::inserirItemPastaInterno` (o MainEdit chama via callback
  `mapaMoverItem`).
- **MAIN EDIT MODE** = `Source/Consolidacao/MainEdit.*` (motor) +
  `ProjetoAberto::editarMain*` (background, uma por vez) + `MainEditPanel`.
  Journal `main_edit_journal` é gravado ANTES do disco; recuperação em
  `recuperarOperacoesDoMain()` ao abrir. NUNCA usar `File::moveFileTo` no MAIN:
  ele apaga o alvo antes (rename só de caixa apagaria o próprio arquivo) — o
  motor usa `std::filesystem::rename` e recusa alvo existente.
- **Clones**: `clone_move_pendente` (por clone, cadeia colapsa) é aplicada por
  `SyncEngine::aplicarMovesPendentesNoClone` antes de comparar, só se a regra de
  divergência liberou; hash só confirma, sem rehash da árvore; fallback = cópia
  normal + log.
- **Quarentena**: `<raiz do MAIN>/_QUARENTENA` (fora de Media/ e Project/, logo
  fora de scan/export/espelhamento). Linha do `consolidacao_registro` sai ao
  deletar (a saúde deixa de contar o item) e o planner não recopia arquivo
  'deletado'.
- **Clone somente leitura**: `ProjetoAberto::somenteLeitura()` (papel CLONE em
  destination.json). Guardas nos mutadores de `ProjetoAberto` + UI; NÃO usa
  `PRAGMA query_only` (thumbnails/caches escrevem no indice e derrubariam telas).
- **EXPORT unificado**: os botões antigos seguem existindo (escondidos) e a
  opção do dropdown dispara o `onClick` deles — não apagar.

Pendência conhecida: sidecar `.xmp` de arquivo deletado/substituído no MAIN não
vai junto pra quarentena (só o rename leva o sidecar).
