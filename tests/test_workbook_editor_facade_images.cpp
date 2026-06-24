#include "test_workbook_editor_facade_common.hpp"

void test_replace_image_updates_target_media_bytes_and_preserves_other_parts()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_two_images("fastxlsx-workbook-editor-image-replace-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-image-replace-output.xlsx");

    const std::filesystem::path replacement_png_path =
        repository_asset("docs/assets/donation/weixin.png");
    const std::filesystem::path replacement_jpeg_path =
        repository_asset("docs/assets/donation/zhifubao.jpg");
    const std::string replacement_png_bytes = fastxlsx::test::read_file(replacement_png_path);
    const std::string replacement_jpeg_bytes = fastxlsx::test::read_file(replacement_jpeg_path);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string data_sheet_before = source_entries.at("xl/worksheets/sheet1.xml");
    const std::string picture_sheet_before = source_entries.at("xl/worksheets/sheet2.xml");
    const std::string picture_sheet_rels_before =
        source_entries.at("xl/worksheets/_rels/sheet2.xml.rels");
    const std::string drawing_before = source_entries.at("xl/drawings/drawing1.xml");
    const std::string drawing_rels_before =
        source_entries.at("xl/drawings/_rels/drawing1.xml.rels");
    const std::string content_types_before = source_entries.at("[Content_Types].xml");
    const std::string package_rels_before = source_entries.at("_rels/.rels");
    const std::string workbook_rels_before = source_entries.at("xl/_rels/workbook.xml.rels");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(threw_fastxlsx_error([&] {
              editor.replace_image("xl/worksheets/sheet1.xml", replacement_png_path);
          }),
        "replacing a non-media target should throw");
    check(editor.pending_change_count() == 0,
        "failed image replacement should not increment pending change count");
    check(!editor.has_pending_changes(),
        "failed image replacement should not queue pending changes");

    check(threw_fastxlsx_error([&] {
              editor.replace_image("xl/media/image1.png", replacement_jpeg_path);
          }),
        "replacing a PNG target with JPEG bytes should throw");
    check(editor.pending_change_count() == 0,
        "failed format-mismatch image replacement should not increment pending change count");
    check(!editor.has_pending_changes(),
        "failed format-mismatch image replacement should not queue pending changes");
    check(editor.last_edit_error().has_value(),
        "failed image replacement should record a public edit diagnostic");

    editor.replace_image("xl/media/image1.png", replacement_png_path);
    editor.replace_image("xl/media/image2.jpg", as_bytes(replacement_jpeg_bytes));
    check(!editor.last_edit_error().has_value(),
        "successful image replacement should clear the public edit diagnostic");
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/media/image1.png") == replacement_png_bytes,
        "PNG media bytes should be replaced from file");
    check(output_entries.at("xl/media/image2.jpg") == replacement_jpeg_bytes,
        "JPEG media bytes should be replaced from memory");
    check(output_entries.at("xl/worksheets/sheet1.xml") == data_sheet_before,
        "data worksheet bytes should be preserved");
    check(output_entries.at("xl/worksheets/sheet2.xml") == picture_sheet_before,
        "picture worksheet bytes should be preserved");
    check(output_entries.at("xl/worksheets/_rels/sheet2.xml.rels") == picture_sheet_rels_before,
        "picture worksheet relationships should be preserved");
    check(output_entries.at("xl/drawings/drawing1.xml") == drawing_before,
        "drawing XML should be preserved");
    check(output_entries.at("xl/drawings/_rels/drawing1.xml.rels") == drawing_rels_before,
        "drawing relationships should be preserved");
    check(output_entries.at("[Content_Types].xml") == content_types_before,
        "content types should be preserved");
    check(output_entries.at("_rels/.rels") == package_rels_before,
        "package relationships should be preserved");
    check(output_entries.at("xl/_rels/workbook.xml.rels") == workbook_rels_before,
        "workbook relationships should be preserved");
}

