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

add_executable(fastxlsx_formula_tests
    test_formula.cpp
)

target_link_libraries(fastxlsx_formula_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_formula_tests PRIVATE cxx_std_20)

add_test(NAME fastxlsx.formula COMMAND fastxlsx_formula_tests)
set_tests_properties(fastxlsx.formula PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
)

add_executable(fastxlsx_workbook_editor_state_tests
    test_workbook_editor_state.cpp
)

target_link_libraries(fastxlsx_workbook_editor_state_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_workbook_editor_state_tests PRIVATE cxx_std_20)

add_test(
    NAME fastxlsx.workbook_editor_state
    COMMAND fastxlsx_workbook_editor_state_tests
)
set_tests_properties(fastxlsx.workbook_editor_state PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
)

add_executable(fastxlsx_workbook_editor_sheet_catalog_tests
    test_workbook_editor_sheet_catalog.cpp
)

target_link_libraries(fastxlsx_workbook_editor_sheet_catalog_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_workbook_editor_sheet_catalog_tests PRIVATE cxx_std_20)

add_test(
    NAME fastxlsx.workbook_editor_sheet_catalog
    COMMAND fastxlsx_workbook_editor_sheet_catalog_tests
)
set_tests_properties(fastxlsx.workbook_editor_sheet_catalog PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
)

add_executable(fastxlsx_workbook_editor_pending_edits_tests
    test_workbook_editor_pending_edits.cpp
)

target_link_libraries(fastxlsx_workbook_editor_pending_edits_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_workbook_editor_pending_edits_tests PRIVATE cxx_std_20)

add_test(
    NAME fastxlsx.workbook_editor_pending_edits
    COMMAND fastxlsx_workbook_editor_pending_edits_tests
)
set_tests_properties(fastxlsx.workbook_editor_pending_edits PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
)

add_executable(fastxlsx_workbook_editor_sheet_data_replacement_tests
    test_workbook_editor_sheet_data_replacement.cpp
)

target_link_libraries(fastxlsx_workbook_editor_sheet_data_replacement_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_workbook_editor_sheet_data_replacement_tests PRIVATE cxx_std_20)

add_test(
    NAME fastxlsx.workbook_editor_sheet_data_replacement
    COMMAND fastxlsx_workbook_editor_sheet_data_replacement_tests
)
set_tests_properties(fastxlsx.workbook_editor_sheet_data_replacement PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
)

add_executable(fastxlsx_workbook_editor_sheet_rename_tests
    test_workbook_editor_sheet_rename.cpp
)

target_link_libraries(fastxlsx_workbook_editor_sheet_rename_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_workbook_editor_sheet_rename_tests PRIVATE cxx_std_20)

add_test(
    NAME fastxlsx.workbook_editor_sheet_rename
    COMMAND fastxlsx_workbook_editor_sheet_rename_tests
)
set_tests_properties(fastxlsx.workbook_editor_sheet_rename PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
)

add_executable(fastxlsx_workbook_editor_worksheet_access_tests
    test_workbook_editor_worksheet_access.cpp
)

target_link_libraries(fastxlsx_workbook_editor_worksheet_access_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_workbook_editor_worksheet_access_tests PRIVATE cxx_std_20)

add_test(
    NAME fastxlsx.workbook_editor_worksheet_access
    COMMAND fastxlsx_workbook_editor_worksheet_access_tests
)
set_tests_properties(fastxlsx.workbook_editor_worksheet_access PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
)

add_executable(fastxlsx_workbook_editor_save_as_policy_tests
    test_workbook_editor_save_as_policy.cpp
)

target_link_libraries(fastxlsx_workbook_editor_save_as_policy_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_workbook_editor_save_as_policy_tests PRIVATE cxx_std_20)

add_test(
    NAME fastxlsx.workbook_editor_save_as_policy
    COMMAND fastxlsx_workbook_editor_save_as_policy_tests
)
set_tests_properties(fastxlsx.workbook_editor_save_as_policy PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
)

add_executable(fastxlsx_workbook_editor_materialized_edits_tests
    test_workbook_editor_materialized_edits.cpp
)

target_link_libraries(fastxlsx_workbook_editor_materialized_edits_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_workbook_editor_materialized_edits_tests PRIVATE cxx_std_20)

add_test(
    NAME fastxlsx.workbook_editor_materialized_edits
    COMMAND fastxlsx_workbook_editor_materialized_edits_tests
)
set_tests_properties(fastxlsx.workbook_editor_materialized_edits PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
)

function(add_fastxlsx_streaming_writer_target target source_file)
    add_executable(${target}
        ${source_file}
    )

    target_link_libraries(${target} PRIVATE FastXLSX::fastxlsx)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE FASTXLSX_ENABLE_TEST_HOOKS=1)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(FASTXLSX_ENABLE_MINIZIP_NG)
        target_link_libraries(${target} PRIVATE MINIZIP::minizip-ng)
        target_compile_definitions(${target} PRIVATE FASTXLSX_TEST_HAS_MINIZIP_NG=1)
    endif()
