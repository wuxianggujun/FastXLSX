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
option(FASTXLSX_ENABLE_MINIZIP_NG "Use minizip-ng for the internal production ZIP writer backend" OFF)

add_library(fastxlsx
    src/opc.cpp
    src/package_writer.cpp
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

if(FASTXLSX_ENABLE_MINIZIP_NG)
    find_package(minizip-ng CONFIG REQUIRED)
    target_link_libraries(fastxlsx PRIVATE MINIZIP::minizip-ng)
    target_compile_definitions(fastxlsx PRIVATE FASTXLSX_HAS_MINIZIP_NG=1)
endif()

if(FASTXLSX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(FASTXLSX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
