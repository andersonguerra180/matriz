# MATRIZ

Software desktop (macOS e Windows) para o ciclo completo de **digitalizar,
catalogar, indexar, navegar e entregar** coleções de mídia — sonora, visual e
documental. Atende dois usos no mesmo software: acervo/preservação (arquivista,
casa de transferência, museu, rádio) e catálogo comercial (selo fonográfico,
produtor, distribuidora).

A especificação completa do produto está em [`SPEC.md`](SPEC.md). Este README
cobre o estado atual da implementação.

## Princípios não-negociáveis

1. **Dois arquivos por padrão** — em projeto de preservação, o master entra
   flat, com checksum, e é travado contra sobrescrita (destravável, com aviso
   único). Correção sempre vira derivada separada, com receita logada.
2. **A IA nunca escreve no material nem na ficha** — modelos escrevem só no
   índice (separado, descartável, reconstruível). Nada de IA jamais toca o
   registro.
3. **IA propõe, humano assina** — todo dado de modelo carrega modelo, versão e
   data, e só vira decisão registrada quando o operador confirma.
4. **Tudo roda local** — nenhum byte sai da máquina sem comando explícito.
5. **Projeto é portátil** — uma pasta no disco com os arquivos e o banco.
   Copiou pro HD externo, abriu em outra máquina, funciona.

Um sexto princípio, que a implementação impôs na prática: **nunca destrutivo**.
Marcador, ficha, organização e hierarquia de backup são registro no banco — o
arquivo original nunca é reescrito. Metadado embutido (cue/iXML) vai na *cópia*
de backup, e um teste garante que o original continua byte a byte idêntico.

## Preservação Digital (OAIS / PREMIS / FAIR)

O sistema implementa uma camada completa de preservação digital patrimonial sem reescrever a arquitetura existente nem alterar os arquivos originais:

1. **Identidade Permanente Aditiva (Persistent Identifier)**:
   - Adiciona um identificador permanente e único (`persistent_id`) no formato `BKR:ASSET:<HEX>` (ex: `BKR:ASSET:8F72A91C`) à tabela `item`.
   - Preserva o UUID v4 técnico (`item.id`) como chave interna, criando uma camada interoperável sem substituir a chave estável.
2. **Schema & Estrutura PREMIS (`schema/registro.sql`)**:
   - `preservation_agent`: cataloga agentes do sistema (`bkr-agent-sistema`, `bkr-agent-ffprobe`, `bkr-agent-exiv2`, operador).
   - `preservation_event`: log auditável append-only de eventos com `event_type`, `event_date_time`, `event_outcome`, `outcome_detail` e `agent_id`.
   - `preservation_right`: gestão de direitos autorais (`PUBLIC_DOMAIN`, `COPYRIGHT`, `LICENSED`, `RESTRICTED`, `UNKNOWN`).
   - View `asset_preservation_status`: apuração automática do estado de preservação (IDENTITY, FIXITY, FORMAT, BACKUP, RIGHTS, PROVENANCE) por item. O `backup_status` exige confirmação estrita de evento PREMIS `BACKUP_VERIFIED` com status `SUCCESS`.
3. **Módulo C++ de Preservação (`Source/Preservation/Preservation.h / .cpp`)**:
   - `verificarFixity`: checagem assíncrona de integridade SHA-256 em background (somente leitura) que registra eventos PREMIS `FIXITY_CHECK` e atualiza a data de verificação.
   - Pacotes de Exportação DIP/FAIR em **JSON** estruturado e **CSV** com escaping correto.
4. **Hooks de Ingest e Consolidação**:
   - Ingestão (`IngestArquivo.cpp`): registra automaticamente eventos `INGEST`, `FIXITY_CALCULATED`, `FORMAT_IDENTIFIED` e `METADATA_EXTRACTED`.
   - Consolidação (`Consolidacao.cpp`): calcula o SHA-256 da cópia no Vault e registra eventos `BACKUP_CREATED` e `BACKUP_VERIFIED` (`SUCCESS` / `FAILURE`).