endfunction()

add_fastxlsx_streaming_writer_target(
    fastxlsx_streaming_writer_core_tests
    test_streaming_writer.cpp
)
add_fastxlsx_streaming_writer_target(
    fastxlsx_streaming_writer_styles_tests
    test_streaming_writer_styles.cpp
)
add_fastxlsx_streaming_writer_target(
    fastxlsx_streaming_writer_conditional_formatting_tests
    test_streaming_writer_conditional_formatting.cpp
)
add_fastxlsx_streaming_writer_target(
    fastxlsx_streaming_writer_metadata_tests
    test_streaming_writer_metadata.cpp
)
add_fastxlsx_streaming_writer_target(
    fastxlsx_streaming_writer_images_tests
    test_streaming_writer_images.cpp
)
add_fastxlsx_streaming_writer_target(
    fastxlsx_streaming_writer_shared_strings_tests
    test_streaming_writer_shared_strings.cpp
)

add_custom_target(fastxlsx_streaming_writer_tests
    DEPENDS
        fastxlsx_streaming_writer_core_tests
        fastxlsx_streaming_writer_styles_tests
        fastxlsx_streaming_writer_conditional_formatting_tests
        fastxlsx_streaming_writer_metadata_tests
        fastxlsx_streaming_writer_images_tests
        fastxlsx_streaming_writer_shared_strings_tests
)

function(add_fastxlsx_streaming_writer_test test_name target timeout_seconds)
    add_test(NAME ${test_name} COMMAND ${target})
    set_tests_properties(${test_name} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        TIMEOUT ${timeout_seconds}
    )
endfunction()

add_fastxlsx_streaming_writer_test(fastxlsx.streaming fastxlsx_streaming_writer_core_tests 60)
add_fastxlsx_streaming_writer_test(
    fastxlsx.streaming.styles fastxlsx_streaming_writer_styles_tests 60
)
add_fastxlsx_streaming_writer_test(
    fastxlsx.streaming.conditional-formatting
    fastxlsx_streaming_writer_conditional_formatting_tests 60
)
add_fastxlsx_streaming_writer_test(
    fastxlsx.streaming.metadata fastxlsx_streaming_writer_metadata_tests 60
)
add_fastxlsx_streaming_writer_test(
    fastxlsx.streaming.images fastxlsx_streaming_writer_images_tests 60
)
add_fastxlsx_streaming_writer_test(
    fastxlsx.streaming.shared-strings fastxlsx_streaming_writer_shared_strings_tests 60
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

function(add_fastxlsx_package_reader_target target source_file)
    add_executable(${target}
        ${source_file}
    )
    target_link_libraries(${target} PRIVATE FastXLSX::fastxlsx)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE FASTXLSX_ENABLE_TEST_HOOKS=1)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(FASTXLSX_ENABLE_MINIZIP_NG)
        target_link_libraries(${target} PRIVATE MINIZIP::minizip-ng)
        target_compile_definitions(${target} PRIVATE FASTXLSX_TEST_HAS_MINIZIP_NG=1)
    endif()
endfunction()

add_fastxlsx_package_reader_target(
    fastxlsx_package_reader_core_tests
    test_package_reader.cpp
)
add_fastxlsx_package_reader_target(
    fastxlsx_package_reader_workbook_tests
    test_package_reader_workbook.cpp
)
add_fastxlsx_package_reader_target(
    fastxlsx_package_reader_zip_failures_tests
    test_package_reader_zip_failures.cpp
)

add_custom_target(fastxlsx_package_reader_tests
    DEPENDS
        fastxlsx_package_reader_core_tests
        fastxlsx_package_reader_workbook_tests
        fastxlsx_package_reader_zip_failures_tests
)

function(add_fastxlsx_package_reader_shard shard target timeout_seconds)
    add_test(
        NAME fastxlsx.package_reader${shard}
        COMMAND ${target}
    )
    set_tests_properties(fastxlsx.package_reader${shard} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        TIMEOUT ${timeout_seconds}
    )
endfunction()

add_fastxlsx_package_reader_shard("" fastxlsx_package_reader_core_tests 60)
add_fastxlsx_package_reader_shard(".workbook" fastxlsx_package_reader_workbook_tests 60)
add_fastxlsx_package_reader_shard(
    ".zip-failures" fastxlsx_package_reader_zip_failures_tests 60
)

