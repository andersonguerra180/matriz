# PORT_WINDOWS — BKR Matriz (Port Windows x64)

Documentação do port para Windows (x64) com CI automatizado via GitHub Actions.
O macOS é a referência estrita de comportamento, visual e dados.

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

1. **Nomes e Normalização Unicode**: Nomes acentuados em NFD/NFC (ex: `Gravação_Épica_Ação`), maiúsculas e minúsculas misturadas (`CaSe_TeSt`), caminhos profundos na árvore (> 260 caracteres).
2. **Ficha de Metadados e Marcadores**: Título, descrição, etiquetas/tags, pessoas, lugares, data, notas estruturadas e marcadores de tempo (P, W, K, R).
3. **Itens Ocultos (CONTENT = Hidden)**: Estado `escondido` preservado sem alteração na importação/exportação entre plataformas.
4. **Folder Maps**: Associação e hierarquias em mapas de pasta manuais.
5. **MAKE BACKUP e Abertura em Outra Raiz**: Projeto criado em raiz Windows (`C:\...`) ou macOS (`/Volumes/...`), com `destination.json` e `destination_id` permitindo resolução correta em raiz arbitrária.
6. **Relink de Origens**: Reconciliação e relink de caminhos em volumes locais e remotos.
7. **Export de Pacote e Ingestão por Intake**: Geração de arquivo de exportação e leitura idêntica no outro sistema operacional.
8. **Checksums e Miniaturas**: Verificação SHA-256 e MD5 idênticos em ambos os sistemas.
9. **Caminhos Canônicos no SQLite**: 0 caminhos relativos contendo contra-barra (`\`) no banco de dados.
10. **Sanidade SQLite**: Transações e checkpoint sem resíduos de arquivo `-wal` pendente.

---

## 4. Auditoria e Aplicação dos Helpers (Etapa 4)

### 4.1. Caminhos de Banco (`Source/Model/CaminhosBanco.h / .cpp`)
Garante que todo caminho relativo gravado em banco utilize estritamente barras normais (`/`), sanitizando leituras e gravações.

- **Pontos de Gravação e Leitura no Banco**:
  - `Source/Ingest/IngestArquivo.cpp`: `arquivo.caminho_relativo` e `arquivo.caminho_absoluto_origem` (via `paraBanco`).
  - `Source/Consolidacao/Consolidacao.cpp`: `consolidacao_registro.caminho_relativo_destino` (via `paraBanco`).
  - `Source/Consolidacao/PacoteCollection.cpp`: Manifesto de exportação e importação de pacotes.
  - `Source/Vault/AssetRelinkEngine.cpp`: Caminhos de relink e reconciliação de volumes.
  - `Source/Consolidacao/BackupScanEngine.cpp`: Varredura incremental de mídias consolidadas.
  - `Source/Catalogo/CatalogoProxies.cpp`: Caminhos relativos de proxies e miniaturas.

### 4.2. Nomes Seguros e Anti-Colisão (`Source/Model/NomesSeguros.h / .cpp`)
Garante que qualquer nome gerado para disco seja válido no Windows e no Mac, evitando caracteres proibidos (`< > : " / \ | ? *`), nomes reservados de dispositivos (`CON`, `PRN`, `AUX`, `NUL`, etc.), espaços/pontos no final e colisões de case-insensitivity.

- **Pontos Auditados**:
  - Folder map export e criação de pastas físicas (`Consolidacao.cpp`).
  - Nomenclatura de arquivos no MAKE BACKUP e CLONE (`Mascara.cpp`, `Consolidacao.cpp`).
  - Exportação e recortes de áudio/vídeo (`PacoteCollection.cpp`).
  - Movimentação de quarentena e lixeira do projeto.
  - Testes unitários com casos `É/é`, `Ç/ç`, decompostos NFD vs compostos NFC, `ã`, `ü` rodando nos dois sistemas com equivalência canônica exata (`NomesCanonicos_test.cpp` / `matriz_selftest`).

---

## 5. Mídia, Preview, Look & Feel e Empacotamento (Etapa 5)

### 5.1. Áudio e Vídeo
- **Player de Vídeo (`Source/Ui/VideoPlayerBridge_win.cpp`)**: Implementação nativa via Windows Media Foundation (`IMFMediaEngine` / DirectComposition).
- **Áudio QuickTime (`Source/Audio/FormatoAudioQuickTime.cpp`)**: Leitura via Media Foundation (`IMFSourceReader`) com fallback via FFmpeg embutido.

### 5.2. Visualização e Miniaturas
- **Miniaturas PSD e RAW (`Source/Ingest/MiniaturaPsd_win.cpp`)**: Renderização através do Windows Imaging Component (WIC) com fallback Exiv2.
- **Document Preview (`Source/Ui/DocumentPreviewBridge_win.cpp`)**: Suporte nativo para documentos e PDF.

### 5.3. Interface e Atalhos
- **Atalhos e Ações (`Source/Ui/MainWindow.cpp`, `Source/I18n/...`)**: Mapeamento do modificador principal para `Ctrl` no Windows e `⌘` no Mac; textos "Mostrar no Explorer" no Windows e "Mostrar no Finder" no macOS.
- **Tema Visual e DWM (`Source/Ui/MatrizLookAndFeel.cpp`)**: Ativação do modo escuro na barra de título do Windows via `DwmSetWindowAttribute` (`DWMWA_USE_IMMERSIVE_DARK_MODE`).
- **Manifesto de Aplicação (`Assets/app.manifest`)**: Habilitação de `<ws2:longPathAware>true</ws2:longPathAware>` e `<dpiAwareness>PerMonitorV2</dpiAwareness>`, embutido em todos os alvos executáveis.

### 5.4. Empacotamento e Distribuição
- **Artefatos Gerados no CI**:
  1. `BKR_Matriz_Setup_v1.5.0.exe` (Instalador Full Windows via Inno Setup).
  2. `BKR_Matriz_Trial_Setup_v1.5.0.exe` (Instalador Trial Windows via Inno Setup).
  3. `BKR_Matriz_Portable_v1.5.0_win64.zip` (Versão Portátil Full com FFmpeg/FFprobe).
  4. `BKR_Matriz_Trial_Portable_v1.5.0_win64.zip` (Versão Portátil Trial com FFmpeg/FFprobe).
- **Binários Externos**: `ffmpeg.exe` e `ffprobe.exe` empacotados na pasta `tools/` com suas respectivas licenças GPL incluídas.

---

## 6. Ressalva Conhecida Mantida (Fora do Escopo)

- **Consolidação na Message Thread**: A consolidação continua rodando na message thread, conforme deliberado para a entrega do port. No Windows, operações longas de I/O podem gerar alerta temporário de "Não respondendo" caso o volume de dados seja muito volumoso. A migração para background thread permanece como tarefa separada.
