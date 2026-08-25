# BKR Matriz — Histórico da sessão

**Data:** 9 de agosto de 2026
**Projeto:** `/Volumes/BUNKER 4TB/Apps/BKR Matriz`
**Branch:** `main`
**Build:** universal (x86_64 + arm64), Debug, JUCE 8.0.14, macOS 13.6.7 (Mac mini 2014)

---

## ⚠️ Estado no fim da sessão — leia antes de tudo

| Item | Situação |
|---|---|
| Correções de AI SCAN | Compiladas ✅ — **não testadas em execução** (não tenho a chave da API) |
| Correções do crash de áudio | Compiladas ✅ — não testadas em execução |
| Correção do MAKE BACKUP invisível | Compilada ✅ |
| Marcador embutido em duplicata | Compilada ✅ |
| **Redesign do layout da aba Backup** | **Escrito no código, NÃO compilado** ❌ — o build foi interrompido |
| Estrutura de pastas na ingestão | **Não começado** ❌ |

Para retomar, a primeira coisa é compilar:

```bash
cd "/Volumes/BUNKER 4TB/Apps/BKR Matriz/build" && cmake --build . --target matriz -- -j4
```

---

## Índice

1. [Auditoria inicial](#1-auditoria-inicial)
2. [Crash do AI SCAN](#2-crash-do-ai-scan-sigabrt)
3. [Quatro pedidos: AI fake, diálogo, pasta padrão, botão](#3-quatro-pedidos)
4. [AI SCAN devolvendo 0 de 1152](#4-ai-scan-devolvendo-0-de-1152)
5. [Quatro problemas persistentes](#5-quatro-problemas-persistentes)
6. [Coluna `extensao` inexistente + áudio](#6-coluna-extensao-inexistente--áudio)
7. [Reescrita completa do AI SCAN (7 defeitos)](#7-reescrita-completa-do-ai-scan)
8. [Crash SIGSEGV no MotorReproducao](#8-crash-sigsegv-no-motorreproducao)
9. [Explicação do fluxo de finalização](#9-explicação-do-fluxo-de-finalização)
10. [MAKE BACKUP invisível](#10-make-backup-invisível)
11. [Redesign da aba Backup](#11-redesign-da-aba-backup)
12. [Pendências](#12-pendências)

---

## 1. Auditoria inicial

> **Pedido:** "já adiantei umas coisas… cheque como está e continue, faça uma auditoria pra ver se tudo está funcionando como deveria"

Auditoria de todos os arquivos novos/modificados: assinaturas de API, membros do `Tema`, chaves de i18n, tabelas do schema.

**3 use-after-free encontrados e corrigidos:**

| Arquivo | Problema |
|---|---|
| `BackupWorkspaceComponent.cpp` | `FileChooser` async capturava `this` cru |
| `BackupWorkspaceComponent.cpp` | `iniciarBackup()` usava ponteiro cru através de `runDispatchLoopUntil()` |
| `CatalogWorkspaceComponent.cpp` | `abrirWorkbench()` com `callAsync` capturando `this` |

Todos convertidos para `juce::Component::SafePointer`.

---

## 2. Crash do AI SCAN (SIGABRT)

> **Pedido:** relatório de crash — `EXC_CRASH (SIGABRT)`, `abort() called`, na message thread ao clicar em AI SCAN.

**Causa:** `Database::prepare()` lança `DatabaseError`. `executarAiScan()` rodava sem `try/catch` → `std::terminate` → `abort()`.

**Correções:**
- `try/catch` em `iniciarAiScan()` com diálogo de erro
- `try/catch` por item dentro de `executarAiScan()`
- `try/catch` em volta de `createInputStream()`

---

## 3. Quatro pedidos

> **Pedido:**
> 1. AI scan é fake — busca por "palhaço" não acha nada; tem que escanear o conteúdo do arquivo (imagem, áudio, vídeo, documento), com barra de progresso, e a info tem que ficar persistente ligada ao arquivo
> 2. Janela CHOOSE FOLDER com texto enorme e clicável só em alguns lugares → "CHOOSE PROJECT DESTINATION FOLDER", clicável em toda a extensão
> 3. A pasta de destino escolhida na criação do projeto deve ser a pasta de backup por padrão
> 4. O backup não tem botão para executar; DONE não faz nada

**Alterações:**

| Arquivo | Mudança |
|---|---|
| `AiScan.cpp` | Retorna contagem de **sucessos**, não de processados |
| `CatalogWorkspaceComponent.{h,cpp}` | Barra de progresso + execução assíncrona |
| `Strings.h` | `botao_escolher_pasta` → "CHOOSE PROJECT DESTINATION FOLDER"; `campo_pasta` → "Project destination folder"; texto de ajuda encurtado |
| `NovoProjetoDialogo.cpp` | Altura da linha reduzida |
| `BackupWorkspaceComponent.cpp` | "START BACKUP" → "MAKE BACKUP"; DONE cai em `aoVoltarHome` |

⚠️ **Erro meu nesta rodada:** defini o destino padrão como a **própria pasta do projeto**, o que fez cada arquivo colidir consigo mesmo → conflito de nome → botão desabilitado. Corrigido na rodada 5.

---

## 4. AI SCAN devolvendo 0 de 1152

> **Pedido:** print — "0 of 1152 asset(s) analyzed successfully", com a chave da API configurada.

**Diagnóstico:** `URL::InputStreamOptions(ParameterHandling::inPostData)` mandava o JUCE mover o `?key=` da URL para o corpo do POST, **destruindo o JSON** e removendo a chave.

**Correção:** `inAddress` + `withParameter("key", apiKey)`.

*(Este diagnóstico estava certo quanto ao mecanismo, mas não era a causa raiz — ver rodada 5.)*

---

## 5. Quatro problemas persistentes

> **Pedido:** 1) tira o texto de ajuda e faz a janela clicável inteira; 2) o MAKE BACKUP não está lá; 3) AI SCAN não funciona; 4) embed metadata não embute nada.

**1 — Diálogo.** `LinhaFormulario::resized()` dava ao botão **toda** a área restante, e os dois labels eram posicionados manualmente **por cima dele** (y=34 e y=52), cobrindo o botão e comendo os cliques.

- `LinhaFormulario` ganhou parâmetro `alturaWidget`
- Botão com faixa própria de 40px; label do caminho abaixo, sem sobreposição
- `setInterceptsMouseClicks(false, false)` nos labels
- Texto de ajuda removido

**2 — MAKE BACKUP.** Destino padrão mudou para `<projeto>/Backup`. O resumo passou a dizer *por que* o botão está desabilitado em vez de só ficar cinza.

**3 — AI SCAN.** Causa raiz encontrada:

> `juce::MemoryBlock::toBase64Encoding()` **não é base64**. É um formato proprietário do JUCE, com o tamanho embutido e outro alfabeto. Toda imagem chegava corrompida na API.

- Trocado por `juce::Base64::toBase64()`
- Arquivos que o JUCE não decodifica (HEIC, RAW) mandavam "descreva esta imagem" **sem imagem** — daí o texto inventado. Agora sobe o arquivo original ou reporta falha
- `rescaled(512,512)` deformava tudo → 1024 no lado maior, proporção preservada
- Erros passaram a ser reportados com status HTTP e mensagem real
- Prompts pedindo `KEYWORDS:` em inglês **e** português

**4 — Embed metadata.** Lia de `consolidacao_registro`, que só é populada por um backup concluído. Como nenhum backup rodava (problema 2), iterava zero linhas.

- `MetadadoParaEmbutir` ganhou `resumoAi`
- Resumo do AI vai para `Xmp.dc.subject` e a descrição EXIF
- 🐛 Bug que eu mesmo introduzi e removi na mesma rodada: `Xmp.matriz.aiContext` usa namespace XMP não registrado, o que faz o Exiv2 lançar e abortar o embed do arquivo inteiro

---

## 6. Coluna `extensao` inexistente + áudio

> **Pedido:** print — "0 of 28: no such column: extensao"
> **Complemento no meio da execução:** "o scan tem que acontecer em qualquer tipo de arquivo, não somente imagens"

**Causa:** a tabela `arquivo` não tem coluna `extensao`. O `prepare()` lançava e o `catch(...)` mudo engolia — 28 falhas silenciosas.

**Correções:**
- Passou a usar `matriz::vault::resolverCaminho()`, o resolvedor canônico do projeto (vault → origem absoluta → pasta do projeto). Concatenar `pastaProjeto + caminho_relativo` na mão só funciona em projeto legado
- Extensão derivada do nome do arquivo
- Adicionado ramo de **áudio** (antes caía no `else` e era pulado em silêncio)
- `schema/registro.sql`: removido `CHECK (tipo_analise IN ('visual','documento','video'))` — `'audio'` violava
- `Project.cpp`: `migrarAiScanSemCheck()` reconstrói a tabela em bancos já criados com o CHECK, preservando as linhas

---

## 7. Reescrita completa do AI SCAN

> **Pedido:** documento `PROMPT-AISCAN-CLAUDE-CODE.md` com 7 defeitos diagnosticados + pendência de threading. Pedia relatar divergências antes de codificar.

### Divergências relatadas

**Defeito 5 — parcialmente incorreto.** O documento afirma que não existe nenhum `INSERT INTO busca_fts` vindo do AI Scan em lugar nenhum do código. **Existe** — como triggers SQL, o que uma busca só no C++ não acha:

```
schema/registro.sql:559  trg_ai_scan_busca_insert
schema/registro.sql:563  trg_ai_scan_busca_delete
```

`busca_fts` é FTS5 comum (`item_id UNINDEXED, conteudo`), **não** `content=` — então o delete-then-insert é seguro (era o ponto de verificação pedido).

**Defeito 2 — não verificável.** O documento afirma que `gemini-2.0-flash` foi desligado em 01/06/2026 e devolve 404, mandando migrar para `gemini-3.6-flash`. Testei o endpoint direto: o Google valida a **chave antes de resolver o modelo**, então `gemini-2.0-flash`, `gemini-3.6-flash` e um nome deliberadamente falso devolvem o mesmo `400 API_KEY_INVALID`. Não há como confirmar nenhuma das duas coisas sem chave válida.

> **Solução adotada:** `AiScanOpcoes::modelos` é uma lista ordenada — `gemini-3.6-flash`, `gemini-2.5-flash`, `gemini-2.0-flash`. É sondada uma vez antes do lote, o primeiro que não der 404 é usado, e o diálogo informa qual respondeu. Funciona independentemente de quem estava certo, e dá para trocar sem recompilar.

**Confirmados:** defeitos 1, 3, 4, 6, 7 e a pendência de threading.

### O que foi implementado

**`Source/Ingest/AiScan.h` — reescrito**
- `AiScanOpcoes` (lista de modelos, keyframes, segundos de áudio, lado da imagem, tentativas, flag de indexação)
- `AiScanFalha` (itemId, arquivo, motivo, httpStatus)
- `AiScanRelatorio` (solicitados, sucessos, ignorados, falhas, cancelado, erroFatal, modeloUsado)
- `AoProgressoScan` agora inclui o nome do arquivo atual
- `reindexarAiScanNaBusca()`
- Documentado no header: **NÃO chame da message thread**

**`Source/Ingest/AiScan.cpp` — reescrito**

| Área | Implementação |
|---|---|
| Autenticação | Chave no header `x-goog-api-key`, fora da URL (não vaza em log de proxy/crash) |
| Erros | `withStatusCode`, `error.message` do corpo, `promptFeedback.blockReason` |
| Classificação | 400/401/403/404 = fatal (aborta o lote); 429/5xx = transitório |
| Retry | Backoff 2s/6s/14s, interrompível pelo cancelamento |
| Imagem | 1024 no lado maior, proporção preservada, JPEG 0.85 |
| Áudio | Inline se ≤14MB e mime suportado; senão proxy ffmpeg mono 16kHz (libopus → aac → libmp3lame) |
| Vídeo | Keyframes reais via `gerarKeyframesVideo()` (768px) + trilha de áudio; prompt proíbe inventar o que há entre frames |
| PDF | Sobe como `application/pdf` |
| .doc/.docx/.odt | Falha explícita "converta para PDF" — melhor campo vazio que texto inventado sobre bytes binários |
| Deduplicação | Id determinístico `itemId + ":" + tipoAnalise` |
| Busca | `DELETE` do resumo anterior antes do `INSERT` em `busca_fts` |
| Temporários | `<projeto>/cache/aiscan_tmp`, apagados por item. **Original nunca é aberto para escrita** |

**`Source/Ui/CatalogWorkspaceComponent.{h,cpp}` — threading**
- `juce::ThreadPool` próprio; scan fora da message thread
- **`runDispatchLoopUntil` removido** (dispatch loop aninhado reentrando em código de componente — era candidato a SIGABRT)
- Botão CANCEL SCAN + `matriz::app::CancelamentoPtr`
- Progresso via `callAsync` com `SafePointer`
- `finalizarAiScan()` mostra sucessos, ignorados, `erroFatal` em destaque e as 5 primeiras falhas
- Destrutor pede cancelamento e espera o job

---

## 8. Crash SIGSEGV no MotorReproducao

> **Pedido:** documento com diagnóstico de `EXC_BAD_ACCESS (SIGSEGV)`, `KERN_INVALID_ADDRESS at 0x1`, em `MotorReproducao.cpp:67` (`buffersVivos_.add(novo)`).

Diagnóstico confirmado integralmente. O `callAsync` capturava `this` cru; `removeAllJobs` não cancela mensagem já postada na fila.

**`MotorReproducao.h`**
- `JUCE_DECLARE_WEAK_REFERENCEABLE(MotorReproducao)`
- `std::atomic<uint64_t> geracao_`
- `class JobCarga` + `aplicarBufferCarregado()`

**`MotorReproducao.cpp`**
- `JobCarga : juce::ThreadPoolJob` — leitura em blocos de 65536 frames, checando `shouldExit()` **e** geração entre blocos. Um `addJob(lambda)` vira `LambdaJobWrapper`, que não tem como consultar `shouldExit()`
- Só posta o `callAsync` se não foi abortado
- Destrutor: `masterReference.clear()` na **primeira linha**, retorno de `removeAllJobs` verificado com `jassertfalse`
- `carregarAsync`: `removeAllJobs(true, 0)` — não bloqueia mais a message thread

**Varredura pedida — outros `this` crus em callbacks assíncronos:**

| Local | Situação |
|---|---|
| `AudioWorkspace.cpp:630` | Corrigido com `SafePointer` |
| `MosaicoComponent::aoItemAlterado` | Corrigido com `SafePointer` — desregistrar do EventBus no destrutor não basta, um evento recebido um instante antes já postou a mensagem |
| `IngestWizardComponent.cpp:590` | **Deixado como está.** Hoje é seguro (destrutor espera com `removeAllJobs(true, 5000)` e o job não posta `callAsync`), mas tem a mesma fraqueza latente: lambda job não observa `shouldExit()`, então essa espera de 5s estoura em disco lento |

---

## 9. Explicação do fluxo de finalização

> **Pedido:** "explique detalhadamente o fluxo de finalização do trabalho, onde já importei, categorizei, pus metadados e abri a aba backup"

Ao tracejar o código para explicar, **3 problemas apareceram**:

**🐛 1 — "Verify checksum after copy (SHA-256)" não faz nada.** O checkbox é criado, estilizado e posicionado, mas o valor **nunca é lido**. A verificação roda sempre, incondicionalmente, dentro de `executarConsolidacao`. O comportamento é o correto; a caixa é decorativa.
→ **Não corrigido — decisão sua:** remover a caixa ou fazê-la valer.

**🐛 2 — Marcadores embutidos duas vezes. CORRIGIDO.** `embutirMarcadoresNoBackup` era chamado no fim de `executarConsolidacao` **e** de novo em `iniciarBackup`. Como a função anexa os chunks ao conteúdo existente, um WAV com marcador ficava com dois blocos `cue`/`LIST`/`iXML` duplicados, crescendo a cada backup.

**🐛 3 — O checksum gravado descreve o arquivo antes dos metadados.** A verificação e o registro acontecem na fase 1; a fase 3 depois **modifica** esses arquivos. O SHA-256 em `consolidacao_registro` corresponde ao conteúdo de origem, não aos bytes no destino. Não quebra o incremental, mas uma auditoria de integridade acusaria divergência em tudo que recebeu metadado.
→ **Não corrigido — muda o significado do dado.** Exigiria coluna `checksum_pos_embed` ou reordenar as fases.

**Observação:** o comentário no topo de `Consolidacao.h` afirma que metadado embutido não é gravado na cópia. Ficou desatualizado — o texto mente, o código embute.

### Fluxo documentado

**Fases do MAKE BACKUP:**

1. **Cópia e verificação** (`executarConsolidacao`), por item: pula conflitos e já-consolidados → copia a master de dentro do projeto → SHA-256 de origem e cópia comparados → grava em `consolidacao_registro` → copia capas → embute marcadores em WAV
2. **Catálogo HTML** no destino (se ligado)
3. **Metadados embutidos** (se ligado): preserva EXIF/XMP original como `item_observacao`, depois escreve título/descrição/artista/código/ano + resumo do AI via Exiv2 (imagens) ou sidecar `.xmp`
4. **Tela Done** com Copied / Verified / Failed

Em nenhuma fase o arquivo de origem é aberto para escrita, movido ou renomeado.

---

## 10. MAKE BACKUP invisível

> **Pedido:** print da aba Backup — "onde está o MAKE BACKUP?"

**Causa:** `addAndMakeVisible()` chama `setVisible(true)` internamente. Isto:

```cpp
btnDone_->setVisible(false);
addAndMakeVisible(*btnDone_);   // desfaz a linha de cima
```

deixava o DONE **visível** desde a abertura. O `resized()` entrava em `if (btnDone_->isVisible())` e **nunca dava bounds** no MAKE BACKUP nem no CANCEL — existiam com tamanho zero.

O mesmo padrão em **9 lugares** explicava outros sintomas do mesmo print: o combo "Clipping (8)" aparecendo sem "From collection" selecionado, e o "OPEN VISUAL EDITOR" aparecendo com "Keep my catalog organization".

**Corrigido** com `addChildComponent()` em:
`BackupWorkspaceComponent.cpp` (comboColecoes_, btnEditarHierarquia_, barraProgresso_, labelProgressoStatus_, btnDone_) · `CatalogWorkspaceComponent.cpp` (btnLimparBusca_, aiProgressBar_, aiProgressLabel_, btnCancelarAi_) · `BarraFerramentasComponent.cpp` (botaoEstruturaOrigem_, botaoEstruturaBackup_)

**Efeito colateral tratado:** os `onChange` que revelam esses controles não chamavam `resized()`. Enquanto tudo estava sempre visível ninguém notou; com a correção, escolher "From collection" mostraria um combo de tamanho zero. `resized()` adicionado.

**Nota:** `FichaPanelComponent.cpp:1272` já tinha comentário sobre exatamente esse bug ("bug real, achado pelo harness") — corrigido pontualmente naquele arquivo e nunca varrido no resto.

---

## 11. Redesign da aba Backup

> **Pedido:** print da tela Done quebrada — "olha que merda esta diagramação… tente algo mais profissional e de boa visualização… aja como se fosse um designer de produto que considera a fadiga, gente que não enxerga direito, bom gosto e boas práticas"

**Causa da quebra:** no estado Done o `resized()` desenhava a barra de progresso e o resumo **por cima** dos controles de configuração — retornava cedo sem nunca escondê-los. Os controles mantinham os bounds da passada anterior. Daí a barra de 86% cruzando o "SOURCE" e o "Copied: 7" em cima da lista de vaults.

### ❌ Escrito mas NÃO compilado

**`BackupWorkspaceComponent.h`**
- `mostrarControlesConfig(bool)`
- `cartoes_`, `cartaoCentral_`, `faixaCabecalho_`, `faixaRodape_`

**`BackupWorkspaceComponent.cpp`**
- `mostrarControlesConfig()` — esconde/mostra os controles por estado (a correção do bug)
- `paint()` — faixas de cabeçalho/rodapé com fio de 1px; cartões com fundo, canto arredondado e borda
- `resized()` reescrito:
  - Cabeçalho 64px e rodapé 72px como faixas fixas
  - Botões de 40px (alvo maior)
  - **Config:** duas colunas com largura máxima de 1180px, centradas — sem teto o formulário estica por 1600px e o olho percorre a tela toda entre rótulo e controle. Cada seção vira um cartão com fundo próprio e respiro, para o olho encontrar 4 blocos em vez de 13 controles soltos
  - **Running/Done:** um cartão só, centrado, 620px — config escondida
  - Controles de 32px, linhas de toggle de 30px
- Progresso vai a **1.0** no fim (a barra parava em 86% ao lado de "concluído com sucesso" — dois sinais contraditórios)
- Status colorido por resultado: verde / âmbar (cancelado) / vermelho, **com texto correspondente**, não só cor
- Resumo final com contagens, caminho de destino e as 4 primeiras falhas

---

## 12. Pendências

### Não compilado
- **Redesign da aba Backup** (seção 11) — escrito, build interrompido

### Não começado
> **Pedido registrado:** na ingestão, o material deve aparecer na janela central dentro da **estrutura original de pastas** por padrão, com opção de alternar para a lista plana atual; right-click num arquivo deve ter **Move to Folder**; tudo dentro do projeto de backup, sem alterar os arquivos de origem.

Ponto de partida levantado: `arquivo.caminho_absoluto_origem` já dá a hierarquia de origem (é o que o `EstruturaOriginal` do backup usa); falta localizar onde vive a "pasta manual" do catálogo, que é o que o Move to Folder alteraria.

### Decisões suas
| Item | Questão |
|---|---|
| Checkbox de checksum | Remover ou fazer valer? |
| Checksum vs. metadado | Quer auditoria de integridade do backup? Exige coluna nova ou reordenar fases |
| Modelo Gemini | A lista de fallback resolve na prática, mas confirmar qual respondeu na primeira execução real |

### Não verificado em execução
Nada nesta sessão foi testado rodando o app — só compilado. Em particular:
- **AI SCAN nunca foi visto funcionando.** O bug do base64 era real e quebrava toda requisição com imagem, mas se ainda falhar, o diálogo agora nomeia o motivo exato (status HTTP + mensagem da API). Rode com ~5 arquivos selecionados, não com os 1152
- Crash de áudio: testar abrindo WAV longo em HD externo e trocando de projeto antes de terminar; e 10 cliques rápidos em itens diferentes

---

## Arquivos alterados na sessão

**Ingest**
`Source/Ingest/AiScan.h` · `Source/Ingest/AiScan.cpp`

**UI**
`Source/Ui/CatalogWorkspaceComponent.h` · `Source/Ui/CatalogWorkspaceComponent.cpp` · `Source/Ui/BackupWorkspaceComponent.h` · `Source/Ui/BackupWorkspaceComponent.cpp` · `Source/Ui/NovoProjetoDialogo.cpp` · `Source/Ui/MosaicoComponent.cpp` · `Source/Ui/BarraFerramentasComponent.cpp` · `Source/Ui/AudioWorkspace.cpp` · `Source/Ui/Strings.h`

**Áudio**
`Source/Audio/MotorReproducao.h` · `Source/Audio/MotorReproducao.cpp`

**Consolidação**
`Source/Consolidacao/MetadadoEmbutido.h` · `Source/Consolidacao/MetadadoEmbutido.cpp`

**Modelo e schema**
`Source/Model/Project.cpp` · `schema/registro.sql`

---

## Comandos

**Compilar**
```bash
cd "/Volumes/BUNKER 4TB/Apps/BKR Matriz/build" && cmake --build . --target matriz -- -j4
```

**Rodar**
```bash
open "/Volumes/BUNKER 4TB/Apps/BKR Matriz/build/matriz_artefacts/Debug/BKR Matriz.app"
```

**Conferir metadado embutido numa cópia de backup**
```bash
mdls -name kMDItemDescription "/caminho/do/projeto/Backup/arquivo.jpg"
```