function(add_fastxlsx_package_editor_target target source_file)
    add_executable(${target}
        ${source_file}
    )
    target_link_libraries(${target} PRIVATE FastXLSX::fastxlsx)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE FASTXLSX_ENABLE_TEST_HOOKS=1)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(FASTXLSX_ENABLE_MINIZIP_NG)
        target_link_libraries(${target} PRIVATE MINIZIP::minizip-ng)
        target_compile_definitions(${target} PRIVATE FASTXLSX_TEST_HAS_MINIZIP_NG=1)
    endif()
endfunction()

add_fastxlsx_package_editor_target(
    fastxlsx_package_editor_core_tests
    test_package_editor_core.cpp
)
add_fastxlsx_package_editor_target(
    fastxlsx_package_editor_core_calc_tests
    test_package_editor_core_calc.cpp
)
add_fastxlsx_package_editor_target(
    fastxlsx_package_editor_core_linked_tests
    test_package_editor_core_linked.cpp
)
add_fastxlsx_package_editor_target(
    fastxlsx_package_editor_c5_core_tests
    test_package_editor_c5.cpp
)
add_fastxlsx_package_editor_target(
    fastxlsx_package_editor_c5_linked_tests
    test_package_editor_c5_linked.cpp
)
add_fastxlsx_package_editor_target(
    fastxlsx_package_editor_c5_chunked_tests
    test_package_editor_c5_chunked.cpp
)
add_fastxlsx_package_editor_target(
    fastxlsx_package_editor_c5_guards_tests
    test_package_editor_c5_guards.cpp
)
add_fastxlsx_package_editor_target(
    fastxlsx_package_editor_c5_large_tests
    test_package_editor_c5_large.cpp
)
add_fastxlsx_package_editor_target(
    fastxlsx_package_editor_policy_core_tests
    test_package_editor_policy.cpp
)
add_fastxlsx_package_editor_target(
    fastxlsx_package_editor_policy_save_as_guards_tests
    test_package_editor_policy_save_as_guards.cpp
)
add_fastxlsx_package_editor_target(
    fastxlsx_package_editor_policy_invalid_inputs_tests
    test_package_editor_policy_invalid_inputs.cpp
)

add_custom_target(fastxlsx_package_editor_c5_tests
    DEPENDS
        fastxlsx_package_editor_c5_core_tests
        fastxlsx_package_editor_c5_linked_tests
        fastxlsx_package_editor_c5_chunked_tests
        fastxlsx_package_editor_c5_guards_tests
        fastxlsx_package_editor_c5_large_tests
)

add_custom_target(fastxlsx_package_editor_policy_tests
    DEPENDS
        fastxlsx_package_editor_policy_core_tests
        fastxlsx_package_editor_policy_save_as_guards_tests
        fastxlsx_package_editor_policy_invalid_inputs_tests
)

add_custom_target(fastxlsx_package_editor_tests
    DEPENDS
        fastxlsx_package_editor_core_tests
        fastxlsx_package_editor_core_calc_tests
        fastxlsx_package_editor_core_linked_tests
        fastxlsx_package_editor_c5_tests
        fastxlsx_package_editor_policy_tests
)

function(add_fastxlsx_package_editor_shard shard target timeout_seconds)
    add_test(
        NAME fastxlsx.package_editor.${shard}
        COMMAND ${target} --shard=${shard}
    )
    set_tests_properties(fastxlsx.package_editor.${shard} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        TIMEOUT ${timeout_seconds}
    )
endfunction()

add_fastxlsx_package_editor_shard(core fastxlsx_package_editor_core_tests 60)
add_fastxlsx_package_editor_shard(core-calc fastxlsx_package_editor_core_calc_tests 60)
add_fastxlsx_package_editor_shard(core-linked fastxlsx_package_editor_core_linked_tests 60)
add_fastxlsx_package_editor_shard(c5 fastxlsx_package_editor_c5_core_tests 60)
add_fastxlsx_package_editor_shard(c5-linked fastxlsx_package_editor_c5_linked_tests 60)
add_fastxlsx_package_editor_shard(c5-chunked fastxlsx_package_editor_c5_chunked_tests 60)
add_fastxlsx_package_editor_shard(c5-guards fastxlsx_package_editor_c5_guards_tests 60)
add_fastxlsx_package_editor_shard(c5-large fastxlsx_package_editor_c5_large_tests 60)
add_fastxlsx_package_editor_shard(policy fastxlsx_package_editor_policy_core_tests 60)
add_fastxlsx_package_editor_shard(
    policy-save-as-guards fastxlsx_package_editor_policy_save_as_guards_tests 60
)
add_fastxlsx_package_editor_shard(
    policy-invalid-inputs fastxlsx_package_editor_policy_invalid_inputs_tests 60
)

