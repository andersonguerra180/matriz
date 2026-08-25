# BKR MATRIZ — SPEC DE ALTERAÇÕES

**Versão:** 1.0
**Alvo:** Claude Code (agente de implementação)
**Projeto:** BKR Matriz — app desktop macOS, C++/JUCE, SQLite3
**Documento base:** `arquitetura_e_status.md`

---

## 0. COMO USAR ESTE DOCUMENTO

Este documento tem **três blocos independentes**:

| Bloco | Conteúdo | Esforço | Ordem |
|---|---|---|---|
| **A** | Regressão / verificação do que já foi implementado | baixo | 1º |
| **B** | Editor de tags em chips (metadados) | baixo | 2º |
| **C** | Substituição do AI Scan (Gemini) pelo **Local AI Indexer** | alto — multi-sessão | 3º |

**REGRA DE EXECUÇÃO:** não tentar fazer o Bloco C inteiro numa tacada. Ele está dividido em **7 fases (C1–C7)**, cada uma com critério de aceitação próprio e cada uma deixando o app compilável e funcional. Terminar uma fase, buildar, rodar os selftests, e só então avançar.

**REGRAS GLOBAIS DO PROJETO (invioláveis):**
1. O Matriz é um **caderno sobre arquivos**. Nunca altera, move, renomeia, converte ou deleta o arquivo original.
2. A estrutura original de pastas permanece intacta.
3. UI **em inglês**. Comentários de código e commits em português.
4. Nada de dependência de rede no caminho de execução padrão.
5. Informação inserida pelo usuário **sempre** tem prioridade sobre inferência automática.

---

# BLOCO A — VERIFICAÇÃO DE REGRESSÃO (não reimplementar)

O `arquitetura_e_status.md` (§4) declara os itens abaixo como **já implementados**. Antes de escrever qualquer código novo, **verificar** cada um. Se estiver funcionando, não mexer. Se estiver quebrado, corrigir e reportar.

- [ ] `ArvoreComponent::recarregar()` / `recarregarSincrono()` preservam seleção via ID (sem dangling pointer).
- [ ] `pedirTexto()` dá `grabKeyboardFocus()` no editor de texto automaticamente.
- [ ] Transporte de áudio fixado no rodapé (145px) tanto no `PreviewComponent` quanto no `AudioWorkspace`.
- [ ] Timeline + botão de marcador visíveis para arquivos de áudio.
- [ ] `sincronizarMarcadoresParaNotas()` grava/atualiza `notas_livres` no formato `(MM:SS: TEXTO)` em criação, edição, movimentação e deleção.
- [ ] `CategoriaMidia::Sessao` existe; extensões `.rpp .ptx .ptf .als .flp .logic .logicx .cpr .npr .rxdoc .pd` classificadas como Sessão.
- [ ] Logos reais de software carregadas de `Assets/` na grade e no preview.
- [ ] Checkbox "Edit Mode" removido; `editMode_ = true` por padrão.

**Itens da spec original que podem ter ficado para trás — conferir explicitamente:**
- [ ] `fichas/sessao.yaml` existe com os campos: `origem`, `ano`, `software_origem` (dropdown: Reaper, Pro Tools, Logic Pro, Ableton Live, Cubase, Nuendo, FL Studio, iZotope RX, Pure Data, outro), `produtor`.
- [ ] View `colecao_revisao` em `Project.cpp` inclui `sessao` exigindo `source_media`.
- [ ] `Strings.h` tem `"catwork.sessions": "Sessions"`.
- [ ] Botão/sidebar "Sessions" existe entre "Documents" e "Needs review", com contagem correta em `atualizarContagens()`.
- [ ] `FichaPanelComponent::determinarCategoriaMidia()` mapeia `tipo_midia == "sessao"` → `MediaCategory::Docs`, e "Session" está nas opções de `content_type`.

---

# BLOCO B — EDITOR DE TAGS EM CHIPS

## B.1 Comportamento pedido

No painel de metadados, o campo **Tags** deve funcionar como token/chip input:

1. Usuário digita texto no campo.
2. Ao pressionar **Enter** (ou **vírgula**, ou **Tab**), o texto vira um **bloco visual de tag (chip)** e o campo é limpo.
3. Usuário pode continuar digitando a próxima tag imediatamente — foco permanece no campo.
4. Chips ficam enfileirados acima/antes do campo de digitação, com wrap em múltiplas linhas.

## B.2 Requisitos de implementação

**Novo componente:** `Source/Ui/TagChipsEditor.h/.cpp`

