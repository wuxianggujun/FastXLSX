cmake_minimum_required(VERSION 3.20)

project(FastXLSX
    VERSION 0.1.0
    DESCRIPTION "A streaming-first C++ XLSX engine"
    LANGUAGES CXX
)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FASTXLSX_BUILD_TESTS "Build FastXLSX tests" ON)
option(FASTXLSX_BUILD_EXAMPLES "Build FastXLSX examples" OFF)
option(FASTXLSX_BUILD_BENCHMARKS "Build FastXLSX manual benchmark tools" OFF)
option(FASTXLSX_BUILD_REFERENCE_BENCHMARKS "Build opt-in third-party XLSX writer reference benchmark adapters" OFF)
option(FASTXLSX_BUILD_QA_TOOLS "Build opt-in local QA tools" OFF)
option(FASTXLSX_BUILD_FEATURE_SMOKES "Build small compile/runtime feature-profile smoke tools" OFF)
option(FASTXLSX_ENABLE_MINIZIP_NG "Use minizip-ng for production stored/DEFLATE ZIP read and write" ON)
option(FASTXLSX_ENABLE_DIRECT_ZLIB_PROFILING "Compile the internal direct-zlib package-writer profiling engine" OFF)
option(FASTXLSX_ENABLE_PORTABLE_CRC_PROFILING "Use the portable PackageEditor CRC32 implementation for internal A/B profiling" OFF)
option(FASTXLSX_ENABLE_IMAGES "Enable stb-backed PNG/JPEG APIs and XLSX image features" ON)

if(FASTXLSX_ENABLE_DIRECT_ZLIB_PROFILING AND NOT FASTXLSX_ENABLE_MINIZIP_NG)
    message(FATAL_ERROR
        "FASTXLSX_ENABLE_DIRECT_ZLIB_PROFILING requires FASTXLSX_ENABLE_MINIZIP_NG=ON")
endif()

if(FASTXLSX_ENABLE_PORTABLE_CRC_PROFILING AND NOT FASTXLSX_ENABLE_MINIZIP_NG)
    message(FATAL_ERROR
        "FASTXLSX_ENABLE_PORTABLE_CRC_PROFILING requires FASTXLSX_ENABLE_MINIZIP_NG=ON")
endif()

set(_fastxlsx_sources
    src/cell_store.cpp
    src/cell_value.cpp
    src/formula.cpp
    src/formula_reference_audit.cpp
    src/opc.cpp
    src/package_editor.cpp
    src/package_reader.cpp
    src/package_writer.cpp
    src/streaming_writer.cpp
    src/workbook.cpp
    src/workbook_editor.cpp
    src/workbook_editor_formula_diagnostics.cpp
    src/worksheet_event_reader.cpp
    src/worksheet_metadata_rewriter.cpp
    src/worksheet_transformer.cpp
    src/workbook_editor_image_edit.cpp
    src/workbook_editor_materialized_edits.cpp
    src/workbook_editor_pending_edits.cpp
    src/workbook_editor_save_as_policy.cpp
    src/workbook_editor_sheet_catalog.cpp
    src/workbook_editor_sheet_data_replacement.cpp
    src/workbook_editor_sheet_rename.cpp
    src/workbook_editor_testing_hooks.cpp
    src/workbook_editor_worksheet_facade.cpp
    src/workbook_editor_worksheet_access.cpp
    src/worksheet_cell_index.cpp
    src/worksheet_reader.cpp
    src/xml.cpp
    src/zip_store_writer.cpp
)

if(FASTXLSX_ENABLE_IMAGES)
    list(APPEND _fastxlsx_sources src/image.cpp)
else()
    list(APPEND _fastxlsx_sources src/image_disabled.cpp)
endif()

add_library(fastxlsx ${_fastxlsx_sources})

target_include_directories(fastxlsx PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(fastxlsx PUBLIC cxx_std_20)

add_library(FastXLSX::fastxlsx ALIAS fastxlsx)

if(FASTXLSX_ENABLE_IMAGES)
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
    target_compile_definitions(fastxlsx PUBLIC FASTXLSX_HAS_IMAGES=1)
else()
    target_compile_definitions(fastxlsx PUBLIC FASTXLSX_HAS_IMAGES=0)
endif()

if(FASTXLSX_ENABLE_MINIZIP_NG)
    find_package(minizip-ng CONFIG REQUIRED)
    target_link_libraries(fastxlsx PRIVATE MINIZIP::minizip-ng)
    target_compile_definitions(fastxlsx PRIVATE FASTXLSX_HAS_MINIZIP_NG=1)
    if(FASTXLSX_ENABLE_PORTABLE_CRC_PROFILING)
        target_compile_definitions(fastxlsx PRIVATE
            FASTXLSX_USE_PORTABLE_PACKAGE_EDITOR_CRC32=1)
    endif()
    if(FASTXLSX_ENABLE_DIRECT_ZLIB_PROFILING)
        find_package(ZLIB REQUIRED)
        target_link_libraries(fastxlsx PRIVATE ZLIB::ZLIB)
        target_compile_definitions(fastxlsx PRIVATE FASTXLSX_HAS_DIRECT_ZLIB_PROFILING=1)
    endif()
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

if(FASTXLSX_BUILD_QA_TOOLS)
    add_subdirectory(tools)
endif()
if(FASTXLSX_BUILD_FEATURE_SMOKES)
    add_executable(fastxlsx_image_disabled_smoke
        tools/feature_smoke/image_disabled_smoke.cpp
    )
    target_link_libraries(fastxlsx_image_disabled_smoke PRIVATE FastXLSX::fastxlsx)
    target_compile_features(fastxlsx_image_disabled_smoke PRIVATE cxx_std_20)
endif()


install(
    TARGETS fastxlsx
    EXPORT FastXLSXTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
    FILES
        include/fastxlsx/cell_value.hpp
        include/fastxlsx/document_properties.hpp
        include/fastxlsx/fastxlsx.hpp
        include/fastxlsx/image.hpp
        include/fastxlsx/streaming_writer.hpp
        include/fastxlsx/workbook.hpp
        include/fastxlsx/workbook_editor.hpp
        include/fastxlsx/worksheet_metadata.hpp
        include/fastxlsx/worksheet_reader.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fastxlsx
)

install(
    EXPORT FastXLSXTargets
    NAMESPACE FastXLSX::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FastXLSX
)

set(FASTXLSX_CONFIG_DEPENDENCIES "")
if(FASTXLSX_ENABLE_MINIZIP_NG)
    string(APPEND FASTXLSX_CONFIG_DEPENDENCIES
        "find_dependency(minizip-ng CONFIG)\n")
endif()
if(FASTXLSX_ENABLE_DIRECT_ZLIB_PROFILING)
    string(APPEND FASTXLSX_CONFIG_DEPENDENCIES
        "find_dependency(ZLIB)\n")
endif()

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FastXLSXConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/FastXLSXConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FastXLSX
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/FastXLSXConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/FastXLSXConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/FastXLSXConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FastXLSX
)

install(
    FILES
        LICENSE
        THIRD_PARTY_NOTICES.md
        CHANGELOG.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)
