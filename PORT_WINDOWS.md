# PORT_WINDOWS — BKR Matriz (Port Windows x64)

Documentação técnica do port para Windows (x64) com CI automatizado via GitHub Actions.
O macOS é a referência estrita de comportamento, visual e dados.
Requisito fundamental: interoperabilidade bidirecional total (projetos, MAIN, CLONE e EXPORT abrem e funcionam nos dois sistemas sem conversão).

---

## 1. Baseline Mac e Correção do Baseline (Etapa 1)

- **Base git inicial**: `429983f`
- **Falha identificada no baseline pré-port**: `Source/Ui/LoteSelfTest.cpp:2448` (`mosaico_->recarregar()` incrementava `versaoSnapshot_` em edições estritamente de geo/localização).
- **Correção cirúrgica**: Commit `a510f90` na branch `main`:
  - Arquivo/Função: `Source/Ui/MainComponent.cpp` (`fichaPanel_->aoAplicarEmLote`).
  - Solução: Verifica se a edição em lote contém apenas campos de geolocalização (`lat`, `lng`, `pais`, `estado`, `cidade`, `bairro`, `logradouro`). Se sim e nenhum filtro de localização estiver ativo, apenas notifica os painéis auxiliares sem recarregar a grade inteira e sem incrementar `versaoSnapshot_`.
- **Revalidação completa do Baseline Mac pós-fix**:
  - `matriz_selftest`: **PASS** (100% dos testes passaram).
  - `matriz_ingest_selftest`: **PASS** (100% dos testes passaram).
  - `--selftest-lote`: **ALL TESTS PASSED** (0 falhas).
  - **ASan (`build-asan/`)**: Limpo, 0 leaks, 0 erros.
  - **TSan (`build-tsan/`)**: Limpo, 0 data races.

---

## 2. CI Real no GitHub Actions (Etapa 2 — Evidência de Execução Verde)

