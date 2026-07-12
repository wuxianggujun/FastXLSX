#include "../src/package_editor.hpp"
#include "test_workbook_editor_facade_common.hpp"

class ScopedWorkbookEditorDocumentPropertiesStagedHook {
public:
    explicit ScopedWorkbookEditorDocumentPropertiesStagedHook(
        fastxlsx::detail::PackageEditorDocumentPropertiesStagedHook hook)
    {
        fastxlsx::detail::testing_set_package_editor_document_properties_staged_hook(hook);
    }

    ~ScopedWorkbookEditorDocumentPropertiesStagedHook()
    {
        fastxlsx::detail::testing_set_package_editor_document_properties_staged_hook(nullptr);
    }

    ScopedWorkbookEditorDocumentPropertiesStagedHook(
        const ScopedWorkbookEditorDocumentPropertiesStagedHook&) = delete;
    ScopedWorkbookEditorDocumentPropertiesStagedHook& operator=(
        const ScopedWorkbookEditorDocumentPropertiesStagedHook&) = delete;
};

void fail_workbook_editor_document_properties_after_staging()
{
    throw fastxlsx::FastXlsxError("injected document properties commit failure");
}

void insert_before_closing_tag(
    std::string& xml, std::string_view closing_tag, std::string_view element)
{
    const std::size_t closing = xml.rfind(closing_tag);
    if (closing == std::string::npos) {
        throw std::runtime_error("fixture XML closing tag is missing");
    }
    xml.insert(closing, element);
}

void erase_self_closing_element_with_marker(
    std::string& xml, std::string_view element_name, std::string_view marker)
{
    const std::size_t marker_position = xml.find(marker);
    if (marker_position == std::string::npos) {
        throw std::runtime_error("fixture XML marker is missing");
    }

    const std::string opening = "<" + std::string(element_name);
    const std::size_t element_begin = xml.rfind(opening, marker_position);
    const std::size_t element_end = xml.find("/>", marker_position);
    if (element_begin == std::string::npos || element_end == std::string::npos) {
        throw std::runtime_error("fixture XML self-closing element is malformed");
    }
    xml.erase(element_begin, element_end + 2 - element_begin);
}

std::size_t occurrence_count(std::string_view text, std::string_view needle)
{
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = text.find(needle, position)) != std::string_view::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

std::filesystem::path write_document_properties_source_with_custom_and_unknown(
    std::string_view name)
{
    const std::filesystem::path path =
        write_two_sheet_source_with_document_properties(name);
    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(path);

    insert_before_closing_tag(entries.at("[Content_Types].xml"), "</Types>",
        R"(<Override PartName="/docProps/custom.xml" ContentType="application/vnd.openxmlformats-officedocument.custom-properties+xml"/>)");
    insert_before_closing_tag(entries.at("_rels/.rels"), "</Relationships>",
        R"(<Relationship Id="rIdCustomProperties" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/custom-properties" Target="docProps/custom.xml"/>)");
    entries.emplace("docProps/custom.xml",
        R"(<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/custom-properties" xmlns:vt="http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes"><property fmtid="{D5CDD505-2E9C-101B-9397-08002B2CF9AE}" pid="2" name="Marker"><vt:lpwstr>preserve custom property</vt:lpwstr></property></Properties>)");
    entries.emplace("custom/opaque.bin", "public document-properties opaque bytes");
    fastxlsx::test::write_stored_zip_entries(path, entries);
    return path;
}

std::filesystem::path write_source_without_document_properties(std::string_view name)
{
    const std::filesystem::path path =
        write_two_sheet_source_with_document_properties(name);
    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(path);

    entries.erase("docProps/core.xml");
    entries.erase("docProps/app.xml");
    erase_self_closing_element_with_marker(
        entries.at("[Content_Types].xml"), "Override", "/docProps/core.xml");
    erase_self_closing_element_with_marker(
        entries.at("[Content_Types].xml"), "Override", "/docProps/app.xml");
    erase_self_closing_element_with_marker(entries.at("_rels/.rels"),
        "Relationship", "relationships/metadata/core-properties");
    erase_self_closing_element_with_marker(entries.at("_rels/.rels"),
        "Relationship", "relationships/extended-properties");
    entries.emplace("custom/opaque.bin", "missing docProps opaque bytes");
    fastxlsx::test::write_stored_zip_entries(path, entries);
    return path;
}