function(add_fastxlsx_package_editor_preservation_target target source_file)
    add_executable(${target}
        ${source_file}
    )
    target_link_libraries(${target} PRIVATE FastXLSX::fastxlsx)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE FASTXLSX_ENABLE_TEST_HOOKS=1)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(FASTXLSX_ENABLE_MINIZIP_NG)
        target_link_libraries(${target} PRIVATE MINIZIP::minizip-ng)
        target_compile_definitions(${target} PRIVATE FASTXLSX_TEST_HAS_MINIZIP_NG=1)
    endif()
endfunction()

add_fastxlsx_package_editor_preservation_target(
    fastxlsx_package_editor_preservation_core_tests
    test_package_editor_preservation_core.cpp
)
add_fastxlsx_package_editor_preservation_target(
    fastxlsx_package_editor_preservation_core_drawings_tests
    test_package_editor_preservation_core_drawings.cpp
)
add_fastxlsx_package_editor_preservation_target(
    fastxlsx_package_editor_preservation_core_docparts_tests
    test_package_editor_preservation_core_docparts.cpp
)
add_fastxlsx_package_editor_preservation_target(
    fastxlsx_package_editor_preservation_core_docparts_drawing_tests
    test_package_editor_preservation_core_docparts_drawing.cpp
)
add_fastxlsx_package_editor_preservation_target(
    fastxlsx_package_editor_preservation_core_docparts_shared_tests
    test_package_editor_preservation_core_docparts_shared.cpp
)
add_fastxlsx_package_editor_preservation_target(
    fastxlsx_package_editor_preservation_core_docparts_vba_tests
    test_package_editor_preservation_core_docparts_vba.cpp
)
add_fastxlsx_package_editor_preservation_target(
    fastxlsx_package_editor_preservation_removal_core_tests
    test_package_editor_preservation_removal.cpp
)
add_fastxlsx_package_editor_preservation_target(
    fastxlsx_package_editor_preservation_removal_policy_tests
    test_package_editor_preservation_removal_policy.cpp
)
add_fastxlsx_package_editor_preservation_target(
    fastxlsx_package_editor_preservation_removal_workbook_parts_tests
    test_package_editor_preservation_removal_workbook_parts.cpp
)
add_fastxlsx_package_editor_preservation_target(
    fastxlsx_package_editor_preservation_removal_drawing_parts_tests
    test_package_editor_preservation_removal_drawing_parts.cpp
)
add_fastxlsx_package_editor_preservation_target(
    fastxlsx_package_editor_preservation_resources_tests
    test_package_editor_preservation_resources.cpp
)
add_fastxlsx_package_editor_preservation_target(
    fastxlsx_package_editor_preservation_comments_tests
    test_package_editor_preservation_comments.cpp
)
add_fastxlsx_package_editor_preservation_target(
    fastxlsx_package_editor_preservation_linked_tests
    test_package_editor_preservation_linked.cpp
)
add_fastxlsx_package_editor_preservation_target(
    fastxlsx_package_editor_preservation_linked_pivot_core_tests
    test_package_editor_preservation_linked_pivot.cpp
)
add_fastxlsx_package_editor_preservation_target(
    fastxlsx_package_editor_preservation_linked_pivot_cache_definition_tests
    test_package_editor_preservation_linked_pivot_cache_definition.cpp
)
add_fastxlsx_package_editor_preservation_target(
    fastxlsx_package_editor_preservation_linked_pivot_cache_records_tests
    test_package_editor_preservation_linked_pivot_cache_records.cpp
)
add_fastxlsx_package_editor_preservation_target(
    fastxlsx_package_editor_preservation_linked_external_links_tests
    test_package_editor_preservation_linked_external_links.cpp
)
add_fastxlsx_package_editor_preservation_target(
    fastxlsx_package_editor_preservation_linked_custom_xml_tests
    test_package_editor_preservation_linked_custom_xml.cpp
)

add_custom_target(fastxlsx_package_editor_preservation_linked_pivot_tests
    DEPENDS
        fastxlsx_package_editor_preservation_linked_pivot_core_tests
        fastxlsx_package_editor_preservation_linked_pivot_cache_definition_tests
        fastxlsx_package_editor_preservation_linked_pivot_cache_records_tests
)

add_custom_target(fastxlsx_package_editor_preservation_removal_tests
    DEPENDS
        fastxlsx_package_editor_preservation_removal_core_tests
        fastxlsx_package_editor_preservation_removal_policy_tests
        fastxlsx_package_editor_preservation_removal_workbook_parts_tests
        fastxlsx_package_editor_preservation_removal_drawing_parts_tests
)

