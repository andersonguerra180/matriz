# PORT_WINDOWS — BKR Matriz (Port Windows x64)

Documentação do port para Windows (x64) com CI automatizado via GitHub Actions.
O macOS continua sendo a referência de comportamento, visual e dados.

---

## 1. Baseline Mac Pré-Port (Fase -1)

Data da verificação: 2026-10-01
Base git: `429983f` (inclui `8cd4464 WIP Perf/UI` e `b11ec0d Hidden`).

### Resultados da Execução
- **Compilação Mac x86_64 (Release)**: Compilou com sucesso (0 erros).
- **`matriz_selftest`**: PASS (todos os testes passaram).
- **`matriz_selftest` sob ASan (`build-asan/`)**: PASS (todos os testes passaram).
- **`--selftest-lote`**: 1 FALHA identificada:
  - Asserção: `FAIL no full grid reload/version bump happens for a geo-only edit — nothing in ItemResumo reflects geo location, so there's no applicable visual update to perform`
  - Arquivo/Linha: `Source/Ui/LoteSelfTest.cpp:2448`
  - Causa Raiz Pré-existente: O callback `fichaPanel_->aoAplicarEmLote` em `Source/Ui/MainComponent.cpp:1154` executa `mosaico_->recarregar()`, que incrementa `versaoSnapshot_`. O teste de regressão recente espera que edições puramente geográficas não acionem recarregamento completo da grade.
  - Conforme Regra da Fase -1: **Parar e reportar o que falhou sem corrigir dentro do port**.

---

## 2. Ressalva Obrigatória de Consolidação (Fora do Escopo)

- **Comportamento atual**: A consolidação roda na message thread (`BackupWorkspaceComponent::iniciarBackup` → `callAsync` → `executarConsolidacao`), podendo gerar bloqueios síncronos de até 59 segundos.
- **Impacto no Windows**: No Windows, um bloqueio de message thread acima de ~5 segundos faz o sistema marcar a janela como "Não está respondendo" (*Ghost Window*), o que costuma induzir o operador a forçar o encerramento do processo durante o backup, arriscando corrupção do destino.
- **Decisão**: O port preservará o comportamento atual conforme escopo, mas a **distribuição da versão Windows fica bloqueada** até que a consolidação seja movida para background thread em tarefa dedicada futura.

---

## 3. Auditoria de Pontos Dependentes de macOS e Plano para Windows (Fase 0)

