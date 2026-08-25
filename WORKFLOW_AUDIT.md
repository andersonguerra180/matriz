# BKR Acervo - Workflow Audit

Phase 0 audit. No code changes.

---

## 0. Analysis Pipeline Map (Rule 8.2)

### What the current pipeline does, step by step

The current `processarLoteEmBackground` in MainComponent.cpp:1293 orchestrates everything. Here is the exact sequence:

**Synchronous phase (message thread, fast):**
1. `expandirArquivos` — recursively lists files, skips hidden/zero/DAW-cache files
2. For each file: INSERT INTO item with tipo_midia=NULL, estado='capturado' — creates the item placeholder
3. `mosaico_->recarregar()` — items appear in grid immediately

**Background phase (ThreadPool, per file, expensive):**
4. `analisarArquivo(file)` — **READ-ONLY, no DB**
   - `verificarSePlaceholderNuvem` — stat() check, no file read
   - `calcularChecksums(file)` — reads entire file once, produces MD5+SHA-256
   - `lerTecnica(file)` — ffprobe/Exiv2/JUCE AudioFormatReader, produces LeituraTecnicaResultado
5. `categoriaPorExtensao(file)` — **READ-ONLY**, extension-based (Audio/Video/Imagem/Documento/Desconhecida)
6. `calcularCache(file, categoria, ...)` — **READ-ONLY, no DB**
   - Thumbnail generation
   - Waveform peak calculation
   - LUFS/LRA/peak measurement (EBU R128)
   - Correlation measurement

**Background phase (under mutex, DB writes):**
7. `buscarAssetPorChecksum(registro, sha256)` — DB read: duplicate check
8. If duplicate: UPDATE item estado='duplicata', registrarLocalizacaoConhecida
9. If new: `gravarArquivoAnalisado(registro, itemId, analise, papel, ehMaster)` — **DB WRITE**: creates arquivo row with checksums, technical JSON, vault reference
10. Assign codigo_acervo (sequential), UPDATE item estado='novo'
11. `gravarCache(registro, arquivoId, cache)` — **DB WRITE**: stores thumbnail/waveform/LUFS blobs
12. `gerarEGravarMiniaturaPrincipal(indice, ...)` — **DB WRITE to indice**: thumbnail in miniatura table

**Post-batch (message thread, once):**
13. Reload trees, filters, mosaic
14. Show summary: N new, N duplicates, N errors

### Classification: currently NOT part of the pipeline

- `ClassificadorFalaMusica` exists but is NOT called during ingest
- `SelecionarTipoMidiaDialogo` is called BEFORE ingest (user picks type for entire batch)
- Items arrive with tipo_midia=NULL, estado='novo'
- Manual classification happens afterward

### What is read-only vs what writes to DB

| Operation | Read-only? | Cost |
|-----------|-----------|------|
| expandirArquivos | Yes (filesystem listing) | Low |
| analisarArquivo | Yes (reads file, no DB) | High (checksum=full read, ffprobe=subprocess) |
| categoriaPorExtensao | Yes (string match) | Negligible |
| calcularCache | Yes (reads file, no DB) | High (decode audio, FFT, LUFS) |
| buscarAssetPorChecksum | DB read only | Low |
| gravarArquivoAnalisado | DB write | Low |
| gravarCache | DB write | Low |
| gerarEGravarMiniaturaPrincipal | DB write | Low |

### What can be reused between wizard stages

The wizard's user-facing stages (DISCOVER → UNDERSTAND → REVIEW → IMPORT) do NOT need to duplicate work:

- **DISCOVER**: `expandirArquivos` + `analisarArquivo` (Phase 1 only). Results stored in memory as `vector<AnaliseDeArquivo>`. This gives file counts, sizes, checksums, technical metadata, categories. No DB writes.
- **UNDERSTAND**: Uses `AnaliseDeArquivo.leitura` (already computed in DISCOVER) + `categoriaPorExtensao` + `ClassificadorFalaMusica` (add to pipeline). Groups results by category/type. Still no DB writes.
- **REVIEW**: Presents the same in-memory results, filtered to items needing human decision. Still no DB writes.
- **IMPORT**: Runs the DB write phase: `gravarArquivoAnalisado` + `gravarCache` + assign codigo_acervo. Uses the SAME `AnaliseDeArquivo` objects from DISCOVER — **zero re-analysis**.
- **VERIFY**: Reads back the written checksums and compares. Could also verify file existence.