- **Workflow Run**: [GitHub Actions Run 36959293389](https://github.com/andersonguerra180/matriz/actions/runs/36959293389)
- **Branch**: `windows-port`
- **Status Geral**: **SUCCESS (100% GREEN)**

### Tabela de Jobs e Evidências

| Job | Runner | Duração | Itens Validados e Status |
| :--- | :--- | :--- | :--- |
| **Build & Test (macOS)** | `macos-14` (Apple Silicon) | 21m 44s | ✓ Compilação Release (`clang++` C++20)<br>✓ `matriz_selftest` (PASS)<br>✓ `matriz_ingest_selftest` (PASS)<br>✓ ThreadSanitizer (`MATRIZ_TSAN=ON`) limpo<br>✓ `matriz_interop_selftest --gerar fixture-mac` (PASS) |
| **Build & Test (Windows x64)** | `windows-2022` (x64) | 20m 24s | ✓ Compilação MSVC Release (`cl.exe` C++20 + Ninja)<br>✓ `matriz_selftest.exe` (PASS)<br>✓ `matriz_ingest_selftest.exe` (PASS)<br>✓ AddressSanitizer (`/fsanitize=address`) limpo<br>✓ `matriz_interop_selftest.exe --gerar fixture-win` (PASS) |
| **Verify macOS Fixture on Windows (Interop Mac -> Win)** | `windows-2022` (x64) | 5s | ✓ `matriz_interop_selftest.exe --verificar fixture-mac` (PASS) |
| **Verify Windows Fixture on macOS (Two-Way Interop)** | `macos-14` (arm64) | 9s | ✓ `./matriz_interop_selftest --verificar fixture-win` (PASS) |
| **Package Release & Trial (Windows x64)** | `windows-2022` (x64) | 24m 51s | ✓ Compilação Full Release (`BKR Matriz.exe`)<br>✓ Compilação Trial (`BKR Matriz Trial.exe`)<br>✓ FFmpeg e FFprobe com hash/licença embutidos<br>✓ Instaladores Inno Setup gerados<br>✓ Pacotes Portáteis ZIP gerados<br>✓ Artefatos publicados no GitHub Actions |

---

## 3. Cobertura da Suíte de Interoperabilidade Bidirecional (Etapa 3)

Implementada em `tools/interop_selftest/main.cpp` e executada de forma cruzada no CI:

1. **`PASS [PROJECT_CREATION]`**: Projeto criado e estruturado com SQLite WAL e schema completo.
2. **`PASS [FIXTURE_MANIFEST_EXISTS]`**: `manifesto_interop.json` gerado e localizado na raiz do fixture.
3. **`PASS [FIXTURE_MANIFEST_PARSED]`**: Parse JSON de integridade concluído.
4. **`PASS [PROJECT_OPEN]`**: Projeto aberto e validado sem conversão de schema.
5. **`PASS [ITEM_COUNT]`**: Contagem de itens íntegra (5 itens).
6. **`PASS [PATHS_CANONICAL]`**: Todos os caminhos relativos gravados no banco SQLite utilizam estritamente barras normais (`/`) — 0 contra-barras (`\`).
7. **`PASS [PATHS_THUMBNAIL]`**: Tabela `miniatura` em `indice.sqlite` indexa caminhos relativos com barra normal (`/`).
8. **`PASS [PATHS_EXPORT_MANIFEST]`**: Tabela `manifesto` em `manifest.sqlite` e arquivo `manifest.sha256` usam caminhos relativos normalizados com `/`.
9. **`PASS [SAFE_NAMES_ORIGIN_PRESERVED]`**: Arquivo com caracteres proibidos no Windows (`:` e `?`) preserva o caminho de origem intacto no banco (`arquivo.caminho_absoluto_origem`) enquanto o arquivo no disco adota nomenclatura sanitizada determinística (`matriz::nomes_seguros::sanitizarComponente`).
10. **`PASS [HIDDEN_ITEMS]`**: Item oculto (`collection_type = 'Hidden'`) preservado e classificado identicamente entre plataformas.
11. **`PASS [METADATA_FIELDS]`**: Metadados estruturados (ex: `artista_principal = 'Anderson Guerra'`) preservados.
12. **`PASS [METADATA_ENTITIES]`**: Entidades relacionadas (`pessoa = 'Gilberto Gil'`, `lugar = 'Teatro Municipal'`) associadas e preservadas.
13. **`PASS [INTAKE_REJECT_MARK]`**: Marcação R (Reject) no Intake preservada (`intake_marca_r`).
14. **`PASS [CANONICAL_UNICODE_NFC]`**: Normalização canônica Unicode NFC (`São Paulo` == `são paulo`).
15. **`PASS [CHECKSUM_PARITY]`**: Hashes SHA-256 e MD5 idênticos bit a bit entre macOS e Windows.
16. **`PASS [FOLDER_MAPS]`**: Estrutura de mapas de pastas manuais (`folder_map`, `acervo_pasta`, `acervo_item_pasta`) preservada.
17. **`PASS [MAIN_DESTINATION_EXISTS]`** e **`PASS [MAIN_DESTINATION_PARSED]`**: `destination.json` na raiz do backup MAIN identificado com papel MAIN.
18. **`PASS [SQLITE_WAL_CLEAN]`**: Checkpoint WAL executado (`PRAGMA wal_checkpoint(TRUNCATE)`), 0 resíduos de `-wal` pendente.
19. **`PASS [LOUDNESS_BS1770_PARITY]`**: Medição de Loudness BS.1770 / EBU R128 (`matriz::ingest::medirLoudness`) com diferença < 0,1 LU.
20. **`PASS [PRESERVATION_RISK]`**: Classificação de risco de formato PREMIS/OAIS (`matriz::preservation::classificarRiscoFormato`: WAV=OK, MP4=OK, WMA=AT_RISK).
21. **`PASS [SQLITE_CONCURRENCY_STRESS]`**: Múltiplas threads leitoras concorrentes executando queries no banco sem contenção ou locks.
22. **`PASS [TEXT_UTF8_LF]`**: Arquivos de texto gerados em UTF-8 sem BOM e com quebras de linha estritas em LF (`\n`).

---

## 4. Auditoria e Aplicação dos Helpers (Etapa 4)

### 4.1. Caminhos de Banco (`Source/Model/CaminhosBanco.h / .cpp`)
Garante que todo caminho relativo gravado em banco utilize estritamente barras normais (`/`), sanitizando leituras e gravações.

- **Lista Completa dos Pontos de Gravação e Leitura no Banco**:
  - `Source/Ingest/IngestArquivo.cpp`: `prepararArquivo()` e `inserirArquivoNoBanco()` (`arquivo.caminho_relativo` via `relativoParaBanco` e `arquivo.caminho_absoluto_origem` via `paraBanco`).
  - `Source/Ingest/Miniaturas.cpp`: `gerarEGravarMiniaturaPrincipal()` (`miniatura.caminho_relativo` via `relativoParaBanco`).
  - `Source/Publicacao/Publicacao.cpp`: `publicar()` (`item_publicacao.caminho_relativo` e `manifesto.caminho_relativo` via `paraBanco` e resolução via `doBanco`).
  - `Source/Preservation/Preservation.cpp`: `exportarFixityManifest()` e `exportarXmlPremis()` (`caminho_relativo` via `paraBanco` e `fileObj` via `doBanco`).
  - `Source/Consolidacao/Consolidacao.cpp`: `consolidarItem()` e `gravarConsolidacaoRegistro()` (`consolidacao_registro.caminho_relativo_destino` via `paraBanco`).
  - `Source/Consolidacao/BackupScanEngine.cpp`: `caminho_relativo_destino` via `paraBanco`.
  - `Source/Vault/AssetRelinkEngine.cpp`: `arquivo.caminho_relativo` via `paraBanco` e reconciliação de volumes.
  - `Source/Vault/Reconciliacao.cpp`: `reconciliar()` (`arquivo.caminho_relativo` via `paraBanco`).
  - `Source/Vault/Resolucao.cpp`: `resolverCaminhoOriginal()`, `resolverDestino()` (resolução via `doBanco`).
  - `Source/Ui/ProjetoAberto.cpp`: `obterMiniatura()`, `obterArquivoMaster()` (resolução via `doBanco`).
  - `Source/Model/Project.cpp`: Triggers FTS (`trg_arquivo_busca_insert`, `trg_arquivo_busca_delete`) indexando caminhos normalizados.
  - `Source/Catalogo/CatalogoProxies.cpp`: Caminhos relativos de proxies e miniaturas via `paraBanco`/`doBanco`.

### 4.2. Nomes Seguros e Anti-Colisão (`Source/Model/NomesSeguros.h / .cpp`)
Garante que qualquer nome gerado para disco seja válido no Windows e no Mac, evitando caracteres proibidos (`< > : " / \ | ? *`), nomes reservados de dispositivos (`CON`, `PRN`, `AUX`, `NUL`, etc.), espaços/pontos no final e colisões de case-insensitivity.

- **Preservação do Nome Original no Banco**:
  - `Source/Ingest/IngestArquivo.cpp` (`prepararArquivo`): O caminho completo original com o nome original inalterado é sempre gravado em `arquivo.caminho_absoluto_origem`.
  - `Source/Consolidacao/Consolidacao.cpp` (`consolidarItem`): Ao consolidar com renomeação de máscara, a proveniência e o nome original são gravados no `ProjectLog` (`Source/Model/ProjectLog.cpp`).
- **Quarentena e Lixeira do Projeto**:
  - Quarentena: `Source/Consolidacao/MainEdit.cpp` (`quarentenaDir()`, `deletarArquivo()`, `substituirArquivo()`, `restaurar()`, `esvaziarQuarentena()`).
  - Lixeira do Projeto: `Source/Ui/ProjetoAberto.cpp` (`lixeiraDir()`, `moverParaLixeira()`, `esvaziarLixeira()`).

---

## 5. Mídia, Preview, Look & Feel e Empacotamento (Etapa 5)

### 5.1. Áudio e Vídeo
- **Player de Vídeo (`Source/Ui/VideoPlayerBridge_win.cpp`)**: Implementação nativa via Windows Media Foundation (`IMFMediaEngine` / DirectComposition).
- **Áudio QuickTime / Formatos Nativos (`Source/Audio/FormatoAudioQuickTime.cpp`)**: Leitura via Media Foundation (`IMFSourceReader`) com fallback via FFmpeg embutido (`tools/ffmpeg.exe`).
- **Dispositivo de Áudio (`Source/Audio/DispositivoAudioApp.cpp`)**: Suporte nativo a WASAPI no Windows e CoreAudio no macOS.

### 5.2. Visualização e Miniaturas
- **Miniaturas PSD e RAW (`Source/Ingest/MiniaturaPsd_win.cpp`)**: Renderização através do Windows Imaging Component (WIC) com fallback Exiv2.
- **Document Preview (`Source/Ui/DocumentPreviewBridge_win.cpp`)**: Suporte a visualização de documentos e PDF via APIs nativas do Windows.

### 5.3. Interface, Tipografia e DWM
- **Tipografia (`Source/Ui/MatrizLookAndFeel.cpp`)**: `getTypefaceForFont` define a fonte Inter como padrão no Windows (com fallback limpo para Segoe UI), mantendo a tipografia do macOS inalterada.
- **DWM e Barra de Título (`Source/Ui/MainWindow.cpp`)**: `lookAndFeelChanged()` e construtor configuram `DWMWA_USE_IMMERSIVE_DARK_MODE` e `DWMWA_CAPTION_COLOR` com a cor de fundo do tema (`tema().fundo`), integrando a barra de título ao tema escuro.
- **Menu da Janela (`Source/Ui/MainWindow.cpp`)**: No Windows, `setMenuBar(this)` embute a barra de menu completa com a mesma árvore de comandos e atalhos do macOS (Ctrl no lugar de Cmd).
- **Preferências (`Source/App/Preferencias.cpp`)**: Armazenadas em `%APPDATA%\BKR\Matriz` no Windows e `~/Library/Application Support/BKR/Matriz` no macOS.
- **Manifesto de Aplicação (`Assets/app.manifest`)**: Habilitação de `<ws2:longPathAware>true</ws2:longPathAware>` e `<dpiAwareness>PerMonitorV2</dpiAwareness>`, embutido em todos os binários executáveis.

### 5.4. Empacotamento e Distribuição
- **Artefatos Gerados no CI**:
  1. `BKR_Matriz_Setup_v1.5.0.exe` (Instalador Full Windows via Inno Setup).
  2. `BKR_Matriz_Trial_Setup_v1.5.0.exe` (Instalador Trial Windows via Inno Setup).
  3. `BKR_Matriz_Portable_v1.5.0_win64.zip` (Versão Portátil Full com FFmpeg/FFprobe).
  4. `BKR_Matriz_Trial_Portable_v1.5.0_win64.zip` (Versão Portátil Trial com FFmpeg/FFprobe).
- **Binários Externos**: `ffmpeg.exe` e `ffprobe.exe` empacotados na pasta `tools/` com suas respectivas licenças GPL incluídas.
- **Assinatura Digital**: Suporte a assinatura opcional via `signtool` utilizando secret `WINDOWS_SIGNING_CERT_BASE64` (pulando sem erro caso ausente).

---

## 6. Ressalva Conhecida Mantida (Fora do Escopo)

- **Consolidação na Message Thread**: A consolidação continua rodando na message thread, conforme documentado no `HANDOFF.md`. No Windows, um bloqueio acima de ~5 s faz a janela virar "Não está respondendo", e o usuário tende a matar o app no meio do backup. Isso bloqueia a distribuição da versão Windows até a consolidação sair da message thread, que é uma tarefa separada.
