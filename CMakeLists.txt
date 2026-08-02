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

find_library(SQLITE3_LIBRARY sqlite3 REQUIRED)

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
    Source/Ficha/FichaYaml.cpp
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
    ${SQLITE3_LIBRARY}
    juce::juce_core
    PUBLIC
    juce::juce_recommended_config_flags)