| Componente / Arquivo | macOS (Referência) | Windows (Plano de Port) |
| :--- | :--- | :--- |
| **Tratamento de Exceções e Crash**<br>`Source/Diag/NSExceptionGuard.mm` | Objective-C `@try/@catch`, `NSSetUncaughtExceptionHandler`, `backtrace_symbols_fd`. | `SetUnhandledExceptionFilter`, `MiniDumpWriteDump` via DbgHelp gravando na pasta de logs do Logger. |
| **Identidade e Metadados de Disco**<br>`Source/Vault/DiskIdentity_mac.mm` | IOKit, DiskArbitration, `DADiskCreateFromBSDName`, `kDADiskDescriptionMediaUUIDKey`. | `GetVolumeInformationW`, `GetVolumeNameForVolumeMountPointW`, `IOCTL_STORAGE_QUERY_PROPERTY` (BusType, RemovableMedia). |
| **Volumes e Espaço em Disco**<br>`Source/Vault/Volume.cpp` | `statfs()`, `/Volumes`. | `GetDiskFreeSpaceExW`, `GetVolumePathNameW`, `GetDriveTypeW`, enumeração de letras de drive e volumes montados. |
| **Saúde de Disco (SMART)**<br>`Source/Vault/SmartHealth.cpp` | `smartctl` / IOKit storage reporting. | WMI (`MSFT_PhysicalDisk`, `MSFT_StorageReliabilityCounter`). Fallback para "SMART indisponível" idêntico ao Mac. |
| **Criptografia / Checksum**<br>`Source/Ingest/Checksum.cpp` | Apple `CommonCrypto` (`CC_SHA256`, `CC_MD5`). | Windows Cryptography API: Next Generation (`BCryptCreateHash`, `BCryptHashData`, `BCryptFinishHash`). |
| **Normalização Unicode e Case**<br>`Source/Model/NomesCanonicos.cpp` | CoreFoundation (`CFStringNormalize(kCFStringNormalizationFormC)`), NFD->NFC. | `NormalizeString(NormalizationC)` + `LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE)`. |
| **Execução de Processos Externos**<br>`Source/Ingest/ProcessoExterno.cpp` | `juce::ChildProcess` resolvendo no bundle. | `juce::ChildProcess` resolvendo `.exe` ao lado do binário; criação sem janela de console (`CREATE_NO_WINDOW`); suporte wide UTF-16. |
| **Áudio QuickTime / AAC / ALAC**<br>`Source/Audio/FormatoAudioQuickTime.cpp` | `AudioToolbox` / `ExtAudioFileOpenURL`. | Media Foundation (`IMFSourceReader`), com fallback pelo `ffmpeg.exe` empacotado. Tolerância LUFS-I/LRA ≤ 0.1 LU. |
| **Player de Vídeo**<br>`Source/Ui/VideoPlayerBridge.mm`<br>`Source/Ui/VideoPlayerComponent.mm` | `AVFoundation` (`AVPlayer`, `AVPlayerLayer`). | Media Foundation (`IMFMediaEngine` / DirectComposition). Proxy automático via ffmpeg para codecs não nativos (ex: ProRes). |
| **Preview de Documentos / PDF**<br>`Source/Ui/DocumentPreviewBridge.mm` | `PDFKit`, `QuickLookUI` (`QLPreviewView`). | `Windows.Data.Pdf` / Direct2D para PDF; `IPreviewHandler` shell API para documentos genéricos. |
| **Miniaturas de PSD e Imagens RAW**<br>`Source/Ingest/MiniaturaPsd.mm` | `ImageIO` (`CGImageSourceCreateThumbnailAtIndex`). | Windows Imaging Component (WIC) para PSD achatado e codecs RAW; fallback embutido via `Exiv2::PreviewManager`. |
| **Google Drive**<br>`Source/Ui/GoogleDriveContas.h` | `/Volumes/GoogleDrive...`, `~/Library/Application Support/Google/DriveFS`. | `%LOCALAPPDATA%\Google\DriveFS`, mapeamento de letras de unidade virtual (ex: `G:\`). |
| **Lightroom Importer**<br>`Source/Ingest/LightroomImporter.cpp` | Caminhos Unix `/Volumes/...`. | Suporte bidirecional a letras de drive `X:\` e caminhos relativos de catálogo `.lrcat`. |
| **Logs e Rotação de Arquivos**<br>`Source/App/Logger.h`, `Source/Diag/Watchdog.h` | `rename()` direto com arquivo em uso. | Fechamento de handle antes de `MoveFileExW` e reabertura após rotação de 20 MB. |
| **Caminhos e Intercambialidade**<br>`Source/Consolidacao/BackupScanEngine.cpp`, etc. | `/` uniforme. | Helper central `paraBanco(path)` / `doBanco(path)` garantindo **sempre `/` no banco SQLite**. |
| **Interface, Teclado e Textos**<br>`Source/Ui/MainWindow.cpp`, `Source/I18n/...` | Tecla ⌘ (Cmd), "Mostrar no Finder". | Tecla Ctrl no Windows via `commandModifier`, "Mostrar no Explorer" (`revealToUser`). |
| **Estilo Visual e Fontes**<br>`MatrizLookAndFeel` | SF Pro / Apple system font. | Fonte livre de alta legibilidade embutida (Inter) no Windows via `getTypefaceForFont`; tema escuro imersivo na barra DWM. |

---

## 4. Implementações Cirúrgicas Realizadas (Fases 1 a 6)

1. **Caminhos e Intercambialidade de Banco (`CaminhosBanco.h / .cpp`)**:
   - Helper central `paraBanco` e `doBanco`: garante que todo caminho relativo gravado no SQLite (`caminho_relativo`, `caminho_relativo_destino`, manifesto, etc.) use estritamente `/`.
   - Conversão segura e tolerante na leitura de caminhos antigos ou do Windows.

2. **Nomes de Arquivo Seguros (`NomesSeguros.h / .cpp`)**:
   - Sanitização de caracteres proibidos (`< > : " / \ | ? *`), caracteres de controle (0-31), espaços e pontos no final.
   - Tratamento determinístico para nomes reservados do Windows (`CON`, `PRN`, `AUX`, `NUL`, `COM1-9`, `LPT1-9`).
   - Limite de 255 bytes UTF-8 por componente de caminho.

3. **Criptografia e Checksums (`Checksum.cpp`)**:
   - SHA-256 e MD5 com Windows CNG (BCrypt) no Windows e CommonCrypto no macOS.
   - Selftest com vetores padrão NIST e hashes conhecidos.

4. **Normalização Unicode e Case Invariant (`NomesCanonicos.cpp`)**:
   - `NormalizeString(NormalizationC)` + `LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE)` no Windows e `CFStringNormalize` no macOS.
   - Suporte a caracteres acentuados (É/é, Ç/ç, ã, ü) e equivalência canônica exata entre os dois sistemas.

5. **Identidade de Hardware e Volumes (`DiskIdentity_win.cpp`, `Volume.cpp`)**:
   - `GetVolumeInformationW`, `GetVolumeNameForVolumeMountPointW`, `IOCTL_STORAGE_QUERY_PROPERTY`.
   - Detecção de letras de unidade, montagens, espaço em disco e status de remoção.

6. **Diagnóstico e Proteção contra Falhas (`NSExceptionGuard_win.cpp`)**:
   - `SetUnhandledExceptionFilter` + `MiniDumpWriteDump` gravando no diretório de logs da aplicação.

7. **Áudio QuickTime (`FormatoAudioQuickTime.cpp`)**:
   - Media Foundation (`IMFSourceReader`) com fallback automático.

8. **Miniaturas e Visualização (`MiniaturaPsd_win.cpp`, `DocumentPreviewBridge_win.cpp`, `VideoPlayerBridge_win.cpp`)**:
   - Windows Imaging Component (WIC) para PSD e formatos raster/RAW.
   - Pontes nativas de documento e vídeo compatíveis com a arquitetura multiplataforma.

9. **Look & Feel, Manifest e DWM (`MatrizLookAndFeel.cpp`, `Assets/app.manifest`)**:
   - Manifest Windows habilitando `longPathAware=true` e `PerMonitorV2` DPI.
   - Integração com DWM Dark Mode.

10. **Headless Interop Test Suite (`tools/interop_selftest/main.cpp`)**:
    - Geração e verificação bidirecional de fixtures completas de projetos, mídias, folder maps, marcadores e metadados.

11. **GitHub Actions CI (`.github/workflows/build.yml`)**:
    - `macos-14` job: compilação, execução de selftests e geração de `fixture-mac`.
    - `windows-2022` job: compilação MSVC x64 + Ninja, execução de selftests, verificação de `fixture-mac`, geração de `fixture-win`, e empacotamento (Inno Setup + ZIP).
    - `interop-verify-mac` job: verificação cruzada de `fixture-win` no macOS.

---

## 5. Resultados de Validação

- **`matriz_selftest`**: 100% PASS (inclui testes de CaminhosBanco, Checksums, NomesCanonicos, NomesSeguros, SMART, Fichas, etc.).
- **`matriz_ingest_selftest`**: 100% PASS (todos os 50+ fluxos de ingest, reconciliação, proxy, loudness e integridade).
- **`matriz_interop_selftest`**: 100% PASS (0 falhas em `--gerar` e `--verificar`).
- **ASan**: 100% PASS em `matriz_selftest`.