add_custom_target(fastxlsx_package_editor_preservation_tests
    DEPENDS
        fastxlsx_package_editor_preservation_core_tests
        fastxlsx_package_editor_preservation_core_drawings_tests
        fastxlsx_package_editor_preservation_core_docparts_tests
        fastxlsx_package_editor_preservation_core_docparts_drawing_tests
        fastxlsx_package_editor_preservation_core_docparts_shared_tests
        fastxlsx_package_editor_preservation_core_docparts_vba_tests
        fastxlsx_package_editor_preservation_removal_tests
        fastxlsx_package_editor_preservation_resources_tests
        fastxlsx_package_editor_preservation_comments_tests
        fastxlsx_package_editor_preservation_linked_tests
        fastxlsx_package_editor_preservation_linked_pivot_tests
        fastxlsx_package_editor_preservation_linked_external_links_tests
        fastxlsx_package_editor_preservation_linked_custom_xml_tests
)

function(add_fastxlsx_package_editor_preservation_shard shard target timeout_seconds)
    add_test(
        NAME fastxlsx.package_editor.${shard}
        COMMAND ${target} --shard=${shard}
    )
    set_tests_properties(fastxlsx.package_editor.${shard} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        TIMEOUT ${timeout_seconds}
    )
endfunction()

add_fastxlsx_package_editor_preservation_shard(
    preservation-core fastxlsx_package_editor_preservation_core_tests 60
)
add_fastxlsx_package_editor_preservation_shard(
    preservation-core-drawings
    fastxlsx_package_editor_preservation_core_drawings_tests 60
)
add_fastxlsx_package_editor_preservation_shard(
    preservation-core-docparts
    fastxlsx_package_editor_preservation_core_docparts_tests 60
)
add_fastxlsx_package_editor_preservation_shard(
    preservation-core-docparts-drawing
    fastxlsx_package_editor_preservation_core_docparts_drawing_tests 60
)
add_fastxlsx_package_editor_preservation_shard(
    preservation-core-docparts-shared
    fastxlsx_package_editor_preservation_core_docparts_shared_tests 60
)
add_fastxlsx_package_editor_preservation_shard(
    preservation-core-docparts-vba
    fastxlsx_package_editor_preservation_core_docparts_vba_tests 60
)
add_fastxlsx_package_editor_preservation_shard(
    preservation-removal fastxlsx_package_editor_preservation_removal_core_tests 60
)
add_fastxlsx_package_editor_preservation_shard(
    preservation-removal-policy
    fastxlsx_package_editor_preservation_removal_policy_tests 60
)
add_fastxlsx_package_editor_preservation_shard(
    preservation-removal-workbook-parts
    fastxlsx_package_editor_preservation_removal_workbook_parts_tests 60
)
add_fastxlsx_package_editor_preservation_shard(
    preservation-removal-drawing-parts
    fastxlsx_package_editor_preservation_removal_drawing_parts_tests 60
)
add_fastxlsx_package_editor_preservation_shard(
    preservation-resources fastxlsx_package_editor_preservation_resources_tests 60
)
add_fastxlsx_package_editor_preservation_shard(
    preservation-comments fastxlsx_package_editor_preservation_comments_tests 60
)
add_fastxlsx_package_editor_preservation_shard(
    preservation-linked fastxlsx_package_editor_preservation_linked_tests 60
)
add_fastxlsx_package_editor_preservation_shard(
    preservation-linked-pivot
    fastxlsx_package_editor_preservation_linked_pivot_core_tests 60
)
add_fastxlsx_package_editor_preservation_shard(
    preservation-linked-pivot-cache-definition
    fastxlsx_package_editor_preservation_linked_pivot_cache_definition_tests 60
)
add_fastxlsx_package_editor_preservation_shard(
    preservation-linked-pivot-cache-records
    fastxlsx_package_editor_preservation_linked_pivot_cache_records_tests 60
)
add_fastxlsx_package_editor_preservation_shard(
    preservation-linked-external-links
    fastxlsx_package_editor_preservation_linked_external_links_tests 60
)
add_fastxlsx_package_editor_preservation_shard(
    preservation-linked-custom-xml
    fastxlsx_package_editor_preservation_linked_custom_xml_tests 60
)

function(add_fastxlsx_package_editor_sheetdata_target target source_file)
    add_executable(${target}
        ${source_file}
    )
    target_link_libraries(${target} PRIVATE FastXLSX::fastxlsx)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE FASTXLSX_ENABLE_TEST_HOOKS=1)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(FASTXLSX_ENABLE_MINIZIP_NG)
        target_link_libraries(${target} PRIVATE MINIZIP::minizip-ng)
        target_compile_definitions(${target} PRIVATE FASTXLSX_TEST_HAS_MINIZIP_NG=1)
    endif()
endfunction()

