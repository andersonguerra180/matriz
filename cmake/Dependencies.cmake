# Dependências externas — todas via FetchContent, nenhuma via gerenciador de
# pacotes do sistema (Homebrew, apt, vcpkg). Ver §13 do SPEC.md: dependência
# de sistema não reproduz em CI nem no Windows, e em macOS mais antigo sem
# bottle disponível o Homebrew recompila toda a cadeia (inclusive o próprio
# CMake) só pra instalar uma lib.

include(FetchContent)

# ------------------------------------------------------------------------------
# SQLite3 — amalgamation oficial (um único .c/.h, sem dependências, sem
# repositório git a versionar: a distribuição oficial é feita por release zip).
# ------------------------------------------------------------------------------
FetchContent_Declare(
    sqlite3_amalgamation
    URL https://www.sqlite.org/2024/sqlite-amalgamation-3450300.zip
    URL_HASH SHA256=ea170e73e447703e8359308ca2e4366a3ae0c4304a8665896f068c736781c651
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sqlite3_amalgamation)

add_library(matriz_sqlite3 STATIC "${sqlite3_amalgamation_SOURCE_DIR}/sqlite3.c")
target_include_directories(matriz_sqlite3 PUBLIC "${sqlite3_amalgamation_SOURCE_DIR}")
target_compile_definitions(matriz_sqlite3 PUBLIC
    SQLITE_THREADSAFE=1
    SQLITE_ENABLE_FTS5
    SQLITE_DQS=0)
if(UNIX)
    target_link_libraries(matriz_sqlite3 PUBLIC pthread dl m)
endif()

# ------------------------------------------------------------------------------
# yaml-cpp — parser YAML completo (âncora, bloco multilinha, aspas em posição
# arbitrária). Substitui o parser de subconjunto escrito à mão (§6.1 promete
# tipo de ficha próprio ao usuário; um parser incompleto engasga com
# mensagem incompreensível em YAML perfeitamente válido).
# ------------------------------------------------------------------------------
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 0.8.0
    GIT_SHALLOW TRUE
)
# O CMakeLists.txt do yaml-cpp 0.8.0 declara cmake_minimum_required(2.8.12),
# incompatível com CMake >= 4 (removeu suporte a política < 3.5). Isso é do
# lado do yaml-cpp, não do nosso projeto — só relaxa a checagem de política
# mínima pro subprojeto buscado, não afeta a política do MATRIZ em si.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(yaml-cpp)
unset(CMAKE_POLICY_VERSION_MINIMUM)

# zlib e Exiv2 não são instalados (não empacotamos um SDK, só linkamos
# estático no nosso app) — desliga as regras de install() dos dois pra evitar
# o erro clássico de FetchContent "target X requires target Y that is not in
# any export set" quando um projeto buscado declara install(EXPORT ...) e o
# outro não. Escopo local: reativado logo depois.
set(CMAKE_SKIP_INSTALL_RULES TRUE)

# ------------------------------------------------------------------------------
# zlib — única dependência transitiva do Exiv2 que optamos por manter (suporte
# a PNG). Buscada via FetchContent como tudo mais, nunca do sistema.
# ------------------------------------------------------------------------------
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(zlib)
# zlib gera zconf.h dentro do diretório de build; expõe os dois diretórios de
# include pro alvo "zlibstatic" (nome do alvo estático do CMakeLists do zlib)
# como se fosse um find_package(ZLIB) de verdade, que é o que o Exiv2 espera.
target_include_directories(zlibstatic PUBLIC "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")
add_library(ZLIB::ZLIB ALIAS zlibstatic)
set(ZLIB_FOUND TRUE)
set(ZLIB_LIBRARY zlibstatic)
set(ZLIB_INCLUDE_DIR "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")

# ------------------------------------------------------------------------------
# Exiv2 — EXIF completo (data original, câmera, lente, orientação, GPS),
# substitui `sips`/`mdls` (macOS only) em Source/Ingest/LeituraTecnica.cpp
# (§A.2). Build mínimo: sem XMP (evita expat), sem BMFF/vídeo/webready/NLS
# (evita brotli/curl/gettext), com PNG (só precisa do zlib acima). HEIC/AVIF
# ficam sem EXIF por não termos BMFF — dimensão continua vindo do ffprobe.
# ------------------------------------------------------------------------------
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(EXIV2_ENABLE_XMP OFF CACHE BOOL "" FORCE)
set(EXIV2_ENABLE_PNG ON CACHE BOOL "" FORCE)
set(EXIV2_ENABLE_BMFF OFF CACHE BOOL "" FORCE)
set(EXIV2_ENABLE_VIDEO OFF CACHE BOOL "" FORCE)
set(EXIV2_ENABLE_WEBREADY OFF CACHE BOOL "" FORCE)
set(EXIV2_ENABLE_CURL OFF CACHE BOOL "" FORCE)
set(EXIV2_ENABLE_NLS OFF CACHE BOOL "" FORCE)
set(EXIV2_ENABLE_INIH OFF CACHE BOOL "" FORCE)
set(EXIV2_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(EXIV2_BUILD_EXIV2_COMMAND OFF CACHE BOOL "" FORCE)
set(EXIV2_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(EXIV2_BUILD_DOC OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    exiv2
    GIT_REPOSITORY https://github.com/Exiv2/exiv2.git
    GIT_TAG v0.28.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(exiv2)

# generate_export_header() escreve exiv2lib_export.h em ${CMAKE_BINARY_DIR}
# (a raiz da nossa árvore de build, não a do subprojeto Exiv2) mas o
# CMakeLists.txt do Exiv2 só adiciona esse caminho ao include PRIVADO de
# exiv2lib — quem consome a lib de fora (nós) precisa do mesmo caminho.
target_include_directories(exiv2lib PUBLIC "${CMAKE_BINARY_DIR}")

set(CMAKE_SKIP_INSTALL_RULES FALSE)
