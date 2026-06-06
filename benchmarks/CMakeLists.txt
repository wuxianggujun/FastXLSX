add_executable(fastxlsx_bench_streaming_writer
    bench_streaming_writer.cpp
)

target_link_libraries(fastxlsx_bench_streaming_writer PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_bench_streaming_writer PRIVATE cxx_std_20)
target_compile_definitions(fastxlsx_bench_streaming_writer PRIVATE
    FASTXLSX_BENCH_DEFAULT_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    FASTXLSX_ENABLE_BENCHMARK_METRICS=1
)

if(FASTXLSX_ENABLE_MINIZIP_NG)
    target_compile_definitions(fastxlsx_bench_streaming_writer PRIVATE FASTXLSX_BENCH_HAS_MINIZIP_NG=1)
endif()

if(WIN32)
    target_link_libraries(fastxlsx_bench_streaming_writer PRIVATE psapi)
endif()