The expensive operations (checksum, ffprobe, LUFS) run ONCE during DISCOVER. The subsequent wizard stages consume those results without re-reading files.

### Key struct that carries state between stages

```cpp
struct AnaliseDeArquivo {
    juce::File arquivo;
    Checksums checksums;         // MD5 + SHA-256
    LeituraTecnicaResultado leitura;  // duration, sample rate, codec, etc.
    bool ehPlaceholderNuvem = false;
};
```

This is already a self-contained, DB-free result. The wizard holds a `vector<AnaliseDeArquivo>` plus the corresponding `AnaliseCache` results (waveform/LUFS/thumbnail). The IMPORT step consumes these without re-analysis.

---

## 1. Current User Flow

### Launch → Project creation

1. App opens → Home screen with two mode cards ("Archive" / "Catalog") + recent projects
2. User clicks a mode card → NovoProjetoDialogo asks for name, folder, fields
3. Project opens → three-column layout with empty grid

### Ingest

1. User drags files onto the window OR clicks "Add Files" OR opens the Finder-style browser
2. SelecionarTipoMidiaDialogo asks the user to pick a media type from a flat list (25 types)
3. Ingest runs in background thread pool — checksums, technical reading, loudness, speech/music classification
4. Items appear in the grid immediately, thumbnails fill in asynchronously
5. No scan/discover step. No review step. No summary. No verification report.

### Classify / Organize

1. User manually selects items in the grid
2. Categorizes via context menu, keyboard shortcuts (1-9), or SelecionarTipoMidiaDialogo
3. Edits metadata in the right-panel FichaPanelComponent (single or batch)
4. Drags items from SOURCE tree to BACKUP TREE folders to organize

### Backup

1. User clicks "Backup" in toolbar → ConsolidacaoDialogo
2. Dialog shows rename preview, conflict detection
3. User clicks "Consolidate" → files copied with naming mask, SHA-256 verified

### Preview

1. Double-click item in grid → PreviewComponent opens in center area
2. Audio → AudioWorkspace (waveform + transport + VU/LUFS/spectrum)
3. Video → VideoPlayerComponent (AVFoundation)
4. Image → inline image viewer

---

## 2. Problems

### P1: No guided ingest workflow
Files enter the system via drag-and-drop or file browser with no intermediate steps. There is no DISCOVER step (scan source, show what was found), no UNDERSTAND step (automatic classification), no REVIEW step (show only items needing human decisions), and no verification summary after import.

### P2: Manual classification despite existing auto-classification
ClassificadorFalaMusica already runs during ingest and writes suggestions to sugestao_campo in indice.sqlite. LeituraTecnica already extracts format/codec/duration/sample-rate. But the user is asked to manually pick a media type for the ENTIRE batch before any analysis runs. The auto-classification results are never surfaced as a grouped review.

### P3: No home screen with task-oriented entry points
The home screen shows mode selection (Archive/Catalog) and recent projects. After a project is open, there is no "what do I want to do" screen. The user must discover operations from the toolbar and menus.

### P4: Internal concepts exposed to the user
The three-column layout presents SOURCE (file-system origin tree) and BACKUP TREE (virtual folder hierarchy) as primary navigation — these are database/storage concepts. The user has to understand the distinction between "where files came from" and "where they're organized" before they can work.

### P5: No guided backup workflow
Backup is a single toolbar button that opens the Consolidation dialog. There is no step to choose what to back up, no selection of destination with capacity info, no progress tracking with verification summary.

### P6: No preservation health dashboard
The built-in smart collections (vulnerable, absent, incomplete, clipping, review) exist as SQL views, but there is no unified "archive health" screen. The PainelInconsistenciasComponent shows QC issues but only for individual items or the current view, not as a global status.

### P7: No "needs attention" queue
Issues discovered during ingest (duplicates, unknown types, metadata conflicts, failed checksums) are not collected into a reviewable queue. The user must discover problems by browsing.

