cmake_minimum_required(VERSION 3.22)
project(MATRIZ VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "" FORCE)
endif()

# Locate JUCE
set(JUCE_DIR "$ENV{HOME}/JUCE" CACHE PATH "Path to JUCE")
if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at '${JUCE_DIR}'. Pass -DJUCE_DIR=/path/to/JUCE.")
endif()

add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake")

# ------------------------------------------------------------------------------
# Schema do banco (schema/registro.sql, schema/indice.sql) embutido no binário
# — aplicado (idempotente, CREATE ... IF NOT EXISTS) toda vez que um projeto é
# criado ou aberto (Source/Model/Project.cpp).
# ------------------------------------------------------------------------------
juce_add_binary_data(MatrizSchemaAssets SOURCES
    schema/registro.sql
    schema/indice.sql)

# ------------------------------------------------------------------------------
# Strings de interface (i18n/pt_BR.yaml) embutidas no binário — B.5, §0.6.
# NAMESPACE/HEADER_NAME próprios: o alvo `matriz` linka isto junto com
# MatrizSchemaAssets, e os dois gerariam "BinaryData.h"/BinaryData:: por
# padrão — colisão de nome de arquivo gerado entre os dois alvos.
# ------------------------------------------------------------------------------
juce_add_binary_data(MatrizI18nAssets
    NAMESPACE I18nBinaryData
    HEADER_NAME I18nBinaryData.h
    SOURCES i18n/en.yaml i18n/pt_BR.yaml)

# ------------------------------------------------------------------------------
# Etapa 1 — self-test headless: parser/validador de ficha + criação/abertura
# de projeto portátil. "Nenhuma UI além do mínimo para testar" (§16).
# ------------------------------------------------------------------------------
juce_add_console_app(matriz_selftest PRODUCT_NAME "matriz_selftest")
juce_generate_juce_header(matriz_selftest)

target_sources(matriz_selftest PRIVATE
    tools/selftest/main.cpp
    Source/Ficha/FichaDefinition.cpp
    Source/Ficha/CatalogoDeFichas.cpp
    Source/Db/Database.cpp
    Source/Model/Project.cpp
    Source/I18n/Strings.cpp
    Source/App/Preferencias.cpp)

target_include_directories(matriz_selftest PRIVATE Source)