Comportamento:
- **Commit da tag:** Enter, vírgula ou Tab. Texto vazio ou só espaço = ignora.
- **Remoção:** cada chip tem um `×` clicável. **Backspace** com o campo de texto vazio remove o último chip.
- **Normalização antes de gravar:** `trim`, colapsar espaços internos, lowercase para a chave canônica. Preservar a forma digitada como *display*.
- **Deduplicação:** se a chave canônica já existe na lista, não adiciona (dar um flash rápido no chip existente como feedback).
- **Colar múltiplas tags:** texto colado contendo vírgulas ou quebras de linha é dividido em vários chips de uma vez.
- **Layout:** flow layout com wrap; o componente cresce em altura e avisa o pai (`resized()` do container precisa reconsultar a altura preferida). Chip com cantos arredondados, padding 6px horizontal, altura 20px.
- **Persistência:** ao commitar ou remover um chip, gravar imediatamente em `item_tag` e reindexar em `busca_fts`. Não depender de "salvar" explícito — o resto do painel já é reativo.
- **Reatividade:** após gravar, disparar o mesmo callback que os demais campos usam para recarregar grade, contadores e painel de inconsistências.

**Modificar:** `Source/Ui/FichaPanelComponent.cpp` — substituir o campo de texto de tags atual pelo `TagChipsEditor`, mantendo o mesmo contrato de leitura/escrita no banco.

## B.3 Critério de aceitação
- [ ] Digitar `mixagem` + Enter cria um chip e limpa o campo, com foco mantido.
- [ ] Digitar três tags seguidas sem tocar no mouse funciona.
- [ ] Backspace com campo vazio remove o último chip.
- [ ] Reabrir o item mostra os chips persistidos.
- [ ] Tag adicionada aparece imediatamente na busca (`busca_fts`).
- [ ] Colar `rock, ao vivo, 1998` cria três chips.

---

# BLOCO C — LOCAL AI INDEXER

## C.0 OBJETIVO E ESCOPO

Substituir **completamente** o sistema AI Scan baseado em Gemini API Key por uma camada de **indexação semântica local**, capaz de processar acervos grandes sem custo por arquivo, sem enviar conteúdo para fora e sem depender de internet após a instalação dos modelos.

**Não é** "trocar a chamada do Gemini por uma chamada de modelo local". É uma arquitetura de pipelines especializados por tipo de mídia, com fila de jobs, processamento incremental, cache, proveniência, confiança e busca híbrida.

**Nome na UI:** `LOCAL AI INDEXER` (o termo "AI Scan" deve desaparecer da interface e do código de produção).

---

## C.0.1 DECISÕES TÉCNICAS (vinculantes)

Estas decisões resolvem ambiguidades da spec original. Seguir salvo impedimento técnico real — nesse caso, parar e reportar antes de improvisar.

### Arquitetura de execução: **sidecar de processo, não in-process**

O JUCE/C++ **não** deve linkar runtimes de ML. Cada capacidade pesada roda como binário externo invocado via `juce::ChildProcess`, com I/O por arquivos temporários e JSON no stdout.

| Camada | Implementação | Justificativa |
|---|---|---|
| Metadata técnica | `ffprobe` + `exiftool` (já existentes no fluxo) | determinístico, zero IA |
| OCR | **macOS Vision framework** (`VNRecognizeTextRequest`) via wrapper Obj-C++ | nativo, offline, sem download de modelo, rápido em Intel e ARM |
| PDF texto | **PDFKit** nativo | idem |
| DOCX/XLSX/PPTX | parser próprio (zip + XML) | evita dependência de Python |
| Transcrição | **whisper.cpp** (binário `whisper-cli`) | Metal em ARM, Accelerate em Intel, sem Python |
| Visão / multimodal | **llama.cpp** (`llama-mtmd-cli` ou `llama-server`) com modelo VL em GGUF | mesmo runtime dos embeddings, troca de modelo por config |
| Embeddings | **llama.cpp** com modelo de embedding GGUF multilíngue | mesmo runtime |
| Busca vetorial | extensão **sqlite-vec** carregada no `indice.db` | mantém tudo em SQLite |

**Sobre o modelo de visão:** a spec original cita "Qwen3.5". Esse nome deve ser **verificado no momento da implementação** — pinar um modelo que exista de fato, com GGUF publicado e suporte multimodal no llama.cpp (família Qwen-VL é a candidata natural). O nome do modelo **não pode ser hardcoded**: fica em configuração, e o `ModelRegistry` (C7) resolve nome → arquivo → URL de download.

**Docling:** fica **fora do escopo inicial**. Ele exige runtime Python, o que contamina o empacotamento macOS e a assinatura/notarização. Os parsers nativos acima cobrem PDF, DOCX, XLSX, PPTX, TXT, MD, RTF. Docling entra depois, como *provider* opcional, se a extração estrutural nativa se mostrar insuficiente. Registrar isso como débito técnico consciente, não como omissão.

### Realidade de hardware (importante)

O sistema roda em máquinas muito diferentes. Isso **não** é um detalhe de polimento — é o que define os defaults.

