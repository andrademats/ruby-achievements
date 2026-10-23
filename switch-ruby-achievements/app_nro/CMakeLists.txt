# Ruby Achievements - app NRO (Borealis, renderer deko3d)
# Build: use o Makefile desta pasta (ele chama o CMake com o toolchain do devkitPro).
cmake_minimum_required(VERSION 3.10)

set(BOREALIS_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/lib/borealis/library)
set(PLATFORM_SWITCH ON CACHE BOOL "" FORCE)
set(USE_DEKO3D ON CACHE BOOL "" FORCE)

include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(ruby_achievements C CXX)
# Versão única do projeto (arquivo VERSION): NACP, aba Atualização e comparação com a release.
set(VERSION_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${VERSION_FILE})
file(STRINGS ${VERSION_FILE} PT_VERSION LIMIT_COUNT 1)
string(STRIP "${PT_VERSION}" PT_VERSION)
if (NOT PT_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR "VERSION precisa ser X.Y.Z (achei '${PT_VERSION}')")
endif()
set(VERSION_MAJOR ${CMAKE_MATCH_1})
set(VERSION_MINOR ${CMAKE_MATCH_2})
set(VERSION_ALTER ${CMAKE_MATCH_3})
set(PROJECT_AUTHOR "Ruby Achievements")
set(APP_TITLE "Ruby Achievements")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.jpg)

set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)
set(SCHEMA_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../schema.sql)
set(ROMFS_DIR ${CMAKE_BINARY_DIR}/romfs)

add_subdirectory(${BOREALIS_LIBRARY} borealis)

# hbl do Sphaira: exefs (main + main.npdm) dos forwarders fantasmas, embutido
# em source/forwarder/owo.cpp com #embed.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/forwarder_hbl hbl)

# schema.sql embutido como literal de string bruta (fonte única do schema).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SCHEMA_FILE})
file(READ ${SCHEMA_FILE} SCHEMA_TEXT)
file(WRITE ${CMAKE_BINARY_DIR}/gen/schema_sql.inc "R\"PTSQL(${SCHEMA_TEXT})PTSQL\"\n")

file(GLOB APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp)
file(GLOB FORWARDER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/source/forwarder/*.cpp)
# Código adaptado do Sphaira: C++23 (std::span, #embed) e o exefs do hbl.
set_source_files_properties(${FORWARDER_SRC} PROPERTIES
    COMPILE_OPTIONS "-std=gnu++23;-Wno-pedantic;--embed-dir=${CMAKE_BINARY_DIR}/hbl")
# common/ach: pacotes de conquistas (os mesmos arquivos do sysmodule).
file(GLOB COMMON_SRC ${COMMON_DIR}/core/*.cpp ${COMMON_DIR}/db/*.cpp ${COMMON_DIR}/ach/*.cpp)
set(SQLITE_SRC ${COMMON_DIR}/sqlite/sqlite3.c)
if (NOT EXISTS ${COMMON_DIR}/sqlite/sqlite3.c)
    message(FATAL_ERROR "Falta common/sqlite/sqlite3.c - rode scripts/fetch_sqlite.sh")
endif ()
set_source_files_properties(${SQLITE_SRC} PROPERTIES
    COMPILE_DEFINITIONS "SQLITE_CUSTOM_INCLUDE=pt_sqlite_config.h"
    COMPILE_OPTIONS "-w")

# Monocypher (BSD-2/CC0): Ed25519 da assinatura do manifesto da atualização (update.cpp).
set(MONOCYPHER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/monocypher)
set(MONOCYPHER_SRC ${MONOCYPHER_DIR}/monocypher.c ${MONOCYPHER_DIR}/monocypher-ed25519.c)
set(MAIN_SRC ${APP_SRC} ${FORWARDER_SRC} ${COMMON_SRC} ${SQLITE_SRC} ${MONOCYPHER_SRC}
    ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)

program_target(${PROJECT_NAME} "${MAIN_SRC}")
add_dependencies(${PROJECT_NAME} hbl_nso hbl_npdm)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${COMMON_DIR}/core ${COMMON_DIR}/db ${COMMON_DIR}/ach ${COMMON_DIR}/sqlite
    ${CMAKE_BINARY_DIR}/gen ${MONOCYPHER_DIR})
target_compile_definitions(${PROJECT_NAME} PRIVATE PT_VERSION="${PT_VERSION}")
target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections ${APP_PLATFORM_OPTION})
# libcurl (pacote switch-curl, SSL pelo serviço do sistema): só para o SteamGridDB.
# turbojpeg (portlibs): grava o ícone JPEG 256x256 do forwarder fantasma (icon_image.cpp).
target_link_libraries(${PROJECT_NAME} PRIVATE borealis ${APP_PLATFORM_LIB} curl turbojpeg z nx)

# RomFS = recursos do Borealis (i18n, ícones Material) + resources/ do app.
# A fonte do sistema é usada no Switch, então resources/font é removida.
gen_dksh("${ROMFS_DIR}/shaders")
add_custom_target(${PROJECT_NAME}.nro DEPENDS ${PROJECT_NAME}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/lib/borealis/resources/i18n ${ROMFS_DIR}/i18n
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/lib/borealis/resources/material ${ROMFS_DIR}/material
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/resources ${ROMFS_DIR}
    COMMAND ${NX_NACPTOOL_EXE} --create "${APP_TITLE}" "${PROJECT_AUTHOR}" "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}" ${PROJECT_NAME}.nacp
    COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro --icon=${PROJECT_ICON} --nacp=${PROJECT_NAME}.nacp --romfsdir=${ROMFS_DIR}
)
