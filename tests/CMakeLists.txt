add_executable(fastxlsx_tests
    test_minimal_xlsx.cpp
)

target_link_libraries(fastxlsx_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_tests PRIVATE cxx_std_20)

add_test(NAME fastxlsx.unit COMMAND fastxlsx_tests)
set_tests_properties(fastxlsx.unit PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
)

add_executable(fastxlsx_streaming_writer_tests
    test_streaming_writer.cpp
)

target_link_libraries(fastxlsx_streaming_writer_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_streaming_writer_tests PRIVATE cxx_std_20)

add_test(NAME fastxlsx.streaming COMMAND fastxlsx_streaming_writer_tests)
set_tests_properties(fastxlsx.streaming PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
)

add_executable(fastxlsx_opc_tests
    test_opc.cpp
)

target_link_libraries(fastxlsx_opc_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_opc_tests PRIVATE cxx_std_20)

add_test(NAME fastxlsx.opc COMMAND fastxlsx_opc_tests)
set_tests_properties(fastxlsx.opc PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
)