- **Apple Silicon (M1+, ≥16GB):** todos os pipelines viáveis. Whisper large-v3-turbo com Metal, visão 7–8B quantizado.
- **Apple Silicon (8GB):** whisper `medium` ou `small`, visão 3–4B quantizado, um modelo por vez, batch pequeno.
- **Intel (incluindo Mac mini 2014, macOS 13):** **visão local é inviável na prática** (dezenas de segundos a minutos por imagem) e whisper large-v3-turbo também. O default nessa classe de hardware deve ser: metadata + OCR nativo + parsing de documento + whisper `base`/`small` opcional. Visão fica **desabilitada por padrão**, com aviso claro na UI de que o hardware não suporta, e opção de habilitar mesmo assim por conta e risco.

O `HardwareProfiler` (C1) decide o perfil default. O usuário pode sobrescrever, mas o app nunca deve prometer o que a máquina não entrega.

---

## C.1 FASE 1 — FUNDAÇÃO: BANCO, FILA E ABSTRAÇÃO

### C.1.1 Remoção do Gemini

Mapear e **eliminar do pipeline padrão**:
- toda chamada a Google Generative AI / Gemini
- campo e validação de API Key
- mensagens de erro específicas do Gemini
- fallback silencioso para Gemini
- o botão e o fluxo "AI Scan"

Não esconder o botão — remover. Se houver estrutura de abstração aproveitável em `Source/Ingest/AiScan.cpp`, reaproveitar **apenas a forma**, nunca a dependência. Ao final desta fase, `grep -ri gemini Source/` não deve retornar nada em código de produção.

### C.1.2 Esquema de banco

Novas tabelas em `indice.db` (dados derivados ficam separados de `registro.db`, que guarda decisão humana):

```sql
-- Job de análise (uma linha por asset por execução de indexação)
CREATE TABLE analysis_job (
  id INTEGER PRIMARY KEY,
  item_id INTEGER NOT NULL,
  profile TEXT NOT NULL,            -- LIGHT | BALANCED | DEEP
  state TEXT NOT NULL,              -- QUEUED|PROCESSING|COMPLETED|PARTIAL|FAILED|CANCELLED
  attempts INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL,
  started_at TEXT,
  finished_at TEXT,
  error TEXT
);

-- Status por camada (independentes entre si)
CREATE TABLE analysis_layer (
  id INTEGER PRIMARY KEY,
  item_id INTEGER NOT NULL,
  layer TEXT NOT NULL,              -- TECHNICAL|CONTENT_EXTRACTION|OCR|TRANSCRIPTION|
                                    -- VISION|SEMANTIC|EMBEDDING
  state TEXT NOT NULL,              -- PENDING|OK|ERROR|SKIPPED|STALE|UNAVAILABLE
  model TEXT,
  model_version TEXT,
  analysis_version INTEGER NOT NULL DEFAULT 1,
  input_hash TEXT,                  -- hash do conteúdo no momento da análise
  updated_at TEXT NOT NULL,
  error TEXT,
  UNIQUE(item_id, layer)
);

-- Texto extraído (OCR, parser de documento, legenda embutida)
CREATE TABLE extracted_text (
  id INTEGER PRIMARY KEY,
  item_id INTEGER NOT NULL,
  source TEXT NOT NULL,             -- OCR | DOC_PARSER | EMBEDDED_METADATA
  page INTEGER,                     -- página ou índice de frame
  frame_ts REAL,                    -- timestamp em vídeo, se aplicável
  bbox TEXT,                        -- JSON [x,y,w,h] normalizado, quando disponível
  text TEXT NOT NULL,
  confidence REAL,                  -- NULL = desconhecido. NUNCA inventar.
  created_at TEXT NOT NULL
);

CREATE TABLE transcript (
  id INTEGER PRIMARY KEY,
  item_id INTEGER NOT NULL,
  language TEXT,
  model TEXT NOT NULL,
  model_version TEXT,
  full_text TEXT,
  created_at TEXT NOT NULL
);

CREATE TABLE transcript_segment (
  id INTEGER PRIMARY KEY,
  transcript_id INTEGER NOT NULL,
  start_s REAL NOT NULL,
  end_s REAL NOT NULL,
  text TEXT NOT NULL,
  confidence REAL
);

-- Cenas / frames representativos de vídeo
CREATE TABLE scene (
  id INTEGER PRIMARY KEY,
  item_id INTEGER NOT NULL,
  start_s REAL NOT NULL,
  end_s REAL,
  keyframe_ts REAL NOT NULL,
  description TEXT,
  created_at TEXT NOT NULL
);

-- Objetos/entidades detectados
CREATE TABLE detected_object (
  id INTEGER PRIMARY KEY,
  item_id INTEGER NOT NULL,
  scene_id INTEGER,
  label TEXT NOT NULL,
  confidence REAL,
  source TEXT NOT NULL,
  model TEXT
);

-- Tags normalizadas
CREATE TABLE semantic_tag (
  id INTEGER PRIMARY KEY,
  canonical_name TEXT NOT NULL UNIQUE,
  category TEXT NOT NULL             -- PEOPLE|PLACE|DATE|EVENT|OBJECT|ACTIVITY|
                                     -- SUBJECT|MEDIA|ORGANIZATION|TECHNICAL|OTHER
);

CREATE TABLE tag_alias (
  id INTEGER PRIMARY KEY,
  tag_id INTEGER NOT NULL,
  alias TEXT NOT NULL,
  lang TEXT,
  UNIQUE(alias)
);

CREATE TABLE item_semantic_tag (
  item_id INTEGER NOT NULL,
  tag_id INTEGER NOT NULL,
  source TEXT NOT NULL,              -- TECHNICAL|EXTRACTED|AI_INFERRED|USER_DEFINED
  model TEXT,
  confidence REAL,
  created_at TEXT NOT NULL,
  PRIMARY KEY(item_id, tag_id, source)
);

-- Contexto semântico curto e factual (não é tag, não é texto literário)
CREATE TABLE ai_context (
  item_id INTEGER PRIMARY KEY,
  description TEXT NOT NULL,
  model TEXT NOT NULL,
  model_version TEXT,
  created_at TEXT NOT NULL
);

-- Inferências estruturadas com proveniência (data provável, local provável, evento…)
CREATE TABLE ai_inference (
  id INTEGER PRIMARY KEY,
  item_id INTEGER NOT NULL,
  field TEXT NOT NULL,               -- probable_date | probable_place | probable_event | …
  value TEXT,                        -- NULL quando indeterminado
  source TEXT NOT NULL,
  model TEXT,
  confidence REAL,
  evidence TEXT,                     -- de onde veio: "OCR p.2", "transcript 00:14", "EXIF"
  created_at TEXT NOT NULL
);

-- Embeddings (vetor bruto + espelho em sqlite-vec)
CREATE TABLE embedding (
  id INTEGER PRIMARY KEY,
  item_id INTEGER NOT NULL,
  kind TEXT NOT NULL,                -- DESCRIPTION|TRANSCRIPT|OCR|DOCUMENT|CONTEXT|IMAGE
  chunk_index INTEGER NOT NULL DEFAULT 0,
  chunk_text TEXT,
  model TEXT NOT NULL,
  dim INTEGER NOT NULL,
  vector BLOB NOT NULL,
  created_at TEXT NOT NULL
);
```