void test_document_properties_last_write_wins_and_save_watermark()
{
    const std::filesystem::path source =
        write_document_properties_source_with_custom_and_unknown(
            "fastxlsx-workbook-editor-public-docprops-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-docprops-output.xlsx");
    const std::filesystem::path retry_output = artifact(
        "fastxlsx-workbook-editor-public-docprops-retry-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    fastxlsx::DocumentProperties first;
    first.creator = "First Author";
    first.title = "First Title";
    first.application = "First Application";
    editor.set_document_properties(first);

    fastxlsx::DocumentProperties final;
    final.creator = "Final & Author";
    final.last_modified_by = "Final Reviewer";
    final.title = "Final <Title>";
    final.subject = "Patch metadata";
    final.description = "Public facade document properties";
    final.keywords = "public,patch";
    final.category = "tests";
    final.application = "Final Application";
    final.app_version = "2.5";
    editor.set_document_properties(final);

    check(editor.has_pending_changes() && editor.pending_change_count() == 2,
        "repeated document properties calls should count both successful edits");
    check(editor.has_unsaved_changes() && editor.unsaved_change_count() == 2,
        "repeated document properties calls should advance the unsaved watermark twice");
    check(!editor.last_edit_error().has_value(),
        "successful document properties calls should keep last_edit_error clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "document properties staging");

    editor.save_as(output);

    check(editor.has_pending_changes() && editor.pending_change_count() == 2,
        "successful document properties save should retain staged diagnostics");
    check(!editor.has_unsaved_changes() && editor.unsaved_change_count() == 0,
        "successful document properties save should advance the saved watermark");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& core_xml = output_entries.at("docProps/core.xml");
    const std::string& app_xml = output_entries.at("docProps/app.xml");
    check_contains(core_xml, "<dc:creator>Final &amp; Author</dc:creator>",
        "document properties output should XML-escape the final creator");
    check_contains(core_xml, "<dc:title>Final &lt;Title&gt;</dc:title>",
        "document properties output should XML-escape the final title");
    check_not_contains(core_xml, "First Author",
        "document properties output should discard the earlier creator");
    check_not_contains(core_xml, "First Title",
        "document properties output should discard the earlier title");
    check_contains(app_xml, "<Application>Final Application</Application>",
        "document properties output should write the final application");
    check_contains(app_xml, "<AppVersion>2.5</AppVersion>",
        "document properties output should write the final application version");
    check(output_entries.at("docProps/custom.xml")
            == source_entries.at("docProps/custom.xml"),
        "document properties output should preserve custom properties bytes");
    check(output_entries.at("custom/opaque.bin")
            == source_entries.at("custom/opaque.bin"),
        "document properties output should preserve unknown bytes");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "document properties output should preserve unrelated worksheet bytes");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "document properties save should leave the source package unchanged");

    fastxlsx::DocumentProperties retry;
    retry.creator = "Retry Author";
    retry.title = "Retry Title";
    retry.application = "Retry Application";
    retry.app_version = "3.0";
    editor.set_document_properties(retry);

    check(editor.pending_change_count() == 3,
        "follow-up document properties edit should increment pending count");
    check(editor.has_unsaved_changes() && editor.unsaved_change_count() == 1,
        "follow-up document properties edit should create one unsaved delta");
    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "saving document properties over the source path should fail");
    check(editor.pending_change_count() == 3,
        "failed document properties save should preserve pending count");
    check(editor.has_unsaved_changes() && editor.unsaved_change_count() == 1,
        "failed document properties save should preserve the unsaved watermark");
    check(!editor.last_edit_error().has_value(),
        "save failure should not create a public edit error");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "failed follow-up save should leave the prior output unchanged");

    editor.save_as(retry_output);
    check(editor.pending_change_count() == 3,
        "successful document properties retry should retain pending count");
    check(!editor.has_unsaved_changes() && editor.unsaved_change_count() == 0,
        "successful document properties retry should advance the saved watermark");
    const auto retry_entries = fastxlsx::test::read_zip_entries(retry_output);
    check_contains(retry_entries.at("docProps/core.xml"),
        "<dc:creator>Retry Author</dc:creator>",
        "document properties retry should write the latest creator");
    check_contains(retry_entries.at("docProps/app.xml"),
        "<Application>Retry Application</Application>",
        "document properties retry should write the latest application");
    check(retry_entries.at("docProps/custom.xml")
            == source_entries.at("docProps/custom.xml"),
        "document properties retry should preserve custom properties bytes");
}

void test_document_properties_adds_missing_parts_and_package_metadata()
{
    const std::filesystem::path source = write_source_without_document_properties(
        "fastxlsx-workbook-editor-public-docprops-missing-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-docprops-missing-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::DocumentProperties properties;
    properties.creator = "Generated Core";
    properties.last_modified_by = "Generated Reviewer";
    properties.application = "Generated App";
    properties.app_version = "4.0";
    editor.set_document_properties(properties);

    check(editor.pending_change_count() == 1,
        "missing document properties staging should count one public edit");
    check(editor.has_unsaved_changes() && editor.unsaved_change_count() == 1,
        "missing document properties staging should create one unsaved delta");

    editor.save_as(output);
    check(!editor.has_unsaved_changes() && editor.unsaved_change_count() == 0,
        "missing document properties save should advance the saved watermark");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("docProps/core.xml"),
        "<dc:creator>Generated Core</dc:creator>",
        "missing document properties output should generate core.xml");
    check_contains(output_entries.at("docProps/app.xml"),
        "<Application>Generated App</Application>",
        "missing document properties output should generate app.xml");

    const std::string& content_types = output_entries.at("[Content_Types].xml");
    check(occurrence_count(content_types, "/docProps/core.xml") == 1,
        "missing document properties output should add one core content type");
    check(occurrence_count(content_types, "/docProps/app.xml") == 1,
        "missing document properties output should add one app content type");

    const std::string& package_relationships = output_entries.at("_rels/.rels");
    check(occurrence_count(package_relationships,
              "relationships/metadata/core-properties") == 1,
        "missing document properties output should add one core relationship");
    check(occurrence_count(package_relationships,
              "relationships/extended-properties") == 1,
        "missing document properties output should add one app relationship");
    check_contains(package_relationships, R"(Target="docProps/core.xml")",
        "missing document properties output should target core.xml");
    check_contains(package_relationships, R"(Target="docProps/app.xml")",
        "missing document properties output should target app.xml");
    check(output_entries.at("custom/opaque.bin")
            == source_entries.at("custom/opaque.bin"),
        "missing document properties output should preserve unknown bytes");
    check(output_entries.at("xl/worksheets/sheet1.xml")
            == source_entries.at("xl/worksheets/sheet1.xml"),
        "missing document properties output should preserve worksheet bytes");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "missing document properties save should leave the source package unchanged");
}

