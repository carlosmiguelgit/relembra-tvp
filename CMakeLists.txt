cmake_minimum_required(VERSION 3.10)

project(TheForgottenServer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Força ABI moderno do C++ (necessário pra evitar erros com Crypto++)
add_definitions(-D_GLIBCXX_USE_CXX11_ABI=1)

# Caminho para módulos personalizados
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules")

# Encontrar dependências
find_package(spdlog REQUIRED)
find_package(Lua 5.3 REQUIRED)
find_package(Boost 1.83.0 REQUIRED COMPONENTS system filesystem iostreams)
find_package(MySQL REQUIRED)

# Tentativa de encontrar Crypto++ usando módulo, se existir
find_package(Crypto++ QUIET)

# Se não encontrar, define manualmente
if (NOT Crypto++_FOUND AND NOT CRYPTOPP_FOUND)
    message(WARNING "Crypto++ não encontrado com find_package, usando fallback manual")

    # Ajuste no caminho para buscar corretamente os arquivos de cabeçalho
    find_path(CRYPTOPP_INCLUDE_DIRS cryptlib.h PATHS /usr/include/crypto++ /usr/local/include)

    # Ajuste no caminho para buscar corretamente as bibliotecas
    find_library(CRYPTOPP_LIBRARY NAMES cryptopp PATHS /usr/lib/x86_64-linux-gnu /usr/local/lib)

    # Verifica se ambos foram encontrados
    if (CRYPTOPP_INCLUDE_DIRS AND CRYPTOPP_LIBRARY)
        set(CRYPTOPP_LIBRARIES ${CRYPTOPP_LIBRARY})
        set(CRYPTOPP_FOUND TRUE)
    else()
        message(FATAL_ERROR "Crypto++ não encontrado. Instale com: sudo apt install libcrypto++-dev")
    endif()
endif()

# Incluir diretórios
include_directories(
    ${CRYPTOPP_INCLUDE_DIRS}
    ${SPDLOG_INCLUDE_DIR}
    ${LUA_INCLUDE_DIR}
    ${Boost_INCLUDE_DIRS}
    ${MYSQL_INCLUDE_DIR}
    /usr/include/pugixml
    src
)

# Buscar arquivos fonte
file(GLOB_RECURSE SOURCES src/*.cpp)

# Criar executável
add_executable(theforgottenserver ${SOURCES})

# Linkar bibliotecas
target_link_libraries(theforgottenserver
    ${CRYPTOPP_LIBRARIES}
    spdlog::spdlog
    ${LUA_LIBRARIES}
    ${Boost_LIBRARIES}
    ${MYSQL_LIBRARIES}
    pugixml
)
