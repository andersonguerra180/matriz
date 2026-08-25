# BKR Acervo - Architecture Audit

Phase 0 audit. No code changes.

---

## 1. Existing Media Preview / Playback Stack

### PRESERVE ALL OF THE FOLLOWING

| Component | File | Purpose | Status |
|-----------|------|---------|--------|
| PreviewComponent | Ui/PreviewComponent.h/.cpp | Dispatches to video, audio, or image preview based on file type. Owns prev/next/close buttons, metadata label. | Working |
| VideoPlayerComponent | Ui/VideoPlayerComponent.h/.cpp | AVFoundation player behind C-linkage bridge. Play/pause/stop/seek at 15 Hz. Embedded via NSViewComponent. | Working (macOS only) |
| VideoPlayerBridge | Ui/VideoPlayerBridge.h/.mm | Pure ObjC++ bridge. AVPlayer + AVPlayerLayer + NSView. No JUCE dependency. | Working |
| AudioWorkspace | Ui/AudioWorkspace.h/.cpp | "Listening Station." Waveform viewer + transport + VU/peak/correlation/vectorscope/spectrum. JKL shuttle, loop points. Pre-computed LUFS-I/LRA/peak/correlation displayed in footer. | Working |
| MotorReproducao | Audio/MotorReproducao.h/.cpp | Audio engine. Full file decode to memory (45 min cap). Lock-free callback with sample-rate conversion. 6-speed JKL shuttle, forward and reverse. Atomic metering (peak/RMS/correlation). Ring buffer for FFT/vectorscope. | Working |
| TimelineComponent | Ui/TimelineComponent.h/.cpp | Zoomable waveform (AudioThumbnail). Timecode ruler (mm:ss or HH:MM:SS:FF). Marker lane with inline editing. Transport bar (Play/Stop/Jog/Shuttle/Zoom/Add Marker). Cmd+wheel zoom. | Working |
| BarraMetricasComponent | Ui/BarraMetricasComponent.h/.cpp | Fixed footer (44px). Format/LUFS/LRA/peak/sample-rate/bit-depth/channels/codec/duration. Stereo VU meter (300ms ballistic, green/amber/red). Adapts to media type. | Working |
| AssetWorkspace | Ui/AssetWorkspace.h | Abstract base: carregarAsset / descarregar. Optional File param for offline cached display. AudioWorkspace is sole subclass. | Working |

### Key connections

