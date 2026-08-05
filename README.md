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
| `MainWindow` / `MainComponent` | janela, menu nativo, layout, drag-and-drop na janela inteira |
| `MosaicoComponent` | grade virtualizada; agrupa por tipo de mídia, artista/lançamento ou **ano** |
| `ArvoreComponent` | árvore EXPLORER (origem em disco, intocada) / BACKUP (estrutura virtual) |
| `FichaPanelComponent` | ficha genérica sobre `FichaDefinition`; origem/ano em destaque, observações, edição em lote, **trocar tipo** |
| `FiltrosComponent` | chips de tipo/estado/extensão/**origem**, faixa de ano, coleções inteligentes |
| `NavegadorArquivos*` | navegador estilo Finder embutido (colunas/lista/ícones, ADD TO BACKUP) |
| `TimelineComponent` | timeline de editor: onda, cursor, zoom, régua/timecode, transporte, jog/shuttle, marcadores |
| `BarraMetricasComponent` | barra fixa no rodapé: LUFS-I, LRA, FPS, VU e formato, adaptando-se ao tipo |
| `ConsolidacaoDialogo` | backup: hierarquia de níveis arrastáveis com prévia da árvore ao vivo |
| `CatalogoComponent` | consulta ao catálogo de proxies sem o volume original |
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
open "build/matriz_artefacts/BKR Matriz.app"
```

Três suítes, todas verdes (109 + 177 + 638 verificações):

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
- **`--selftest-uitest`** — harness de UI headless (ver abaixo).

O app tem outros dois modos de verificação headless: `--selftest-mosaico-10k` (benchmark
de virtualização contra 10 mil itens) e `--selftest-ingerir-arquivos` (ingest via
`MainComponent` de verdade, não um atalho).

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

**Achado real durante a construção do harness** (não uma limitação do harness, um bug do
próprio JUCE/AlertWindow nesta configuração): deixar o `MessageManager` bombear o loop de
mensagens enquanto um `AlertWindow` modal aberto por dentro de uma `MainComponent` fora do
Desktop ainda está vivo — mesmo bem depois de `exitModalState()` — faz o macOS
eventualmente tentar compor essa janela de verdade (NSView/CoreAnimation reais) e crasha
dentro de `juce::AlertWindow::paint()` -> `Component::getName()`. O contorno usado no
harness é chamar `removeFromDesktop()` explicitamente logo depois de `exitModalState()`,
antes de qualquer novo bombeamento do loop de mensagens; a causa raiz não foi isolada.

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
- **Scrub com áudio contínuo no jog** (item 7.2). O jog reposiciona com precisão de
  amostra, mas não toca o áudio "esticado" durante o giro — isso pede resampling em tempo
  real, que o `AudioTransportSource` atual não faz. Shuttle pra trás também move a posição
  sem áudio.
- **LUFS-I/LRA de vídeo.** Medidos só pra áudio legível pelos formatos do JUCE; a trilha
  de um vídeo precisaria ser extraída com ffmpeg antes.
- **Contagem de páginas de PDF.** PDFium e MuPDF avaliados e descartados (sem build
  FetchContent+CMake viável nos dois sistemas) — ausente de forma explícita em vez de um
  parser artesanal que erra em silêncio.
- **Descrições do painel de inconsistências** ainda são strings em português escritas
  antes da externalização de UI; não passam por `i18n::t()`.
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
- **Gesto de drag-and-drop do sistema operacional.** `screencapture` e
  System Events/AppleScript não têm permissão nesta máquina (Gravação de Tela e
  Automação). O pipeline de drop é testado chamando `isInterestedInFileDrag`/`filesDropped`
  direto; o arrasto físico do Finder, não.
- **Roda de jog em mouse com scroll ring.** O código trata `mouseWheelMove` (e o harness
  exercita `girarRoda` direto), mas não há mouse com scroll ring aqui pra sentir a
  resposta.
