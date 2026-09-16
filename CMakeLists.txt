cmake_minimum_required(VERSION 3.16)
project(devil-v2 C)

# Find lexbor
find_library(LEXBOR_LIBRARY lexbor PATHS /usr/local/lib)
find_path(LEXBOR_INCLUDE_DIR lexbor/html/parser.h PATHS /usr/local/include /usr/include)

# Library
add_library(devil-v2 STATIC
    src/devil.c
    src/filters/json-ld-extract.c
)
target_include_directories(devil-v2 PUBLIC include PRIVATE ${LEXBOR_INCLUDE_DIR})
target_link_libraries(devil-v2 ${LEXBOR_LIBRARY})

# CLI binary
add_executable(devil-v2-cli src/main.c)
target_link_libraries(devil-v2-cli devil-v2)

# Install
install(TARGETS devil-v2 devil-v2-cli DESTINATION bin)