### P8: Backup tree navigation affects center panel
The BACKUP TREE is in the right column but selecting nodes was designed to also filter the center grid, creating confusion about what's "source" vs "backup."

### P9: No step-by-step import verification
After ingest completes, there is no summary screen showing: X files imported, Y verified, Z failed. The user returns to the grid with no confirmation of what just happened.

---

## 3. Proposed User Flow

### Home (project already open)

```
┌──────────────────────────────────┐
│                                  │
│   + INGEST MEDIA                 │
│                                  │
│   CATALOG                        │
│                                  │
│   BACKUP                         │
│                                  │
│   PRESERVATION                   │
│                                  │
├──────────────────────────────────┤
│   NEEDS ATTENTION                │
│                                  │
│   12 files need review           │
│   8 assets have only one copy    │
│   3 checksum errors              │
│                                  │
└──────────────────────────────────┘
```

Implementation: New panel inside MainComponent. Appears when project is open but no specific workflow is active. Uses existing ProjetoAberto queries for counts (built-in collections: vulnerable, absent, review, incomplete).

### Ingest Wizard

Explicit state machine (Rule 8.6):

```
enum class IngestState {
    IDLE,
    SELECTING_SOURCE,
    DISCOVERING,       // scanning + analyzing (one pass)
    REVIEWING,         // user reviews exceptions
    READY_TO_IMPORT,   // summary, waiting for confirmation
    IMPORTING,         // writing to DB
    VERIFYING,         // confirming writes
    COMPLETED,
    FAILED
};
```

**STEP 1: SOURCE** (state: SELECTING_SOURCE)
- Select folder/drive via system file dialog or NavegadorArquivosComponent
- No media type question (Rule 8.5)
- Discovery is non-destructive (Rule 8.3): never moves/renames/deletes

**STEP 2: DISCOVER + UNDERSTAND** (state: DISCOVERING)
- User-facing: two conceptual stages. Internally: ONE pass (Rule 8.2)
- Runs `expandirArquivos` to list files
- Runs `analisarArquivo` per file in ThreadPool (checksum + technical reading)
- Also runs `calcularCache` (waveform/LUFS/thumbnail)
- Also runs `classificarFalaMusica` for audio files (currently not in pipeline — to be added)
- Also runs `categoriaPorExtensao` + inference from technical metadata
- Results stored in memory as `vector<AnaliseDeArquivo>` + `vector<AnaliseCache>`
- NO DATABASE WRITES during this phase
- Progress shown: file count, total size, breakdown by category
- Failures are collected, not fatal (Rule 8.7):
  "9,980 analyzed, 20 failed → [REVIEW FAILED] [CONTINUE WITH 9,980]"

**STEP 3: REVIEW** (state: REVIEWING)
- Shows ONLY items needing human decision
- Groups: unknown content type, possible duplicate, metadata conflict, unsupported format
- "Apply to all" for batch decisions
- Skippable when nothing needs attention
- Uses the in-memory analysis results — no re-reading of files

**STEP 4: IMPORT** (state: READY_TO_IMPORT → IMPORTING)
- Summary: file count, size, breakdown
- "No files will be modified on the original source" (Rule 8.4)
- Import = register in database, NOT relocate files
- Single [IMPORT] button
- Runs `gravarArquivoAnalisado` + `gravarCache` using SAME `AnaliseDeArquivo` from DISCOVER
- Zero re-analysis (Rule 8.2)
- Assigns codigo_acervo (sequential) and sets estado='novo'

**STEP 5: VERIFY** (state: VERIFYING)
- Confirms DB writes succeeded
- Shows: imported / verified / failed counts
- Failures become reviewable, not silenced (Rule 8.7)

**STEP 6: DONE** (state: COMPLETED)
- "X assets added to the archive"
- [VIEW ARCHIVE] → goes to catalog workbench
- [REVIEW ISSUES] → goes to items needing attention

### Catalog (existing workbench, improved navigation)

```
Entry points (metadata-driven, not tree-driven):
  ALL ASSETS
  RECENTLY INGESTED
  AUDIO / VIDEO / IMAGES / DOCUMENTS
  NEEDS REVIEW
  NO BACKUP
  SINGLE COPY
  OFFLINE

Uses: existing ProjetoAberto.listarItens with filter queries
Uses: existing MosaicoComponent for grid display
Uses: existing FiltrosComponent (already has chip-based filtering)
```