void test_replace_image_rejects_missing_or_mismatched_targets()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_two_images("fastxlsx-workbook-editor-image-reject-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-image-reject-output.xlsx");

    const std::filesystem::path replacement_png_path =
        repository_asset("docs/assets/donation/weixin.png");
    const std::filesystem::path replacement_jpeg_path =
        repository_asset("docs/assets/donation/zhifubao.jpg");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] {
              editor.replace_image("xl/media/missing.png", replacement_png_path);
          }),
        "replacing a missing media target should throw");
    check(editor.pending_change_count() == 0,
        "missing-target image replacement should not increment pending change count");
    check(!editor.has_pending_changes(),
        "missing-target image replacement should not queue pending changes");

    check(threw_fastxlsx_error([&] {
              editor.replace_image("xl/media/image1.png", replacement_jpeg_path);
          }),
        "replacing a PNG target with JPEG bytes should throw");
    check(editor.pending_change_count() == 0,
        "format-mismatch image replacement should not increment pending change count");
    check(!editor.has_pending_changes(),
        "format-mismatch image replacement should not queue pending changes");

    editor.replace_image("xl/media/image1.png", replacement_png_path);
    editor.replace_image("xl/media/image2.jpg", replacement_jpeg_path);
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/media/image1.png") == fastxlsx::test::read_file(replacement_png_path),
        "PNG replacement should remain usable after rejected attempts");
    check(output_entries.at("xl/media/image2.jpg") == fastxlsx::test::read_file(replacement_jpeg_path),
        "JPEG replacement should remain usable after rejected attempts");
}

void test_replace_image_failure_diagnostics_include_context()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_two_images(
            "fastxlsx-workbook-editor-image-diagnostics-source.xlsx");

    const std::filesystem::path replacement_png_path =
        repository_asset("docs/assets/donation/weixin.png");
    const std::filesystem::path replacement_jpeg_path =
        repository_asset("docs/assets/donation/zhifubao.jpg");
    const std::string replacement_jpeg_bytes = fastxlsx::test::read_file(replacement_jpeg_path);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    try {
        editor.replace_image("xl/media/missing.png", replacement_png_path);
        check(false, "missing-target image replacement should throw");
    } catch (const fastxlsx::FastXlsxError& error) {
        const std::string message = error.what();
        check_contains(message, "WorkbookEditor::replace_image() failed",
            "missing-target diagnostic should name the public API");
        check_contains(message, "xl/media/missing.png",
            "missing-target diagnostic should include the requested media part");
        check_contains(message, replacement_png_path.generic_string(),
            "missing-target diagnostic should include the replacement file path");
        check_contains(message, "image target is not present in current package",
            "missing-target diagnostic should preserve the root cause");
        check(editor.last_edit_error().has_value() && *editor.last_edit_error() == message,
            "missing-target last_edit_error should match the thrown diagnostic");
    }
    check(editor.pending_change_count() == 0,
        "missing-target diagnostic failure should not increment pending changes");
    check(!editor.has_pending_changes(),
        "missing-target diagnostic failure should not queue pending changes");

    try {
        editor.replace_image("xl/media/image1.png", as_bytes(replacement_jpeg_bytes));
        check(false, "memory format-mismatch image replacement should throw");
    } catch (const fastxlsx::FastXlsxError& error) {
        const std::string message = error.what();
        check_contains(message, "WorkbookEditor::replace_image() failed",
            "memory diagnostic should name the public API");
        check_contains(message, "xl/media/image1.png",
            "memory diagnostic should include the requested media part");
        check_contains(message,
            std::string("from memory bytes (") + std::to_string(replacement_jpeg_bytes.size())
                + " bytes)",
            "memory diagnostic should include the staged byte count");
        check_contains(message, "image replacement format does not match target media part",
            "memory diagnostic should preserve the root cause");
        check(editor.last_edit_error().has_value() && *editor.last_edit_error() == message,
            "memory last_edit_error should match the thrown diagnostic");
    }
    check(editor.pending_change_count() == 0,
        "memory diagnostic failure should not increment pending changes");
    check(!editor.has_pending_changes(),
        "memory diagnostic failure should not queue pending changes");

    editor.replace_image("xl/media/image1.png", replacement_png_path);
    check(!editor.last_edit_error().has_value(),
        "successful image replacement should clear prior failure diagnostics");
}

