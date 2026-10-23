cmake_minimum_required(VERSION 3.22)
project(MATRIZ VERSION 1.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "" FORCE)
elseif(MSVC)
    add_compile_options(/utf-8 /EHsc)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    if(MATRIZ_ASAN)
        add_compile_definitions(_DISABLE_VECTOR_ANNOTATION _DISABLE_STRING_ANNOTATION)
    endif()
endif()

# Locate JUCE
set(JUCE_DIR "$ENV{HOME}/JUCE" CACHE PATH "Path to JUCE")
if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(STATUS "JUCE not found locally at '${JUCE_DIR}', fetching via FetchContent (v8.0.14)...")
    include(FetchContent)
    FetchContent_Declare(
        juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.14
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(juce)
else()
    add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake")

# ------------------------------------------------------------------------------
# AddressSanitizer opcional
# ------------------------------------------------------------------------------
option(MATRIZ_ASAN "Compila os alvos do MATRIZ com AddressSanitizer" OFF)
option(MATRIZ_TRIAL "Compila a versao Trial do MATRIZ" OFF)

function(matriz_aplicar_asan alvo)
    if(MATRIZ_ASAN)
        if(MSVC)
            target_compile_options(${alvo} PRIVATE /fsanitize=address /Zi)
            target_compile_definitions(${alvo} PRIVATE _DISABLE_VECTOR_ANNOTATION _DISABLE_STRING_ANNOTATION)
        else()
            target_compile_options(${alvo} PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
            target_link_options(${alvo} PRIVATE -fsanitize=address)
        endif()
    endif()
endfunction()

# ------------------------------------------------------------------------------
# ThreadSanitizer opcional (macOS / Clang)
# ------------------------------------------------------------------------------
option(MATRIZ_TSAN "Compila os alvos do MATRIZ com ThreadSanitizer" OFF)

function(matriz_aplicar_tsan alvo)
    if(MATRIZ_TSAN)
        if(NOT MSVC)
            target_compile_options(${alvo} PRIVATE -fsanitize=thread -g)
            target_link_options(${alvo} PRIVATE -fsanitize=thread)
        endif()
    endif()
endfunction()

# ------------------------------------------------------------------------------
# Schema do banco embutido no binário
# ------------------------------------------------------------------------------
juce_add_binary_data(MatrizSchemaAssets SOURCES
    schema/registro.sql
    schema/indice.sql)

# ------------------------------------------------------------------------------
# Strings de interface embutidas no binário
# ------------------------------------------------------------------------------
juce_add_binary_data(MatrizI18nAssets
    NAMESPACE I18nBinaryData
    HEADER_NAME I18nBinaryData.h
    SOURCES i18n/en.yaml i18n/pt_BR.yaml)

# Ícones do menu IMPORT (Assets/Icons/container_icons/ci_<categoria>_<nome>.png): um recurso embutido por arquivo
# (ci_aud_midi.png -> AssetsBinaryData::ci_aud_midi_png). Trocar um PNG exige recompilar; arquivo novo, reconfigurar.
file(GLOB MATRIZ_ICONES_IMPORTAR CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Icons/container_icons/*.png")

# Ícone genérico do card Link (vídeo): só entra no binário se o arquivo existir (GLOB reavalia ao soltar o arquivo na pasta).
file(GLOB MATRIZ_ICONE_VIDEOWEB CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Icons/videoweb.png")

# Assets embutidos no binário
juce_add_binary_data(MatrizAssets
    NAMESPACE AssetsBinaryData
    HEADER_NAME AssetsBinaryData.h
    SOURCES Assets/splash.png Assets/icon.jpeg Assets/googledrive.png Assets/lrlogo.png Assets/bridgelogo.png Assets/home.png Assets/geo.png
        Assets/gallery.png
        Assets/whatsapp.png
        Assets/android.png
        Assets/apple.png
        Assets/whatsapp.jpg
        ${MATRIZ_ICONES_IMPORTAR}
        ${MATRIZ_ICONE_VIDEOWEB}
        # Logos das redes sociais do menu IMPORT
        Assets/Icons/instagram.jpeg
        Assets/Icons/meta.png
        Assets/Icons/tiktok.jpeg
        Assets/Icons/x.png
        # Logos originais Adobe, YouTube e o ícone Tools do menu IMPORT
        Assets/Icons/lrlogo_original.png
        Assets/Icons/youtube.png
        Assets/Icons/Tools.png
        "Assets/Icons/Film Sound.png"
        "Assets/Icons/Tape Cassette Rip.png"
        Assets/Icons/Other.png
        Assets/Icons/DDP.png
        # Logos por tipo de arquivo (LogosTipoArquivo.cpp): embutidos pra funcionar em qualquer Mac
        "Assets/ableton live.png"
        "Assets/adobeaftereffects.png"
        "Assets/adobepremiere.png"
        "Assets/corel.jpeg"
        "Assets/excel.jpeg"
        "Assets/groovesculptor.png"
        "Assets/indesign.jpg"
        "Assets/illustrator.jpg"
        "Assets/capcut.jpeg"
        "Assets/finalcutpro.jpg"
        "Assets/izotope.png"
        "Assets/logic.png"
        "Assets/pdf.jpeg"
        "Assets/photoshop.jpeg"
        "Assets/pro tools.png"
        "Assets/puredata.png"
        "Assets/reaper.png"
        "Assets/vegas.jpeg"
        "Assets/3d.jpg"
        "Assets/zip.jpeg"
        "Assets/word.jpeg"
        # Jost (OFL): fonte única da interface, embutida para ser igual em macOS/Windows/Linux
        Assets/fonts/Jost-Regular.ttf
        Assets/fonts/Jost-Medium.ttf
        Assets/fonts/Jost-Bold.ttf
        # IBM Plex (OFL): fonte padrão da interface (Sans) e caminhos/hashes/timecodes (Mono)
        Assets/fonts/IBMPlexSans-Regular.ttf
        Assets/fonts/IBMPlexSans-Medium.ttf
        Assets/fonts/IBMPlexSans-SemiBold.ttf
        Assets/fonts/IBMPlexMono-Regular.ttf)

# Fichas (fichas/*.yaml) embutidas no binário (Ui/FichasEmbutidas.cpp): o app não depende da pasta do
# código-fonte. Glob com CONFIGURE_DEPENDS: acrescentar um tipo continua sendo só escrever um YAML.
file(GLOB MATRIZ_FICHAS_YAML CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fichas/*.yaml")
juce_add_binary_data(MatrizFichasAssets
    NAMESPACE FichasBinaryData
    HEADER_NAME FichasBinaryData.h
    SOURCES ${MATRIZ_FICHAS_YAML})
# Página web que o celular recebe ao ler o QR code (HTML+CSS+JS num arquivo só, dentro do binário).
juce_add_binary_data(MatrizCelularAssets
    NAMESPACE CelularBinaryData
    HEADER_NAME CelularBinaryData.h
    SOURCES Source/Celular/pagina_celular.html)

# ------------------------------------------------------------------------------
# Etapa 1 — self-test headless
# ------------------------------------------------------------------------------
juce_add_console_app(matriz_selftest PRODUCT_NAME "matriz_selftest")
juce_generate_juce_header(matriz_selftest)

target_sources(matriz_selftest PRIVATE
    tools/selftest/main.cpp
    Source/Ficha/FichaDefinition.cpp
    Source/Ficha/CatalogoDeFichas.cpp
    Source/Db/Database.cpp
    Source/Model/Project.cpp
    Source/Model/CaminhosBanco.cpp
    Source/Model/NomesCanonicos.cpp
    Source/Model/NomesSeguros.cpp
    Source/I18n/Strings.cpp
    Source/App/Preferencias.cpp
    Source/Ingest/LeituraTecnica.cpp
    Source/Ingest/Loudness.cpp
    Source/Ingest/ProcessoExterno.cpp
    Source/Ingest/Checksum.cpp
    Source/Vault/Volume.cpp
    Source/Vault/SmartHealth.cpp
    Source/Vault/DeviceUsageLog.cpp
    Source/Model/ProjectLog.cpp)

if(APPLE)
    target_sources(matriz_selftest PRIVATE Source/Vault/DiskIdentity_mac.mm)
elseif(WIN32)
    target_sources(matriz_selftest PRIVATE Source/Vault/DiskIdentity_win.cpp)
endif()

target_include_directories(matriz_selftest PRIVATE Source)

target_compile_definitions(matriz_selftest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="MATRIZ selftest"
    JUCE_APPLICATION_VERSION_STRING="0.1.0"
    MATRIZ_FICHAS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fichas"
    # Teste do ProcessoExterno (ffprobe/ffmpeg/powershell/sleep): resolve pelo PATH, como o ingest_selftest. A regra de
    # produção (a ferramenta tem que estar ao lado do executável) não é o que este alvo testa.
    MATRIZ_DEV_BUILD=1)

target_link_libraries(matriz_selftest PRIVATE
    MatrizSchemaAssets
    MatrizI18nAssets
    matriz_sqlite3
    yaml-cpp::yaml-cpp
    Exiv2::exiv2lib
    juce::juce_core
    juce::juce_data_structures
    juce::juce_audio_formats
    juce::juce_graphics
    juce::juce_events
    $<$<PLATFORM_ID:Darwin>:-framework\ DiskArbitration>
    $<$<PLATFORM_ID:Darwin>:-framework\ IOKit>
    $<$<PLATFORM_ID:Windows>:bcrypt>
    $<$<PLATFORM_ID:Windows>:dbghelp>
    $<$<PLATFORM_ID:Windows>:psapi>
    PUBLIC
    juce::juce_recommended_config_flags)

matriz_aplicar_asan(matriz_selftest)
matriz_aplicar_tsan(matriz_selftest)

if(MSVC)
    target_link_options(matriz_selftest PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/Assets/app.manifest")
endif()

# ------------------------------------------------------------------------------
# Etapa 2 — self-test headless de ingestão
# ------------------------------------------------------------------------------
juce_add_console_app(matriz_ingest_selftest PRODUCT_NAME "matriz_ingest_selftest")
juce_generate_juce_header(matriz_ingest_selftest)

target_sources(matriz_ingest_selftest PRIVATE
    tools/ingest_selftest/main.cpp
    Source/Ingest/Checksum.cpp
    Source/Ingest/CacheArquivo.cpp
    Source/Ingest/ProcessoExterno.cpp
    Source/Ingest/LeituraTecnica.cpp
    Source/Ingest/Loudness.cpp
    Source/Ingest/Miniaturas.cpp
    Source/Ingest/Duplicata.cpp
    Source/Ingest/Pcm.cpp
    Source/Ingest/ClassificadorFalaMusica.cpp
    Source/Ingest/InferenciaEstrutura.cpp
    Source/Ingest/IngestArquivo.cpp
    Source/Ingest/FluxoLote.cpp
    Source/Ingest/PainelInconsistencias.cpp
    Source/Preservation/Preservation.cpp
    Source/Vault/Volume.cpp
    Source/Vault/SmartHealth.cpp
    Source/Vault/DeviceUsageLog.cpp
    Source/Vault/Resolucao.cpp
    Source/Vault/Reconciliacao.cpp
    Source/Vault/AssetRelinkEngine.cpp
    Source/Publicacao/Publicacao.cpp
    Source/Consolidacao/Mascara.cpp
    Source/Consolidacao/Consolidacao.cpp
    Source/Consolidacao/LimpezaImagem.cpp
    Source/Consolidacao/SaidaBounce.cpp
    Source/Consolidacao/BackupScanEngine.cpp
    Source/Consolidacao/MetadadoEmbutido.cpp
    Source/Consolidacao/BridgeXmp.cpp
    Source/Consolidacao/PacoteCollection.cpp
    Source/Catalogo/CatalogoProxies.cpp
    Source/Ficha/FichaDefinition.cpp
    Source/Ficha/CatalogoDeFichas.cpp
    Source/Db/Database.cpp
    Source/Analytics/AssetGeolocation.cpp
    Source/Ui/OriginalSourceMedium.cpp
    Source/I18n/Strings.cpp
    Source/Model/ProjectLog.cpp
    Source/Model/NotasEstruturadas.cpp
    Source/Model/NomesCanonicos.cpp
    Source/Model/CaminhosBanco.cpp
    Source/Model/NomesSeguros.cpp
    Source/Model/Project.cpp)

if(APPLE)
    target_sources(matriz_ingest_selftest PRIVATE
        Source/Vault/DiskIdentity_mac.mm
        Source/Ingest/MiniaturaPsd.mm
        Source/Ingest/MiniaturaDocumento_mac.mm)
elseif(WIN32)
    target_sources(matriz_ingest_selftest PRIVATE
        Source/Vault/DiskIdentity_win.cpp
        Source/Ingest/MiniaturaPsd_win.cpp
        Source/Ui/PdfRenderer_win.cpp
        Source/Ingest/MiniaturaDocumento_win.cpp)
endif()

target_include_directories(matriz_ingest_selftest PRIVATE Source)

target_compile_definitions(matriz_ingest_selftest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="MATRIZ ingest selftest"
    JUCE_APPLICATION_VERSION_STRING="0.1.0"
    MATRIZ_FICHAS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fichas"
    MATRIZ_DEV_BUILD=1)

target_link_libraries(matriz_ingest_selftest PRIVATE
    MatrizSchemaAssets
    matriz_sqlite3
    yaml-cpp::yaml-cpp
    Exiv2::exiv2lib
    juce::juce_core
    juce::juce_cryptography
    juce::juce_dsp
    juce::juce_graphics
    juce::juce_audio_formats
    $<$<PLATFORM_ID:Darwin>:-framework\ DiskArbitration>
    $<$<PLATFORM_ID:Darwin>:-framework\ IOKit>
    $<$<PLATFORM_ID:Darwin>:-framework\ PDFKit>
    $<$<PLATFORM_ID:Darwin>:-framework\ ImageIO>
    $<$<PLATFORM_ID:Darwin>:-framework\ QuickLook>
    $<$<PLATFORM_ID:Windows>:bcrypt>
    $<$<PLATFORM_ID:Windows>:dbghelp>
    $<$<PLATFORM_ID:Windows>:psapi>
    $<$<PLATFORM_ID:Windows>:windowscodecs>
    $<$<PLATFORM_ID:Windows>:RuntimeObject>
    $<$<PLATFORM_ID:Windows>:windowsapp>
    PUBLIC
    juce::juce_recommended_config_flags)

matriz_aplicar_asan(matriz_ingest_selftest)
matriz_aplicar_tsan(matriz_ingest_selftest)

if(MSVC)
    target_link_options(matriz_ingest_selftest PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/Assets/app.manifest")
endif()

# ------------------------------------------------------------------------------
# Interoperabilidade Headless (matriz_interop_selftest)
# ------------------------------------------------------------------------------
juce_add_console_app(matriz_interop_selftest PRODUCT_NAME "matriz_interop_selftest")
juce_generate_juce_header(matriz_interop_selftest)

target_sources(matriz_interop_selftest PRIVATE
    tools/interop_selftest/main.cpp
    Source/Model/Project.cpp
    Source/Model/CaminhosBanco.cpp
    Source/Model/NomesCanonicos.cpp
    Source/Model/NomesSeguros.cpp
    Source/Ingest/Checksum.cpp
    Source/Ingest/LeituraTecnica.cpp
    Source/Ingest/Loudness.cpp
    Source/Ingest/ProcessoExterno.cpp
    Source/Db/Database.cpp
    Source/I18n/Strings.cpp
    Source/App/Preferencias.cpp
    Source/Model/ProjectLog.cpp
    Source/Vault/Volume.cpp
    Source/Vault/SmartHealth.cpp
    Source/Vault/DeviceUsageLog.cpp
    Source/Vault/Resolucao.cpp
    Source/Vault/AssetRelinkEngine.cpp
    Source/Consolidacao/PacoteCollection.cpp
    Source/Consolidacao/Consolidacao.cpp
    Source/Consolidacao/LimpezaImagem.cpp
    Source/Consolidacao/SaidaBounce.cpp
    Source/Consolidacao/MetadadoEmbutido.cpp
    Source/Consolidacao/BridgeXmp.cpp
    Source/Consolidacao/Mascara.cpp
    Source/Analytics/AssetGeolocation.cpp
    Source/Preservation/Preservation.cpp)

if(APPLE)
    target_sources(matriz_interop_selftest PRIVATE Source/Vault/DiskIdentity_mac.mm)
elseif(WIN32)
    target_sources(matriz_interop_selftest PRIVATE Source/Vault/DiskIdentity_win.cpp)
endif()

target_include_directories(matriz_interop_selftest PRIVATE Source)

target_compile_definitions(matriz_interop_selftest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="MATRIZ Interop Selftest"
    JUCE_APPLICATION_VERSION_STRING="0.1.0")

target_link_libraries(matriz_interop_selftest PRIVATE
    MatrizSchemaAssets
    MatrizI18nAssets
    matriz_sqlite3
    yaml-cpp::yaml-cpp
    Exiv2::exiv2lib
    juce::juce_core
    juce::juce_cryptography
    juce::juce_data_structures
    juce::juce_audio_formats
    juce::juce_graphics
    juce::juce_events
    $<$<PLATFORM_ID:Darwin>:-framework\ DiskArbitration>
    $<$<PLATFORM_ID:Darwin>:-framework\ IOKit>
    $<$<PLATFORM_ID:Windows>:bcrypt>
    $<$<PLATFORM_ID:Windows>:dbghelp>
    $<$<PLATFORM_ID:Windows>:psapi>
    PUBLIC
    juce::juce_recommended_config_flags)

matriz_aplicar_asan(matriz_interop_selftest)
matriz_aplicar_tsan(matriz_interop_selftest)

if(MSVC)
    target_link_options(matriz_interop_selftest PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/Assets/app.manifest")
endif()

# ------------------------------------------------------------------------------
# Etapa 3 — app principal (matriz)
# ------------------------------------------------------------------------------
if(MATRIZ_TRIAL)
    set(MATRIZ_PRODUCT_NAME "BKR Matriz Trial")
    set(MATRIZ_BUNDLE_ID "com.bkr.matriz.trial")
    set(MATRIZ_VERSION_STRING "1.5.0 TRIAL")
    set(MATRIZ_UI_APP_NAME "BKR Matriz Trial (NOT FOR SALE)")
    set(MATRIZ_TRIAL_DEF "MATRIZ_IS_TRIAL=1")
else()
    set(MATRIZ_PRODUCT_NAME "BKR Matriz")
    set(MATRIZ_BUNDLE_ID "com.bkr.matriz")
    set(MATRIZ_VERSION_STRING "1.5.0")
    set(MATRIZ_UI_APP_NAME "BKR Matriz")
    set(MATRIZ_TRIAL_DEF "MATRIZ_IS_TRIAL=0")
endif()

# .mtz (Coleção) e .bkm (Catálogo) com o ícone do Matriz no Finder; duplo clique abre no app
# (MatrizApplication::anotherInstanceStarted). Não usa DOCUMENT_EXTENSIONS do JUCE: ele aponta o ícone para
# "Icon", que não existe no bundle (o ícone é AppIcon.icns, copiado no POST_BUILD abaixo).
set(MATRIZ_PLIST_DOCUMENTOS [=[<plist><dict>
<key>CFBundleDocumentTypes</key>
<array>
  <dict>
    <key>CFBundleTypeName</key><string>BKR Matriz Collection</string>
    <key>CFBundleTypeRole</key><string>Editor</string>
    <key>LSHandlerRank</key><string>Owner</string>
    <key>CFBundleTypeIconFile</key><string>AppIcon</string>
    <key>LSItemContentTypes</key><array><string>com.bkr.matriz.collection</string></array>
  </dict>
  <dict>
    <key>CFBundleTypeName</key><string>BKR Matriz Catalog</string>
    <key>CFBundleTypeRole</key><string>Editor</string>
    <key>LSHandlerRank</key><string>Owner</string>
    <key>CFBundleTypeIconFile</key><string>AppIcon</string>
    <key>LSItemContentTypes</key><array><string>com.bkr.matriz.catalog</string></array>
  </dict>
</array>
<key>UTExportedTypeDeclarations</key>
<array>
  <dict>
    <key>UTTypeIdentifier</key><string>com.bkr.matriz.collection</string>
    <key>UTTypeDescription</key><string>BKR Matriz Collection</string>
    <key>UTTypeConformsTo</key><array><string>public.data</string></array>
    <key>UTTypeIconFile</key><string>AppIcon</string>
    <key>UTTypeTagSpecification</key><dict><key>public.filename-extension</key><array><string>mtz</string></array></dict>
  </dict>
  <dict>
    <key>UTTypeIdentifier</key><string>com.bkr.matriz.catalog</string>
    <key>UTTypeDescription</key><string>BKR Matriz Catalog</string>
    <key>UTTypeConformsTo</key><array><string>public.data</string></array>
    <key>UTTypeIconFile</key><string>AppIcon</string>
    <key>UTTypeTagSpecification</key><dict><key>public.filename-extension</key><array><string>bkm</string></array></dict>
  </dict>
</array>
</dict></plist>]=])

juce_add_gui_app(matriz
    PRODUCT_NAME "${MATRIZ_PRODUCT_NAME}"
    PLIST_TO_MERGE "${MATRIZ_PLIST_DOCUMENTOS}"
    VERSION 1.5.0
    COMPANY_NAME "BKR"
    BUNDLE_ID "${MATRIZ_BUNDLE_ID}"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Icon.iconset/icon_256x256.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Icon.iconset/icon_32x32.png")
juce_generate_juce_header(matriz)

if(APPLE)
    # Replace JUCE-generated icns with our pre-built one (has all required sizes)
    add_custom_command(TARGET matriz POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon.icns"
            "$<TARGET_BUNDLE_DIR:matriz>/Contents/Resources/AppIcon.icns"
        COMMENT "Copying pre-built AppIcon.icns into app bundle")
endif()

target_sources(matriz PRIVATE
    Source/Main.cpp
    Source/App/Preferencias.cpp
    Source/I18n/Strings.cpp
    Source/Ui/AboutDialog.cpp
    Source/Ui/HelpDialog.cpp
    Source/Ui/BatchWatermarkDialog.cpp
    Source/Ui/PublishHtmlDialog.cpp
    Source/Ui/ExportZipDialog.cpp
    Source/Ui/SendToPrintDialog.cpp
    Source/Imagem/ProcessamentoImagem.cpp
    Source/Ui/TrialNagDialog.cpp
    Source/Ui/Tokens.h
    Source/Ui/Tokens.cpp
    Source/Ui/MatrizLookAndFeel.h
    Source/Ui/MatrizLookAndFeel.cpp
    Source/Ui/ProjetoAberto.cpp
    Source/Ui/MainEditPanel.cpp
    Source/Ui/AcoesItem.cpp
    Source/Ui/PastaPickerWindow.cpp
    Source/Ui/ArvoreComponent.cpp
    Source/Ui/BarraFerramentasComponent.cpp
    Source/Ui/BarraGuiaComponent.cpp
    Source/Ui/BarraNavegacaoComponent.cpp
    Source/Ui/BarraProgressoGlobalComponent.cpp
    Source/Ui/ProgressoGlobal.cpp
    Source/Ui/TarefaGlobalModalDialog.cpp
    Source/Ui/BarraSelecaoComponent.cpp
    Source/Ui/FiltrosComponent.cpp
    Source/Ui/MetadadosOriginaisComponent.cpp
    Source/Ui/MosaicoComponent.cpp
    Source/Ui/EstatisticasComponent.cpp
    Source/Ui/VulnerabilidadesDialog.cpp
    Source/Ui/AnalyticsTreemapComponent.cpp
    Source/Ui/AnalyticsTableComponent.cpp
    Source/Ui/AnalyticsChartComponent.cpp
    Source/Ui/GeoMapComponent.cpp
    Source/Ui/CollectionStatsDashboardComponent.cpp
    Source/Analytics/AssetGeolocation.cpp
    Source/Analytics/AnalyticsFormatter.cpp
    Source/Analytics/AnalyticsEngine.cpp
    Source/Analytics/AnalyticsExport.cpp
    Source/Analytics/SavedAnalyses.cpp
    Source/Ui/ArvoreBackupComponent.cpp
    Source/Ui/TrashWindow.cpp
    Source/Ui/JanelaPastaFlutuante.cpp
    Source/Ui/MosaicoStressTest.cpp
    Source/Ui/IngerirArquivosTest.cpp
    Source/Ui/UiSelfTest.cpp
    Source/Ui/LoteSelfTest.cpp
    Source/Ui/BounceSelfTest.cpp
    Source/Ui/VerifySourceSelfTest.cpp
    Source/Ui/RebuildSelfTest.cpp
    Source/Ui/ManifestoSelfTest.cpp
    Source/Ui/LogosTipoArquivo.cpp
    Source/Ui/FichasEmbutidas.cpp
    Source/Ui/ModalLoopSelfTest.cpp
    Source/Ui/OverlayComponent.cpp
    Source/Ui/AudioWorkspace.cpp
    Source/Ui/BackupFileSelectorDialog.cpp
    Source/Ui/BackupWorkspaceComponent.cpp
    Source/Ui/BackupVersionsComponent.cpp
    Source/Ui/BackupSyncDialog.cpp
    Source/Ui/BackupRecoveryDialog.cpp
    Source/Ui/CatalogHubComponent.cpp
    Source/Ui/CatalogWorkspaceComponent.cpp
    Source/Ui/IntakeWorkspaceComponent.cpp
    Source/Ui/PainelMetadadosArquivo.cpp
    Source/Ui/CartoesAnalyticsComponent.cpp
    Source/Ui/ImportarGeoFavoritosDialogo.cpp
    Source/Ui/StorageWorkspaceComponent.cpp
    Source/Ui/DuplicatesWorkspaceComponent.cpp
    Source/Ui/DuplicateResolutionDialog.cpp
    Source/Ui/HierarquiaEditorComponent.cpp
    Source/Ui/TagChipsEditor.cpp
    Source/Ui/PeoplePickerComponent.cpp
    Source/Ui/AutoCompleteTextEditor.cpp
    Source/Ui/OriginalSourceMedium.cpp
    Source/Ui/FichaPanelComponent.cpp
    Source/Ui/HomePanelComponent.cpp
    Source/Ui/IngestWizardComponent.cpp
    Source/Ui/PainelInconsistenciasComponent.cpp
    Source/Ui/PainelDuplicatasComponent.cpp
    Source/Ui/PreviewComponent.cpp
    Source/Ui/PreservationWorkspaceComponent.cpp
    Source/Ui/NovoProjetoDialogo.cpp
    Source/Ui/BarraMetricasComponent.cpp
    Source/Ui/TimelineComponent.cpp
    Source/Ui/NavegadorArquivosComponent.cpp
    Source/Ui/NavegadorArquivosDialogo.cpp
    Source/Ui/FormatoTempo.cpp
    Source/Ui/ConfiguracoesProjetoDialogo.cpp
    Source/Ui/CatalogoComponent.cpp
    Source/Ui/ConsolidacaoDialogo.cpp
    Source/Ui/SelecionarTipoMidiaDialogo.cpp
    Source/Ui/TiposContainer.cpp
    Source/Ui/InitialRelinkDialog.cpp
    Source/Ui/OfflineAssetRelinkDialog.cpp
    Source/Ui/MainComponent.cpp
    Source/Ui/MainWindow.cpp
    Source/Ui/VideoPlayerComponent.cpp
    Source/Ui/DocumentPreviewComponent.cpp
    Source/Audio/MotorReproducao.cpp
    Source/Audio/FormatoAudioQuickTime.cpp
    Source/Audio/DispositivoAudioApp.cpp
    Source/Ingest/FluxoLote.cpp
    Source/Ingest/IngestArquivo.cpp
    Source/Ingest/LeituraTecnica.cpp
    Source/Model/NotasEstruturadas.cpp
    Source/Model/CompactacaoRegistro.cpp
    Source/Model/NomesCanonicos.cpp
    Source/Model/CaminhosBanco.cpp
    Source/Model/NomesSeguros.cpp
    Source/Model/MergeFichas.cpp
    Source/Ui/ConflitosMergeDialog.cpp
    Source/Ui/NotesEstruturadasComponent.cpp
    Source/Ingest/LightroomImporter.cpp
    Source/Ingest/Loudness.cpp
    Source/Ingest/Checksum.cpp
    Source/Ingest/CacheArquivo.cpp
    Source/Ingest/AiScan.cpp
    Source/Ingest/ProcessoExterno.cpp
    Source/Ingest/PainelInconsistencias.cpp
    Source/Preservation/Preservation.cpp
    Source/Ingest/Miniaturas.cpp
    Source/Ingest/ClassificadorFalaMusica.cpp
    Source/Ingest/Pcm.cpp
    Source/Ingest/Duplicata.cpp
    Source/Vault/Volume.cpp
    Source/Vault/SmartHealth.cpp
    Source/Vault/DeviceUsageLog.cpp
    Source/Vault/Resolucao.cpp
    Source/Vault/Reconciliacao.cpp
    Source/Vault/AssetRelinkEngine.cpp
    Source/Vault/RescanEngine.cpp
    Source/Vault/VerifySource.cpp
    Source/Publicacao/Publicacao.cpp
    Source/Consolidacao/Mascara.cpp
    Source/Consolidacao/Consolidacao.cpp
    Source/Consolidacao/LimpezaImagem.cpp
    Source/Consolidacao/SaidaBounce.cpp
    Source/Consolidacao/MainEdit.cpp
    Source/Consolidacao/BackupScanEngine.cpp
    Source/Consolidacao/MetadadoEmbutido.cpp
    Source/Consolidacao/BridgeXmp.cpp
    Source/Consolidacao/PacoteCollection.cpp
    Source/Catalogo/CatalogoProxies.cpp
    Source/Catalogo/CatalogSiteExport.cpp
    Source/Ficha/FichaDefinition.cpp
    Source/Ficha/CatalogoDeFichas.cpp
    Source/Ficha/FichaI18n.cpp
    Source/Ficha/OrigemPadrao.cpp
    Source/Ficha/AutocompleteHistorico.cpp
    Source/Db/Database.cpp
    Source/Model/Project.cpp
    Source/Model/ProjectLog.cpp
    Source/Ui/ProjectLogViewerDialog.cpp
    Source/Ui/IngestProgressModalDialog.cpp
    Source/Ui/ProjectLoadingModalDialog.cpp
    Source/Ui/RescanBackupSourcesDialog.cpp
    Source/Ui/RescanProgressModalDialog.cpp
    Source/Ui/VerifySourceDialog.cpp
    Source/Ui/BackupScanProgressDialog.cpp
    Source/Sync/SyncEngine.cpp
    Source/Ui/SyncDestinationDialog.cpp
    Source/Celular/QrCodeImagem.cpp
    Source/Celular/RecebimentoCelular.cpp
    Source/Celular/ServidorCelular.cpp
    Source/Celular/ExistentesProjeto.cpp
    Source/Celular/DispositivoAndroidUsb.cpp
    Source/Whatsapp/ServidorWhatsappApp.cpp
    Source/Whatsapp/WhatsappConversa.cpp
    Source/Whatsapp/WhatsappIngestao.cpp
    Source/Ui/Tablet.cpp
    Source/Ui/OverlayComponent.cpp
    Source/Ui/PainelReceberCelular.cpp
    Source/Ui/PainelAndroidUsb.cpp
    Source/Ui/PainelImportarWhatsapp.cpp
    Source/Ui/ImportarMenu.cpp
    Source/Ui/ImportarFichas.cpp
    Source/Ui/ContainerVisual.cpp
    Source/Ui/GeoAcoes.cpp
    Source/Ui/CapaProjeto.cpp
    Source/Ui/JanelaContainerDdp.cpp
    Source/Ui/JanelaPlayerDdp.cpp
    Source/Ddp/DdpModel.cpp
    Source/Ddp/DdpPlayback.cpp
    Source/Ui/ConversaWhatsappComponent.cpp
    Source/ProvaData/ExportadorTrecho.cpp
    Source/ProvaData/OpenTimestamps.cpp
    Source/ProvaData/PdfRaster.cpp
    Source/ProvaData/ProvaData.cpp
    Source/Model/VolumeLog.cpp
    Source/Ingest/SobreMaterial.cpp
    Source/Ingest/IngestaoStaging.cpp)

if(APPLE)
    target_sources(matriz PRIVATE
        Source/Diag/NSExceptionGuard.mm
        Source/App/SemSono_mac.mm
        Source/App/AreaTransferenciaImagem_mac.mm
        Source/Vault/DiskIdentity_mac.mm
        Source/Ingest/MiniaturaPsd.mm
        Source/Ingest/MiniaturaDocumento_mac.mm
        Source/Ui/VideoPlayerBridge.mm
        Source/Ui/DocumentPreviewBridge.mm
        Source/Ui/PinturaImediata_mac.mm)
elseif(WIN32)
    target_sources(matriz PRIVATE
        Source/Diag/NSExceptionGuard_win.cpp
        Source/App/SemSono_win.cpp
        Source/App/AreaTransferenciaImagem_win.cpp
        Source/Vault/DiskIdentity_win.cpp
        Source/Ingest/MiniaturaPsd_win.cpp
        Source/Ingest/MiniaturaDocumento_win.cpp
        Source/Ui/VideoPlayerBridge_win.cpp
        Source/Ui/DocumentPreviewBridge_win.cpp
        Source/Ui/PdfRenderer_win.cpp
        Source/Ui/PdfPageView_win.cpp
        Source/Ui/PinturaImediata_win.cpp)
endif()

target_include_directories(matriz PRIVATE Source)

# PHONE DUMP > ANDROID USB: libmtp 1.1.23 (LGPL 2.1) compilada aqui como biblioteca compartilhada (libmatriz_mtp.dylib,
# copiada para Contents/Frameworks com a libusb, como no Android File Transfer), com a extensão matriz/matriz_mtp.c
# (listagem de pasta numa transação só). Sem a libusb o módulo compila desligado (SessaoAndroidUsb::suportado() = false).
# WINPORT: no Windows o equivalente é WPD (IPortableDevice); ainda não implementado.
if(APPLE)
    find_path(MATRIZ_LIBUSB_INCLUDE libusb.h PATH_SUFFIXES libusb-1.0 HINTS /opt/homebrew/opt/libusb/include /usr/local/opt/libusb/include)
    find_library(MATRIZ_LIBUSB_LIB NAMES usb-1.0 HINTS /opt/homebrew/opt/libusb/lib /usr/local/opt/libusb/lib)
endif()
if(APPLE AND MATRIZ_LIBUSB_INCLUDE AND MATRIZ_LIBUSB_LIB)
    set(MATRIZ_LIBMTP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/libmtp-1.1.23")
    add_library(matriz_mtp SHARED
        "${MATRIZ_LIBMTP_DIR}/src/libmtp.c"
        "${MATRIZ_LIBMTP_DIR}/src/unicode.c"
        "${MATRIZ_LIBMTP_DIR}/src/util.c"
        "${MATRIZ_LIBMTP_DIR}/src/playlist-spl.c"
        "${MATRIZ_LIBMTP_DIR}/src/ptp.c"
        "${MATRIZ_LIBMTP_DIR}/src/libusb1-glue.c"
        "${MATRIZ_LIBMTP_DIR}/matriz/matriz_mtp.c")
    target_include_directories(matriz_mtp PUBLIC "${MATRIZ_LIBMTP_DIR}/src" "${MATRIZ_LIBMTP_DIR}/matriz"
                                          PRIVATE "${MATRIZ_LIBUSB_INCLUDE}")
    target_compile_definitions(matriz_mtp PRIVATE HAVE_CONFIG_H)
    target_compile_options(matriz_mtp PRIVATE -w)
    target_link_libraries(matriz_mtp PRIVATE "${MATRIZ_LIBUSB_LIB}" iconv "-framework IOKit" "-framework CoreFoundation")
    set_target_properties(matriz_mtp PROPERTIES INSTALL_NAME_DIR "@executable_path/../Frameworks" BUILD_WITH_INSTALL_NAME_DIR ON)
    target_link_libraries(matriz PRIVATE matriz_mtp)
    target_compile_definitions(matriz PRIVATE MATRIZ_COM_LIBMTP=1)
    add_custom_command(TARGET matriz POST_BUILD
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/tools/embutir_libmtp.sh" "$<TARGET_BUNDLE_DIR:matriz>" "$<TARGET_FILE:matriz_mtp>"
        COMMENT "Embedding libmatriz_mtp + libusb into the app bundle")
    message(STATUS "MATRIZ: libusb ${MATRIZ_LIBUSB_LIB} - PHONE DUMP > ANDROID USB ligado (libmtp embutida)")
else()
    target_compile_definitions(matriz PRIVATE MATRIZ_COM_LIBMTP=0)
    message(STATUS "MATRIZ: libusb nao encontrada - PHONE DUMP > ANDROID USB desligado")
endif()

target_compile_definitions(matriz PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=1
    JUCE_APPLICATION_NAME_STRING="${MATRIZ_PRODUCT_NAME}"
    JUCE_APPLICATION_VERSION_STRING="${MATRIZ_VERSION_STRING}"
    MATRIZ_UI_APP_NAME_STRING="${MATRIZ_UI_APP_NAME}"
    ${MATRIZ_TRIAL_DEF}
    MATRIZ_FICHAS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fichas"
    MATRIZ_DEV_BUILD=1
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(matriz PRIVATE
    MatrizSchemaAssets
    MatrizI18nAssets
    MatrizAssets
    MatrizFichasAssets
    MatrizCelularAssets
    matriz_httplib
    matriz_qrcodegen
    matriz_sqlite3
    yaml-cpp::yaml-cpp
    Exiv2::exiv2lib
    juce::juce_core
    juce::juce_cryptography
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_dsp
    $<$<PLATFORM_ID:Darwin>:-framework\ DiskArbitration>
    $<$<PLATFORM_ID:Darwin>:-framework\ IOKit>
    $<$<PLATFORM_ID:Darwin>:-framework\ AVFoundation>
    $<$<PLATFORM_ID:Darwin>:-framework\ CoreMedia>
    $<$<PLATFORM_ID:Darwin>:-framework\ PDFKit>
    $<$<PLATFORM_ID:Darwin>:-framework\ Quartz>
    $<$<PLATFORM_ID:Darwin>:-framework\ QuickLookUI>
    $<$<PLATFORM_ID:Darwin>:-framework\ QuickLook>
    $<$<PLATFORM_ID:Darwin>:-framework\ ImageIO>
    $<$<PLATFORM_ID:Windows>:bcrypt>
    $<$<PLATFORM_ID:Windows>:dbghelp>
    $<$<PLATFORM_ID:Windows>:psapi>
    $<$<PLATFORM_ID:Windows>:windowscodecs>
    $<$<PLATFORM_ID:Windows>:RuntimeObject>
    $<$<PLATFORM_ID:Windows>:windowsapp>
    $<$<PLATFORM_ID:Windows>:mfplat>
    $<$<PLATFORM_ID:Windows>:mfreadwrite>
    $<$<PLATFORM_ID:Windows>:mfuuid>
    $<$<PLATFORM_ID:Windows>:dwmapi>
    PUBLIC
    juce::juce_recommended_config_flags)

if(APPLE)
    target_link_options(matriz PRIVATE "LINKER:-stack_size,0x4000000")
elseif(MSVC)
    target_link_options(matriz PRIVATE "/STACK:67108864" "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/Assets/app.manifest")
endif()

matriz_aplicar_asan(matriz)
matriz_aplicar_tsan(matriz)

# ------------------------------------------------------------------------------
# Selftest - Analytics Multidimensional Engine & Benchmarks
# ------------------------------------------------------------------------------
juce_add_console_app(matriz_analytics_selftest PRODUCT_NAME "matriz_analytics_selftest")
juce_generate_juce_header(matriz_analytics_selftest)

target_sources(matriz_analytics_selftest PRIVATE
    tools/analytics_selftest/main.cpp
    Source/Analytics/AnalyticsFormatter.cpp
    Source/Analytics/AnalyticsEngine.cpp
    Source/Analytics/AssetGeolocation.cpp
    Source/Analytics/AnalyticsExport.cpp
    Source/Analytics/SavedAnalyses.cpp
    Source/Ingest/LeituraTecnica.cpp
    Source/Ingest/ProcessoExterno.cpp
    Source/Ingest/Loudness.cpp
    Source/Db/Database.cpp
    Source/Model/ProjectLog.cpp
    Source/Model/Project.cpp
    Source/Model/CaminhosBanco.cpp
    Source/Model/NomesCanonicos.cpp
    Source/Model/NomesSeguros.cpp
    Source/I18n/Strings.cpp
    Source/App/Preferencias.cpp)

target_include_directories(matriz_analytics_selftest PRIVATE Source)

target_compile_definitions(matriz_analytics_selftest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="MATRIZ analytics selftest"
    JUCE_APPLICATION_VERSION_STRING="0.1.0"
    MATRIZ_FICHAS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fichas"
    MATRIZ_DEV_BUILD=1)

target_link_libraries(matriz_analytics_selftest PRIVATE
    MatrizSchemaAssets
    matriz_sqlite3
    yaml-cpp::yaml-cpp
    Exiv2::exiv2lib
    juce::juce_core
    juce::juce_data_structures
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_audio_basics
    juce::juce_audio_formats
    $<$<PLATFORM_ID:Windows>:bcrypt>
    $<$<PLATFORM_ID:Windows>:dbghelp>
    $<$<PLATFORM_ID:Windows>:psapi>
    PUBLIC
    juce::juce_recommended_config_flags)

matriz_aplicar_asan(matriz_analytics_selftest)
matriz_aplicar_tsan(matriz_analytics_selftest)

if(MSVC)
    target_link_options(matriz_analytics_selftest PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/Assets/app.manifest")
endif()