Asset detail: existing PreviewComponent + existing AudioWorkspace/VideoPlayer + FichaPanelComponent side by side. No change to the media experience.

### Backup Wizard

```
STEP 1: WHAT
  → Everything / Recently ingested / Selected / Needs backup / A collection
  → Uses: existing ProjetoAberto queries

STEP 2: WHERE
  → Show connected volumes with capacity
  → Uses: existing Vault/Volume system

STEP 3: PREVIEW
  → Asset count, total size, already backed up, new data
  → Uses: existing Consolidacao.planejarConsolidacao

STEP 4: COPY
  → Progress bar
  → Uses: existing Consolidacao.executarConsolidacao

STEP 5: VERIFY
  → Checksum verification progress
  → Uses: existing verification in executarConsolidacao

STEP 6: DONE
  → Summary: copied/verified/failed
```

### Preservation Dashboard

```
ARCHIVE HEALTH: GOOD / WARNING / CRITICAL

248,391 assets
231,842 verified
8,421 single-copy
6,128 need backup
21 checksum problems

Each item is clickable → goes to the relevant action

Uses: existing built-in SQL views (vulnerable, absent, incomplete, clipping)
Uses: existing PainelInconsistencias checks
Uses: existing Vault.verificacaoCompleta results
```

---

## 4. What Changes vs What Stays

### CHANGES (workflow layer)

| What | How |
|------|-----|
| Home screen | New panel in MainComponent with 4 task entry points + attention queue |
| Ingest flow | New multi-step overlay/wizard panel wrapping existing ingest engine |
| Catalog navigation | New metadata-driven entry points panel, existing grid underneath |
| Backup flow | New multi-step overlay/wizard wrapping existing Consolidacao |
| Preservation status | New dashboard panel reading existing DB views/checks |
| BarraGuiaComponent | Repurpose as wizard step indicator |

### STAYS UNCHANGED

| Component | Why |
|-----------|-----|
| PreviewComponent | Core media preview dispatch |
| VideoPlayerComponent + Bridge | Video playback |
| AudioWorkspace | Listening station (waveform, transport, metering) |
| MotorReproducao | Audio engine |
| TimelineComponent | Waveform editor with markers |
| BarraMetricasComponent | Format info + VU meter |
| MosaicoComponent | Grid/list view |
| FichaPanelComponent | Metadata editing |
| FiltrosComponent | Chip-based filtering |
| ArvoreComponent | Tree view (used within catalog) |
| All of Ingest/* | Engine, not UI |
| All of Db/* | Database layer |
| All of Model/* | Project model |
| All of Vault/* | Volume/reconciliation/resolution |
| All of Consolidacao/* | Copy engine |
| All of Publicacao/* | Publication engine |
| All of Ficha/* | YAML metadata definitions |
| ProjetoAberto | UI-to-data bridge |

---

## 5. Implementation Phases

### Phase 0 — THIS DOCUMENT
Audit. No code changes.

### Phase 1 — Home with task entry points
New `HomePanel` component inside MainComponent. Four buttons + attention counts from existing DB queries. Appears when project is open and no workflow is active.

### Phase 2 — Ingest Wizard
New `IngestWizard` component (multi-step panel). Steps 1-7 as overlays. Wraps existing `IngestArquivo` + `LeituraTecnica` + `ClassificadorFalaMusica` + `FluxoLote`. No engine changes.

### Phase 3 — Connect wizard to existing Catalog
After ingest completes, "View Archive" goes to existing MosaicoComponent with the new metadata-driven entry points.

### Phase 4 — Improve Asset Detail
Keep existing preview/player. Improve layout: preview on left/main, metadata on right. No component rewrites.

### Phase 5 — Backup Wizard
New `BackupWizard` component wrapping existing `Consolidacao`. Multi-step overlay.

### Phase 6 — Preservation dashboard
New `PreservationPanel` component reading existing DB views and `PainelInconsistencias`.

### Phase 7 — Navigation polish
Metadata-driven catalog entry points. Attention queue click-through. Overall flow polish.
