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
schema/           registro.sql (projeto/item/arquivo/marcador/assunto/histórico)
                   e indice.sql (embeddings/transcrição/OCR/rostos/fingerprints)
fichas/            14 definições YAML de ficha (uma por tipo de mídia)
docs/              formato-ficha.md — especificação normativa do YAML de ficha
Source/Ficha/      parser de YAML-subconjunto + validador/carregador de definição
Source/Db/         wrapper fino sobre sqlite3
Source/Model/      Project — criação/abertura de projeto portátil
tools/selftest/    self-test headless (sem UI) da fundação
```

## Build

Requer JUCE clonado em `~/JUCE` (ou passe `-DJUCE_DIR=/caminho/pro/JUCE`).

```bash
cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES=x86_64   # ou arm64, conforme a máquina
cmake --build build --target matriz_selftest -j 8
./build/matriz_selftest_artefacts/matriz_selftest
```

O self-test carrega e valida as 14 definições de ficha, cria um projeto de
teste, exercita a trava de imutabilidade do master (P1) e confirma que os
dados persistem ao reabrir o projeto do zero (P5).

## Estado da implementação

Ordem de construção interna, não etapas de lançamento (§16 da especificação —
nada vai ao usuário antes do conjunto inteiro estar pronto).

- [x] **Etapa 1 — Fundação.** Schema do banco, parser/validador de definição
      de ficha, as 14 definições YAML, imutabilidade embutida na estrutura
      (triggers SQLite), criação/abertura de projeto portátil.
- [ ] Etapa 2 — Ingestão e leitura técnica
- [ ] Etapa 3 — Interface principal
- [ ] Etapa 4 — Índice e IA leve
- [ ] Etapa 5 — Captura de áudio
- [ ] Etapa 6 — Vídeo e imagem
- [ ] Etapa 7 — Transcrição e áudio falado
- [ ] Etapa 8 — Rostos, mapa e linha do tempo
- [ ] Etapa 9 — Nuvem, integridade e exports
- [ ] Etapa 10 — Acabamento
