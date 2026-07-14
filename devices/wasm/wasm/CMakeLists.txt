cmake_minimum_required(VERSION 3.10)

set(CMAKE_BUILD_TYPE Release)
set(PYNTER_NUMERIC_OPT 1)
set(PYNTER_DEBUG 1)
set(PYNTER_DEBUG_MEMORY_CHECK 1)
set(PYNTER_DEBUG_LOGLEVEL 2)
set(PYNTER_STATIC_HEAP 0)

project(pynter-wasm C)

add_subdirectory(../../../vm libpynter)

add_executable(pynterwasm lib.c)

target_link_options(pynterwasm
  PRIVATE "SHELL:-s WASM=1" "SHELL:-s ALLOW_MEMORY_GROWTH=1"
  PRIVATE "SHELL:-s MODULARIZE=1" "SHELL:-s EXPORT_NAME=pynterwasm"
  PRIVATE "SHELL:-s EXPORTED_RUNTIME_METHODS='[\"ccall\", \"cwrap\", \"HEAPU8\"]'"
  PRIVATE "SHELL:--emit-tsd pynterwasm.d.ts"
)

target_link_libraries(pynterwasm pynter)