void test_document_properties_staging_failure_preserves_prior_patch_and_retries()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_document_properties(
            "fastxlsx-workbook-editor-public-docprops-failure-source.xlsx");
    const std::filesystem::path failed_output = artifact(
        "fastxlsx-workbook-editor-public-docprops-failure-output.xlsx");
    const std::filesystem::path retry_output = artifact(
        "fastxlsx-workbook-editor-public-docprops-failure-retry-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.request_full_calculation();
    check(editor.pending_change_count() == 1,
        "document properties failure setup should retain the calc metadata edit");
    check(editor.has_unsaved_changes() && editor.unsaved_change_count() == 1,
        "document properties failure setup should expose one unsaved calc edit");

    fastxlsx::DocumentProperties properties;
    properties.creator = "Transactional Author";
    properties.title = "Transactional Title";
    properties.application = "Transactional Application";

    {
        ScopedWorkbookEditorDocumentPropertiesStagedHook hook(
            fail_workbook_editor_document_properties_after_staging);
        check(threw_fastxlsx_error(
                  [&] { editor.set_document_properties(properties); }),
            "injected document properties staging failure should throw FastXlsxError");
    }

    check(editor.pending_change_count() == 1,
        "document properties staging failure should preserve pending count");
    check(editor.has_unsaved_changes() && editor.unsaved_change_count() == 1,
        "document properties staging failure should preserve the unsaved watermark");
    check(editor.last_edit_error().has_value(),
        "document properties staging failure should record public error context");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(),
            "WorkbookEditor::set_document_properties() failed",
            "document properties staging failure should identify the public API");
        check_contains(*editor.last_edit_error(),
            "injected document properties commit failure",
            "document properties staging failure should retain injected context");
    }

    editor.save_as(failed_output);
    check(!editor.has_unsaved_changes() && editor.unsaved_change_count() == 0,
        "saving the prior patch should advance its saved watermark");
    check(editor.pending_change_count() == 1,
        "saving the prior patch should retain only its pending edit count");
    check(editor.last_edit_error().has_value(),
        "saving the prior patch should preserve the edit failure diagnostic");

    const auto failed_entries = fastxlsx::test::read_zip_entries(failed_output);
    check_contains(failed_entries.at("xl/workbook.xml"),
        R"(fullCalcOnLoad="1")",
        "document properties staging failure should preserve the prior calc patch");
    check(failed_entries.at("docProps/core.xml")
            == source_entries.at("docProps/core.xml"),
        "document properties staging failure should not leak core.xml replacement");
    check(failed_entries.at("docProps/app.xml")
            == source_entries.at("docProps/app.xml"),
        "document properties staging failure should not leak app.xml replacement");

    editor.set_document_properties(properties);
    check(editor.pending_change_count() == 2,
        "document properties retry should count the successful metadata edit");
    check(editor.has_unsaved_changes() && editor.unsaved_change_count() == 1,
        "document properties retry should create one new unsaved delta");
    check(!editor.last_edit_error().has_value(),
        "document properties retry should clear the prior edit error");

    editor.save_as(retry_output);
    check(!editor.has_unsaved_changes() && editor.unsaved_change_count() == 0,
        "document properties retry save should advance the saved watermark");
    const auto retry_entries = fastxlsx::test::read_zip_entries(retry_output);
    check_contains(retry_entries.at("xl/workbook.xml"),
        R"(fullCalcOnLoad="1")",
        "document properties retry should retain the prior calc patch");
    check_contains(retry_entries.at("docProps/core.xml"),
        "<dc:creator>Transactional Author</dc:creator>",
        "document properties retry should write core.xml");
    check_contains(retry_entries.at("docProps/app.xml"),
        "<Application>Transactional Application</Application>",
        "document properties retry should write app.xml");
}

} // namespace

int main()
{
    try {
        test_document_properties_last_write_wins_and_save_watermark();
        test_document_properties_adds_missing_parts_and_package_metadata();
        test_document_properties_staging_failure_preserves_prior_patch_and_retries();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr,
            "%d WorkbookEditor public document-properties check(s) failed\n",
            g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor public document-properties tests passed\n");
    return 0;
}
