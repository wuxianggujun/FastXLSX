cmake_minimum_required(VERSION 3.20)

project(FastXLSX
    VERSION 0.1.0
    DESCRIPTION "A streaming-first C++ XLSX engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FASTXLSX_BUILD_TESTS "Build FastXLSX tests" ON)
option(FASTXLSX_BUILD_EXAMPLES "Build FastXLSX examples" OFF)
option(FASTXLSX_ENABLE_DOM_EDITING "Enable optional DOM-based editing helpers" ON)

add_library(fastxlsx
    src/opc.cpp
    src/streaming_writer.cpp
    src/workbook.cpp
    src/xml.cpp
    src/zip_store_writer.cpp
)

target_include_directories(fastxlsx PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(fastxlsx PUBLIC cxx_std_20)

add_library(FastXLSX::fastxlsx ALIAS fastxlsx)

if(FASTXLSX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