add_fastxlsx_package_editor_sheetdata_target(
    fastxlsx_package_editor_sheetdata_base_tests
    test_package_editor_sheetdata_base.cpp
)
add_fastxlsx_package_editor_sheetdata_target(
    fastxlsx_package_editor_sheetdata_catalog_core_tests
    test_package_editor_sheetdata_catalog.cpp
)
add_fastxlsx_package_editor_sheetdata_target(
    fastxlsx_package_editor_sheetdata_catalog_guards_tests
    test_package_editor_sheetdata_catalog_guards.cpp
)
add_fastxlsx_package_editor_sheetdata_target(
    fastxlsx_package_editor_sheetdata_catalog_audits_tests
    test_package_editor_sheetdata_catalog_audits.cpp
)
add_fastxlsx_package_editor_sheetdata_target(
    fastxlsx_package_editor_sheetdata_guards_tests
    test_package_editor_sheetdata_guards.cpp
)
add_fastxlsx_package_editor_sheetdata_target(
    fastxlsx_package_editor_sheetdata_linked_core_tests
    test_package_editor_sheetdata_linked.cpp
)
add_fastxlsx_package_editor_sheetdata_target(
    fastxlsx_package_editor_sheetdata_linked_object_parts_tests
    test_package_editor_sheetdata_linked_objects.cpp
)
add_fastxlsx_package_editor_sheetdata_target(
    fastxlsx_package_editor_sheetdata_linked_background_vml_tests
    test_package_editor_sheetdata_linked_background.cpp
)

add_custom_target(fastxlsx_package_editor_sheetdata_catalog_tests
    DEPENDS
        fastxlsx_package_editor_sheetdata_catalog_core_tests
        fastxlsx_package_editor_sheetdata_catalog_guards_tests
        fastxlsx_package_editor_sheetdata_catalog_audits_tests
)

add_custom_target(fastxlsx_package_editor_sheetdata_linked_tests
    DEPENDS
        fastxlsx_package_editor_sheetdata_linked_core_tests
        fastxlsx_package_editor_sheetdata_linked_object_parts_tests
        fastxlsx_package_editor_sheetdata_linked_background_vml_tests
)

add_custom_target(fastxlsx_package_editor_sheetdata_tests
    DEPENDS
        fastxlsx_package_editor_sheetdata_base_tests
        fastxlsx_package_editor_sheetdata_catalog_tests
        fastxlsx_package_editor_sheetdata_guards_tests
        fastxlsx_package_editor_sheetdata_linked_tests
)

function(add_fastxlsx_package_editor_sheetdata_shard shard target timeout_seconds)
    add_test(
        NAME fastxlsx.package_editor.${shard}
        COMMAND ${target} --shard=${shard}
    )
    set_tests_properties(fastxlsx.package_editor.${shard} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        TIMEOUT ${timeout_seconds}
    )
endfunction()

add_fastxlsx_package_editor_sheetdata_shard(sheetdata fastxlsx_package_editor_sheetdata_base_tests 60)
add_fastxlsx_package_editor_sheetdata_shard(
    sheetdata-catalog fastxlsx_package_editor_sheetdata_catalog_core_tests 60
)
add_fastxlsx_package_editor_sheetdata_shard(
    sheetdata-catalog-guards fastxlsx_package_editor_sheetdata_catalog_guards_tests 60
)
add_fastxlsx_package_editor_sheetdata_shard(
    sheetdata-catalog-audits fastxlsx_package_editor_sheetdata_catalog_audits_tests 60
)
add_fastxlsx_package_editor_sheetdata_shard(
    sheetdata-guards fastxlsx_package_editor_sheetdata_guards_tests 60
)
add_fastxlsx_package_editor_sheetdata_shard(
    sheetdata-linked fastxlsx_package_editor_sheetdata_linked_core_tests 60
)
add_fastxlsx_package_editor_sheetdata_shard(
    sheetdata-linked-object-parts
    fastxlsx_package_editor_sheetdata_linked_object_parts_tests 60
)
add_fastxlsx_package_editor_sheetdata_shard(
    sheetdata-linked-background-vml
    fastxlsx_package_editor_sheetdata_linked_background_vml_tests 60
)

function(add_fastxlsx_package_editor_cellstore_target target source_file)
    add_executable(${target}
        ${source_file}
    )

    target_link_libraries(${target} PRIVATE FastXLSX::fastxlsx)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE FASTXLSX_ENABLE_TEST_HOOKS=1)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(FASTXLSX_ENABLE_MINIZIP_NG)
        target_link_libraries(${target} PRIVATE MINIZIP::minizip-ng)
        target_compile_definitions(${target} PRIVATE FASTXLSX_TEST_HAS_MINIZIP_NG=1)
    endif()
endfunction()

