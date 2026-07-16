add_executable(fastxlsx_bench_streaming_writer
    bench_streaming_writer.cpp
)

add_executable(fastxlsx_bench_workbook_editor
    bench_workbook_editor.cpp
)

add_executable(fastxlsx_bench_package_editor_cell_replacement
    bench_package_editor_cell_replacement.cpp
)

add_executable(fastxlsx_bench_package_writer
    bench_package_writer.cpp
    package_writer_direct_deflate.cpp
)

target_link_libraries(fastxlsx_bench_streaming_writer PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_bench_streaming_writer PRIVATE cxx_std_20)
target_compile_definitions(fastxlsx_bench_streaming_writer PRIVATE
    FASTXLSX_BENCH_DEFAULT_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    FASTXLSX_ENABLE_BENCHMARK_METRICS=1
)

target_link_libraries(fastxlsx_bench_workbook_editor PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_bench_workbook_editor PRIVATE cxx_std_20)
target_compile_definitions(fastxlsx_bench_workbook_editor PRIVATE
    FASTXLSX_BENCH_DEFAULT_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    FASTXLSX_ENABLE_TEST_HOOKS=1
)
if(FASTXLSX_ENABLE_MINIZIP_NG AND NOT FASTXLSX_ENABLE_PORTABLE_CRC_PROFILING)
    target_compile_definitions(fastxlsx_bench_workbook_editor PRIVATE
        FASTXLSX_BENCH_PACKAGE_EDITOR_CRC_MINIZIP_NG=1)
else()
    target_compile_definitions(fastxlsx_bench_workbook_editor PRIVATE
        FASTXLSX_BENCH_PACKAGE_EDITOR_CRC_PORTABLE=1)
endif()

target_link_libraries(fastxlsx_bench_package_editor_cell_replacement PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_bench_package_editor_cell_replacement PRIVATE cxx_std_20)
target_compile_definitions(fastxlsx_bench_package_editor_cell_replacement PRIVATE
    FASTXLSX_BENCH_DEFAULT_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    FASTXLSX_ENABLE_TEST_HOOKS=1
)

target_link_libraries(fastxlsx_bench_package_writer PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_bench_package_writer PRIVATE cxx_std_20)
target_compile_definitions(fastxlsx_bench_package_writer PRIVATE
    FASTXLSX_BENCH_DEFAULT_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

if(FASTXLSX_ENABLE_MINIZIP_NG)
    target_compile_definitions(fastxlsx_bench_streaming_writer PRIVATE FASTXLSX_BENCH_HAS_MINIZIP_NG=1)
    find_package(ZLIB REQUIRED)
    target_link_libraries(fastxlsx_bench_package_writer PRIVATE ZLIB::ZLIB)
    target_compile_definitions(fastxlsx_bench_package_writer PRIVATE FASTXLSX_BENCH_HAS_ZLIB=1)
    if(FASTXLSX_ENABLE_DIRECT_ZLIB_PROFILING)
        target_compile_definitions(fastxlsx_bench_package_writer PRIVATE
            FASTXLSX_BENCH_HAS_DIRECT_ZLIB_PROFILING=1)
    endif()
endif()

if(WIN32)
    target_link_libraries(fastxlsx_bench_streaming_writer PRIVATE psapi)
    target_link_libraries(fastxlsx_bench_workbook_editor PRIVATE psapi)
    target_link_libraries(fastxlsx_bench_package_editor_cell_replacement PRIVATE psapi)
    target_link_libraries(fastxlsx_bench_package_writer PRIVATE psapi)
endif()

function(fastxlsx_configure_reference_benchmark target_name)
    target_compile_features(${target_name} PRIVATE cxx_std_20)
    target_compile_definitions(${target_name} PRIVATE
        FASTXLSX_REFERENCE_BENCH_DEFAULT_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    )
    if(WIN32)
        target_link_libraries(${target_name} PRIVATE psapi)
    endif()
endfunction()

if(FASTXLSX_BUILD_REFERENCE_BENCHMARKS)
    find_package(OpenXLSX CONFIG QUIET)
    if(TARGET OpenXLSX::OpenXLSX)
        add_executable(fastxlsx_bench_reference_openxlsx
            bench_reference_openxlsx.cpp
        )
        target_link_libraries(fastxlsx_bench_reference_openxlsx PRIVATE OpenXLSX::OpenXLSX)
        fastxlsx_configure_reference_benchmark(fastxlsx_bench_reference_openxlsx)
    elseif(TARGET OpenXLSX)
        add_executable(fastxlsx_bench_reference_openxlsx
            bench_reference_openxlsx.cpp
        )
        target_link_libraries(fastxlsx_bench_reference_openxlsx PRIVATE OpenXLSX)
        fastxlsx_configure_reference_benchmark(fastxlsx_bench_reference_openxlsx)
    else()
        message(STATUS "OpenXLSX reference benchmark adapter skipped: OpenXLSX package target not found")
    endif()

    find_package(Xlnt CONFIG QUIET)
    if(TARGET xlnt::xlnt)
        add_executable(fastxlsx_bench_reference_xlnt
            bench_reference_xlnt.cpp
        )
        target_link_libraries(fastxlsx_bench_reference_xlnt PRIVATE xlnt::xlnt)
        fastxlsx_configure_reference_benchmark(fastxlsx_bench_reference_xlnt)
    else()
        message(STATUS "xlnt reference benchmark adapter skipped: xlnt::xlnt package target not found")
    endif()
endif()