**Regras de esquema:**
- `confidence` é `REAL NULL`. `NULL` significa *desconhecido* e a UI mostra `unknown`. **Proibido fabricar 0.95.**
- Toda linha derivada carrega `source` + `model` + `created_at`.
- Reindexação com modelo novo **não sobrescreve**: incrementa `analysis_version` e mantém o histórico. Consultas usam a versão mais recente por `(item_id, layer)`.

### C.1.3 Abstração de provider

```cpp
// Source/AI/AIProvider.h
class AIProvider {
public:
    virtual ~AIProvider() = default;
    virtual juce::String name() const = 0;
    virtual bool isAvailable(const HardwareProfile&) const = 0;
    virtual juce::String modelId() const = 0;
    virtual juce::String modelVersion() const = 0;
};

class VisionProvider    : public AIProvider { /* analyzeImage(file) -> VisionResult */ };
class AudioProvider     : public AIProvider { /* transcribe(file)   -> TranscriptResult */ };
class DocumentProvider  : public AIProvider { /* parse(file)        -> DocumentResult */ };
class OcrProvider       : public AIProvider { /* recognize(image)   -> OcrResult */ };
class EmbeddingProvider : public AIProvider { /* embed(texts)       -> vectors */ };
```

Implementações desta fase: `LocalVisionProvider`, `LocalAudioProvider`, `LocalDocumentProvider`, `LocalOcrProvider`, `LocalEmbeddingProvider`. O provider padrão é sempre **LOCAL**. A interface existe para permitir outro provider no futuro sem tocar no modelo de Asset — não para reintroduzir Gemini.

### C.1.4 Worker e fila

- `Source/AI/IndexerService.h/.cpp` — serviço único, dono da fila.
- Thread pool com N workers (default: `max(1, cores/2)`, teto de 4).
- Frontend **nunca** executa IA. Frontend só faz `enqueue(...)` e lê estado.
- Estados: `QUEUED → PROCESSING → COMPLETED | PARTIAL | FAILED | CANCELLED`.
- Retry com backoff; `maxAttempts` configurável (default 3). Esgotado → `FAILED`, e a fila **segue**. Um arquivo corrompido nunca para o lote.
- Cancelamento cooperativo: o worker checa flag entre camadas e mata o `ChildProcess` do sidecar em curso.
- `PARTIAL` = pelo menos uma camada `OK` e pelo menos uma `ERROR`/`UNAVAILABLE`.

