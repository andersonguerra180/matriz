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

## Stack

C++20 / [JUCE](https://juce.com) via CMake, seguindo a convenção das outras
casas BKR. Banco de registro e de índice são SQLite separados dentro da pasta
do projeto (`registro.sqlite`, `indice.sqlite`).

**Regra de dependência:** nada entra por Homebrew/apt/vcpkg — toda dependência
externa (SQLite, yaml-cpp, zlib, Exiv2) é buscada via `FetchContent` e compilada
junto com o projeto (`cmake/Dependencies.cmake`). As únicas duas ferramentas
externas invocadas como subprocesso são `ffmpeg`/`ffprobe`; tudo o mais roda
in-process. Ver a nota completa em `SPEC.md` §13.

## Estrutura

```
schema/                registro.sql (projeto/item/arquivo/marcador/assunto/histórico/miniatura/forma_onda)
                        e indice.sql (embeddings/transcrição/OCR/rostos/fingerprints/fila de processamento)
fichas/                 14 definições YAML de ficha (uma por tipo de mídia)
docs/                   formato-ficha.md — especificação normativa do YAML de ficha
cmake/                  Dependencies.cmake — todo FetchContent (sqlite3, yaml-cpp, zlib, Exiv2)
Source/Ficha/           validador/carregador de definição de ficha sobre yaml-cpp
Source/Db/              wrapper fino sobre sqlite3
Source/Model/           Project — criação/abertura de projeto portátil
Source/Ingest/          motor de ingestão: checksum, leitura técnica (ffprobe + Exiv2), miniatura/forma
                        de onda, duplicata (pHash/fingerprint), corte de banda, classificador fala x
                        música, inferência de estrutura de pasta, fluxo de ficha em lote, painel de
                        inconsistências, resolução de binário externo (ProcessoExterno.h)
Source/App/             preferências de nível de aplicativo (idioma, projetos recentes) — fora de
                        qualquer pasta de projeto, em ~/Library/Application Support/MATRIZ
Source/I18n/            strings de interface (Strings.h/.cpp sobre i18n/en.yaml e i18n/pt_BR.yaml,
                        embutidos no binário; carregar() pode trocar de locale em tempo real)
Source/Ui/              app principal: MainWindow/MainComponent (layout de 3 painéis), MosaicoComponent
                        (virtualizado, agrupado por modo — tipo de mídia no Archive, artista/lançamento
                        no Catalog), FichaPanelComponent (ficha genérica sobre FichaDefinition),
                        PainelInconsistenciasComponent (posição fixa na área central no modo Catalog),
                        diálogos de novo/abrir projeto e configurações, design tokens (Tokens.h)
i18n/                   en.yaml (padrão) e pt_BR.yaml (trocável em Preferências) — strings de
                        interface (§0.6, Parte 2 da correção de fluxo)
tools/selftest/         self-test headless da fundação (Etapa 1)
tools/ingest_selftest/  self-test headless do motor de ingestão (Etapa 2), sobre mídia sintética via ffmpeg
```

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
cmake --build build --target matriz_selftest matriz_ingest_selftest matriz -j 8
./build/matriz_selftest_artefacts/matriz_selftest
./build/matriz_ingest_selftest_artefacts/matriz_ingest_selftest
open "build/matriz_artefacts/MATRIZ.app"
```

O self-test da fundação carrega e valida as 14 definições de ficha, cria um projeto de
teste, exercita a trava de imutabilidade do master (P1) e confirma que os
dados persistem ao reabrir o projeto do zero (P5). O self-test de ingestão gera
mídia sintética com ffmpeg e roda o pipeline completo: leitura técnica, checksum,
miniatura/forma de onda, pHash, fingerprint de áudio, corte de banda, classificador
fala x música, EXIF real via Exiv2, inferência de pasta, ingest real de arquivo em
disco, fluxo de ficha em lote e painel de inconsistências. O app principal (`matriz`)
tem três modos ocultos de verificação headless: `--selftest-mosaico-10k` (benchmark de
virtualização do mosaico contra 10 mil itens sintéticos, `Source/Ui/MosaicoStressTest.cpp`),
`--selftest-ingerir-arquivos` (ingere mídia sintética via `MainComponent` de verdade —
não um atalho — e confere item/arquivo/checksum/leitura técnica no banco,
`Source/Ui/IngerirArquivosTest.cpp`) e `--selftest-uitest` (harness de UI headless,
`Source/Ui/UiSelfTest.cpp` — ver seção própria abaixo).

### Harness de UI headless (`--selftest-uitest`)

`juce::Component::createComponentSnapshot()` renderiza qualquer tela pra uma imagem em
memória sem passar pelo compositor do sistema — não depende de permissão de Gravação de
Tela (essa permissão só falta pro `screencapture`, não pro JUCE). `MATRIZ --selftest-uitest`
usa isso pra renderizar a tela inicial (nos dois idiomas), os diálogos de criação e de
escolha de tipo de mídia, a janela principal nos dois modos, o mosaico com e sem itens, as
14 fichas (uma por tipo de mídia) e o painel de inconsistências — tudo em
`test-output/*.png` — e verifica automaticamente que nenhum componente tem tamanho zero ou
sai dos limites do pai (exceto conteúdo rolável dentro de `Viewport`, que legitimamente
extrapola). Interação real (foco de teclado, `filesDropped`) usa uma janela real fora da
tela (`Component::addToDesktop`, posicionada fora do monitor) — é uma janela do próprio
processo MATRIZ, não inspeciona nem controla outro app, então também não precisa de
permissão de Acessibilidade.

**Achado real durante a construção do harness** (não uma limitação do harness, um bug do
próprio JUCE/AlertWindow nesta configuração): deixar o `MessageManager` bombear o loop de
mensagens enquanto um `AlertWindow` modal aberto por dentro de uma `MainComponent` fora do
Desktop ainda está vivo — mesmo bem depois de `exitModalState()` — faz o macOS
eventualmente tentar compor essa janela de verdade (NSView/CoreAnimation reais) e crasha
dentro de `juce::AlertWindow::paint()` -> `Component::getName()`. O contorno usado no
harness é chamar `removeFromDesktop()` explicitamente logo depois de `exitModalState()`,
antes de qualquer novo bombeamento do loop de mensagens; a causa raiz não foi isolada a
tempo desta correção.

**Ingerir material real:** com um projeto aberto, `Arquivo > Ingerir arquivos…` ou
arrastar arquivos (ou pastas inteiras — expandidas recursivamente) direto no mosaico.
Antes de processar, o software pergunta o tipo de mídia do lote (nunca mais adivinha
pela extensão — cada tipo tem sua própria ficha, e assumir errado forçava o operador a
responder campos que não fazem sentido pro formato real). Cada arquivo vira um item
novo, com checksum e leitura técnica automáticos (§7.2 estágio 1), rodando em background
(`juce::ThreadPool`) — a janela não trava durante o lote, e um rótulo mostra o progresso.
Fechar/trocar de projeto fica desabilitado no menu enquanto um lote está em andamento.
Ingest de pasta com inferência de estrutura completa (§7.3 — release/faixa/capa por
convenção de nome) ainda não existe; por ora cada arquivo dentro da pasta vira um item
independente do mesmo tipo escolhido.

**Build/testes verificados em:** macOS (Intel, macOS 13), arquitetura x86_64. **Não
verificado nesta máquina:** Windows — o projeto foi desenhado pra ser multiplataforma
(FetchContent em vez de gerenciador de pacote, sem `#ifdef __APPLE__` fora da resolução
de caminho de binário) mas não há como compilar/rodar num Windows real a partir daqui.
**Aparência visual:** `screencapture` continua sem funcionar nesta máquina (sem permissão
de Gravação de Tela) e System Events/AppleScript também não (sem permissão de Automação) —
mas isso só limita ferramentas de captura de tela do sistema, não o JUCE. O harness de UI
(`--selftest-uitest`, seção acima) renderiza as telas de verdade via
`createComponentSnapshot()` e o layout interno FOI visto — 23 PNGs inspecionados
diretamente, não só testados por invariante automática. Isso revelou um gap real (ver
"Estado da implementação"): os rótulos das 14 fichas (`fichas/*.yaml`) nunca passaram pelo
sistema de i18n — mesmo com locale=en, seção/campo/tipo de mídia aparecem em português.
**Também não verificado nesta máquina:** o efeito visual real da troca de idioma em
Preferências (menu Preferências > English/Português) — tentei confirmar via
automação de UI (System Events/AppleScript), mas esta sessão também não tem permissão
de Automação/Acessibilidade concedida (`osascript`: "Not authorized to send Apple
events to System Events"). O que foi verificado de outro jeito, com certeza: um
self-test novo (`tools/selftest/main.cpp`, seção "i18n") carrega os dois locales de
verdade via `matriz::i18n::carregar()`/`t()` — o mesmo código que a UI chama — e
confirma que os valores mudam corretamente entre inglês e português, inclusive
trocando de volta (não é só a primeira carga). O que não está coberto: se
`MainWindow::trocarIdioma` de fato reconstrói a janela na tela sem artefato visual.

## Estado da implementação

Ordem de construção interna, não etapas de lançamento (§16 da especificação —
nada vai ao usuário antes do conjunto inteiro estar pronto).

- [x] **Etapa 1 — Fundação.** Schema do banco, parser/validador de definição
      de ficha, as 14 definições YAML, imutabilidade embutida na estrutura
      (triggers SQLite), criação/abertura de projeto portátil.
- [x] **Etapa 2 — Ingest e leitura técnica.** Checksum (MD5/SHA-256), leitura
      técnica via ffprobe + Exiv2, miniatura/keyframes/forma de onda, detecção
      de duplicata (pHash de imagem, fingerprint espectral próprio de áudio)
      e de corte de banda lossy, classificador fala×música heurístico (DSP,
      não ML — ver nota em `Source/Ingest/ClassificadorFalaMusica.h`),
      inferência de estrutura de pasta, ingest real de arquivo pro projeto,
      fluxo de ficha em lote, painel de inconsistências (§7.4).
- [x] **Portabilidade (revisão pós-Etapa 2).** Removidos `sips`/`mdls`
      (macOS-only) em favor de ffprobe + Exiv2; parser YAML artesanal trocado
      por yaml-cpp; SQLite e todas as dependências passaram a vir por
      FetchContent (nunca Homebrew/apt/vcpkg); contador de páginas de PDF
      artesanal removido sem substituto (PDFium/MuPDF avaliados e descartados
      — sem build FetchContent+CMake viável, ver `Source/Ingest/LeituraTecnica.cpp`);
      ffmpeg/ffprobe resolvidos por caminho relativo ao executável, com
      fallback a PATH só em build de desenvolvimento. Build/testes verificados
      em macOS x86_64; Windows não verificado nesta máquina.
- [~] **Etapa 3 — Interface principal (em andamento — checkpoint B.1.1-B.1.3).**
      Janela principal com menu nativo, criação/abertura de projeto, layout de
      três painéis (§11.1), mosaico virtualizado (miniatura sob demanda em
      thread separada, cor de estado, halo de sincronizado, seleção/busca/
      ordenação/filtro, testado com 10 mil itens sintéticos), ficha lateral
      genérica (uma tela só, consome FichaDefinition em runtime — todos os
      tipos de campo, visivel_se reativo, herança, leitura técnica, sugestão
      de IA com confirmação explícita P3, níveis aninhados, arquivos
      esperados), aviso único de sobrescrita de master (P1), i18n desde a
      primeira tela. Visualizador, transporte/jog/shuttle, tira de
      diagnóstico, marcadores/vocabulário e tema System CRT ficam para depois
      do retorno do usuário (B.1.4 em diante) — parado aqui de propósito, por
      pedido explícito.
- [~] **Correção de fluxo (pós-teste real do usuário, em andamento).** Depois
      do checkpoint acima, o uso real do app revelou: ingest travava a janela,
      pasta solta era ignorada, tipo de mídia era adivinhado pela extensão
      (corrigidos — ver commit anterior), e um pedido maior de reorganização
      do fluxo (modos Archive/Catalog separados desde a primeira tela, inglês
      como idioma padrão, captura de áudio ao vivo). Concluído até agora:
      **Parte 2 — idioma.** `i18n/en.yaml` é o padrão na primeira execução
      (era `pt_BR`); português disponível no novo menu Preferências, trocado
      em tempo real (`matriz::i18n::carregar()` recarrega a tabela inteira, e
      `MainWindow` reconstrói o `MainComponent` preservando o projeto aberto —
      não existe um `retranslateUi()` por Component, então é assim que toda
      string em tela reflete o idioma novo sem reiniciar o processo).
      Preferência de idioma persiste em `~/Library/Application Support/MATRIZ`
      (nível de app, fora de qualquer pasta de projeto — P5).
      **Parte 1 — modos Archive/Catalog.** A tela inicial virou a escolha de
      modo (§1.1): dois cartões grandes (Archive/Catalog) com a descrição de
      público de cada um, "Open…" e lista de projetos recentes (nome + selo
      de modo, persistida junto com o idioma). O modo escolhido restringe os
      tipos de mídia oferecidos no ingest (Archive: os 14; Catalog: release,
      sample, fita de rolo, vinil — "faixa" é nível dentro de release.yaml,
      não tipo à parte; "master"/"stems" são papéis de arquivo, não têm ficha
      própria e não foram inventados aqui). O mosaico agora tem cabeçalho de
      seção — por tipo de mídia no Archive, por artista/lançamento no Catalog
      (lido de `release.artista_principal`/`titulo`, nível raiz — nunca de
      `item.titulo`); os grupos são sempre um intervalo contíguo, então
      `paint()`/`boundsDaCelula()`/`indiceNaPosicao()` continuam O(células
      visíveis) + O(grupos), nunca O(total de itens) — confirmado de novo no
      stress test de 10 mil itens. O painel de inconsistências
      (`Source/Ingest/PainelInconsistencias.h`, Etapa 2) ganha uma UI própria
      (`PainelInconsistenciasComponent`) com posição fixa na área central só
      no modo Catalog — no Archive essa área continua o placeholder vazio de
      B.1.4. **Gap conhecido, não escondido:** as descrições de inconsistência
      (`Inconsistencia::descricao`) são strings em português escritas na
      Etapa 2, antes da externalização de UI (§0.6/B.5) — ainda não passam
      por `i18n::t()`; corrigir exige estruturar cada tipo por parâmetros
      (tarefa em aberto, ver task tracker). Ainda faltam: **Parte 3**
      (onboarding com "New Archive/New Catalog/Open" já fluido, diálogo de
      tipo em grade de ícones com sugestão automática por conteúdo, pasta
      vira material com opção copiar/mover e organização automática em
      disco) e **Parte 4** (captura de áudio ao vivo — depende de tira de
      diagnóstico, transporte/jog/shuttle e marcadores existirem primeiro;
      domínio de tempo real diferente do resto do app, verificação completa
      exige hardware de áudio real).
- [x] **Correção crítica (pós-teste real do usuário #2).** O checkpoint acima nunca tinha
      sido de fato testado como interface — só compilado. Uso real revelou dois bugs
      graves: drag-and-drop não funcionava em lugar nenhum da janela, e a ficha descartava
      o que o operador tinha digitado. Nenhum dos dois aparecia em teste automatizado
      porque nenhum teste até então exercitava a UI de verdade, só lógica por trás dela.
      **Parte 1 — harness de UI headless** (`--selftest-uitest`,
      `Source/Ui/UiSelfTest.cpp`): renderiza telas de verdade pra PNG via
      `createComponentSnapshot()` (não depende de Gravação de Tela) e dirige interação real
      — foco de teclado, `filesDropped` — via janela off-screen própria do processo (não
      depende de Acessibilidade). 216 verificações, incluindo invariantes automáticas de
      layout em cada tela. Ver seção própria acima. **Parte 2 — ficha nunca descarta dado
      digitado.** Causa raiz real: `FichaConteudo::limpar()` destruía os widgets da ficha
      sem commitar o que estava neles — um campo em edição que nunca perdeu o foco (ex.:
      operador clica direto noutro item do mosaico) tinha o valor perdido pra sempre, nunca
      chegava ao banco. Corrigido commitando todos os campos visíveis antes de destruí-los,
      incondicional a estado de foco. Reproduzido e confirmado corrigido pelo harness
      (digita sem dar blur, troca de item, reabre — valor sobrevive). Validação de campo
      continua só um aviso visual, nunca apaga o texto digitado. **Parte 3 — drag-and-drop
      cobria só o mosaico.** Causa raiz real: só `MosaicoComponent` implementava
      `FileDragAndDropTarget`, cobrindo uma coluna de 320px — soltar em qualquer outro
      lugar da janela (a maior parte dela) não fazia nada. Corrigido movendo o alvo pra
      `MainComponent`, cobrindo a janela inteira; o mosaico só recebe o estado visual
      (`definirArrastandoArquivo`). O gesto do sistema operacional em si não dá pra simular
      nesta máquina (mesma limitação de Acessibilidade de sempre) — testado chamando
      `isInterestedInFileDrag`/o resto do pipeline direto. **Achado novo, não escondido,
      fora do escopo desta correção:** inspecionar os PNGs revelou que os rótulos das 14
      fichas (`fichas/*.yaml` — seções, campos, tipos de mídia) nunca passaram pelo sistema
      de i18n — aparecem em português mesmo com locale=en. Registrado como tarefa própria,
      não corrigido aqui (escopo grande, toca as 14 definições). Selftests existentes
      (fundação, ingest, mosaico 10k, ponte de ingest) continuam passando sem regressão.
- [ ] Etapa 4 — Índice e IA leve
- [ ] Etapa 5 — Captura de áudio
- [ ] Etapa 6 — Vídeo e imagem
- [ ] Etapa 7 — Transcrição e áudio falado
- [ ] Etapa 8 — Rostos, mapa e linha do tempo
- [ ] Etapa 9 — Nuvem, integridade e exports
- [ ] Etapa 10 — Acabamento
