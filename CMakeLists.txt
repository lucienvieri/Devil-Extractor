cmake_minimum_required(VERSION 3.16)
project(devil-v2 C)

# Find lexbor
find_library(LEXBOR_LIBRARY lexbor PATHS /usr/local/lib)
find_path(LEXBOR_INCLUDE_DIR lexbor/html/parser.h PATHS /usr/local/include /usr/include)

# Library
add_library(devil-v2 STATIC src/devil.c)
target_include_directories(devil-v2 PUBLIC include PRIVATE ${LEXBOR_INCLUDE_DIR})
target_link_libraries(devil-v2 ${LEXBOR_LIBRARY})

# CLI binary
add_executable(devil-v2-cli src/main.c)
target_link_libraries(devil-v2-cli devil-v2)

# ========================================================================
# Filter plugin system (optional)
# ========================================================================

# Enable noise filter by default (drops script, style, nav, etc.)
option(DEVIL_FILTER_NOISE "Enable noise tag filter plugin" ON)
if(DEVIL_FILTER_NOISE)
    target_compile_definitions(devil-v2 PRIVATE DEVIL_FILTER_NOISE=1)
    target_compile_definitions(devil-v2-cli PRIVATE DEVIL_FILTER_NOISE=1)
endif()

# Enable ad-block / CSS-class filter (drops nav, sidebar, ad, modal, cookie, etc.)
option(DEVIL_FILTER_AD_BLOCK "Enable CSS class-based ad-block filter" ON)
if(DEVIL_FILTER_AD_BLOCK)
    target_compile_definitions(devil-v2 PRIVATE DEVIL_FILTER_AD_BLOCK=1)
    target_compile_definitions(devil-v2-cli PRIVATE DEVIL_FILTER_AD_BLOCK=1)
endif()

# Install
install(TARGETS devil-v2 devil-v2-cli DESTINATION bin)
