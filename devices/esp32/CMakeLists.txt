cmake_minimum_required(VERSION 3.5)

project(pynter-esp32 C)
set(SUPPORTED_TARGETS esp32)

include($ENV{IDF_PATH}/tools/cmake/idf.cmake)
idf_build_process(esp32
  COMPONENTS esp32 freertos esptool_py
  SDKCONFIG ${CMAKE_BINARY_DIR}/sdkconfig
  BUILD_DIR ${CMAKE_BINARY_DIR})

add_subdirectory(../../vm libpynter)

add_executable(pynter-esp32
  src/main.c)

target_link_libraries(pynter-esp32 idf::esp32 idf::freertos pynter)
idf_build_executable(pynter-esp32)

