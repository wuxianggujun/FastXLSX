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

add_executable(fastxlsx_worksheet_event_reader_tests
    test_worksheet_event_reader.cpp
)

target_link_libraries(fastxlsx_worksheet_event_reader_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_worksheet_event_reader_tests PRIVATE cxx_std_20)

add_test(NAME fastxlsx.worksheet_event_reader COMMAND fastxlsx_worksheet_event_reader_tests)
set_tests_properties(fastxlsx.worksheet_event_reader PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
)

add_executable(fastxlsx_worksheet_transformer_tests
    test_worksheet_transformer.cpp
)

target_link_libraries(fastxlsx_worksheet_transformer_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_worksheet_transformer_tests PRIVATE cxx_std_20)

add_test(NAME fastxlsx.worksheet_transformer COMMAND fastxlsx_worksheet_transformer_tests)
set_tests_properties(fastxlsx.worksheet_transformer PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
)

add_executable(fastxlsx_package_reader_tests
    test_package_reader.cpp
)

target_link_libraries(fastxlsx_package_reader_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_package_reader_tests PRIVATE cxx_std_20)
target_compile_definitions(fastxlsx_package_reader_tests PRIVATE FASTXLSX_ENABLE_TEST_HOOKS=1)
target_include_directories(fastxlsx_package_reader_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(FASTXLSX_ENABLE_MINIZIP_NG)
    target_link_libraries(fastxlsx_package_reader_tests PRIVATE MINIZIP::minizip-ng)
    target_compile_definitions(fastxlsx_package_reader_tests PRIVATE FASTXLSX_TEST_HAS_MINIZIP_NG=1)
endif()

add_test(NAME fastxlsx.package_reader COMMAND fastxlsx_package_reader_tests)
set_tests_properties(fastxlsx.package_reader PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
)

add_executable(fastxlsx_package_editor_tests
    test_package_editor.cpp
)

target_link_libraries(fastxlsx_package_editor_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_package_editor_tests PRIVATE cxx_std_20)
target_compile_definitions(fastxlsx_package_editor_tests PRIVATE FASTXLSX_ENABLE_TEST_HOOKS=1)
target_include_directories(fastxlsx_package_editor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(FASTXLSX_ENABLE_MINIZIP_NG)
    target_link_libraries(fastxlsx_package_editor_tests PRIVATE MINIZIP::minizip-ng)
    target_compile_definitions(fastxlsx_package_editor_tests PRIVATE FASTXLSX_TEST_HAS_MINIZIP_NG=1)
endif()

function(add_fastxlsx_package_editor_shard shard timeout_seconds)
    add_test(
        NAME fastxlsx.package_editor.${shard}
        COMMAND fastxlsx_package_editor_tests --shard=${shard}
    )
    set_tests_properties(fastxlsx.package_editor.${shard} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        TIMEOUT ${timeout_seconds}
    )
endfunction()

add_fastxlsx_package_editor_shard(core 60)
add_fastxlsx_package_editor_shard(c5 60)
add_fastxlsx_package_editor_shard(preservation-core 60)
add_fastxlsx_package_editor_shard(preservation-removal 60)
add_fastxlsx_package_editor_shard(preservation-resources 60)
add_fastxlsx_package_editor_shard(preservation-comments 60)
add_fastxlsx_package_editor_shard(preservation-linked 60)
add_fastxlsx_package_editor_shard(cellstore 60)
add_fastxlsx_package_editor_shard(sheetdata 60)
add_fastxlsx_package_editor_shard(sheetdata-catalog 60)
add_fastxlsx_package_editor_shard(sheetdata-guards 60)
add_fastxlsx_package_editor_shard(sheetdata-linked 60)
add_fastxlsx_package_editor_shard(policy 60)

add_executable(fastxlsx_workbook_editor_tests
    test_workbook_editor.cpp
)

target_link_libraries(fastxlsx_workbook_editor_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_workbook_editor_tests PRIVATE cxx_std_20)
target_compile_definitions(fastxlsx_workbook_editor_tests PRIVATE FASTXLSX_ENABLE_TEST_HOOKS=1)
target_include_directories(fastxlsx_workbook_editor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(FASTXLSX_ENABLE_MINIZIP_NG)
    target_link_libraries(fastxlsx_workbook_editor_tests PRIVATE MINIZIP::minizip-ng)
    target_compile_definitions(fastxlsx_workbook_editor_tests PRIVATE FASTXLSX_TEST_HAS_MINIZIP_NG=1)
endif()

add_test(NAME fastxlsx.workbook_editor COMMAND fastxlsx_workbook_editor_tests)
set_tests_properties(fastxlsx.workbook_editor PROPERTIES
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