target_compile_definitions(matriz_selftest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="MATRIZ selftest"
    JUCE_APPLICATION_VERSION_STRING="0.1.0"
    MATRIZ_FICHAS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fichas")

target_link_libraries(matriz_selftest PRIVATE
    MatrizSchemaAssets
    MatrizI18nAssets
    matriz_sqlite3
    yaml-cpp::yaml-cpp
    juce::juce_core
    juce::juce_data_structures
    PUBLIC
    juce::juce_recommended_config_flags)

# ------------------------------------------------------------------------------
# Etapa 2 — self-test headless: motor de ingestão (leitura técnica, checksum,
# duplicata, agrupamento, painel de inconsistências). Cresce a cada peça da
# Etapa 2; roda sobre mídia sintética gerada com ffmpeg em tempo de teste.
# ------------------------------------------------------------------------------
juce_add_console_app(matriz_ingest_selftest PRODUCT_NAME "matriz_ingest_selftest")
juce_generate_juce_header(matriz_ingest_selftest)

target_sources(matriz_ingest_selftest PRIVATE
    tools/ingest_selftest/main.cpp
    Source/Ingest/Checksum.cpp
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
    Source/Consolidacao/Mascara.cpp
    Source/Consolidacao/Consolidacao.cpp
    Source/Consolidacao/MetadadoEmbutido.cpp
    Source/Catalogo/CatalogoProxies.cpp
    Source/Ficha/FichaDefinition.cpp
    Source/Ficha/CatalogoDeFichas.cpp
    Source/Db/Database.cpp
    Source/Model/Project.cpp)

target_include_directories(matriz_ingest_selftest PRIVATE Source)

target_compile_definitions(matriz_ingest_selftest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="MATRIZ ingest selftest"
    JUCE_APPLICATION_VERSION_STRING="0.1.0"
    MATRIZ_FICHAS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fichas"
    # Self-test roda fora de um bundle empacotado — sem isso, ffmpeg/ffprobe
    # não seriam encontrados ao lado do executável e a build falharia (por
    # design: build de produção não deve depender do PATH, ver §A.4/A.6).
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
    # Loudness EBU R128 (item 6.1) decodifica o arquivo com os formatos do
    # JUCE pra medir LUFS-I/LRA na leitura técnica.
    juce::juce_audio_formats
    PUBLIC
    juce::juce_recommended_config_flags)

# ------------------------------------------------------------------------------
# Etapa 3 — app principal (B.1.1-B.1.3, B.4, B.5): janela, criação/abertura de
# projeto, layout de três painéis, mosaico virtualizado, ficha lateral
# genérica, configurações do projeto, i18n. Tira de diagnóstico, transporte,
# marcadores e tema System CRT são B.1.4+ (fronteira de etapa).
# ------------------------------------------------------------------------------
juce_add_gui_app(matriz PRODUCT_NAME "BKR Matriz" VERSION 0.1.0)
juce_generate_juce_header(matriz)

target_sources(matriz PRIVATE
    Source/Main.cpp
    Source/App/Preferencias.cpp
    Source/I18n/Strings.cpp
    Source/Ui/Tokens.h
    Source/Ui/ProjetoAberto.cpp
    Source/Ui/AcoesItem.cpp
    Source/Ui/ArvoreComponent.cpp
    Source/Ui/BarraFerramentasComponent.cpp
    Source/Ui/BarraGuiaComponent.cpp
    Source/Ui/BarraSelecaoComponent.cpp
    Source/Ui/FiltrosComponent.cpp
    Source/Ui/MetadadosOriginaisComponent.cpp
    Source/Ui/MosaicoComponent.cpp
    Source/Ui/MosaicoStressTest.cpp
    Source/Ui/IngerirArquivosTest.cpp
    Source/Ui/UiSelfTest.cpp
    Source/Ui/FichaPanelComponent.cpp
    Source/Ui/PainelInconsistenciasComponent.cpp
    Source/Ui/PreviewComponent.cpp
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
    Source/Ui/MainComponent.cpp
    Source/Ui/MainWindow.cpp
    Source/Ingest/FluxoLote.cpp
    Source/Ingest/IngestArquivo.cpp
    Source/Ingest/LeituraTecnica.cpp
    Source/Ingest/Loudness.cpp
    Source/Ingest/Checksum.cpp
    Source/Ingest/ProcessoExterno.cpp
    Source/Ingest/PainelInconsistencias.cpp
    Source/Ingest/Miniaturas.cpp
    Source/Consolidacao/Mascara.cpp
    Source/Consolidacao/Consolidacao.cpp
    Source/Consolidacao/MetadadoEmbutido.cpp
    Source/Catalogo/CatalogoProxies.cpp
    Source/Ficha/FichaDefinition.cpp
    Source/Ficha/CatalogoDeFichas.cpp
    Source/Ficha/FichaI18n.cpp
    Source/Ficha/OrigemPadrao.cpp
    Source/Db/Database.cpp
    Source/Model/Project.cpp)

target_include_directories(matriz PRIVATE Source)

target_compile_definitions(matriz PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="BKR Matriz"
    JUCE_APPLICATION_VERSION_STRING="0.1.0"
    MATRIZ_FICHAS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fichas"
    # Ver nota em matriz_ingest_selftest — a build empacotada (Etapa 10) não
    # deve definir isto; até lá, o app de desenvolvimento cai pro PATH se não
    # achar ffmpeg/ffprobe ao lado do executável.
    MATRIZ_DEV_BUILD=1
    # Necessário pra MessageManager::runDispatchLoopUntil, usado só pelo
    # selftest headless de ingest (Source/Ui/IngerirArquivosTest.cpp) pra
    # bombear os callbacks assíncronos do ingest em background.
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(matriz PRIVATE
    MatrizSchemaAssets
    MatrizI18nAssets
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
    # AudioThumbnail/AudioThumbnailCache: forma de onda da timeline (item 8).
    juce::juce_audio_utils
    PUBLIC
    juce::juce_recommended_config_flags)