### C.1.5 Critério de aceitação — Fase 1
- [ ] `grep -ri "gemini" Source/` vazio em código de produção.
- [ ] App abre e opera normalmente sem nenhuma API key.
- [ ] Tabelas criadas com migração idempotente (app antigo → app novo sem perda).
- [ ] É possível enfileirar jobs no-op, ver progresso e cancelar sem travar a UI.

---

## C.2 FASE 2 — CAMADA DETERMINÍSTICA E INCREMENTALIDADE

**Metadata não é IA.** Esta camada roda sempre, primeiro, e é barata.

### C.2.1 Extração
Reaproveitar `LeituraTecnica.cpp` e ampliar:
- **Imagem:** EXIF completo — câmera, lente, data, GPS, resolução, orientação.
- **Áudio:** duração, sample rate, canais, codec, bitrate, artista, álbum, título, metadata embutida.
- **Vídeo:** duração, resolução, codec, frame rate, codec de áudio, canais.
- **Documento:** autor, data de criação, data de modificação, contagem de páginas, formato.

Gravar com `source = TECHNICAL` e `confidence = 1.0`. **Nunca** classificar como `AI_INFERRED`.

### C.2.2 Incrementalidade

Chave de mudança por item: `sha256 + file_size + mtime` (SHA-256 já existe no ingest).

- Se o hash não mudou → **não reprocessar nada**.
- Se **só o nome/caminho** mudou → atualizar path, manter todas as análises válidas. Não rodar visão de novo.
- Se o **conteúdo** mudou → marcar as camadas dependentes de conteúdo como `STALE` e reprocessar só elas.
- Se o **modelo** mudou → marcar apenas as camadas daquele modelo como `STALE`.

### C.2.3 Cache de intermediários
Diretório de trabalho: `<projeto>/.matriz/cache/`.
- Frames extraídos de vídeo, PCM temporário do whisper, imagens redimensionadas para visão.
- Chave de cache = `sha256(conteúdo) + operação + parâmetros`.
- **Todo temporário de mídia é apagado após o processamento.** Nunca manter segunda cópia permanente de áudio ou vídeo.
- Cache tem teto de tamanho configurável com política LRU.

### C.2.4 Critério de aceitação — Fase 2
- [ ] Reindexar um acervo já indexado, sem alterações, não dispara nenhum sidecar.
- [ ] Renomear um arquivo não invalida OCR/visão/transcrição.
- [ ] Editar o conteúdo de um arquivo marca as camadas corretas como `STALE`.
- [ ] Nenhum arquivo temporário sobra em `.matriz/cache/` após job concluído.

---

## C.3 FASE 3 — TEXTO: DOCUMENTOS E OCR

### C.3.1 Parsing de documentos
Pipeline: `FILE → parser → texto + estrutura → (OCR se necessário) → armazenar`.

Extrair: texto, headings, páginas, tabelas, listas, imagens embutidas, estrutura.
Se o parser resolve, **não** chamar LLM. O modelo só entra depois, para interpretação semântica (Fase 6).

PDF sem camada de texto → rasterizar páginas e cair no OCR.

### C.3.2 OCR como camada própria
OCR via macOS Vision. Roda em: fotografia, documento, capa de álbum, cartaz, encarte, screenshot e frame de vídeo.

Armazenar em `extracted_text`: texto, bbox quando disponível, página/frame, confidence real reportada pela API. Todo texto de OCR entra em `busca_fts` e é pesquisável.

### C.3.3 Critério de aceitação — Fase 3
- [ ] PDF com texto → extração sem OCR.
- [ ] PDF escaneado → OCR por página, com número de página gravado.
- [ ] Foto de cartaz → texto reconhecido e localizável na busca.
- [ ] DOCX com tabelas → texto e tabelas preservados.

---

## C.4 FASE 4 — ÁUDIO: TRANSCRIÇÃO LOCAL

Pipeline: `FFmpeg → PCM 16kHz mono temporário → whisper.cpp → transcript → apagar PCM`.

Armazenar: idioma detectado, texto completo, segmentos com `start`/`end`, confidence quando o modelo fornecer.

Modelo default por perfil de hardware:
- Apple Silicon ≥16GB → `large-v3-turbo` quantizado
- Apple Silicon 8GB → `medium` ou `small`
- Intel → `base` ou `small`, e avisar o custo de tempo antes de lote grande

Transcript e segmentos entram em `busca_fts`. Buscar uma palavra deve levar ao **timestamp** dentro do arquivo.

**Integração com marcadores:** oferecer (não automático) a ação `Create markers from transcript` — converte segmentos em marcadores no formato já existente `(MM:SS: TEXTO)`. Nunca sobrescrever marcadores criados pelo operador.

