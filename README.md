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
Source/I18n/            strings de interface (Strings.h/.cpp sobre i18n/pt_BR.yaml, embutido no binário)
Source/Ui/              app principal: MainWindow/MainComponent (layout de 3 painéis), MosaicoComponent
                        (virtualizado), FichaPanelComponent (ficha genérica sobre FichaDefinition),
                        diálogos de novo/abrir projeto e configurações, design tokens (Tokens.h)
i18n/                   pt_BR.yaml — strings de interface (§0.6)
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
tem dois modos ocultos de verificação headless: `--selftest-mosaico-10k` (benchmark de
virtualização do mosaico contra 10 mil itens sintéticos, `Source/Ui/MosaicoStressTest.cpp`)
e `--selftest-ingerir-arquivos` (ingere mídia sintética via `MainComponent` de verdade —
não um atalho — e confere item/arquivo/checksum/leitura técnica no banco,
`Source/Ui/IngerirArquivosTest.cpp`).

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
**Também não verificado nesta máquina:** a aparência visual pixel a pixel do app —
o ambiente de desenvolvimento não tem permissão de Gravação de Tela (TCC) concedida,
então `screencapture` só captura o papel de parede, nunca o conteúdo de nenhuma janela
de nenhum app (confirmado testando contra um app BKR já publicado, que mostra o mesmo
comportamento). O app builda, abre, e a barra de menu nativa aparece com as strings
traduzidas corretas — verificado — mas o layout interno da janela não foi visto.

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
- [ ] Etapa 4 — Índice e IA leve
- [ ] Etapa 5 — Captura de áudio
- [ ] Etapa 6 — Vídeo e imagem
- [ ] Etapa 7 — Transcrição e áudio falado
- [ ] Etapa 8 — Rostos, mapa e linha do tempo
- [ ] Etapa 9 — Nuvem, integridade e exports
- [ ] Etapa 10 — Acabamento