add_fastxlsx_package_editor_cellstore_target(
    fastxlsx_package_editor_cellstore_core_tests
    test_package_editor_cellstore.cpp
)
add_fastxlsx_package_editor_cellstore_target(
    fastxlsx_package_editor_cellstore_chunks_tests
    test_package_editor_cellstore_chunks.cpp
)
add_fastxlsx_package_editor_cellstore_target(
    fastxlsx_package_editor_cellstore_source_tests
    test_package_editor_cellstore_source.cpp
)
add_fastxlsx_package_editor_cellstore_target(
    fastxlsx_package_editor_cellstore_failures_tests
    test_package_editor_cellstore_failures.cpp
)
add_fastxlsx_package_editor_cellstore_target(
    fastxlsx_package_editor_cellstore_catalog_tests
    test_package_editor_cellstore_catalog.cpp
)

add_custom_target(fastxlsx_package_editor_cellstore_tests
    DEPENDS
        fastxlsx_package_editor_cellstore_core_tests
        fastxlsx_package_editor_cellstore_chunks_tests
        fastxlsx_package_editor_cellstore_source_tests
        fastxlsx_package_editor_cellstore_failures_tests
        fastxlsx_package_editor_cellstore_catalog_tests
)

function(add_fastxlsx_package_editor_cellstore_shard shard target timeout_seconds)
    add_test(
        NAME fastxlsx.package_editor.${shard}
        COMMAND ${target} --shard=${shard}
    )
    set_tests_properties(fastxlsx.package_editor.${shard} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        TIMEOUT ${timeout_seconds}
    )
endfunction()

add_fastxlsx_package_editor_cellstore_shard(
    cellstore-core fastxlsx_package_editor_cellstore_core_tests 60
)
add_fastxlsx_package_editor_cellstore_shard(
    cellstore-chunks fastxlsx_package_editor_cellstore_chunks_tests 60
)
add_fastxlsx_package_editor_cellstore_shard(
    cellstore-source fastxlsx_package_editor_cellstore_source_tests 60
)
add_fastxlsx_package_editor_cellstore_shard(
    cellstore-failures fastxlsx_package_editor_cellstore_failures_tests 60
)
add_fastxlsx_package_editor_cellstore_shard(
    cellstore-catalog fastxlsx_package_editor_cellstore_catalog_tests 60
)

function(add_fastxlsx_workbook_editor_target target source_file)
    add_executable(${target}
        ${source_file}
    )
    target_link_libraries(${target} PRIVATE FastXLSX::fastxlsx)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE FASTXLSX_ENABLE_TEST_HOOKS=1)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(FASTXLSX_ENABLE_MINIZIP_NG)
        target_link_libraries(${target} PRIVATE MINIZIP::minizip-ng)
        target_compile_definitions(${target} PRIVATE FASTXLSX_TEST_HAS_MINIZIP_NG=1)
    endif()
endfunction()

add_fastxlsx_workbook_editor_target(
    fastxlsx_workbook_editor_core_shard_tests
    test_workbook_editor_core.cpp
)
add_fastxlsx_workbook_editor_target(
    fastxlsx_workbook_editor_public_shard_tests
    test_workbook_editor_public.cpp
)
add_fastxlsx_workbook_editor_target(
    fastxlsx_workbook_editor_public_retry_tests
    test_workbook_editor_public_retry.cpp
)
add_fastxlsx_workbook_editor_target(
    fastxlsx_workbook_editor_public_state_tests
    test_workbook_editor_public_state.cpp
)
add_fastxlsx_workbook_editor_target(
    fastxlsx_workbook_editor_public_guards_tests
    test_workbook_editor_public_guards.cpp
)
add_fastxlsx_workbook_editor_target(
    fastxlsx_workbook_editor_public_edge_shard_tests
    test_workbook_editor_public_edge.cpp
)

add_custom_target(fastxlsx_workbook_editor_tests
    DEPENDS
        fastxlsx_workbook_editor_core_shard_tests
        fastxlsx_workbook_editor_public_shard_tests
        fastxlsx_workbook_editor_public_retry_tests
        fastxlsx_workbook_editor_public_state_tests
        fastxlsx_workbook_editor_public_guards_tests
        fastxlsx_workbook_editor_public_edge_shard_tests
)

function(add_fastxlsx_workbook_editor_shard shard target timeout_seconds)
    add_test(
        NAME fastxlsx.workbook_editor.${shard}
        COMMAND ${target} --shard=${shard}
    )
    set_tests_properties(fastxlsx.workbook_editor.${shard} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        TIMEOUT ${timeout_seconds}
    )
endfunction()

add_fastxlsx_workbook_editor_shard(core fastxlsx_workbook_editor_core_shard_tests 60)
add_fastxlsx_workbook_editor_shard(public fastxlsx_workbook_editor_public_shard_tests 60)
add_fastxlsx_workbook_editor_shard(
    public-retry fastxlsx_workbook_editor_public_retry_tests 60
)
add_fastxlsx_workbook_editor_shard(
    public-state fastxlsx_workbook_editor_public_state_tests 60
)
add_fastxlsx_workbook_editor_shard(
    public-guards fastxlsx_workbook_editor_public_guards_tests 60
)
add_fastxlsx_workbook_editor_shard(
    public-edge fastxlsx_workbook_editor_public_edge_shard_tests 60
)