- `PreviewComponent::mostrarItem(itemId)` looks up item via `ProjetoAberto`, determines category, creates `TimelineComponent` (audio) or `VideoPlayerComponent` (video) or loads image.
- `AudioWorkspace` owns its own `MotorReproducao` instance. Receives metering via 30 Hz timer reading atomics.
- `TimelineComponent` uses `juce::AudioTransportSource` + shared `AudioDeviceManager` (separate from AudioWorkspace's engine).
- Marker edits go through `ProjetoAberto` to `marcador` table with provenance.

---

## 2. Data Layer

### Database architecture

Two SQLite databases per project:

- **registro.sqlite** (24+ tables) - authoritative catalog
- **indice.sqlite** (14 tables) - disposable AI/analysis index

### Key tables (registro.sqlite)

| Table | Purpose |
|-------|---------|
| projeto | Single-row project config (mode, name, naming mask, institution) |
| item | Archival object (id, codigo_acervo, titulo, tipo_midia, estado with 11 states) |
| item_campo | Field-value store (nivel, campo_id, valor, fonte: humano/herdado/leitura_tecnica) |
| item_historico | Audit trail (creation, field edit, state change, AI confirmation) |
| arquivo | Files (vault_id, paths, papel, eh_master, checksums MD5+SHA-256, JSON tech characteristics) |
| vault | Preservation volumes (type: local/optico/lto/rede/nuvem_sync, UUID, status) |
| acervo_pasta | Virtual folder tree (backup organization) |
| marcador | Time markers with author |
| colecao_inteligente | Saved search definitions |
| consolidacao_registro | Incremental copy tracking |
| proveniencia | Append-only audit trail |
| busca_fts | FTS5 full-text search with sync triggers |
| cache_arquivo | Thumbnail/waveform/loudness blobs |

### Key tables (indice.sqlite)

| Table | Purpose |
|-------|---------|
| fila_processamento | Task queue for embeddings/transcription/OCR |
| miniatura | Thumbnail/keyframe cache |
| forma_onda | Pre-computed waveform peaks (BLOB) |
| embedding | CLIP vectors |
| duplicata | Near-duplicate detection results |
| sugestao_campo | AI field suggestions (model, confidence, confirmable) |
| transcricao / transcricao_palavra | Word-level timestamps with diarization |

### Built-in smart collections (SQL views)

clipping, absent, not-downloaded, incomplete, vulnerable, review

---

## 3. Ingest Engine

### Pipeline (IngestArquivo)

Two-phase concurrent ingest:

1. **Phase 1 - analisarArquivo** (thread-safe, no DB): checksums (MD5+SHA-256 simultaneous), technical reading, cloud placeholder detection
2. **Phase 2 - gravarArquivoAnalisado** (write mutex): creates/finds Vault, writes arquivo row, duplicate check by SHA-256

In-place preservation: files referenced where they live, never copied into project.

### Analysis modules

| Module | File | What it does |
|--------|------|-------------|
| LeituraTecnica | Ingest/LeituraTecnica.h/.cpp | Duration, sample rate, bit depth, channels, codec, resolution, FPS, EXIF, ID3, lossy flag. Audio via JUCE readers; video via ffprobe subprocess; images via Exiv2. |
| Checksum | Ingest/Checksum.h/.cpp | MD5 + SHA-256 in single pass (64KB buffer). CommonCrypto on macOS. |
| Duplicata | Ingest/Duplicata.h/.cpp | Image pHash (Hamming <= 10), audio spectral fingerprint, lossy bandwidth cutoff detection. |
| Loudness | Ingest/Loudness.h/.cpp | EBU R128 / BS.1770-4. K-weighting, gated measurement, LRA per Tech 3342. Sample peak (not true peak). 10 min cap. |
| ClassificadorFalaMusica | Ingest/ClassificadorFalaMusica.h/.cpp | Heuristic DSP (not ML). Syllabic modulation + ZCR. Result goes to sugestao_campo, never directly to registro. |
| PainelInconsistencias | Ingest/PainelInconsistencias.h/.cpp | QC checks: missing ISRC, lossy master, no cover, duplicate ISRC, divergent sample rates, file existence + SHA-256 verification on disk. |
| Miniaturas | Ingest/Miniaturas.h/.cpp | Thumbnail generation |
| Pcm | Ingest/Pcm.h/.cpp | PCM utilities |
| CacheArquivo | Ingest/CacheArquivo.h/.cpp | File caching |

---

## 4. Vault / Backup System

| Module | Purpose |
|--------|---------|
| Volume | Identifies physical volumes via statfs(2) + DiskArbitration UUID. Handles macOS firmlinks. |
| Reconciliacao | Fast scan (path+size match) and full verification (rehash every byte). Move detection. Missing files marked `ausente`, corrupted marked `corrompido`. RAII reconciliation window for master updates. Append-only proveniencia. |
| Resolucao | Resolves where a file lives: vault path > absolute origin > project-relative. Returns nullopt if offline. |

---

## 5. Consolidation & Publication

| Module | Purpose |
|--------|---------|
| Consolidacao | Copies masters to structured output folder. Naming mask engine with tokens. Conflict detection. Incremental (via consolidacao_registro). Embeds markers as WAV cue/adtl/labl + iXML chunks in backup copies only. |
| Publicacao | Self-contained preservation package (.matriz/). manifest.txt (shasum -a 256 format), manifest.sqlite, catalog.sqlite, cache/, exports/. Every byte read-back verified. verificarPacote re-reads against manifest. |

---

## 6. Ficha System

YAML-driven metadata definitions in `fichas/*.yaml`. 25 types. Each declares: tipo, rotulo, modos (archive/catalog), ordem, icone, categoria (digital/analog), grupos with campos (id, tipo, opcoes, validacao, visivelSe, preenchidoPor, sugeridoPor). Parsed by FichaDefinition.cpp with slugified group keys for i18n. CatalogoDeFichas discovers types at runtime by scanning the directory.

---

## 7. UI Layer

| Component | Purpose |
|-----------|---------|
| MainComponent | Three screens: Home, Project, Catalog. Project screen = 3-column layout with draggable dividers. |
| MainWindow | Native title bar, menu bar (File/Project/Preferences/Help). |
| ArvoreComponent | Tree view for SOURCE and BACKUP TREE. List and Icon modes. Drag-and-drop target. |
| MosaicoComponent | Grid/list view of items. Grouped by media type or artist/release. Thumbnails with status borders. Multi-selection. |
| FiltrosComponent | Chip-based filters: media type, status, extension, origin, vaults, smart collections. |
| FichaPanelComponent | Metadata form. Single-item and batch modes. Conditional visibility. |
| BarraFerramentasComponent | Top toolbar: Add Files, Browse, Search, Grid Size, View Mode, Details toggle, Backup. |
| NavegadorArquivosComponent | Finder-style file browser (Columns/List/Icons). Back/forward/up. Async search. |
| OverlayComponent | Internal modal replacement (no native window). |
| PainelInconsistenciasComponent | QC issue display. |
| BarraGuiaComponent | Workflow step indicator (Empty/Classify/Organize/Backup). |

---

## 8. ProjetoAberto (UI-to-Data Bridge)

Single point of access from UI to database. Caches FichaDefinitions. Provides:
- listarItens, valorCampo, arquivoPrincipal
- sugestaoPendente / confirmarSugestao (AI workflow)
- observacoes CRUD
- arvoreOrigem / arvoreAcervo (tree building)
- folder CRUD (criarPastaAcervo, renomear, apagar, adicionarItens)
- buscarItens (FTS5)
- filter counts by tipo_midia/estado/extensao/origem
- smart collections (listar, salvar, apagar) + built-in views
- vault listing and re-evaluation
- batch media type assignment
- item removal (cascading DB delete, never touches disk)