5. **Dashboard e Interface (ANALYTICS & Ficha)**:
   - `PreservationWorkspaceComponent`: apuração em tempo real de 12 métricas de saúde do acervo e banner de integridade (`ARCHIVE HEALTH STATUS: GOOD / WARNING / CRITICAL`).
   - `FichaPanelComponent`: seções de ficha `PRESERVATION` (com Persistent ID copyable e botão *"Verify Integrity"*), `RIGHTS` e `EVENT HISTORY`.

## Stack

C++20 / [JUCE](https://juce.com) via CMake, seguindo a convenção das outras
casas BKR. Banco de registro e de índice são SQLite separados dentro da pasta
do projeto (`registro.sqlite`, `indice.sqlite`).

**Regra de dependência:** nada entra por Homebrew/apt/vcpkg — toda dependência
externa (SQLite, yaml-cpp, zlib, Exiv2) é buscada via `FetchContent` e compilada
junto com o projeto (`cmake/Dependencies.cmake`). As únicas duas ferramentas
externas invocadas como subprocesso são `ffmpeg`/`ffprobe`; tudo o mais roda
in-process. Ver a nota completa em `SPEC.md` §13.

Loudness EBU R128 (LUFS-I/LRA) é implementado no projeto
(`Source/Ingest/Loudness.cpp`) em vez de vir de biblioteca: libebur128 não tem
caminho de FetchContent viável pros dois sistemas, e o algoritmo (filtro K +
gating de BS.1770-4) é curto o suficiente pra ser escrito e validado contra a
referência de 1 kHz da própria norma.

## Estrutura

```
schema/                 registro.sql (projeto/item/arquivo/marcador/observação/assunto/histórico/
                        consolidação) e indice.sql (embeddings/transcrição/OCR/rostos/fingerprints)
fichas/                 19 definições YAML de ficha — DESCOBERTAS em tempo de execução, sem lista
                        de ids em código: cada arquivo declara tipo/rotulo/modos/ordem/icone
docs/                   formato-ficha.md — especificação normativa do YAML de ficha
cmake/                  Dependencies.cmake — todo FetchContent (sqlite3, yaml-cpp, zlib, Exiv2)
Source/Ficha/           validador/carregador de definição de ficha sobre yaml-cpp; CatalogoDeFichas
                        (descoberta em runtime), FichaI18n (tradução de rótulo por chave estável),
                        OrigemPadrao (default digital/analógico por tipo de mídia)
Source/Db/              wrapper fino sobre sqlite3
Source/Model/           Project — criação/abertura de projeto portátil, migração aditiva de coluna
Source/Ingest/          motor de ingestão: checksum, leitura técnica (ffprobe + Exiv2), loudness
                        EBU R128, miniatura/forma de onda, duplicata (pHash/fingerprint), corte de
                        banda, classificador fala x música, inferência de estrutura de pasta, fluxo
                        de ficha em lote, painel de inconsistências
Source/Consolidacao/    backup: hierarquia de pastas configurável, máscara de nomenclatura,
                        verificação por checksum, metadado embutido na cópia (cue/adtl/iXML)
Source/Preservation/    camada de preservação digital (OAIS/PREMIS/FAIR): verificação de fixity
                        SHA-256 em background, pacotes de exportação DIP (JSON/CSV), gestão de direitos
Source/Catalogo/        catálogo de proxies — consultar o backup sem o volume original conectado
Source/App/             preferências de nível de aplicativo (idioma, projetos recentes) e
                        cancelamento cooperativo de operação longa
Source/I18n/            strings de interface (i18n/en.yaml e i18n/pt_BR.yaml embutidos no binário;
                        carregar() troca de locale em tempo real)
Source/Ui/              app principal — ver seção "Interface" abaixo
i18n/                   en.yaml (padrão) e pt_BR.yaml, incluindo a tradução de TODOS os rótulos
                        das 19 fichas (seções ficha_tipos/grupos/campos/opcoes/alertas)
tools/selftest/         self-test headless da fundação
tools/ingest_selftest/  self-test headless do motor de ingestão/backup, sobre mídia sintética
```

### Interface (`Source/Ui/`)

| Componente | O que é |
|---|---|
| `MainWindow` / `MainComponent` | janela, menu nativo (File/Edit/Preferences), layout, drag-and-drop na janela inteira, Cmd+Z/S/Shift+S |
| `MosaicoComponent` | grade virtualizada; agrupa por tipo de mídia, artista/lançamento ou **ano** |
| `ArvoreComponent` | árvore EXPLORER (origem em disco, intocada) / BACKUP (estrutura virtual) |
| `ArvoreBackupComponent` | TREE workspace n8n-style: grafo de nós, zoom/pan/minimap, edição inline, conexão por arrasto |
| `CatalogWorkspaceComponent` | sidebar com contagens + grid + ficha; RECENTLY INGESTED com timer de refresh |
| `FichaPanelComponent` | ficha genérica sobre `FichaDefinition`; Apply funcional (single/batch), undo, "Needs review" |
| `FiltrosComponent` | chips de tipo/estado/extensão/**origem**, faixa de ano, coleções inteligentes |
| `NavegadorArquivos*` | navegador estilo Finder embutido (colunas/lista/ícones, ADD TO BACKUP) |
| `IngestWizardComponent` | diálogo pré-ingest: pasta destino, flatten, auto-classificação |
| `TimelineComponent` | timeline de editor: onda, cursor, zoom, régua/timecode, transporte, jog/shuttle, marcadores |
| `BarraMetricasComponent` | barra fixa no rodapé: LUFS-I, LRA, FPS, VU e formato, adaptando-se ao tipo |
| `ConsolidacaoDialogo` | backup: hierarquia de níveis arrastáveis com prévia da árvore ao vivo |
| `BackupWorkspaceComponent` | workspace de consolidação e exportação de backup/catálogo limpo e focado |
| `StorageWorkspaceComponent` | STORAGE workspace: detecção de hardware IOKit (Source e Backup Drives), inventário de discos e histórico de ingestão/backup |
| `CatalogoComponent` | consulta ao catálogo de proxies sem o volume original |
| `ProjetoAberto` | estado central do projeto aberto: undo stack (10 níveis), leitura/gravação de metadado, tags |
| `Tokens.h` | design tokens (BKR Dark é o padrão; nenhuma cor literal fora deste arquivo) |

## Build

Requer JUCE clonado em `~/JUCE` (ou passe `-DJUCE_DIR=/caminho/pro/JUCE`). O resto das
dependências é buscado automaticamente pelo CMake na primeira configuração — a build
inicial demora (compila SQLite, yaml-cpp, zlib e Exiv2 do zero), as seguintes são
incrementais e rápidas.

`ffmpeg`/`ffprobe` continuam externos (subprocesso, não linkados): em build de produção
o app espera os binários ao lado do executável (empacotamento é Etapa 10); os alvos de
self-test definem `MATRIZ_DEV_BUILD`, que libera fallback pro PATH — por isso `ffmpeg`
precisa estar instalado e no PATH pra rodar os self-tests localmente.

```bash
cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES=x86_64   # ou arm64, conforme a máquina
cmake --build build -j 8
./build/matriz_selftest_artefacts/matriz_selftest
./build/matriz_ingest_selftest_artefacts/matriz_ingest_selftest
"build/matriz_artefacts/BKR Matriz.app/Contents/MacOS/BKR Matriz" --selftest-uitest
"build/matriz_artefacts/BKR Matriz.app/Contents/MacOS/BKR Matriz" --selftest-modal-loop
"build/matriz_artefacts/BKR Matriz.app/Contents/MacOS/BKR Matriz" --selftest-mosaico-10k
open "build/matriz_artefacts/BKR Matriz.app"
```

Quatro suítes, todas verdes:

- **`matriz_selftest`** — carrega e valida as 19 definições de ficha (descobertas do
  diretório, não de lista fixa), confirma que todo grupo de toda ficha tem tradução em
  inglês, cria projeto de teste, exercita a trava de imutabilidade do master (P1) e a
  persistência ao reabrir (P5), e a troca de locale em tempo real.
- **`matriz_ingest_selftest`** — gera mídia sintética com ffmpeg e roda o pipeline
  completo: leitura técnica, checksum, loudness (validado contra a referência de 1 kHz de
  BS.1770 — mono a −20 dBFS mede −20,0 LUFS, e o mesmo sinal em estéreo mede +3 dB, que é
  a soma de potência da norma), miniatura/forma de onda, pHash, fingerprint de áudio, corte
  de banda, classificador fala×música, EXIF real via Exiv2, inferência de pasta, ingest
  real, ficha em lote, painel de inconsistências, hierarquia de backup, consolidação
  incremental/cancelável, catálogo de proxies, e o metadado embutido na cópia **com prova
  de que o original fica byte a byte idêntico**.
  Cobre também, desde a reconstrução em leva única: o **cache de análise** (I2 — LUFS-I
  conferido contra o `ebur128` do ffmpeg dentro de 0,1 LU, critério 9), a **reconciliação
  de Vault** (§8 — mover, alterar, apagar e reconectar volume, com a ficha e o histórico
  seguindo o asset), as **coleções inteligentes** (§10 — views SQL que se atualizam
  sozinhas) e a **publicação** (§9 — pacote `.matriz` com `manifest.txt` conferível por
  `shasum -c`, releitura de cada byte gravado, e recusa explícita de publicar com o Vault
  desconectado).
- **`--selftest-uitest`** — harness de UI headless (ver abaixo). Inclui a Estação de
  Escuta carregando **offline** em 1 ms (critério 2) e os atalhos 1–9 categorizando 200
  itens de uma vez (critério 14).
- **`--selftest-modal-loop`** — 500 ciclos de abrir/fechar diálogo na message thread
  (§3, critério 21). Feito pra rodar sob AddressSanitizer:

  ```bash
  cmake -B build-asan -DMATRIZ_ASAN=ON
  cmake --build build-asan --target matriz -j 8
  "build-asan/matriz_artefacts/BKR Matriz.app/Contents/MacOS/BKR Matriz" --selftest-modal-loop
  ```

### Como o critério 3 é medido (e duas medições erradas antes dela)

`--selftest-ingerir-arquivos` ingere 5.000 arquivos e mede se a interface congelou. Chegar
na medida certa custou três tentativas, e vale registrar porque as duas primeiras me
levaram a conclusões erradas:

1. **Cronometrar em volta de `runDispatchLoopUntil()`** acusou picos de 18 s. Mede também
   o tempo em que a thread não foi *escalonada* — com 6 workers de ingest e milhares de
   subprocessos de ffprobe disputando CPU, isso não é a janela travada, é o sistema
   dividindo a máquina.
2. **Atraso de `juce::Timer`** acusou travamentos de exatamente 20 s, a cada 20 s —
   enquanto o loop de mensagens demonstravelmente girava 3.160 vezes no mesmo período. O
   Timer do JUCE depende de uma thread interna que também disputa CPU; num laço de
   bombeamento apertado ela deixa de disparar sem que a interface esteja travada.
3. **Latência de entrega de `callAsync`** — o caminho que o handler de um botão percorre.
   Se o callback demora, a interface demorou. É a medida que ficou: **232 ms no pior caso**
   ao longo dos 5.000 arquivos.

O caminho até lá encontrou defeitos reais, cada um medido com `sample(1)` e com o próprio
`perf.log` em vez de adivinhado:

- `Project::modo()` fazia um `SELECT` a cada chamada, no caminho de reordenar a grade.
  Com o ingest concorrente, cada `prepare()` esperava o mutex da conexão SQLite. O modo não
  muda depois da criação — agora é lido uma vez (lote de 5.000: 207 s → 151 s).
- `verificarArquivosNoDisco` mantinha um statement SQLite **aberto** enquanto calculava
  SHA-256 de milhares de arquivos, e a conexão é uma só, compartilhada com a message
  thread.
- O ingest postava um `callAsync` por arquivo: 5.000 idas à message thread, drenadas em
  rajadas. Virou um contador atômico consultado por timer a 10 Hz.
- Árvore, filtros, painel de inconsistências e barra de métricas rodavam agregações e
  varreduras de disco na message thread. Todos foram pra background, com guarda contra
  empilhar jobs.
- O `MessageLoopMonitor` era um `static` local: um `juce::Timer` destruído na destruição de
  estáticos, depois de o MessageManager já ter morrido — crash no fim do processo.

O app tem outros dois modos de verificação headless: `--selftest-mosaico-10k` (benchmark
de virtualização contra **100 mil** itens, critério 18) e `--selftest-ingerir-arquivos`
(ingest via `MainComponent` de verdade, não um atalho).

### Harness de UI headless (`--selftest-uitest`)

`juce::Component::createComponentSnapshot()` renderiza qualquer tela pra uma imagem em
memória sem passar pelo compositor do sistema — não depende de permissão de Gravação de
Tela (essa permissão só falta pro `screencapture`, não pro JUCE). O harness usa isso pra
renderizar a tela inicial (nos dois idiomas), os diálogos, a janela principal nos dois
modos, o mosaico com e sem itens, as **19** fichas, o painel de inconsistências, o
navegador de arquivos e o catálogo — 51 PNGs em `test-output/` — e verifica
automaticamente que nenhum componente tem tamanho zero ou sai dos limites do pai (exceto
conteúdo rolável dentro de `Viewport`, que legitimamente extrapola). Interação real (foco
de teclado, `filesDropped`) usa uma janela real fora da tela (`Component::addToDesktop`)
— é uma janela do próprio processo, não inspeciona nem controla outro app, então também
não precisa de permissão de Acessibilidade.

Este harness já pagou por si várias vezes: pegou a ficha descartando dado digitado, o
drag-and-drop cobrindo só uma coluna da janela, a visão em lista do navegador mantendo as
colunas ancestrais na tela, e os rótulos de ficha aparecendo em português com locale=en.
Nenhum desses aparecia em teste de lógica.

**O crash de `AlertWindow` foi resolvido, não contornado.** O modo de falha era: com um
`AlertWindow` modal ainda vivo, o macOS eventualmente tenta compor a janela de verdade
(NSView/CoreAnimation) e crasha dentro de `juce::AlertWindow::paint()` ->
`Component::getName()` — inclusive depois de `exitModalState()`. A causa é o modelo de
janela: o `AlertWindow` cria um **peer nativo próprio** e entra num loop modal do sistema,
e nenhuma ordem de destruição do lado do JUCE fecha isso por completo.

A solução do §3 tem duas partes:

1. **`PainelOverlay`** (`Source/Ui/OverlayComponent.h`) — diálogo como Component filho
   comum do `MainComponent`, sem peer, sem loop modal, sem janela nativa. Pintar é a
   mesma pintura de qualquer painel; fechar é remover um filho. O seletor de tipo de mídia
   (justamente o diálogo do crash) já usa isto.
2. **`retirarPeerDaTela()`** (`Source/Ui/ModalMitigacao.h`) — pros `AlertWindow` que
   sobraram, todo callback modal começa tirando o peer da tela, antes de ler qualquer
   campo. Sem peer não há quem repinte.

`--selftest-modal-loop` sob AddressSanitizer é a prova: 500 ciclos de abrir/confirmar/
cancelar/descartar, bombeando mensagens com o painel aberto E depois de fechado, mais o
caso de abrir um overlay por cima de outro e o de destruir a janela com diálogo vivo.

## Como se usa (fluxo real)

1. **Tela inicial** — escolhe ARCHIVE (material variado) ou CATALOG (música), ou abre um
   projeto recente. Novo projeto de Archive pede só nome e pasta de backup; o campo de
   registrante ISRC só aparece no modo Catalog, onde faz sentido.
2. **Entrar material** — arrastar arquivos/pastas em qualquer lugar da janela (sem
   diálogo, sem pergunta: aparece na grade na hora, com checksum e leitura técnica em
   background), ou **Browse files…** pra abrir o navegador estilo Finder embutido, navegar
   em colunas, ver contagem e tamanho da seleção, e clicar ADD TO BACKUP — que fecha a
   janela sozinho. Adicionar pasta traz a subárvore inteira por padrão; achatar é escolha
   explícita. O navegador não move, não renomeia e não apaga nada.
3. **Classificar** — clicar no item e escolher o tipo de mídia. Mudou de ideia depois?
   **Change type** no topo da ficha reabre o seletor. O campo de origem
   (Digital/Analógico) já vem preenchido pelo tipo (fita/cassete/vinil nascem analógicos;
   sample/release nascem digitais), e o ano fica em destaque no topo — os dois são
   editáveis em lote, inclusive numa seleção com tipos de mídia diferentes.
4. **Achar** — busca por código, título, valor de ficha ou assunto; faixa de ano
   (`1978-1985`) direto na busca; chips de tipo/estado/extensão/origem; agrupamento da
   grade por tipo ou por ano; coleções inteligentes que guardam a *definição* da busca,
   não o resultado.
5. **Ouvir e marcar** — clicar num áudio abre a timeline de editor: forma de onda, cursor
   em tempo real, zoom até detalhe de amostra, régua em mm:ss ou timecode.
   `espaço` toca/para, START/END, jog com precisão de amostra e shuttle ±16× (roda do
   mouse; `Cmd`+roda dá zoom). `M` crava um marcador no ponto atual e abre o campo pra
   escrever o que ele indica — `Enter` fecha sem parar a reprodução. Marcador é arrastável
   e apagável. **Marcador e observação são a mesma coisa**: a timeline e a ficha leem a
   mesma tabela, então editar num aparece no outro.
6. **Backup** — escolhe destino, monta a hierarquia de pastas arrastando os níveis
   (padrão Projeto → Ano → Tipo de Mídia → Tipo de Arquivo) e vê a árvore resultante ao
   lado, atualizando a cada mudança, antes de qualquer arquivo ser copiado. Material sem o
   campo de um nível vai pra "No year"/"No origin" em vez de desaparecer. A cópia é
   verificada por checksum, é incremental (não recopia o que já está lá), é cancelável a
   qualquer momento sem perder o que já foi gravado, e leva os marcadores como metadado
   embutido (cue/adtl/iXML em WAV). Junto vai um catálogo de proxies, que permite
   consultar o backup depois sem o HD original conectado.

## Estado da implementação

Ordem de construção interna, não etapas de lançamento (§16 da especificação —
nada vai ao usuário antes do conjunto inteiro estar pronto).

- [x] **Etapa 1 — Fundação.** Schema do banco, parser/validador de definição de ficha,
      as definições YAML, imutabilidade embutida na estrutura (triggers SQLite),
      criação/abertura de projeto portátil.
- [x] **Etapa 2 — Ingest e leitura técnica.** Checksum (MD5/SHA-256), leitura técnica via
      ffprobe + Exiv2, loudness EBU R128, miniatura/keyframes/forma de onda, detecção de
      duplicata (pHash de imagem, fingerprint espectral próprio de áudio) e de corte de
      banda lossy, classificador fala×música heurístico (DSP, não ML), inferência de
      estrutura de pasta, ingest real, fluxo de ficha em lote, painel de inconsistências.
- [x] **Portabilidade.** Removidos `sips`/`mdls` (macOS-only) em favor de ffprobe + Exiv2;
      parser YAML artesanal trocado por yaml-cpp; tudo por FetchContent; ffmpeg/ffprobe
      resolvidos por caminho relativo ao executável.
- [x] **Etapa 3 — Interface principal.** Layout de três painéis, mosaico virtualizado
      (10 mil itens), árvore EXPLORER/BACKUP, ficha genérica dirigida por definição,
      barra de ferramentas e de seleção, filtros e coleções, preview, timeline de editor
      com transporte/jog/shuttle e marcadores, barra de métricas, navegador de arquivos,
      backup com hierarquia configurável, catálogo de proxies, i18n completo, tema BKR
      Dark por tokens. **Fora desta etapa, ainda:** tira de diagnóstico multi-faixa
      (espectro/fase/nível empilhados, §11.3), vocabulário controlado de assuntos (§10.1)
      e tema System CRT.
- [x] **Acréscimos de UI/catalogação (itens 1 a 9).** Tema Logic Pro dark por padrão e
      fontes +2pt; tela inicial ARCHIVE/CATALOG; novo projeto sem ISRC no modo Archive;
      navegador estilo Finder; 5 tipos de mídia novos (áudio digital, gravação de campo,
      efeito sonoro, vídeo digital, arquivo 3D) e descoberta de tipos em runtime; campo de
      origem e ano em destaque com filtro/busca/agrupamento/lote; hierarquia de pastas do
      backup; barra de métricas; transporte e jog; timeline com marcadores; observações
      multi-entrada ligadas aos marcadores.
- [x] **Reconstrução em leva única.** Preservação in-place (I5 — o original nunca é
      copiado, movido nem reescrito; a catalogação registra a referência), identidade de
      volume por UUID (DiskArbitration), cache de análise no registro (I2/I3 — miniatura,
      forma de onda, LUFS-I/LRA/true peak/correlação como BLOB e números prontos),
      Vaults com reconciliação automática ao conectar o volume e auditoria completa de
      bit rot (§8), Estação de Escuta com JKL/jog/VU/correlação/vetorscópio/espectrograma
      (§7), publicação de pacote `.matriz` autocontido com `manifest.txt` e preservation
      report (§9), coleções inteligentes como views SQL (§10), overlays internos no lugar
      dos modais nativos (§3), watchdog de 16 ms com call stack em `~/Library/Logs/MATRIZ/
      perf.log` (§0), atalhos 1–9 pra categorizar em lote, e idioma único inglês (§6).
- [x] **Catálogo, metadados e TREE (itens 22–31).** Diálogo pré-ingest com seletor de
      pasta destino e opção de achatamento; menu File completo (New/Open/Recent/Save/
      Save As/Preferences/Quit); auto-classificação de tipo de mídia por extensão na
      ingestão; painel de metadados unificado com Apply funcional (single + batch) e
      "Needs review" para campos obrigatórios vazios; UNDO operacional com até 10 níveis
      e Cmd+Z/Cmd+S/Cmd+Shift+S; RECENTLY INGESTED baseado em tempo (default 3h,
      configurável em Preferences com modo TIME ou CLEAR LIST on start, refresh
      automático a cada 60s); workspace TREE com zoom in/out (+/- keys, scroll wheel,
      range 15%–300%), pan por arrasto no canvas, minimap/navigator no canto inferior
      direito com arrasto pra navegar, edição inline de nomes de pasta por duplo-clique
      ou F2, e botões +/−/FIT na toolbar; AI Scan via Gemini API (chave em Preferences).

- [ ] Etapa 4 — Índice e IA leve
- [ ] Etapa 5 — Captura de áudio ao vivo
- [ ] Etapa 6 — Vídeo e imagem (player com fps original, pulldown, conformidade)
- [ ] Etapa 7 — Transcrição e áudio falado
- [ ] Etapa 8 — Rostos, mapa e linha do tempo
- [ ] Etapa 9 — Nuvem, integridade e exports
- [ ] Etapa 10 — Acabamento (instalador assinado, manual, migração entre versões)

## Escopo declarado (o que falta e por quê)

Coisas que a especificação pede e que **não** estão prontas — declaradas aqui em vez de
parecerem prontas:

- **Capítulos em MP4** (item 8.3). Marcador embutido funciona em WAV (cue/adtl/iXML);
  MP4 exigiria remuxar o container, operação com risco de perda que precisa de
  verificação byte a byte dedicada. O marcador continua na ficha e no relatório.
- **Duração de reprodução limitada a 45 minutos.** A Estação de Escuta reproduz a partir
  de um buffer em memória, e não streaming do disco — é o que permite shuttle reverso e
  jog com precisão de amostra sem leitura de arquivo dentro do audio callback. Acima do
  teto (`kMaxSegundosEmMemoria`), os primeiros 45 minutos carregam e o rodapé avisa; o
  resto não toca. Declarado, não truncado em silêncio.
- **LUFS-I/LRA de vídeo.** Medidos só pra áudio legível pelos formatos do JUCE; a trilha
  de um vídeo precisaria ser extraída com ffmpeg antes.
- **Contagem de páginas de PDF.** PDFium e MuPDF avaliados e descartados (sem build
  FetchContent+CMake viável nos dois sistemas) — ausente de forma explícita em vez de um
  parser artesanal que erra em silêncio.
- **Arrastar item pra uma coleção inteligente** não faz nada, por construção: uma view SQL
  guarda a pergunta, não a resposta, e "adicionar um item a uma pergunta" não quer dizer
  nada. Quem coleciona item a item é a árvore BACKUP, que recebe o mesmo arrasto. Arrastar
  pra um **Vault** planeja backup (§6) e funciona.
- **Prévia de áudio/vídeo dentro do navegador de arquivos** mostra nome/tamanho/data e
  imagem quando é imagem; gerar waveform/keyframe por arquivo selecionado dentro de um
  navegador seria caro demais.

## Verificação por plataforma

**Build e testes verificados em:** macOS (Intel, macOS 13), x86_64. As três suítes passam
com zero falhas. Os 51 PNGs do harness são todos gerados e validados por invariante
automática (nenhum componente com tamanho zero ou fora dos limites do pai); uma parte
deles — tela inicial nos dois idiomas, janela principal, diálogo de novo projeto, várias
fichas, navegador em colunas e em lista — foi também inspecionada a olho, que é como o
português nos rótulos de ficha e a visão em lista quebrada foram descobertos. Os demais
não foram olhados um por um.

**Não verificado nesta máquina:**

- **Windows.** O projeto é desenhado pra ser multiplataforma (FetchContent em vez de
  gerenciador de pacote, sem `#ifdef __APPLE__` fora da resolução de caminho de binário),
  mas não há como compilar/rodar num Windows real a partir daqui.
- **Áudio em hardware real.** O transporte, o jog e o VU são exercitados pelo harness sem
  abrir dispositivo de áudio (o dispositivo só é aberto de fato no primeiro play). O som
  saindo pela placa, a sensação do jog e a balística do VU em uso contínuo não foram
  ouvidos aqui.
- **Volume físico sendo montado/desmontado.** A reconciliação de Vault é testada com
  volumes sintéticos (pastas movidas), não com um HD externo sendo plugado. O UUID de
  volume via DiskArbitration só devolve valor pra volume montado de verdade — em pasta
  comum cai no fallback por caminho, que é o caminho exercitado pelos testes.
- **Operação contínua de 30 minutos** (critério 7). Precisa de tempo de uso real, não de
  harness.
- **Gesto de drag-and-drop do sistema operacional.** `screencapture` e
  System Events/AppleScript não têm permissão nesta máquina (Gravação de Tela e
  Automação). O pipeline de drop é testado chamando `isInterestedInFileDrag`/`filesDropped`
  direto; o arrasto físico do Finder, não.
- **Roda de jog em mouse com scroll ring.** O código trata `mouseWheelMove` (e o harness
  exercita `girarRoda` direto), mas não há mouse com scroll ring aqui pra sentir a
  resposta.