function(add_fastxlsx_workbook_editor_standalone_test test_name source_file timeout_seconds)
    set(target_name fastxlsx_workbook_editor_${test_name}_tests)
    add_executable(${target_name}
        ${source_file}
    )

    target_link_libraries(${target_name} PRIVATE FastXLSX::fastxlsx)
    target_compile_features(${target_name} PRIVATE cxx_std_20)
    target_compile_definitions(${target_name} PRIVATE FASTXLSX_ENABLE_TEST_HOOKS=1)
    target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(FASTXLSX_ENABLE_MINIZIP_NG)
        target_link_libraries(${target_name} PRIVATE MINIZIP::minizip-ng)
        target_compile_definitions(${target_name} PRIVATE FASTXLSX_TEST_HAS_MINIZIP_NG=1)
    endif()

    add_test(
        NAME fastxlsx.workbook_editor_${test_name}
        COMMAND ${target_name}
    )
    set_tests_properties(fastxlsx.workbook_editor_${test_name} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        TIMEOUT ${timeout_seconds}
    )
endfunction()

function(add_fastxlsx_workbook_editor_source_success_test ctest_name target timeout_seconds)
    add_test(
        NAME ${ctest_name}
        COMMAND ${target}
    )
    set_tests_properties(${ctest_name} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        TIMEOUT ${timeout_seconds}
    )
endfunction()

add_fastxlsx_workbook_editor_target(
    fastxlsx_workbook_editor_source_success_core_tests
    test_workbook_editor_source_success.cpp
)
add_fastxlsx_workbook_editor_target(
    fastxlsx_workbook_editor_source_success_shared_strings_tests
    test_workbook_editor_source_success_shared_strings.cpp
)
add_fastxlsx_workbook_editor_target(
    fastxlsx_workbook_editor_source_success_max_coordinate_tests
    test_workbook_editor_source_success_max_coordinate.cpp
)
add_fastxlsx_workbook_editor_target(
    fastxlsx_workbook_editor_source_success_formulas_tests
    test_workbook_editor_source_success_formulas.cpp
)

add_custom_target(fastxlsx_workbook_editor_source_success_tests
    DEPENDS
        fastxlsx_workbook_editor_source_success_core_tests
        fastxlsx_workbook_editor_source_success_shared_strings_tests
        fastxlsx_workbook_editor_source_success_max_coordinate_tests
        fastxlsx_workbook_editor_source_success_formulas_tests
)

add_fastxlsx_workbook_editor_source_success_test(
    fastxlsx.workbook_editor_source_success
    fastxlsx_workbook_editor_source_success_core_tests
    60
)
add_fastxlsx_workbook_editor_source_success_test(
    fastxlsx.workbook_editor_source_success_shared_strings
    fastxlsx_workbook_editor_source_success_shared_strings_tests
    60
)
add_fastxlsx_workbook_editor_source_success_test(
    fastxlsx.workbook_editor_source_success_max_coordinate
    fastxlsx_workbook_editor_source_success_max_coordinate_tests
    60
)
add_fastxlsx_workbook_editor_source_success_test(
    fastxlsx.workbook_editor_source_success_formulas
    fastxlsx_workbook_editor_source_success_formulas_tests
    60
)
add_fastxlsx_workbook_editor_standalone_test(source_failures test_workbook_editor_source_failures.cpp 60)
add_fastxlsx_workbook_editor_standalone_test(materialized_sessions test_workbook_editor_materialized_sessions.cpp 60)
add_fastxlsx_workbook_editor_standalone_test(facade test_workbook_editor_facade.cpp 60)

add_executable(fastxlsx_workbook_editor_formula_rewrite_tests
    test_workbook_editor_formula_rewrite.cpp
)

target_link_libraries(fastxlsx_workbook_editor_formula_rewrite_tests PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_workbook_editor_formula_rewrite_tests PRIVATE cxx_std_20)
target_include_directories(fastxlsx_workbook_editor_formula_rewrite_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(FASTXLSX_ENABLE_MINIZIP_NG)
    target_link_libraries(fastxlsx_workbook_editor_formula_rewrite_tests PRIVATE MINIZIP::minizip-ng)
    target_compile_definitions(fastxlsx_workbook_editor_formula_rewrite_tests PRIVATE FASTXLSX_TEST_HAS_MINIZIP_NG=1)
endif()

add_test(
    NAME fastxlsx.workbook_editor_formula_rewrite
    COMMAND fastxlsx_workbook_editor_formula_rewrite_tests
)
set_tests_properties(fastxlsx.workbook_editor_formula_rewrite PROPERTIES
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
