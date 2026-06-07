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
option(FASTXLSX_BUILD_BENCHMARKS "Build FastXLSX manual benchmark tools" OFF)
option(FASTXLSX_ENABLE_DOM_EDITING "Enable optional DOM-based editing helpers" ON)
option(FASTXLSX_ENABLE_MINIZIP_NG "Use minizip-ng for the internal production ZIP writer backend" OFF)

add_library(fastxlsx
    src/image.cpp
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

if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND CMAKE_MODULE_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/stb")
    list(APPEND _fastxlsx_stb_include_dirs "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
endif()
file(GLOB _fastxlsx_stb_module_dirs "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/*/share/stb")
list(APPEND CMAKE_MODULE_PATH ${_fastxlsx_stb_module_dirs})
file(GLOB _fastxlsx_stb_manifest_include_dirs "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/*/include")
list(APPEND _fastxlsx_stb_include_dirs ${_fastxlsx_stb_manifest_include_dirs})
find_path(Stb_INCLUDE_DIR NAMES stb_image.h PATHS ${_fastxlsx_stb_include_dirs} NO_DEFAULT_PATH)
find_package(Stb MODULE REQUIRED)
target_include_directories(fastxlsx PRIVATE ${Stb_INCLUDE_DIR})

if(FASTXLSX_ENABLE_MINIZIP_NG)
    find_package(minizip-ng CONFIG REQUIRED)
    target_link_libraries(fastxlsx PRIVATE MINIZIP::minizip-ng)
    target_compile_definitions(fastxlsx PRIVATE FASTXLSX_HAS_MINIZIP_NG=1)
endif()

if(FASTXLSX_BUILD_TESTS)
    target_compile_definitions(fastxlsx PRIVATE FASTXLSX_ENABLE_TEST_HOOKS=1)
    enable_testing()
    add_subdirectory(tests)
endif()

if(FASTXLSX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(FASTXLSX_BUILD_BENCHMARKS)
    target_compile_definitions(fastxlsx PRIVATE FASTXLSX_ENABLE_BENCHMARK_METRICS=1)
    add_subdirectory(benchmarks)
endif()
