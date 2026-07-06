cmake_minimum_required(VERSION 3.10)

set(PYNTER_DEBUG_LOGLEVEL 0 CACHE STRING "Debug level")
set(PYNTER_STATIC_HEAP 1 CACHE STRING "Enable static heap")
set(PYNTER_TEST_SHORT_DOUBLE 0 CACHE STRING "Test short double workaround")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Defaulting to Debug build.")
  set(CMAKE_BUILD_TYPE Debug)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

project(libpynter C)

add_library(pynter
  src/vm.c
  src/fault.c
  src/memory.c
  src/main.c
  src/debug.c
  src/debug_memorycheck.c
  src/inline.c
  src/primitives.c
)

target_compile_options(pynter
  PRIVATE -Wall -Wextra -Wswitch-enum -std=c11 -pedantic -Werror -fwrapv -g
  PRIVATE $<$<CONFIG:Debug>:$<IF:$<BOOL:${PYNTER_NUMERIC_OPT}>,-O0,-Og>>
  PRIVATE $<$<CONFIG:Release>:-O2 -DNDEBUG>
  PUBLIC $<$<OR:$<CONFIG:Debug>,$<BOOL:${PYNTER_DEBUG}>>:-DPYNTER_DEBUG>
  PUBLIC $<$<BOOL:${PYNTER_STATIC_HEAP}>:-DPYNTER_STATIC_HEAP>
  PUBLIC -DPYNTER_DEBUG_LOGLEVEL=${PYNTER_DEBUG_LOGLEVEL}
  PUBLIC $<$<BOOL:${PYNTER_DEBUG_ABORT_ON_FAULT}>:-DPYNTER_DEBUG_ABORT_ON_FAULT>
  PUBLIC $<$<BOOL:${PYNTER_DEBUG_MEMORY_CHECK}>:-DPYNTER_DEBUG_MEMORY_CHECK>
  PUBLIC $<$<BOOL:${PYNTER_DISABLE_CHECKS}>:-DPYNTER_DISABLE_CHECKS>
  PUBLIC $<$<BOOL:${PYNTER_TEST_SHORT_DOUBLE}>:-DPYNTER_TEST_SHORT_DOUBLE>
  PUBLIC $<$<BOOL:${PYNTER_COVERAGE}>:--coverage -fno-inline -fno-inline-small-functions -fno-default-inline>
)

if(DEFINED PYNTER_HEAP_SIZE)
  target_compile_options(pynter PUBLIC -DPYNTER_HEAP_SIZE=${PYNTER_HEAP_SIZE})
  message(STATUS "Setting PYNTER_HEAP_SIZE to ${PYNTER_HEAP_SIZE}")
endif()

if(DEFINED PYNTER_STACK_ENTRIES)
  target_compile_options(pynter PUBLIC -DPYNTER_STACK_ENTRIES=${PYNTER_STACK_ENTRIES})
  message(STATUS "Setting PYNTER_STACK_ENTRIES to ${PYNTER_STACK_ENTRIES}")
endif()

target_link_options(pynter
  PUBLIC $<$<BOOL:${PYNTER_COVERAGE}>:--coverage>
)

target_include_directories(pynter
  PUBLIC include
)

target_link_libraries(pynter m)