### C.4.1 Critério de aceitação — Fase 4
- [ ] WAV de 10 min transcrito offline, com segmentos e timestamps.
- [ ] Sem PCM temporário sobrando.
- [ ] Busca por palavra do transcript retorna o item e o timestamp.
- [ ] Falha do whisper marca só `TRANSCRIPTION = ERROR`; as outras camadas seguem válidas.

---

## C.5 FASE 5 — VISÃO: IMAGEM E VÍDEO

### C.5.1 Imagem
Saída **estruturada em JSON**, não descrição literária longa:

```json
{
  "objects": ["guitar", "microphone", "mixing_console"],
  "people_count": 1,
  "environment": ["recording studio"],
  "activities": ["music performance"],
  "visual_subjects": ["music", "recording"],
  "detected_text": null,
  "probable_event": null,
  "probable_location": null,
  "probable_period": null,
  "tags": ["guitar", "studio", "music", "recording"],
  "description": "Fotografia de sessão de gravação em estúdio, com músico tocando guitarra diante de mesa de mixagem."
}
```

**Anti-alucinação (obrigatório):**
- O prompt do modelo deve exigir `null` para qualquer campo indeterminado.
- Validar a saída contra um schema. Campo fora do schema é descartado, não "corrigido no chute".
- O modelo **não pode** inventar nomes de pessoas, datas, lugares, eventos, títulos, artistas ou organizações sem evidência. Campo sem evidência = `null`.
- `probable_*` só é preenchido se houver evidência rastreável, e `ai_inference.evidence` deve dizer qual.

**Pessoas:** **não** implementar reconhecimento facial com identificação por nome. A visão pode reportar `people_count` e características visuais genéricas. Nome de pessoa só se associa ao asset quando vier de metadata, OCR, transcript, documento relacionado, cadastro do usuário ou relação já existente no banco — sempre com `source` correspondente.

### C.5.2 Vídeo
**Nunca** mandar o vídeo inteiro para o modelo. Amostragem obrigatória:

1. Detecção de mudança de cena (`ffmpeg` filtro `select='gt(scene,0.4)'`), com teto de frames por perfil.
2. Fallback / complemento: amostragem por intervalo configurável (default 30s no BALANCED).
3. Teto duro de frames por vídeo (default: 40 no BALANCED, 120 no DEEP).

Cada frame analisado grava `timestamp`, análise, tags e confidence. O vídeo recebe uma camada consolidada: transcript + cenas + sujeitos visuais + texto detectado + tags + embedding.

### C.5.3 Critério de aceitação — Fase 5
- [ ] Imagem de estúdio produz JSON válido conforme schema, sem inventar nomes.
- [ ] Vídeo de 2h gera dezenas de frames analisados, não milhares.
- [ ] Campo indeterminado vem como `null`, nunca como palpite.
- [ ] Em hardware sem capacidade, `VISION = UNAVAILABLE` e o resto do pipeline conclui normalmente.

---

## C.6 FASE 6 — SEMÂNTICA: TAGS, CONTEXTO, EMBEDDINGS E BUSCA

### C.6.1 Normalização de tags
A IA não pode criar centenas de variações redundantes.

- Toda tag proposta passa por resolução: `alias → canonical`.
- `guitar`, `electric guitar`, `guitarra`, `guitarra elétrica` → canônica `guitar`, com aliases registrados em `tag_alias`.
- Categorias: `PEOPLE PLACE DATE EVENT OBJECT ACTIVITY SUBJECT MEDIA ORGANIZATION TECHNICAL OTHER`.
- Semear `semantic_tag`/`tag_alias` com um dicionário inicial PT/EN focado em áudio, música, estúdio, formatos físicos e eventos.
- Tag nova que não casa com nenhum alias entra como canônica nova, marcada para revisão do usuário.
- Tag `USER_DEFINED` **sempre** vence a `AI_INFERRED` em caso de conflito, e nunca é removida por reindexação.

### C.6.2 Contexto
`ai_context.description`: **curto e factual**. Uma a duas frases. Nada de texto literário. É campo separado das tags e tem embedding próprio.

### C.6.3 Embeddings
- Gerados localmente para: descrição, transcript, OCR, contexto e conteúdo documental.
- Texto longo → chunking com overlap; `chunk_index` na tabela.
- Embedding multimodal de imagem: opcional, só quando houver suporte adequado.
- Armazenados **separados** dos dados principais. Trocar o modelo de embedding **não pode** destruir metadados — invalida só a camada `EMBEDDING`.
- `model` e `dim` gravados por linha; misturar dimensões diferentes na mesma busca é erro.