void test_replace_image_file_save_failure_preserves_pending_state()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_two_images(
            "fastxlsx-workbook-editor-image-file-save-failure-source.xlsx");
    const std::filesystem::path staged_png_path =
        artifact("fastxlsx-workbook-editor-image-file-save-failure-staged.png");
    const std::filesystem::path failed_output =
        artifact("fastxlsx-workbook-editor-image-file-save-failure-output.xlsx");
    const std::filesystem::path recovered_output =
        artifact("fastxlsx-workbook-editor-image-file-save-recovered-output.xlsx");

    const std::filesystem::path replacement_png_path =
        repository_asset("docs/assets/donation/weixin.png");
    const std::string replacement_png_bytes = fastxlsx::test::read_file(replacement_png_path);
    write_binary_file(staged_png_path, replacement_png_bytes);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_image("xl/media/image1.png", staged_png_path);
    check(editor.has_pending_changes(),
        "file-backed image replacement should queue pending work before save_as");
    check(editor.pending_change_count() == 1,
        "file-backed image replacement should increment public pending change count");
    check(!editor.last_edit_error().has_value(),
        "successful file-backed image replacement should leave no diagnostic");

    std::filesystem::remove(staged_png_path);
    check(threw_fastxlsx_error([&] { editor.save_as(failed_output); }),
        "save_as should fail if the staged replacement image file disappears");
    check(editor.has_pending_changes(),
        "failed file-backed image save_as should preserve pending work");
    check(editor.pending_change_count() == 1,
        "failed file-backed image save_as should preserve pending change count");
    check(!editor.last_edit_error().has_value(),
        "failed save_as should not create last_edit_error for missing staged image file");

    write_binary_file(staged_png_path, replacement_png_bytes);
    editor.save_as(recovered_output);
    check(editor.has_pending_changes(),
        "successful file-backed image save_as should preserve pending work for another save_as");
    check(editor.pending_change_count() == 1,
        "successful file-backed image save_as should preserve pending change count");
    check(!editor.last_edit_error().has_value(),
        "successful file-backed image save_as should not create last_edit_error");

    const auto output_entries = fastxlsx::test::read_zip_entries(recovered_output);
    check(output_entries.at("xl/media/image1.png") == replacement_png_bytes,
        "restored staged image file should let save_as write the queued replacement");

    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-image-file-save-second-output.xlsx");
    editor.save_as(second_output);

    const auto second_output_entries = fastxlsx::test::read_zip_entries(second_output);
    check(second_output_entries.at("xl/media/image1.png") == replacement_png_bytes,
        "file-backed image replacement should remain reusable for a second save_as");
}

void test_replace_image_file_crc_failure_preserves_pending_state()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_two_images(
            "fastxlsx-workbook-editor-image-file-crc-failure-source.xlsx");
    const std::filesystem::path staged_png_path =
        artifact("fastxlsx-workbook-editor-image-file-crc-failure-staged.png");
    const std::filesystem::path failed_output =
        artifact("fastxlsx-workbook-editor-image-file-crc-failure-output.xlsx");
    const std::filesystem::path recovered_output =
        artifact("fastxlsx-workbook-editor-image-file-crc-recovered-output.xlsx");

    const std::filesystem::path replacement_png_path =
        repository_asset("docs/assets/donation/weixin.png");
    const std::string replacement_png_bytes = fastxlsx::test::read_file(replacement_png_path);
    std::string corrupted_png_bytes = replacement_png_bytes;
    check(!corrupted_png_bytes.empty(),
        "replacement fixture should contain bytes for CRC mutation");
    const std::size_t mutation_index = corrupted_png_bytes.size() / 2U;
    corrupted_png_bytes[mutation_index] = static_cast<char>(
        static_cast<unsigned char>(corrupted_png_bytes[mutation_index]) ^ 0x01U);
    write_binary_file(staged_png_path, replacement_png_bytes);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_image("xl/media/image1.png", staged_png_path);
    check(editor.has_pending_changes(),
        "file-backed image replacement should queue pending work before CRC failure");
    check(editor.pending_change_count() == 1,
        "file-backed image replacement should increment pending count before CRC failure");
    check(!editor.last_edit_error().has_value(),
        "successful file-backed image replacement should leave no diagnostic before CRC failure");

    write_binary_file(staged_png_path, corrupted_png_bytes);
    try {
        editor.save_as(failed_output);
        check(false, "save_as should fail if the staged replacement image file changes");
    } catch (const fastxlsx::FastXlsxError& error) {
        const std::string message = error.what();
        check_contains(message, "CRC32 changed after staging",
            "changed staged image failure should report the CRC contract");
    }
    check(editor.has_pending_changes(),
        "CRC-failed file-backed image save_as should preserve pending work");
    check(editor.pending_change_count() == 1,
        "CRC-failed file-backed image save_as should preserve pending change count");
    check(!editor.last_edit_error().has_value(),
        "CRC-failed save_as should not create last_edit_error");

    write_binary_file(staged_png_path, replacement_png_bytes);
    editor.save_as(recovered_output);
    check(editor.has_pending_changes(),
        "successful save_as after CRC recovery should preserve pending work");
    check(editor.pending_change_count() == 1,
        "successful save_as after CRC recovery should preserve pending change count");
    check(!editor.last_edit_error().has_value(),
        "successful save_as after CRC recovery should not create last_edit_error");

    const auto output_entries = fastxlsx::test::read_zip_entries(recovered_output);
    check(output_entries.at("xl/media/image1.png") == replacement_png_bytes,
        "restored staged image file should write the original queued replacement");
}

