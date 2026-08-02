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

## Estrutura

```
schema/                registro.sql (projeto/item/arquivo/marcador/assunto/histórico/miniatura/forma_onda)
                        e indice.sql (embeddings/transcrição/OCR/rostos/fingerprints/fila de processamento)
fichas/                 14 definições YAML de ficha (uma por tipo de mídia)
docs/                   formato-ficha.md — especificação normativa do YAML de ficha
Source/Ficha/           parser de YAML-subconjunto + validador/carregador de definição
Source/Db/              wrapper fino sobre sqlite3
Source/Model/           Project — criação/abertura de projeto portátil
Source/Ingest/          motor de ingestão: checksum, leitura técnica, miniatura/forma de onda,
                        duplicata (pHash/fingerprint), corte de banda, classificador fala x música,
                        inferência de estrutura de pasta, fluxo de ficha em lote, painel de inconsistências
tools/selftest/         self-test headless da fundação (Etapa 1)
tools/ingest_selftest/  self-test headless do motor de ingestão (Etapa 2), sobre mídia sintética via ffmpeg
```

## Build

Requer JUCE clonado em `~/JUCE` (ou passe `-DJUCE_DIR=/caminho/pro/JUCE`), e `ffmpeg`/`ffprobe`
no PATH (usados pela leitura técnica e pelos self-tests). Imagem/PDF usam `sips` e um contador
de páginas próprio em vez de ExifTool — ver nota em `Source/Ingest/LeituraTecnica.h`.

```bash
cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES=x86_64   # ou arm64, conforme a máquina
cmake --build build --target matriz_selftest matriz_ingest_selftest -j 8
./build/matriz_selftest_artefacts/matriz_selftest
./build/matriz_ingest_selftest_artefacts/matriz_ingest_selftest
```

O self-test da fundação carrega e valida as 14 definições de ficha, cria um projeto de
teste, exercita a trava de imutabilidade do master (P1) e confirma que os
dados persistem ao reabrir o projeto do zero (P5). O self-test de ingestão gera
mídia sintética com ffmpeg e roda o pipeline completo: leitura técnica, checksum,
miniatura/forma de onda, pHash, fingerprint de áudio, corte de banda, classificador
fala x música, inferência de pasta, ingest real de arquivo em disco, fluxo de ficha
em lote e painel de inconsistências.

## Estado da implementação

Ordem de construção interna, não etapas de lançamento (§16 da especificação —
nada vai ao usuário antes do conjunto inteiro estar pronto).

- [x] **Etapa 1 — Fundação.** Schema do banco, parser/validador de definição
      de ficha, as 14 definições YAML, imutabilidade embutida na estrutura
      (triggers SQLite), criação/abertura de projeto portátil.
- [x] **Etapa 2 — Ingest e leitura técnica.** Checksum (MD5/SHA-256), leitura
      técnica via ffprobe/sips, miniatura/keyframes/forma de onda, detecção
      de duplicata (pHash de imagem, fingerprint espectral próprio de áudio)
      e de corte de banda lossy, classificador fala×música heurístico (DSP,
      não ML — ver nota em `Source/Ingest/ClassificadorFalaMusica.h`),
      inferência de estrutura de pasta, ingest real de arquivo pro projeto,
      fluxo de ficha em lote, painel de inconsistências (§7.4).
- [ ] Etapa 3 — Interface principal
- [ ] Etapa 4 — Índice e IA leve
- [ ] Etapa 5 — Captura de áudio
- [ ] Etapa 6 — Vídeo e imagem
- [ ] Etapa 7 — Transcrição e áudio falado
- [ ] Etapa 8 — Rostos, mapa e linha do tempo
- [ ] Etapa 9 — Nuvem, integridade e exports
- [ ] Etapa 10 — Acabamento
