file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}" FASTXLSX_QA_REPOSITORY_ROOT)

add_executable(fastxlsx_workbook_editor_qa_tool
    workbook_editor_qa_tool.cpp
)

target_link_libraries(fastxlsx_workbook_editor_qa_tool PRIVATE FastXLSX::fastxlsx)
target_compile_features(fastxlsx_workbook_editor_qa_tool PRIVATE cxx_std_20)
target_include_directories(fastxlsx_workbook_editor_qa_tool PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)
target_compile_definitions(fastxlsx_workbook_editor_qa_tool PRIVATE
    FASTXLSX_REPOSITORY_ROOT="${FASTXLSX_QA_REPOSITORY_ROOT}"
)

if(FASTXLSX_ENABLE_MINIZIP_NG)
    target_link_libraries(fastxlsx_workbook_editor_qa_tool PRIVATE MINIZIP::minizip-ng)
    target_compile_definitions(fastxlsx_workbook_editor_qa_tool PRIVATE FASTXLSX_TEST_HAS_MINIZIP_NG=1)
endif()

