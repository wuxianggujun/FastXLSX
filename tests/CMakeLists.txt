add_executable(fastxlsx_tests
    test_minimal_xlsx.cpp
)

target_link_libraries(fastxlsx_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_tests PRIVATE cxx_std_20)
target_include_directories(fastxlsx_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(FASTXLSX_ENABLE_MINIZIP_NG)
    target_link_libraries(fastxlsx_tests PRIVATE MINIZIP::minizip-ng)
    target_compile_definitions(fastxlsx_tests PRIVATE FASTXLSX_TEST_HAS_MINIZIP_NG=1)
endif()

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
target_compile_definitions(fastxlsx_streaming_writer_tests PRIVATE FASTXLSX_ENABLE_TEST_HOOKS=1)
target_include_directories(fastxlsx_streaming_writer_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(FASTXLSX_ENABLE_MINIZIP_NG)
    target_link_libraries(fastxlsx_streaming_writer_tests PRIVATE MINIZIP::minizip-ng)
    target_compile_definitions(fastxlsx_streaming_writer_tests PRIVATE FASTXLSX_TEST_HAS_MINIZIP_NG=1)
endif()

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

add_executable(fastxlsx_image_tests
    test_image.cpp
)

target_link_libraries(fastxlsx_image_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_image_tests PRIVATE cxx_std_20)

add_test(NAME fastxlsx.image COMMAND fastxlsx_image_tests)
set_tests_properties(fastxlsx.image PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
)