### C.6.4 Busca híbrida
A busca do Matriz combina, nesta ordem de fusão:
1. **Exact** (código de acervo, nome de arquivo, ISRC, EAN)
2. **Full-text** (`busca_fts`: tags, notas, OCR, transcript, texto de documento)
3. **Filtros de metadata** (tipo, data, vault, estado, categoria)
4. **Semântica** (similaridade de embedding via sqlite-vec)

Fusão por *reciprocal rank fusion*, com pesos ajustáveis. Nunca depender só do nome do arquivo.

### C.6.5 Proveniência no resultado
Todo resultado vindo de informação derivada mostra **por que casou**:

```
MATCH: IMG001.jpg
  matched because → TAG: guitar
  source: AI Vision · model: <modelo> · confidence: 96%

MATCH: interview_03.wav
  matched because → TRANSCRIPT: "Salvador" @ 00:14:22
  source: Whisper <modelo>
```

Confidence só aparece quando existe de verdade. Sem valor → `unknown`.

### C.6.6 Critério de aceitação — Fase 6
- [ ] `"fotos antigas de shows com guitarra"` retorna resultados combinando tag + data + atividade + similaridade.
- [ ] Busca exata por código de acervo continua instantânea e no topo.
- [ ] Cada resultado derivado exibe origem, modelo e confidence (ou `unknown`).
- [ ] Trocar modelo de embedding e reindexar preserva tags, transcripts e OCR.

---

## C.7 FASE 7 — INTERFACE, MODELOS E CONFIGURAÇÕES

### C.7.1 Ações disponíveis ao usuário
- Index selected
- Index folder
- Index collection
- Index all not yet analyzed
- Reindex
- Cancel processing

### C.7.2 Painel de progresso
```
Indexed: 1,284    Processing: 12    Queued: 4,982    Failed: 3
```
Clicável: `Failed` abre a lista com o erro por item e botão de retry.

### C.7.3 Settings → AI / Indexing
- **Local AI Engine:** Vision Model · Audio Model · Document Engine · Embedding Model
- **Hardware:** CPU / GPU / RAM detectados, e quais capacidades são viáveis nesta máquina
- **Processing:** Light / Balanced / Deep
- **Storage:** localização dos temporários e dos modelos, uso atual, botão de limpar cache

Perfis:
| Perfil | Camadas |
|---|---|
| **LIGHT** | metadata + OCR + parsing de documento + transcrição básica quando necessário |
| **BALANCED** | metadata + OCR + transcrição + visão + tags semânticas |
| **DEEP** | tudo do balanced + análise visual detalhada + cenas + embeddings + contexto expandido |

O usuário escolhe o perfil **antes** de iniciar uma operação em massa. Defaults funcionais desde o primeiro uso — nunca obrigar configuração manual de modelo.

### C.7.4 Model Manager
- Modelos baixados **sob demanda**, nunca no instalador.
- Primeiro uso sem modelo: `Vision model not installed → [Download]`, com tamanho declarado antes de começar.
- Mostrar por modelo: `Model installed · Model size · Available · Download · Update · Remove`.
- Download com barra de progresso, verificação de checksum e retomada.
- Carregar modelo **só quando necessário**. Não manter Vision + Whisper + Embedding + LLM na memória ao mesmo tempo.
- `ModelManager` decide load/unload por pressão de memória: `load → process batch → unload ou manter conforme RAM`.
- Batch adaptativo ao hardware. Não processar uma imagem por vez quando o lote for seguro.

### C.7.5 Degradação graciosa
Hardware insuficiente para um modelo **não pode quebrar o Matriz**. O sistema deve:
1. detectar a incompatibilidade,
2. informar exatamente qual componente não pode rodar,
3. seguir com as camadas possíveis.

Exemplo: `VISION = UNAVAILABLE`, mas metadata, OCR, whisper e parsing de documento continuam funcionando normalmente.

### C.7.6 Critério de aceitação — Fase 7
- [ ] Primeiro uso funciona sem o usuário abrir Settings.
- [ ] Nenhum modelo é baixado sem o usuário mandar.
- [ ] Após instalar os modelos, o indexador funciona **com a rede desligada**.
- [ ] Monitor de rede confirma zero tráfego para APIs externas durante indexação.
- [ ] Uso de RAM não estoura com múltiplos modelos carregados simultaneamente.

---

## C.8 FRONTEIRAS E GARANTIAS

### C.8.1 O indexador não é backup
O Local AI Indexer não interfere no sistema de Backup. Ele só acrescenta conhecimento ao Asset. Nenhuma análise pode mover, copiar, renomear, deletar ou reorganizar arquivos originais.

### C.8.2 A árvore permanece intacta
A estrutura física original de pastas é preservada. A camada de IA é semântica e paralela:

```
SOURCE TREE                    AI KNOWLEDGE
1998/                          IMG001.jpg
  Shows/                        ├── date: 1998        (EXIF, 1.0)
    Salvador/                   ├── place: Salvador   (path + OCR, 0.8)
      IMG001.jpg                ├── activity: live performance (AI_VISION)
                                ├── object: guitar    (AI_VISION, 0.96)
                                └── event: unknown
```

### C.8.3 Privacidade
Nunca enviar imagens, vídeos, áudios, documentos, transcripts, OCR ou metadata para servidores externos durante o processamento normal. Processamento padrão 100% local. Única exceção aceitável de rede: **download de modelo**, iniciado explicitamente pelo usuário.

---

## C.9 CRITÉRIO DE ACEITAÇÃO GLOBAL DO BLOCO C

Só considerar concluído quando **todos** os itens abaixo passarem:

**Remoção do legado**
- [ ] AI Scan antigo não é mais utilizado
- [ ] Gemini API Key não é necessária em nenhum ponto

**Capacidades**
- [ ] Imagens analisadas localmente
- [ ] Áudio transcrito localmente
- [ ] Vídeo analisado por áudio + frames amostrados
- [ ] Documentos extraídos localmente
- [ ] OCR funciona como camada independente

**Qualidade da informação**
- [ ] Tags estruturadas e normalizadas
- [ ] Contexto separado das tags
- [ ] Proveniência existe para toda inferência
- [ ] Confidence nunca é inventada
- [ ] Campo indeterminado retorna `null`/`unknown`

**Busca**
- [ ] Embeddings gerados localmente
- [ ] Busca textual funciona
- [ ] Busca semântica funciona
- [ ] Busca híbrida funciona
- [ ] Resultado mostra a origem do match

**Integridade**
- [ ] Arquivos originais permanecem intocados
- [ ] Estrutura original de pastas permanece intacta

**Execução**
- [ ] Processamento em background, UI nunca trava
- [ ] Jobs podem ser cancelados
- [ ] Jobs com erro podem ser retomados
- [ ] Processamento incremental
- [ ] Arquivos já indexados não são reprocessados sem necessidade
- [ ] Modelos carregados sob demanda
- [ ] Funciona sem internet após instalação dos modelos
- [ ] Frontend não faz processamento de IA
- [ ] Backend/worker controla todo o pipeline
- [ ] Sistema continua funcional quando uma capacidade de IA está indisponível

---

## C.10 REGRA FINAL

Não fazer implementação superficial trocando `Gemini API → modelo local`. Isso não resolve o problema.

A implementação correta é:

```
GEMINI AI SCAN
      ↓
LOCAL AI INDEXER
```

com pipelines especializados por tipo de mídia, processamento incremental, fila de jobs, cache, proveniência, confidence, embeddings e busca híbrida.

O objetivo **não** é "usar IA para descrever arquivos". O objetivo é **transformar o conteúdo do acervo em uma camada de conhecimento pesquisável**, mantendo os arquivos originais e sua estrutura completamente independentes dessa camada.

---

# VERIFICAÇÃO

Ao final de cada fase:

```bash
cmake --build build --target matriz_selftest && \
  ./build/matriz_selftest_artefacts/Debug/matriz_selftest

"./build/matriz_artefacts/Debug/BKR Matriz.app/Contents/MacOS/BKR Matriz" --selftest-uitest
```

Adicionar testes novos para: migração de esquema, máquina de estados da fila, invalidação incremental (hash / rename / conteúdo), validação de schema da saída de visão, e resolução de alias de tag.

**Teste manual de aceitação do Bloco C:** importar um diretório contendo JPG, PNG, TIFF, WAV, MP3, AIFF, MP4, MOV, PDF, DOCX e TXT. O Matriz deve identificar cada arquivo, preservar nome, extensão e estrutura, extrair metadata, executar o pipeline específico de cada tipo, extrair texto, transcrever, analisar imagens e frames, interpretar documentos, criar tags normalizadas e contexto, gerar embeddings, registrar origem e modelo de cada informação, e indexar tudo para busca — **sem nenhuma API key**.

---

# PONTOS ABERTOS (reportar antes de decidir sozinho)

1. **Modelo de visão:** confirmar o nome/versão real e a existência de GGUF com suporte multimodal no llama.cpp na data da implementação.
2. **Empacotamento dos sidecars:** `whisper-cli` e `llama-*` precisam entrar no bundle `.app` assinados e notarizados junto (ver skill `bkr-release`), ou ser instalados em `~/Library/Application Support/BKR Matriz/bin/`. Decidir antes da Fase 4.
3. **sqlite-vec:** carregar como extensão dinâmica exige `sqlite3_enable_load_extension`; confirmar que o SQLite empacotado permite. Alternativa: força bruta em C++ com vetores quantizados em int8 (viável até ~50k itens).
4. **Docling:** fora do escopo inicial por dependência de Python. Reavaliar após a Fase 3.
