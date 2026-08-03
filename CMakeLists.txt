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
# Etapa 1 — self-test headless: parser/validador de ficha + criação/abertura
# de projeto portátil. "Nenhuma UI além do mínimo para testar" (§16).
# ------------------------------------------------------------------------------
juce_add_console_app(matriz_selftest PRODUCT_NAME "matriz_selftest")
juce_generate_juce_header(matriz_selftest)

target_sources(matriz_selftest PRIVATE
    tools/selftest/main.cpp
    Source/Ficha/FichaDefinition.cpp
    Source/Db/Database.cpp
    Source/Model/Project.cpp)

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
    matriz_sqlite3
    yaml-cpp::yaml-cpp
    juce::juce_core
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
    Source/Ingest/Miniaturas.cpp
    Source/Ingest/Duplicata.cpp
    Source/Ingest/Pcm.cpp
    Source/Ingest/ClassificadorFalaMusica.cpp
    Source/Ingest/InferenciaEstrutura.cpp
    Source/Ingest/IngestArquivo.cpp
    Source/Ingest/FluxoLote.cpp
    Source/Ingest/PainelInconsistencias.cpp
    Source/Ficha/FichaDefinition.cpp
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
    PUBLIC
    juce::juce_recommended_config_flags)