void test_replace_image_same_part_later_replacement_wins()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_two_images(
            "fastxlsx-workbook-editor-image-replace-latest-source.xlsx");
    const std::filesystem::path staged_png_path =
        artifact("fastxlsx-workbook-editor-image-replace-latest-staged.png");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-image-replace-latest-output.xlsx");

    const std::filesystem::path first_png_path =
        repository_asset("docs/assets/donation/weixin.png");
    const std::string first_png_bytes = fastxlsx::test::read_file(first_png_path);
    const std::span<const std::byte> second_png_span = fastxlsx::test::tiny_png_bytes();
    std::string second_png_bytes;
    second_png_bytes.assign(reinterpret_cast<const char*>(second_png_span.data()),
        reinterpret_cast<const char*>(second_png_span.data()) + second_png_span.size());
    check(first_png_bytes != second_png_bytes,
        "same-part image replacement fixtures should have distinct bytes");

    write_binary_file(staged_png_path, first_png_bytes);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_image("xl/media/image1.png", staged_png_path);
    editor.replace_image("xl/media/image1.png", second_png_span);
    check(editor.has_pending_changes(),
        "same-part image replacement override should leave pending work");
    check(editor.pending_change_count() == 2,
        "same-part image replacement override should still count both public edit calls");
    check(!editor.last_edit_error().has_value(),
        "successful same-part image replacement override should leave no diagnostic");

    std::filesystem::remove(staged_png_path);
    editor.save_as(output);
    check(!editor.last_edit_error().has_value(),
        "successful save_as after same-part override should not create last_edit_error");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/media/image1.png") == second_png_bytes,
        "later memory-backed image replacement should override earlier file-backed replacement");
    check(output_entries.at("xl/media/image1.png") != first_png_bytes,
        "superseded file-backed image replacement should not leak into output");
}

void test_replace_image_memory_source_copies_bytes_before_save_as()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_two_images(
            "fastxlsx-workbook-editor-image-memory-lifetime-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-image-memory-lifetime-output.xlsx");

    const std::filesystem::path replacement_png_path =
        repository_asset("docs/assets/donation/weixin.png");
    const std::string replacement_png_bytes = fastxlsx::test::read_file(replacement_png_path);
    std::string caller_buffer = replacement_png_bytes;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_image("xl/media/image1.png", as_bytes(caller_buffer));
    check(editor.has_pending_changes(),
        "memory-backed image replacement should queue pending work before save_as");
    check(editor.pending_change_count() == 1,
        "memory-backed image replacement should increment public pending change count");
    check(!editor.last_edit_error().has_value(),
        "successful memory-backed image replacement should leave no diagnostic");

    caller_buffer.assign(caller_buffer.size(), '\0');
    editor.save_as(output);

    check(editor.has_pending_changes(),
        "successful save_as should preserve memory-backed pending public edit state");
    check(editor.pending_change_count() == 1,
        "successful save_as should preserve memory-backed pending change count");
    check(!editor.last_edit_error().has_value(),
        "successful memory-backed save_as should not create last_edit_error");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/media/image1.png") == replacement_png_bytes,
        "memory-backed image replacement should copy caller bytes before save_as");
    check(output_entries.at("xl/media/image1.png") != caller_buffer,
        "memory-backed image replacement should not observe later caller buffer mutation");

    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-image-memory-lifetime-second-output.xlsx");
    editor.save_as(second_output);

    const auto second_output_entries = fastxlsx::test::read_zip_entries(second_output);
    check(second_output_entries.at("xl/media/image1.png") == replacement_png_bytes,
        "memory-backed image replacement should remain reusable for a second save_as");
}

} // namespace

int main()
{
    try {
        test_replace_image_updates_target_media_bytes_and_preserves_other_parts();
        test_replace_image_rejects_missing_or_mismatched_targets();
        test_replace_image_failure_diagnostics_include_context();
        test_replace_image_file_save_failure_preserves_pending_state();
        test_replace_image_file_crc_failure_preserves_pending_state();
        test_replace_image_same_part_later_replacement_wins();
        test_replace_image_memory_source_copies_bytes_before_save_as();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor facade images check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor facade images tests passed\n");
    return 0;
}
