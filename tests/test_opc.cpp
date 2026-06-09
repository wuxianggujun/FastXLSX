#include <fastxlsx/detail/opc.hpp>

#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

class TestFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void check(bool condition, const char* message)
{
    if (!condition) {
        throw TestFailure(message);
    }
}

template <typename Notes>
bool has_note_containing(const Notes& notes, std::initializer_list<std::string_view> needles)
{
    for (const std::string& note : notes) {
        bool matched = true;
        for (std::string_view needle : needles) {
            if (note.find(needle) == std::string::npos) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return true;
        }
    }
    return false;
}

template <typename Audits>
bool has_workbook_payload_audit(const Audits& audits,
    const fastxlsx::detail::PartName& workbook_part,
    fastxlsx::detail::WorkbookPayloadDependencyAuditKind kind,
    fastxlsx::detail::WorkbookPayloadDependencyAuditScope scope,
    std::string_view element,
    std::initializer_list<std::string_view> note_needles = {})
{
    for (const fastxlsx::detail::WorkbookPayloadDependencyAudit& audit : audits) {
        if (audit.workbook_part != workbook_part || audit.kind != kind
            || audit.scope != scope || audit.element != element) {
            continue;
        }

        bool matched = true;
        for (std::string_view needle : note_needles) {
            if (audit.note.find(needle) == std::string::npos) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return true;
        }
    }
    return false;
}

void test_part_name_normalization()
{
    const fastxlsx::detail::PartName workbook("xl//worksheets/../workbook.xml");
    check(workbook.value() == "/xl/workbook.xml", "part name normalization failed");
    check(workbook.zip_path() == "xl/workbook.xml", "part name zip path failed");
    check(workbook.extension() == "xml", "part extension failed");

    const fastxlsx::detail::PartName worksheet("\\xl\\.\\worksheets\\sheet1.XML");
    check(worksheet.value() == "/xl/worksheets/sheet1.XML",
        "backslash part name normalization failed");
    check(worksheet.extension() == "xml", "uppercase part extension normalization failed");

    bool parent_escape_failed = false;
    try {
        fastxlsx::detail::PartName invalid("../xl/workbook.xml");
        (void)invalid;
    } catch (const std::exception&) {
        parent_escape_failed = true;
    }
    check(parent_escape_failed, "part names should not escape package root");

    bool root_failed = false;
    try {
        fastxlsx::detail::PartName invalid("/");
        (void)invalid;
    } catch (const std::exception&) {
        root_failed = true;
    }
    check(root_failed, "package root should not be accepted as a part name");
}

void test_content_types_manifest()
{
    fastxlsx::detail::ContentTypesManifest manifest;
    manifest.add_default(".XML", "application/xml");
    manifest.add_default("rels", "application/vnd.openxmlformats-package.relationships+xml");
    manifest.add_override(fastxlsx::detail::PartName("xl/workbook.xml"),
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml");

    const auto* workbook_type =
        manifest.content_type_for(fastxlsx::detail::PartName("/xl//workbook.xml"));
    check(workbook_type != nullptr, "workbook content type should be found");
    check(*workbook_type
            == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml",
        "override content type should win over default");

    const auto* sheet_type =
        manifest.content_type_for(fastxlsx::detail::PartName("xl/worksheets/sheet1.XML"));
    check(sheet_type != nullptr, "default content type should be found by extension");
    check(*sheet_type == "application/xml", "default content type lookup failed");

    const auto* missing_type =
        manifest.content_type_for(fastxlsx::detail::PartName("xl/media/image1.png"));
    check(missing_type == nullptr, "unknown extension should not return a content type");

    manifest.add_default("xml", "application/xml");
    manifest.add_override(fastxlsx::detail::PartName("/xl/./workbook.xml"),
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml");
    check(manifest.defaults().size() == 2, "duplicate default should be de-duplicated");
    check(manifest.overrides().size() == 1, "duplicate override should be de-duplicated");

    bool default_conflict_failed = false;
    try {
        manifest.add_default("xml", "text/xml");
    } catch (const std::exception&) {
        default_conflict_failed = true;
    }
    check(default_conflict_failed, "conflicting default content type should be rejected");

    bool override_conflict_failed = false;
    try {
        manifest.add_override(fastxlsx::detail::PartName("/xl/workbook.xml"), "application/xml");
    } catch (const std::exception&) {
        override_conflict_failed = true;
    }
    check(override_conflict_failed, "conflicting override content type should be rejected");

    const std::string xml = fastxlsx::detail::serialize_content_types(manifest);
    check(xml.find("<Default Extension=\"xml\" ContentType=\"application/xml\"/>")
            != std::string::npos,
        "content types serializer should include defaults");
    check(xml.find(
              "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>")
            != std::string::npos,
        "content types serializer should include overrides");
}

void test_content_type_registry_helper()
{
    fastxlsx::detail::ContentTypeRegistry registry;
    registry.add_default(".PNG", "image/png");
    registry.add_default("xml", "application/xml");
    registry.add_override(fastxlsx::detail::PartName("/xl/workbook.xml"),
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml");

    const auto* png_default = registry.default_for("png");
    check(png_default != nullptr, "content type registry default lookup failed");
    check(png_default->content_type == "image/png",
        "content type registry default value mismatch");

    const auto* workbook_override =
        registry.override_for(fastxlsx::detail::PartName("xl//workbook.xml"));
    check(workbook_override != nullptr, "content type registry override lookup failed");
    check(workbook_override->content_type
            == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml",
        "content type registry override value mismatch");

    const auto* workbook_type =
        registry.content_type_for(fastxlsx::detail::PartName("/xl/workbook.xml"));
    check(workbook_type != nullptr, "content type registry override resolution failed");
    check(*workbook_type
            == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml",
        "content type registry override should win");

    const auto* image_type =
        registry.content_type_for(fastxlsx::detail::PartName("/xl/media/image1.PNG"));
    check(image_type != nullptr, "content type registry default resolution failed");
    check(*image_type == "image/png", "content type registry default mismatch");

    check(registry.manifest().defaults().size() == 2,
        "content type registry should expose defaults");
    check(registry.manifest().overrides().size() == 1,
        "content type registry should expose overrides");

    bool default_conflict_failed = false;
    try {
        registry.add_default("png", "image/jpeg");
    } catch (const std::exception&) {
        default_conflict_failed = true;
    }
    check(default_conflict_failed,
        "content type registry should reject conflicting defaults");

    bool override_conflict_failed = false;
    try {
        registry.add_override(
            fastxlsx::detail::PartName("/xl/workbook.xml"), "application/xml");
    } catch (const std::exception&) {
        override_conflict_failed = true;
    }
    check(override_conflict_failed,
        "content type registry should reject conflicting overrides");
}

void test_relationship_set()
{
    fastxlsx::detail::RelationshipSet relationships;
    relationships.add("rId1",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet",
        "worksheets/sheet1.xml");
    relationships.add("rId2",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink",
        "https://example.test/",
        fastxlsx::detail::Relationship::TargetMode::External);

    check(relationships.size() == 2, "relationship count mismatch");

    const auto* worksheet = relationships.find_by_id("rId1");
    check(worksheet != nullptr, "relationship id lookup failed");
    check(worksheet->target == "worksheets/sheet1.xml", "relationship target not preserved");
    check(worksheet->type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet",
        "relationship type not preserved");
    check(worksheet->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "internal relationship target mode mismatch");

    const auto* hyperlink = relationships.find_by_id("rId2");
    check(hyperlink != nullptr, "external relationship lookup failed");
    check(hyperlink->target_mode == fastxlsx::detail::Relationship::TargetMode::External,
        "external relationship target mode mismatch");

    const std::string xml = fastxlsx::detail::serialize_relationships(relationships);
    check(xml.find("Id=\"rId1\"") != std::string::npos,
        "relationship serializer should include ids");
    check(xml.find("Target=\"worksheets/sheet1.xml\"") != std::string::npos,
        "relationship serializer should include internal target");
    check(xml.find("Target=\"https://example.test/\" TargetMode=\"External\"")
            != std::string::npos,
        "relationship serializer should include external target mode");

    bool duplicate_id_failed = false;
    try {
        relationships.add("rId1",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles",
            "styles.xml");
    } catch (const std::exception&) {
        duplicate_id_failed = true;
    }
    check(duplicate_id_failed, "duplicate relationship ids should be rejected");
}

void test_part_index()
{
    fastxlsx::detail::PartIndex index;
    check(index.empty(), "new part index should be empty");

    auto& workbook = index.add_part(fastxlsx::detail::PartName("xl//workbook.xml"),
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml");
    auto& duplicate = index.add_part(fastxlsx::detail::PartName("/xl/./workbook.xml"),
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml");

    check(&workbook == &duplicate, "part index duplicate should return existing part");
    check(index.size() == 1, "part index duplicate should not be inserted");

    const auto* lookup = index.find_part(fastxlsx::detail::PartName("/xl/workbook.xml"));
    check(lookup != nullptr, "part index lookup failed");
    check(lookup->content_type
            == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml",
        "part index content type mismatch");
    check(lookup->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "part index should keep default write mode for new parts");

    const auto* workbook_type =
        index.content_types().content_type_for(fastxlsx::detail::PartName("/xl/workbook.xml"));
    check(workbook_type != nullptr, "part index should register content type override");
    check(*workbook_type
            == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml",
        "part index registered content type mismatch");

    bool duplicate_conflict_failed = false;
    try {
        index.add_part(fastxlsx::detail::PartName("/xl/workbook.xml"), "application/xml");
    } catch (const std::exception&) {
        duplicate_conflict_failed = true;
    }
    check(duplicate_conflict_failed, "part index should reject duplicate content type conflict");

    index.add_part(fastxlsx::detail::PartName("/xl/worksheets/sheet1.xml"),
        "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml");
    check(index.size() == 2, "part index should insert distinct parts");

    index.content_types().add_override(
        fastxlsx::detail::PartName("/xl/styles.xml"), "application/xml");

    bool registry_conflict_failed = false;
    try {
        index.add_part(fastxlsx::detail::PartName("/xl/styles.xml"),
            "application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml");
    } catch (const std::exception&) {
        registry_conflict_failed = true;
    }
    check(registry_conflict_failed, "part index should surface content type registry conflict");
    check(index.find_part(fastxlsx::detail::PartName("/xl/styles.xml")) == nullptr,
        "failed part insert should not leave an indexed part");
}

void test_relationship_graph()
{
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    fastxlsx::detail::PartIndex index;
    index.add_part(workbook_part,
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml");
    index.add_part(worksheet_part,
        "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml");

    fastxlsx::detail::RelationshipGraph graph(index);

    auto& package_relationship = graph.add_package_relationship(
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument",
        "xl/workbook.xml");
    check(package_relationship.id == "rId1",
        "package owner should auto-allocate first relationship id");

    auto& workbook_relationship = graph.add_relationship(workbook_part,
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet",
        "worksheets/sheet1.xml");
    check(workbook_relationship.id == "rId1",
        "part owner should have an independent relationship id space");

    graph.add_package_relationship(fastxlsx::detail::Relationship {
        "rId2",
        "http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties",
        "docProps/core.xml",
    });
    graph.add_package_relationship(fastxlsx::detail::Relationship {
        "customId",
        "http://schemas.openxmlformats.org/package/2006/relationships/metadata/custom",
        "docProps/custom.xml",
    });
    auto& next_package_relationship = graph.add_package_relationship(
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties",
        "docProps/app.xml");
    check(next_package_relationship.id == "rId3",
        "package owner auto relationship id should skip existing ids");

    auto& external_relationship = graph.add_relationship(workbook_part,
        fastxlsx::detail::Relationship {
            "rId2",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink",
            "https://example.test/path?a=1&b=2",
            fastxlsx::detail::Relationship::TargetMode::External,
        });
    check(external_relationship.target == "https://example.test/path?a=1&b=2",
        "relationship graph should preserve external target text");
    check(external_relationship.target_mode
            == fastxlsx::detail::Relationship::TargetMode::External,
        "relationship graph should preserve external target mode");

    auto& next_workbook_relationship = graph.add_relationship(workbook_part,
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles",
        "styles.xml");
    check(next_workbook_relationship.id == "rId3",
        "part owner auto relationship id should skip existing ids");

    auto& worksheet_relationship = graph.add_relationship(worksheet_part,
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink",
        "https://worksheet.example/",
        fastxlsx::detail::Relationship::TargetMode::External);
    check(worksheet_relationship.id == "rId1",
        "worksheet owner should have an independent relationship id space");

    const auto* workbook_relationships =
        graph.relationships_for(fastxlsx::detail::PartName("xl//workbook.xml"));
    check(workbook_relationships != nullptr,
        "relationship graph should find relationships by normalized source part");
    check(workbook_relationships->size() == 3,
        "relationship graph part relationship count mismatch");
    check(workbook_relationships->find_by_id("rId2")->target_mode
            == fastxlsx::detail::Relationship::TargetMode::External,
        "relationship graph external lookup mismatch");

    const std::string workbook_rels_xml =
        fastxlsx::detail::serialize_relationships(*workbook_relationships);
    check(workbook_rels_xml.find("TargetMode=\"External\"") != std::string::npos,
        "relationship serializer should preserve external target mode from graph");

    bool same_owner_conflict_failed = false;
    try {
        graph.add_relationship(workbook_part,
            fastxlsx::detail::Relationship {
                "rId1",
                "http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme",
                "theme/theme1.xml",
            });
    } catch (const std::exception&) {
        same_owner_conflict_failed = true;
    }
    check(same_owner_conflict_failed,
        "relationship graph should reject duplicate ids within the same owner");

    bool missing_source_failed = false;
    try {
        graph.add_relationship(fastxlsx::detail::PartName("/xl/styles.xml"),
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme",
            "theme/theme1.xml");
    } catch (const std::exception&) {
        missing_source_failed = true;
    }
    check(missing_source_failed,
        "relationship graph should reject unregistered source parts");

    const auto* worksheet_relationships = graph.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "relationship graph should find worksheet owner relationships");
    check(worksheet_relationships->size() == 1,
        "relationship graph worksheet owner relationship count mismatch");
    check(worksheet_relationships->find_by_id("rId1")->target
            == "https://worksheet.example/",
        "relationship graph worksheet relationship target mismatch");
}

void test_package_manifest()
{
    fastxlsx::detail::PackageManifest manifest;
    auto& workbook = manifest.add_part(fastxlsx::detail::PartName("xl//workbook.xml"),
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml");
    auto& duplicate = manifest.add_part(fastxlsx::detail::PartName("/xl/./workbook.xml"),
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml");

    check(&workbook == &duplicate, "duplicate manifest part should return existing part");
    check(manifest.size() == 1, "duplicate manifest part should not be inserted");

    manifest.add_part(fastxlsx::detail::PartName("xl/worksheets/sheet1.xml"),
        "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml");
    check(manifest.size() == 2, "second manifest part should be inserted");

    const auto* workbook_type =
        manifest.content_types().content_type_for(fastxlsx::detail::PartName("xl/workbook.xml"));
    check(workbook_type != nullptr, "manifest should register content type override");
    check(*workbook_type
            == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml",
        "manifest content type override mismatch");

    manifest.add_relationship(fastxlsx::detail::PartName("xl/workbook.xml"),
        fastxlsx::detail::Relationship {
            "rId1",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet",
            "worksheets/sheet1.xml",
        });
    const auto* workbook_relationships =
        manifest.relationships_for(fastxlsx::detail::PartName("/xl/workbook.xml"));
    check(workbook_relationships != nullptr, "part relationship set should be found");
    check(workbook_relationships->size() == 1, "part relationship should be attached");
    check(workbook_relationships->find_by_id("rId1")->target == "worksheets/sheet1.xml",
        "attached part relationship target mismatch");

    manifest.add_package_relationship(fastxlsx::detail::Relationship {
        "rId1",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument",
        "xl/workbook.xml",
    });
    check(manifest.package_relationships().size() == 1, "package relationship should be attached");

    bool missing_source_failed = false;
    try {
        manifest.add_relationship(fastxlsx::detail::PartName("xl/styles.xml"),
            fastxlsx::detail::Relationship {
                "rId1",
                "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles",
                "styles.xml",
            });
    } catch (const std::exception&) {
        missing_source_failed = true;
    }
    check(missing_source_failed, "relationship source part should be registered first");
}

void test_package_part_edit_state()
{
    using fastxlsx::detail::PartWriteMode;

    fastxlsx::detail::PackageManifest manifest;
    auto& unknown = manifest.ensure_part(fastxlsx::detail::PartName("/custom/unknown.bin"));
    check(unknown.write_mode == PartWriteMode::CopyOriginal,
        "unknown parts should default to copy original mode");
    check(unknown.preserve_original, "copy original mode should preserve original part");
    check(!unknown.dirty, "copy original mode should not be dirty");
    check(!unknown.generated, "copy original mode should not be generated");

    manifest.add_part(fastxlsx::detail::PartName("/xl/workbook.xml"),
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml");
    auto& workbook =
        manifest.mark_part_generated(fastxlsx::detail::PartName("/xl/workbook.xml"));
    check(workbook.write_mode == PartWriteMode::GenerateSmallXml,
        "generated XML part should use generate mode");
    check(!workbook.preserve_original, "generated XML part should not preserve original bytes");
    check(workbook.dirty, "generated XML part should be dirty");
    check(workbook.generated, "generated XML part should be marked generated");

    manifest.add_part(fastxlsx::detail::PartName("/xl/worksheets/sheet1.xml"),
        "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml");
    auto& worksheet = manifest.set_part_write_mode(
        fastxlsx::detail::PartName("/xl/worksheets/sheet1.xml"), PartWriteMode::StreamRewrite);
    check(worksheet.write_mode == PartWriteMode::StreamRewrite,
        "worksheet rewrite should record stream rewrite mode");
    check(!worksheet.preserve_original, "stream rewrite should replace original bytes");
    check(worksheet.dirty, "stream rewrite should be dirty");
    check(!worksheet.generated, "stream rewrite should not be generated");

    manifest.add_part(fastxlsx::detail::PartName("/docProps/app.xml"), "application/xml");
    auto& app_props = manifest.set_part_write_mode(
        fastxlsx::detail::PartName("/docProps/app.xml"), PartWriteMode::LocalDomRewrite);
    check(app_props.write_mode == PartWriteMode::LocalDomRewrite,
        "small XML part should record local DOM rewrite mode");
    check(!app_props.preserve_original, "local DOM rewrite should replace original bytes");
    check(app_props.dirty, "local DOM rewrite should be dirty");
    check(!app_props.generated, "local DOM rewrite should not be generated");

    manifest.add_part(fastxlsx::detail::PartName("/docProps/core.xml"), "application/xml");
    auto& core_props =
        manifest.mark_part_dirty(fastxlsx::detail::PartName("/docProps/core.xml"));
    check(core_props.write_mode == PartWriteMode::LocalDomRewrite,
        "dirty small XML part should default to local DOM rewrite mode");
    check(!core_props.preserve_original, "dirty part should not preserve original bytes");
    check(core_props.dirty, "modified part should be dirty");
    check(!core_props.generated, "dirty existing part should not be generated");

    auto& copied_sheet = manifest.set_part_write_mode(
        fastxlsx::detail::PartName("/xl/worksheets/sheet1.xml"), PartWriteMode::CopyOriginal);
    check(copied_sheet.write_mode == PartWriteMode::CopyOriginal,
        "copy mode should be restorable");
    check(copied_sheet.preserve_original, "restored copy mode should preserve original part");
    check(!copied_sheet.dirty, "restored copy mode should clear dirty state");
    check(!copied_sheet.generated, "restored copy mode should clear generated state");
}

void test_edit_plan_and_rewrite_planner()
{
    using fastxlsx::detail::CalcChainAction;
    using fastxlsx::detail::PartWriteMode;
    using fastxlsx::detail::ReferencePolicyAction;

    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName vml_drawing_part("/xl/drawings/vmlDrawing1.vml");
    const fastxlsx::detail::PartName percent_encoded_drawing_part(
        "/xl/drawings/drawing space.xml");
    const fastxlsx::detail::PartName absolute_drawing_part(
        "/xl/drawings/absoluteDrawing.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/unknown.bin");

    fastxlsx::detail::PackageManifest manifest;
    manifest.ensure_part(unknown_part);
    manifest.add_part(workbook_part,
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml");
    manifest.add_part(worksheet_part,
        "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml");
    manifest.add_part(shared_strings_part,
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml");
    manifest.add_part(styles_part,
        "application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml");
    manifest.add_part(calc_chain_part,
        "application/vnd.openxmlformats-officedocument.spreadsheetml.calcChain+xml");
    manifest.add_part(drawing_part,
        "application/vnd.openxmlformats-officedocument.drawing+xml");
    manifest.add_part(chart_part,
        "application/vnd.openxmlformats-officedocument.drawingml.chart+xml");
    manifest.add_part(image_part, "image/png");
    manifest.add_part(table_part,
        "application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml");
    manifest.add_part(vml_drawing_part,
        "application/vnd.openxmlformats-officedocument.vmlDrawing");
    manifest.add_part(percent_encoded_drawing_part,
        "application/vnd.openxmlformats-officedocument.drawing+xml");
    manifest.add_part(absolute_drawing_part,
        "application/vnd.openxmlformats-officedocument.drawing+xml");
    manifest.add_relationship(worksheet_part,
        fastxlsx::detail::Relationship {
            "rId1",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
            "../drawings/drawing1.xml",
        });
    manifest.add_relationship(worksheet_part,
        fastxlsx::detail::Relationship {
            "rIdExternal",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink",
            "https://example.invalid/",
            fastxlsx::detail::Relationship::TargetMode::External,
        });
    manifest.add_relationship(worksheet_part,
        fastxlsx::detail::Relationship {
            "rId3",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/table",
            "../tables/table1.xml",
        });
    manifest.add_relationship(worksheet_part,
        fastxlsx::detail::Relationship {
            "rIdMissing",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments",
            "../comments/comment1.xml",
        });
    manifest.add_relationship(worksheet_part,
        fastxlsx::detail::Relationship {
            "rIdFragment",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
            "../drawings/drawing1.xml#shape1",
        });
    manifest.add_relationship(worksheet_part,
        fastxlsx::detail::Relationship {
            "rIdVmlFragment",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
            "../drawings/vmlDrawing1.vml#shape1",
        });
    manifest.add_relationship(worksheet_part,
        fastxlsx::detail::Relationship {
            "rIdPercentEncoded",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
            "../drawings/drawing%20space.xml",
        });
    manifest.add_relationship(worksheet_part,
        fastxlsx::detail::Relationship {
            "rIdAbsoluteTarget",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
            "/xl/drawings/./absoluteDrawing.xml",
        });
    manifest.add_relationship(worksheet_part,
        fastxlsx::detail::Relationship {
            "rIdOpaqueCustom",
            "https://fastxlsx.invalid/relationships/opaque-extension",
            "../../custom/unknown.bin",
        });
    manifest.add_relationship(worksheet_part,
        fastxlsx::detail::Relationship {
            "rIdEscapes",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments",
            "../../../outside.bin",
        });
    manifest.add_relationship(drawing_part,
        fastxlsx::detail::Relationship {
            "rId1",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
            "../media/image1.png",
        });
    manifest.add_relationship(drawing_part,
        fastxlsx::detail::Relationship {
            "rId2",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
            "../charts/chart1.xml",
        });
    manifest.add_relationship(drawing_part,
        fastxlsx::detail::Relationship {
            "rIdExternalDrawing",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink",
            "https://drawing.example.invalid/link",
            fastxlsx::detail::Relationship::TargetMode::External,
        });
    manifest.add_relationship(drawing_part,
        fastxlsx::detail::Relationship {
            "rIdFragmentDrawing",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
            "../charts/chart1.xml#plotArea",
        });
    manifest.add_relationship(drawing_part,
        fastxlsx::detail::Relationship {
            "rIdMissingDrawing",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject",
            "../embeddings/oleObject1.bin",
        });
    manifest.add_relationship(drawing_part,
        fastxlsx::detail::Relationship {
            "rIdEscapesDrawing",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject",
            "../../../outside-from-drawing.bin",
        });

    const fastxlsx::detail::DependencyAnalysis analysis =
        fastxlsx::detail::DependencyAnalyzer(manifest).analyze_worksheet_stream_rewrite(
            worksheet_part);
    check(analysis.parts.size() == 13,
        "worksheet dependency analysis should include target and known linked workbook parts");
    check(analysis.has_worksheet_relationships,
        "worksheet dependency analysis should notice worksheet relationships");
    check(analysis.has_external_relationship_targets,
        "worksheet dependency analysis should notice external relationship targets");
    check(analysis.has_uri_qualified_internal_relationship_targets,
        "worksheet dependency analysis should notice URI-qualified internal targets");
    check(analysis.has_invalid_internal_relationship_targets,
        "worksheet dependency analysis should notice invalid internal relationship targets");
    check(analysis.has_unresolved_internal_relationship_targets,
        "worksheet dependency analysis should notice unresolved internal relationship targets");
    check(analysis.has_calc_chain, "worksheet dependency analysis should notice calcChain");
    bool found_drawing_dependency = false;
    bool found_chart_dependency = false;
    bool found_image_dependency = false;
    bool found_table_dependency = false;
    bool found_vml_drawing_dependency = false;
    bool found_percent_encoded_dependency = false;
    bool found_absolute_dependency = false;
    bool found_opaque_custom_dependency = false;
    bool found_external_hyperlink_dependency = false;
    bool found_uri_qualified_dependency = false;
    bool found_invalid_internal_dependency = false;
    bool found_missing_internal_dependency = false;
    for (const fastxlsx::detail::PartDependency& dependency : analysis.parts) {
        if (dependency.part_name == drawing_part
            && dependency.reason.find("rId1") != std::string::npos
            && dependency.reason.find("relationships/drawing") != std::string::npos
            && dependency.reason.find("/xl/drawings/drawing1.xml") != std::string::npos
            && dependency.relationship_owner_part == worksheet_part.value()
            && dependency.relationship_id == "rId1"
            && dependency.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing"
            && dependency.relationship_target == "../drawings/drawing1.xml") {
            found_drawing_dependency = true;
        }
        if (dependency.part_name == chart_part
            && dependency.reason.find("/xl/drawings/drawing1.xml") != std::string::npos
            && dependency.reason.find("/xl/charts/chart1.xml") != std::string::npos
            && dependency.reason.find("rId2") != std::string::npos) {
            found_chart_dependency = true;
        }
        if (dependency.part_name == image_part
            && dependency.reason.find("/xl/drawings/drawing1.xml") != std::string::npos
            && dependency.reason.find("/xl/media/image1.png") != std::string::npos
            && dependency.reason.find("rId1") != std::string::npos) {
            found_image_dependency = true;
        }
        if (dependency.part_name == table_part
            && dependency.reason.find("rId3") != std::string::npos
            && dependency.reason.find("/xl/tables/table1.xml") != std::string::npos) {
            found_table_dependency = true;
        }
        if (dependency.part_name == vml_drawing_part
            && dependency.reason.find("rIdVmlFragment") != std::string::npos
            && dependency.reason.find("/xl/drawings/vmlDrawing1.vml") != std::string::npos) {
            found_vml_drawing_dependency = true;
        }
        if (dependency.part_name == percent_encoded_drawing_part
            && dependency.reason.find("rIdPercentEncoded") != std::string::npos
            && dependency.reason.find("/xl/drawings/drawing space.xml") != std::string::npos) {
            found_percent_encoded_dependency = true;
        }
        if (dependency.part_name == absolute_drawing_part
            && dependency.reason.find("rIdAbsoluteTarget") != std::string::npos
            && dependency.reason.find("/xl/drawings/absoluteDrawing.xml") != std::string::npos) {
            found_absolute_dependency = true;
        }
        if (dependency.part_name == unknown_part
            && dependency.reason.find("rIdOpaqueCustom") != std::string::npos
            && dependency.reason.find("https://fastxlsx.invalid/relationships/opaque-extension")
                != std::string::npos
            && dependency.reason.find("/custom/unknown.bin") != std::string::npos
            && dependency.relationship_owner_part == worksheet_part.value()
            && dependency.relationship_id == "rIdOpaqueCustom"
            && dependency.relationship_type
                == "https://fastxlsx.invalid/relationships/opaque-extension"
            && dependency.relationship_target == "../../custom/unknown.bin") {
            found_opaque_custom_dependency = true;
        }
        if (dependency.reason.find("rIdExternal") != std::string::npos) {
            found_external_hyperlink_dependency = true;
        }
        if (dependency.reason.find("rIdFragment") != std::string::npos) {
            found_uri_qualified_dependency = true;
        }
        if (dependency.reason.find("rIdEscapes") != std::string::npos) {
            found_invalid_internal_dependency = true;
        }
        if (dependency.reason.find("rIdMissing") != std::string::npos) {
            found_missing_internal_dependency = true;
        }
        if (dependency.reason.find("rIdExternalDrawing") != std::string::npos
            || dependency.reason.find("rIdMissingDrawing") != std::string::npos
            || dependency.reason.find("rIdEscapesDrawing") != std::string::npos) {
            throw TestFailure(
                "drawing-owned relationship audit targets should not become package dependencies");
        }
    }
    check(found_drawing_dependency,
        "worksheet dependency analysis should include known internal relationship targets");
    check(found_chart_dependency,
        "worksheet dependency analysis should include drawing-owned chart targets");
    check(found_image_dependency,
        "worksheet dependency analysis should include drawing-owned image targets");
    check(found_table_dependency,
        "worksheet dependency analysis should include worksheet-owned table targets");
    check(found_vml_drawing_dependency,
        "worksheet dependency analysis should include URI-qualified base targets");
    check(found_percent_encoded_dependency,
        "worksheet dependency analysis should include percent-decoded internal relationship targets");
    check(found_absolute_dependency,
        "worksheet dependency analysis should include normalized absolute internal relationship targets");
    check(found_opaque_custom_dependency,
        "worksheet dependency analysis should conservatively include unknown relationship types with registered internal targets");
    check(!found_external_hyperlink_dependency,
        "worksheet dependency analysis should not treat external hyperlinks as package parts");
    check(!found_uri_qualified_dependency,
        "worksheet dependency analysis should not add duplicate dependencies for already known URI-qualified base targets");
    check(!found_invalid_internal_dependency,
        "worksheet dependency analysis should not invent parts for invalid internal targets");
    check(!found_missing_internal_dependency,
        "worksheet dependency analysis should not invent parts for unresolved internal targets");
    check(analysis.relationship_target_audits.size()
            == analysis.relationship_target_notes.size(),
        "relationship target audits should mirror relationship target notes");
    bool found_structured_external_audit = false;
    bool found_structured_uri_audit = false;
    bool found_structured_unresolved_audit = false;
    bool found_structured_invalid_audit = false;
    for (const fastxlsx::detail::RelationshipTargetAudit& audit :
        analysis.relationship_target_audits) {
        if (audit.owner_part == worksheet_part && audit.relationship_id == "rIdExternal"
            && audit.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink"
            && audit.target == "https://example.invalid/" && audit.normalized_target.empty()
            && audit.note.find("external relationship targets") != std::string::npos) {
            found_structured_external_audit = true;
        }
        if (audit.owner_part == worksheet_part && audit.relationship_id == "rIdVmlFragment"
            && audit.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing"
            && audit.target == "../drawings/vmlDrawing1.vml#shape1"
            && audit.normalized_target == "/xl/drawings/vmlDrawing1.vml"
            && audit.note.find("has base part /xl/drawings/vmlDrawing1.vml")
                != std::string::npos) {
            found_structured_uri_audit = true;
        }
        if (audit.owner_part == worksheet_part && audit.relationship_id == "rIdMissing"
            && audit.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments"
            && audit.target == "../comments/comment1.xml"
            && audit.normalized_target == "/xl/comments/comment1.xml"
            && audit.note.find("resolves to unregistered part /xl/comments/comment1.xml")
                != std::string::npos) {
            found_structured_unresolved_audit = true;
        }
        if (audit.owner_part == worksheet_part && audit.relationship_id == "rIdEscapes"
            && audit.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments"
            && audit.target == "../../../outside.bin" && audit.normalized_target.empty()
            && audit.note.find("cannot be normalized as a package part")
                != std::string::npos) {
            found_structured_invalid_audit = true;
        }
    }
    check(found_structured_external_audit,
        "dependency analysis should keep structured external relationship audit fields");
    check(found_structured_uri_audit,
        "dependency analysis should keep structured URI-qualified relationship audit fields");
    check(found_structured_unresolved_audit,
        "dependency analysis should keep structured unresolved relationship audit fields");
    check(found_structured_invalid_audit,
        "dependency analysis should keep structured invalid relationship audit fields");

    fastxlsx::detail::PartRewritePlanner planner(manifest);
    const fastxlsx::detail::EditPlan copy_plan = planner.plan_copy_original();
    check(copy_plan.size() == manifest.size(), "copy plan should include all known parts");
    const auto* copied_unknown = copy_plan.find_part(unknown_part);
    check(copied_unknown != nullptr, "copy plan should include unknown parts");
    check(copied_unknown->write_mode == PartWriteMode::CopyOriginal,
        "unknown parts should stay copy-original in copy plan");

    fastxlsx::detail::EditPlan metadata_plan;
    using fastxlsx::detail::PackageEntryAuditKind;
    metadata_plan.set_package_entry("[Content_Types].xml", PartWriteMode::LocalDomRewrite,
        "content types rewrite", PackageEntryAuditKind::ContentTypes);
    const auto* content_types_entry =
        metadata_plan.find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "edit plan should record package metadata entry rewrites");
    check(content_types_entry->write_mode == PartWriteMode::LocalDomRewrite,
        "package metadata entry rewrite should keep write mode");
    check(content_types_entry->audit_kind == PackageEntryAuditKind::ContentTypes,
        "package metadata entry rewrite should keep structured content-types audit role");
    check(content_types_entry->owner_part.empty(),
        "content-types package-entry audit should not carry a source owner");
    metadata_plan.set_package_entry("[Content_Types].xml", PartWriteMode::GenerateSmallXml,
        "content types generated rewrite", PackageEntryAuditKind::ContentTypes);
    check(metadata_plan.package_entries().size() == 1,
        "package metadata entry rewrites should upsert by entry name");
    check(metadata_plan.find_package_entry("[Content_Types].xml")->write_mode
            == PartWriteMode::GenerateSmallXml,
        "package metadata entry rewrite upsert should replace write mode");
    metadata_plan.add_note("duplicate package audit note");
    metadata_plan.add_note("duplicate package audit note");
    check(metadata_plan.notes().size() == 1,
        "edit plan should ignore duplicate audit notes");
    metadata_plan.add_relationship_target_audit(
        fastxlsx::detail::RelationshipTargetAudit {
            worksheet_part,
            "rIdExternal",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink",
            "https://example.invalid/",
            "",
            "first external target audit",
        });
    metadata_plan.add_relationship_target_audit(
        fastxlsx::detail::RelationshipTargetAudit {
            worksheet_part,
            "rIdExternal",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink",
            "https://example.invalid/",
            "",
            "updated external target audit",
        });
    check(metadata_plan.relationship_target_audits().size() == 1,
        "edit plan should upsert duplicate relationship target audits");
    check(metadata_plan.relationship_target_audits().front().note
            == "updated external target audit",
        "edit plan relationship target audit upsert should refresh note text");
    metadata_plan.add_worksheet_relationship_reference_audit(
        fastxlsx::detail::WorksheetRelationshipReferenceAudit {
            worksheet_part,
            fastxlsx::detail::WorksheetRelationshipReferenceAuditKind::TypeMismatch,
            "drawing",
            "rId2",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink",
            "first worksheet relationship reference audit",
        });
    metadata_plan.add_worksheet_relationship_reference_audit(
        fastxlsx::detail::WorksheetRelationshipReferenceAudit {
            worksheet_part,
            fastxlsx::detail::WorksheetRelationshipReferenceAuditKind::TypeMismatch,
            "drawing",
            "rId2",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink",
            "updated worksheet relationship reference audit",
        });
    check(metadata_plan.worksheet_relationship_reference_audits().size() == 1,
        "edit plan should upsert duplicate worksheet relationship reference audits");
    check(metadata_plan.worksheet_relationship_reference_audits().front().note
            == "updated worksheet relationship reference audit",
        "edit plan worksheet relationship reference audit upsert should refresh note text");
    metadata_plan.add_worksheet_payload_dependency_audit(
        fastxlsx::detail::WorksheetPayloadDependencyAudit {
            worksheet_part,
            fastxlsx::detail::WorksheetPayloadDependencyAuditKind::RelationshipMetadata,
            fastxlsx::detail::WorksheetPayloadDependencyAuditScope::WorksheetReplacement,
            "drawing",
            "first worksheet payload dependency audit",
        });
    metadata_plan.add_worksheet_payload_dependency_audit(
        fastxlsx::detail::WorksheetPayloadDependencyAudit {
            worksheet_part,
            fastxlsx::detail::WorksheetPayloadDependencyAuditKind::RelationshipMetadata,
            fastxlsx::detail::WorksheetPayloadDependencyAuditScope::WorksheetReplacement,
            "drawing",
            "updated worksheet payload dependency audit",
        });
    check(metadata_plan.worksheet_payload_dependency_audits().size() == 1,
        "edit plan should upsert duplicate worksheet payload dependency audits");
    check(metadata_plan.worksheet_payload_dependency_audits().front().note
            == "updated worksheet payload dependency audit",
        "edit plan worksheet payload dependency audit upsert should refresh note text");
    metadata_plan.set_package_entry("xl/_rels/workbook.xml.rels", PartWriteMode::CopyOriginal,
        "workbook relationships preserved",
        PackageEntryAuditKind::SourceRelationships, workbook_part.value());
    const auto* workbook_relationship_entry =
        metadata_plan.find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationship_entry != nullptr,
        "source-owned relationships audit should be recorded as a package entry");
    check(workbook_relationship_entry->audit_kind
            == PackageEntryAuditKind::SourceRelationships,
        "source-owned relationships audit should keep structured role");
    check(workbook_relationship_entry->owner_part == workbook_part.value(),
        "source-owned relationships audit should keep owner part");
    metadata_plan.remove_package_entry("xl/_rels/calcChain.xml.rels",
        "removed calcChain relationships", PackageEntryAuditKind::SourceRelationships,
        calc_chain_part.value());
    const auto* removed_calc_chain_relationship_entry =
        metadata_plan.find_removed_package_entry("xl/_rels/calcChain.xml.rels");
    check(removed_calc_chain_relationship_entry != nullptr,
        "edit plan should record removed package metadata entries");
    check(removed_calc_chain_relationship_entry->audit_kind
            == PackageEntryAuditKind::SourceRelationships,
        "removed source-owned relationships audit should keep structured role");
    check(removed_calc_chain_relationship_entry->owner_part == calc_chain_part.value(),
        "removed source-owned relationships audit should keep owner part");
    metadata_plan.set_package_entry("xl/_rels/calcChain.xml.rels",
        PartWriteMode::CopyOriginal, "restored calcChain relationships",
        PackageEntryAuditKind::SourceRelationships, calc_chain_part.value());
    const auto* restored_calc_chain_relationship_entry =
        metadata_plan.find_package_entry("xl/_rels/calcChain.xml.rels");
    check(restored_calc_chain_relationship_entry != nullptr,
        "edit plan should restore a removed package metadata entry");
    check(restored_calc_chain_relationship_entry->write_mode == PartWriteMode::CopyOriginal,
        "restored package metadata entry should keep write mode");
    check(restored_calc_chain_relationship_entry->audit_kind
            == PackageEntryAuditKind::SourceRelationships,
        "restored package metadata entry should keep structured role");
    check(restored_calc_chain_relationship_entry->owner_part == calc_chain_part.value(),
        "restored package metadata entry should keep owner part");
    check(metadata_plan.find_removed_package_entry("xl/_rels/calcChain.xml.rels") == nullptr,
        "restoring a package metadata entry should clear the removed package-entry audit");
    metadata_plan.remove_package_entry("xl/_rels/calcChain.xml.rels",
        "removed calcChain relationships again", PackageEntryAuditKind::SourceRelationships,
        calc_chain_part.value());
    check(metadata_plan.find_package_entry("xl/_rels/calcChain.xml.rels") == nullptr,
        "removing a package metadata entry should clear the active package-entry audit");
    check(metadata_plan.find_removed_package_entry("xl/_rels/calcChain.xml.rels") != nullptr,
        "removing a restored package metadata entry should record removed package-entry audit");
    bool wrong_content_types_entry_failed = false;
    try {
        metadata_plan.set_package_entry("xl/_rels/workbook.xml.rels",
            PartWriteMode::LocalDomRewrite, "wrong content types entry",
            PackageEntryAuditKind::ContentTypes);
    } catch (const std::exception&) {
        wrong_content_types_entry_failed = true;
    }
    check(wrong_content_types_entry_failed,
        "content-types package-entry audit should reject non-content-types entries");
    bool wrong_package_relationships_entry_failed = false;
    try {
        metadata_plan.set_package_entry("[Content_Types].xml",
            PartWriteMode::LocalDomRewrite, "wrong package relationships entry",
            PackageEntryAuditKind::PackageRelationships);
    } catch (const std::exception&) {
        wrong_package_relationships_entry_failed = true;
    }
    check(wrong_package_relationships_entry_failed,
        "package-relationships audit should reject non-package relationship entries");
    bool source_relationship_without_owner_failed = false;
    try {
        metadata_plan.set_package_entry("xl/_rels/workbook.xml.rels",
            PartWriteMode::CopyOriginal, "missing owner",
            PackageEntryAuditKind::SourceRelationships);
    } catch (const std::exception&) {
        source_relationship_without_owner_failed = true;
    }
    check(source_relationship_without_owner_failed,
        "source relationship package-entry audit should require an owner part");
    bool source_relationship_owner_mismatch_failed = false;
    try {
        metadata_plan.set_package_entry("xl/_rels/workbook.xml.rels",
            PartWriteMode::CopyOriginal, "wrong owner",
            PackageEntryAuditKind::SourceRelationships, worksheet_part.value());
    } catch (const std::exception&) {
        source_relationship_owner_mismatch_failed = true;
    }
    check(source_relationship_owner_mismatch_failed,
        "source relationship package-entry audit should match entry name to owner part");
    bool removed_source_relationship_owner_mismatch_failed = false;
    try {
        metadata_plan.remove_package_entry("xl/_rels/calcChain.xml.rels",
            "wrong removed owner", PackageEntryAuditKind::SourceRelationships,
            workbook_part.value());
    } catch (const std::exception&) {
        removed_source_relationship_owner_mismatch_failed = true;
    }
    check(removed_source_relationship_owner_mismatch_failed,
        "removed source relationship package-entry audit should match entry name to owner part");
    bool generic_owner_failed = false;
    try {
        metadata_plan.set_package_entry("custom/entry.bin", PartWriteMode::CopyOriginal,
            "generic owner", PackageEntryAuditKind::Generic, workbook_part.value());
    } catch (const std::exception&) {
        generic_owner_failed = true;
    }
    check(generic_owner_failed,
        "generic package-entry audit should reject owner parts");
    metadata_plan.remove_part(calc_chain_part, "removed calcChain");
    check(metadata_plan.find_removed_part(calc_chain_part) != nullptr,
        "edit plan should record removed parts");
    metadata_plan.set_part(calc_chain_part, PartWriteMode::LocalDomRewrite,
        "calcChain restored as local DOM rewrite");
    check(metadata_plan.find_part(calc_chain_part) != nullptr,
        "edit plan should restore an active part entry after removal");
    check(metadata_plan.find_part(calc_chain_part)->write_mode
            == PartWriteMode::LocalDomRewrite,
        "edit plan should keep restored part write mode");
    check(metadata_plan.find_removed_part(calc_chain_part) == nullptr,
        "restoring a part entry should clear the removed-part audit entry");
    check(!metadata_plan.empty(),
        "package metadata entries should make an edit plan non-empty");

    const fastxlsx::detail::EditPlan removal_plan =
        planner.plan_part_removal(calc_chain_part, "calcChain removed after worksheet rewrite");
    check(removal_plan.find_part(calc_chain_part) == nullptr,
        "part removal plan should not leave the removed part active");
    const auto* removed_calc_chain = removal_plan.find_removed_part(calc_chain_part);
    check(removed_calc_chain != nullptr,
        "part removal plan should record the removed target part");
    check(removed_calc_chain->reason.find("calcChain") != std::string::npos,
        "part removal plan should retain the removal reason");
    const auto* removal_workbook_plan = removal_plan.find_part(workbook_part);
    check(removal_workbook_plan != nullptr,
        "part removal plan should keep other parts as copy-original");
    check(removal_workbook_plan->write_mode == PartWriteMode::CopyOriginal,
        "part removal plan should preserve untouched parts");

    const fastxlsx::detail::EditPlan chart_removal_plan =
        planner.plan_part_removal(chart_part, "chart removed after drawing review");
    const auto* removed_chart = chart_removal_plan.find_removed_part(chart_part);
    check(removed_chart != nullptr,
        "part removal plan should record removed chart parts");
    check(removed_chart->reason.find("inbound relationship preserved")
            != std::string::npos,
        "part removal plan should audit inbound relationships that still target removed parts");
    check(removed_chart->reason.find("/xl/drawings/drawing1.xml")
            != std::string::npos,
        "part removal inbound audit should include the relationship owner part");
    check(removed_chart->reason.find("rId2") != std::string::npos,
        "part removal inbound audit should include the relationship id");
    check(removed_chart->reason.find("../charts/chart1.xml") != std::string::npos,
        "part removal inbound audit should include the original relationship target");
    check(removed_chart->reason.find("/xl/charts/chart1.xml") != std::string::npos,
        "part removal inbound audit should include the removed target part");
    check(removed_chart->inbound_relationships.size() == 2,
        "part removal should keep structured inbound chart relationship audit");
    const fastxlsx::detail::RemovedPartInboundRelationshipAudit* chart_inbound = nullptr;
    const fastxlsx::detail::RemovedPartInboundRelationshipAudit* chart_fragment_inbound = nullptr;
    for (const auto& audit : removed_chart->inbound_relationships) {
        if (audit.relationship_id == "rId2") {
            chart_inbound = &audit;
        } else if (audit.relationship_id == "rIdFragmentDrawing") {
            chart_fragment_inbound = &audit;
        }
    }
    check(chart_inbound != nullptr,
        "structured part removal audit should include the direct chart relationship");
    check(chart_inbound->owner_part == drawing_part.value(),
        "structured chart removal audit should keep owner part");
    check(chart_inbound->owner_entry == "xl/drawings/_rels/drawing1.xml.rels",
        "structured chart removal audit should keep owner relationship entry");
    check(chart_inbound->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
        "structured chart removal audit should keep relationship type");
    check(chart_inbound->relationship_target == "../charts/chart1.xml",
        "structured chart removal audit should keep raw target");
    check(chart_inbound->target_part == chart_part,
        "structured chart removal audit should keep normalized removed target part");
    check(chart_fragment_inbound != nullptr,
        "structured part removal audit should include URI-qualified chart relationships");
    check(chart_fragment_inbound->relationship_target == "../charts/chart1.xml#plotArea",
        "structured chart removal audit should keep URI-qualified raw target");
    check(chart_fragment_inbound->target_part == chart_part,
        "structured URI-qualified chart audit should keep normalized target part");

    manifest.add_package_relationship(fastxlsx::detail::Relationship {
        "rIdPackageWorkbook",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument",
        "xl/workbook.xml",
    });
    const fastxlsx::detail::EditPlan workbook_removal_plan =
        planner.plan_part_removal(workbook_part, "workbook removed after package review");
    const auto* removed_workbook = workbook_removal_plan.find_removed_part(workbook_part);
    check(removed_workbook != nullptr,
        "part removal plan should record removed workbook parts");
    check(removed_workbook->reason.find("package _rels/.rels") != std::string::npos,
        "part removal inbound audit should include package relationship owner");
    check(removed_workbook->reason.find("rIdPackageWorkbook") != std::string::npos,
        "part removal inbound audit should include package relationship id");
    check(removed_workbook->reason.find("xl/workbook.xml") != std::string::npos,
        "part removal inbound audit should include package relationship target");
    check(removed_workbook->inbound_relationships.size() == 1,
        "workbook removal should keep structured package relationship audit");
    const auto& workbook_inbound = removed_workbook->inbound_relationships.front();
    check(workbook_inbound.owner_part.empty(),
        "structured package relationship audit should not carry an owner part");
    check(workbook_inbound.owner_entry == "_rels/.rels",
        "structured package relationship audit should keep package relationship entry");
    check(workbook_inbound.relationship_id == "rIdPackageWorkbook",
        "structured package relationship audit should keep relationship id");
    check(workbook_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument",
        "structured package relationship audit should keep relationship type");
    check(workbook_inbound.relationship_target == "xl/workbook.xml",
        "structured package relationship audit should keep raw target");
    check(workbook_inbound.target_part == workbook_part,
        "structured package relationship audit should keep normalized removed target part");

    manifest.add_relationship(drawing_part,
        fastxlsx::detail::Relationship {
            "rIdMalformedPercent",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
            "../media/image%ZZ.png",
        });
    const fastxlsx::detail::EditPlan malformed_target_removal_plan =
        planner.plan_part_removal(chart_part, "chart removed with malformed unrelated target");
    const auto* malformed_target_removed_chart =
        malformed_target_removal_plan.find_removed_part(chart_part);
    check(malformed_target_removed_chart != nullptr,
        "malformed unrelated targets should not block part removal planning");
    check(malformed_target_removed_chart->inbound_relationships.size() == 2,
        "malformed unrelated targets should not be recorded as inbound chart links");
    check(has_note_containing(malformed_target_removal_plan.notes(),
              {"invalid relationship target skipped during removed-part inbound audit",
                  "/xl/drawings/drawing1.xml", "rIdMalformedPercent",
                  "../media/image%ZZ.png", "percent escape is invalid"}),
        "part removal planning should audit malformed relationship targets without failing");

    bool missing_removal_failed = false;
    try {
        (void)planner.plan_part_removal(
            fastxlsx::detail::PartName("/xl/missing.xml"), "missing part");
    } catch (const std::exception&) {
        missing_removal_failed = true;
    }
    check(missing_removal_failed,
        "part removal planning should reject unregistered target parts");

    const fastxlsx::detail::DependencyAnalysis planning_analysis =
        fastxlsx::detail::DependencyAnalyzer(manifest).analyze_worksheet_stream_rewrite(
            worksheet_part);
    const fastxlsx::detail::EditPlan sheet_plan =
        planner.plan_worksheet_stream_rewrite(worksheet_part);
    using WorkbookAuditKind = fastxlsx::detail::WorkbookPayloadDependencyAuditKind;
    using WorkbookAuditScope = fastxlsx::detail::WorkbookPayloadDependencyAuditScope;
    check(has_workbook_payload_audit(planning_analysis.workbook_payload_dependency_audits,
              workbook_part, WorkbookAuditKind::CalcMetadata,
              WorkbookAuditScope::WorksheetRewrite, "calcPr",
              {"worksheet rewrite", "calcPr"}),
        "worksheet dependency analysis should structure workbook calc metadata review");
    check(has_workbook_payload_audit(planning_analysis.workbook_payload_dependency_audits,
              workbook_part, WorkbookAuditKind::DefinedNames,
              WorkbookAuditScope::WorksheetRewrite, "definedNames",
              {"worksheet rewrite", "definedNames"}),
        "worksheet dependency analysis should structure workbook definedNames review");
    check(sheet_plan.size() + sheet_plan.removed_parts().size() == manifest.size(),
        "worksheet plan should account for all known parts as active or removed");
    const auto* rewritten_sheet = sheet_plan.find_part(worksheet_part);
    check(rewritten_sheet != nullptr, "worksheet plan should include target sheet");
    check(rewritten_sheet->write_mode == PartWriteMode::StreamRewrite,
        "worksheet plan should stream-rewrite the target sheet");
    check(sheet_plan.find_part(unknown_part)->write_mode == PartWriteMode::CopyOriginal,
        "worksheet plan should preserve unknown parts by default");
    check(sheet_plan.find_part(unknown_part)->reason.find("rIdOpaqueCustom")
            != std::string::npos,
        "worksheet plan should annotate unknown relationship type copy decisions");
    check(sheet_plan.find_part(unknown_part)
              ->reason.find("https://fastxlsx.invalid/relationships/opaque-extension")
            != std::string::npos,
        "worksheet plan should annotate relationship types in copy decisions");
    check(sheet_plan.find_part(unknown_part)->reason.find("/custom/unknown.bin")
            != std::string::npos,
        "worksheet plan should annotate unknown relationship type target parts");
    check(sheet_plan.find_part(workbook_part)->reason.find("definedNames")
            != std::string::npos,
        "worksheet plan should annotate workbook definedNames review");
    check(has_workbook_payload_audit(sheet_plan.workbook_payload_dependency_audits(),
              workbook_part, WorkbookAuditKind::CalcMetadata,
              WorkbookAuditScope::WorksheetRewrite, "calcPr",
              {"worksheet rewrite", "calcPr"}),
        "worksheet plan should propagate structured workbook calc metadata audit");
    check(has_workbook_payload_audit(sheet_plan.workbook_payload_dependency_audits(),
              workbook_part, WorkbookAuditKind::DefinedNames,
              WorkbookAuditScope::WorksheetRewrite, "definedNames",
              {"worksheet rewrite", "definedNames"}),
        "worksheet plan should propagate structured workbook definedNames audit");
    check(sheet_plan.find_part(shared_strings_part)->write_mode == PartWriteMode::CopyOriginal,
        "worksheet plan should not rewrite sharedStrings without an explicit dependency decision");
    check(sheet_plan.find_part(shared_strings_part)->reason.find("shared strings")
            != std::string::npos,
        "worksheet plan should annotate sharedStrings copy decision from dependency analysis");
    check(sheet_plan.find_part(shared_strings_part)->relationship_owner_part.empty(),
        "non-relationship dependency should not carry relationship owner metadata");
    const auto* drawing_plan = sheet_plan.find_part(drawing_part);
    check(drawing_plan->write_mode == PartWriteMode::CopyOriginal,
        "worksheet plan should preserve known internal relationship targets by default");
    check(drawing_plan->reason.find("rId1") != std::string::npos,
        "worksheet plan should annotate known relationship target copy decision");
    check(drawing_plan->reason.find("/xl/drawings/drawing1.xml") != std::string::npos,
        "worksheet plan should annotate normalized relationship target part");
    check(drawing_plan->relationship_owner_part == worksheet_part.value(),
        "worksheet plan should keep structured relationship owner metadata");
    check(drawing_plan->relationship_id == "rId1",
        "worksheet plan should keep structured relationship id metadata");
    check(drawing_plan->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "worksheet plan should keep structured relationship type metadata");
    check(drawing_plan->relationship_target == "../drawings/drawing1.xml",
        "worksheet plan should keep structured relationship target metadata");
    fastxlsx::detail::EditPlan rewritten_dependency_plan = sheet_plan;
    rewritten_dependency_plan.set_part(drawing_part, PartWriteMode::LocalDomRewrite,
        "drawing metadata rewrite");
    const auto* rewritten_drawing_plan =
        rewritten_dependency_plan.find_part(drawing_part);
    check(rewritten_drawing_plan != nullptr,
        "edit plan should keep the rewritten relationship-derived part");
    check(rewritten_drawing_plan->write_mode == PartWriteMode::LocalDomRewrite,
        "edit plan should update relationship-derived part write mode");
    check(rewritten_drawing_plan->relationship_owner_part.empty()
            && rewritten_drawing_plan->relationship_id.empty()
            && rewritten_drawing_plan->relationship_type.empty()
            && rewritten_drawing_plan->relationship_target.empty(),
        "rewriting a relationship-derived part should clear stale relationship metadata");
    const auto* chart_plan = sheet_plan.find_part(chart_part);
    check(chart_plan->write_mode == PartWriteMode::CopyOriginal,
        "worksheet plan should preserve drawing-owned chart targets by default");
    check(chart_plan->reason.find("/xl/drawings/drawing1.xml")
            != std::string::npos,
        "worksheet plan should annotate chart copy decision from relationship traversal");
    check(chart_plan->reason.find("/xl/charts/chart1.xml") != std::string::npos,
        "worksheet plan should annotate normalized chart target part");
    check(chart_plan->relationship_owner_part == drawing_part.value(),
        "worksheet plan should keep drawing-owned relationship owner metadata");
    check(chart_plan->relationship_id == "rId2",
        "worksheet plan should keep drawing-owned relationship id metadata");
    check(chart_plan->relationship_target == "../charts/chart1.xml",
        "worksheet plan should keep drawing-owned relationship target metadata");
    check(sheet_plan.find_part(image_part)->write_mode == PartWriteMode::CopyOriginal,
        "worksheet plan should preserve drawing-owned image targets by default");
    check(sheet_plan.find_part(image_part)->reason.find("/xl/drawings/drawing1.xml")
            != std::string::npos,
        "worksheet plan should annotate image copy decision from relationship traversal");
    check(sheet_plan.find_part(image_part)->reason.find("/xl/media/image1.png")
            != std::string::npos,
        "worksheet plan should annotate normalized image target part");
    check(sheet_plan.find_part(table_part)->write_mode == PartWriteMode::CopyOriginal,
        "worksheet plan should preserve worksheet-owned table targets by default");
    check(sheet_plan.find_part(table_part)->reason.find("rId3") != std::string::npos,
        "worksheet plan should annotate table copy decision from relationship traversal");
    check(sheet_plan.find_part(table_part)->reason.find("/xl/tables/table1.xml")
            != std::string::npos,
        "worksheet plan should annotate normalized table target part");
    check(sheet_plan.find_part(vml_drawing_part)->write_mode == PartWriteMode::CopyOriginal,
        "worksheet plan should preserve URI-qualified base targets by default");
    check(sheet_plan.find_part(vml_drawing_part)->reason.find("rIdVmlFragment")
            != std::string::npos,
        "worksheet plan should annotate URI-qualified base target copy decision");
    check(sheet_plan.find_part(vml_drawing_part)->reason.find("/xl/drawings/vmlDrawing1.vml")
            != std::string::npos,
        "worksheet plan should annotate normalized URI-qualified base target part");
    check(sheet_plan.find_part(percent_encoded_drawing_part)->write_mode
            == PartWriteMode::CopyOriginal,
        "worksheet plan should preserve percent-decoded internal targets by default");
    check(sheet_plan.find_part(percent_encoded_drawing_part)->reason.find("rIdPercentEncoded")
            != std::string::npos,
        "worksheet plan should annotate percent-decoded target copy decision");
    check(sheet_plan.find_part(percent_encoded_drawing_part)
              ->reason.find("/xl/drawings/drawing space.xml")
            != std::string::npos,
        "worksheet plan should annotate normalized percent-decoded target part");
    check(sheet_plan.find_part(absolute_drawing_part)->write_mode
            == PartWriteMode::CopyOriginal,
        "worksheet plan should preserve absolute internal targets by default");
    check(sheet_plan.find_part(absolute_drawing_part)->reason.find("rIdAbsoluteTarget")
            != std::string::npos,
        "worksheet plan should annotate absolute target copy decision");
    check(sheet_plan.find_part(absolute_drawing_part)
              ->reason.find("/xl/drawings/absoluteDrawing.xml")
            != std::string::npos,
        "worksheet plan should annotate normalized absolute target part");
    check(sheet_plan.find_part(calc_chain_part) == nullptr,
        "worksheet plan should not leave stale calcChain as an active copy decision");
    const auto* removed_sheet_calc_chain = sheet_plan.find_removed_part(calc_chain_part);
    check(removed_sheet_calc_chain != nullptr,
        "worksheet plan should record stale calcChain as removed-part audit");
    check(removed_sheet_calc_chain != nullptr
            && removed_sheet_calc_chain->reason.find("worksheet rewrite") != std::string::npos,
        "worksheet plan should annotate calcChain removal from dependency analysis");
    check(sheet_plan.full_calculation_on_load(),
        "worksheet stream rewrite should request full calculation by default");
    check(sheet_plan.calc_chain_action() == CalcChainAction::Remove,
        "worksheet stream rewrite should default to removing stale calcChain");
    check(!sheet_plan.notes().empty(),
        "worksheet stream rewrite with relationships should record policy notes");
    bool found_external_relationship_note = false;
    bool found_external_relationship_detail_note = false;
    bool found_uri_qualified_note = false;
    bool found_uri_qualified_detail_note = false;
    bool found_invalid_internal_note = false;
    bool found_invalid_internal_detail_note = false;
    bool found_unresolved_internal_note = false;
    bool found_unresolved_internal_detail_note = false;
    for (const std::string& note : sheet_plan.notes()) {
        if (note.find("external relationship targets") != std::string::npos) {
            found_external_relationship_note = true;
        }
        if (note.find("external relationship targets are preserved in owner .rels")
                != std::string::npos
            && note.find("/xl/worksheets/sheet1.xml") != std::string::npos
            && note.find("rIdExternal") != std::string::npos
            && note.find("relationships/hyperlink") != std::string::npos
            && note.find("https://example.invalid/") != std::string::npos) {
            found_external_relationship_detail_note = true;
        }
        if (note.find("URI-qualified internal relationship targets") != std::string::npos) {
            found_uri_qualified_note = true;
        }
        if (note.find("URI-qualified internal relationship targets require package structure review")
                != std::string::npos
            && note.find("rIdVmlFragment") != std::string::npos
            && note.find("relationships/vmlDrawing") != std::string::npos
            && note.find("../drawings/vmlDrawing1.vml#shape1") != std::string::npos
            && note.find("/xl/drawings/vmlDrawing1.vml") != std::string::npos) {
            found_uri_qualified_detail_note = true;
        }
        if (note.find("invalid internal relationship targets") != std::string::npos) {
            found_invalid_internal_note = true;
        }
        if (note.find("invalid internal relationship targets require package structure review")
                != std::string::npos
            && note.find("rIdEscapes") != std::string::npos
            && note.find("../../../outside.bin") != std::string::npos) {
            found_invalid_internal_detail_note = true;
        }
        if (note.find("unresolved internal relationship targets") != std::string::npos) {
            found_unresolved_internal_note = true;
        }
        if (note.find("unresolved internal relationship targets require package structure review")
                != std::string::npos
            && note.find("rIdMissing") != std::string::npos
            && note.find("../comments/comment1.xml") != std::string::npos
            && note.find("/xl/comments/comment1.xml") != std::string::npos) {
            found_unresolved_internal_detail_note = true;
        }
    }
    check(found_external_relationship_note,
        "worksheet stream rewrite should audit external relationship targets without package parts");
    check(found_external_relationship_detail_note,
        "worksheet stream rewrite should include external relationship owner and target details");
    check(found_uri_qualified_note,
        "worksheet stream rewrite should audit URI-qualified relationship targets");
    check(found_uri_qualified_detail_note,
        "worksheet stream rewrite should include URI-qualified relationship owner and target details");
    check(found_invalid_internal_note,
        "worksheet stream rewrite should audit invalid internal relationship targets");
    check(found_invalid_internal_detail_note,
        "worksheet stream rewrite should include invalid relationship owner and target details");
    check(found_unresolved_internal_note,
        "worksheet stream rewrite should audit unresolved internal relationship targets");
    check(found_unresolved_internal_detail_note,
        "worksheet stream rewrite should include unresolved relationship owner and target details");
    check(has_note_containing(sheet_plan.notes(),
              {"external relationship targets are preserved in owner .rels",
                  "/xl/drawings/drawing1.xml", "rIdExternalDrawing",
                  "https://drawing.example.invalid/link"}),
        "worksheet stream rewrite should include drawing-owned external relationship details");
    check(has_note_containing(sheet_plan.notes(),
              {"URI-qualified internal relationship targets require package structure review",
                  "/xl/drawings/drawing1.xml", "rIdFragmentDrawing",
                  "../charts/chart1.xml#plotArea", "/xl/charts/chart1.xml"}),
        "worksheet stream rewrite should include drawing-owned URI-qualified relationship details");
    check(has_note_containing(sheet_plan.notes(),
              {"unresolved internal relationship targets require package structure review",
                  "/xl/drawings/drawing1.xml", "rIdMissingDrawing",
                  "../embeddings/oleObject1.bin", "/xl/embeddings/oleObject1.bin"}),
        "worksheet stream rewrite should include drawing-owned unresolved relationship details");
    check(has_note_containing(sheet_plan.notes(),
              {"invalid internal relationship targets require package structure review",
                  "/xl/drawings/drawing1.xml", "rIdEscapesDrawing",
                  "../../../outside-from-drawing.bin"}),
        "worksheet stream rewrite should include drawing-owned invalid relationship details");
    if (sheet_plan.relationship_target_audits().size()
        != planning_analysis.relationship_target_audits.size()) {
        throw TestFailure(
            "worksheet stream rewrite should preserve structured relationship target audit count: plan "
            + std::to_string(sheet_plan.relationship_target_audits().size()) + " analysis "
            + std::to_string(planning_analysis.relationship_target_audits.size()));
    }
    bool found_plan_external_audit = false;
    bool found_plan_drawing_external_audit = false;
    bool found_plan_drawing_uri_audit = false;
    bool found_plan_drawing_unresolved_audit = false;
    bool found_plan_drawing_invalid_audit = false;
    for (const fastxlsx::detail::RelationshipTargetAudit& audit :
        sheet_plan.relationship_target_audits()) {
        if (audit.owner_part == worksheet_part && audit.relationship_id == "rIdExternal"
            && audit.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink"
            && audit.target == "https://example.invalid/" && audit.normalized_target.empty()
            && audit.note.find("external relationship targets") != std::string::npos) {
            found_plan_external_audit = true;
        }
        if (audit.owner_part == drawing_part
            && audit.relationship_id == "rIdExternalDrawing"
            && audit.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink"
            && audit.target == "https://drawing.example.invalid/link"
            && audit.normalized_target.empty()
            && audit.note.find("external relationship targets") != std::string::npos) {
            found_plan_drawing_external_audit = true;
        }
        if (audit.owner_part == drawing_part
            && audit.relationship_id == "rIdFragmentDrawing"
            && audit.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart"
            && audit.target == "../charts/chart1.xml#plotArea"
            && audit.normalized_target == "/xl/charts/chart1.xml"
            && audit.note.find("has base part /xl/charts/chart1.xml")
                != std::string::npos) {
            found_plan_drawing_uri_audit = true;
        }
        if (audit.owner_part == drawing_part && audit.relationship_id == "rIdMissingDrawing"
            && audit.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject"
            && audit.target == "../embeddings/oleObject1.bin"
            && audit.normalized_target == "/xl/embeddings/oleObject1.bin"
            && audit.note.find("resolves to unregistered part /xl/embeddings/oleObject1.bin")
                != std::string::npos) {
            found_plan_drawing_unresolved_audit = true;
        }
        if (audit.owner_part == drawing_part && audit.relationship_id == "rIdEscapesDrawing"
            && audit.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject"
            && audit.target == "../../../outside-from-drawing.bin"
            && audit.normalized_target.empty()
            && audit.note.find("cannot be normalized as a package part")
                != std::string::npos) {
            found_plan_drawing_invalid_audit = true;
        }
    }
    check(found_plan_external_audit,
        "worksheet stream rewrite should preserve structured worksheet-owned external audit");
    check(found_plan_drawing_external_audit,
        "worksheet stream rewrite should preserve structured drawing-owned external audit");
    check(found_plan_drawing_uri_audit,
        "worksheet stream rewrite should preserve structured drawing-owned URI-qualified audit");
    check(found_plan_drawing_unresolved_audit,
        "worksheet stream rewrite should preserve structured drawing-owned unresolved audit");
    check(found_plan_drawing_invalid_audit,
        "worksheet stream rewrite should preserve structured drawing-owned invalid audit");

    fastxlsx::detail::ReferencePolicy recalc_policy;
    recalc_policy.request_full_calculation_on_sheet_rewrite = false;
    recalc_policy.unsupported_linked_part_action =
        ReferencePolicyAction::RequestRecalculation;
    recalc_policy.calc_chain_action = CalcChainAction::Preserve;
    const fastxlsx::detail::EditPlan linked_recalc_plan =
        planner.plan_worksheet_stream_rewrite(worksheet_part, recalc_policy);
    check(linked_recalc_plan.full_calculation_on_load(),
        "request-recalculation policy should request full calculation for linked sheets");
    check(linked_recalc_plan.calc_chain_action() == CalcChainAction::Preserve,
        "request-recalculation policy should keep its calcChain action");
    check(linked_recalc_plan.find_part(worksheet_part)->write_mode
            == PartWriteMode::StreamRewrite,
        "request-recalculation policy should still rewrite the target worksheet");
    const auto* preserved_calc_chain = linked_recalc_plan.find_part(calc_chain_part);
    check(preserved_calc_chain != nullptr,
        "preserve calcChain policy should keep calcChain as an active copy decision");
    check(preserved_calc_chain != nullptr
            && preserved_calc_chain->write_mode == PartWriteMode::CopyOriginal,
        "preserve calcChain policy should keep calcChain copy-original");
    check(preserved_calc_chain != nullptr
            && preserved_calc_chain->reason.find("calcChain") != std::string::npos,
        "preserve calcChain policy should keep the calcChain dependency reason");
    check(linked_recalc_plan.find_removed_part(calc_chain_part) == nullptr,
        "preserve calcChain policy should not record removed calcChain audit");

    const fastxlsx::detail::EditPlan workbook_plan = planner.plan_part_rewrite(
        workbook_part, PartWriteMode::LocalDomRewrite, "small workbook metadata rewrite");
    check(workbook_plan.find_part(workbook_part)->write_mode == PartWriteMode::LocalDomRewrite,
        "small XML rewrite plan should local-DOM-rewrite the target part");
    check(workbook_plan.find_part(unknown_part)->write_mode == PartWriteMode::CopyOriginal,
        "small XML rewrite plan should still preserve unknown parts");

    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action = ReferencePolicyAction::Fail;
    bool linked_part_failed = false;
    try {
        (void)planner.plan_worksheet_stream_rewrite(worksheet_part, fail_policy);
    } catch (const std::exception&) {
        linked_part_failed = true;
    }
    check(linked_part_failed, "reference policy should be able to fail linked sheet rewrites");

    bool copy_rewrite_failed = false;
    try {
        (void)planner.plan_part_rewrite(workbook_part, PartWriteMode::CopyOriginal);
    } catch (const std::exception&) {
        copy_rewrite_failed = true;
    }
    check(copy_rewrite_failed, "target rewrite plan should reject copy-original mode");

    bool missing_part_failed = false;
    try {
        (void)planner.plan_part_rewrite(
            fastxlsx::detail::PartName("/xl/worksheets/missing.xml"),
            PartWriteMode::StreamRewrite);
    } catch (const std::exception&) {
        missing_part_failed = true;
    }
    check(missing_part_failed, "rewrite planner should reject missing target parts");
}

void test_minimal_workbook_manifest()
{
    using fastxlsx::detail::PartWriteMode;

    const auto manifest = fastxlsx::detail::make_minimal_workbook_manifest(2);
    check(manifest.size() == 5, "minimal workbook manifest part count mismatch");
    check(manifest.content_types().overrides().size() == 5,
        "minimal workbook manifest override count mismatch");
    check(manifest.package_relationships().size() == 3,
        "minimal workbook manifest package relationship count mismatch");

    const auto* workbook_relationships =
        manifest.relationships_for(fastxlsx::detail::PartName("/xl/workbook.xml"));
    check(workbook_relationships != nullptr, "minimal workbook relationships should exist");
    check(workbook_relationships->size() == 2, "minimal workbook relationship count mismatch");
    check(workbook_relationships->find_by_id("rId2")->target == "worksheets/sheet2.xml",
        "minimal workbook relationship target mismatch");

    const std::string content_types_xml =
        fastxlsx::detail::serialize_content_types(manifest.content_types());
    check(content_types_xml.find("/xl/worksheets/sheet2.xml") != std::string::npos,
        "minimal workbook content types should include sheet2");
    check(content_types_xml.find("/docProps/core.xml") != std::string::npos,
        "minimal workbook content types should include core properties");
    check(content_types_xml.find(
              "application/vnd.openxmlformats-package.core-properties+xml")
            != std::string::npos,
        "minimal workbook content types should include core properties type");
    check(content_types_xml.find("/docProps/app.xml") != std::string::npos,
        "minimal workbook content types should include extended properties");
    check(content_types_xml.find(
              "application/vnd.openxmlformats-officedocument.extended-properties+xml")
            != std::string::npos,
        "minimal workbook content types should include extended properties type");

    const std::string package_rels_xml =
        fastxlsx::detail::serialize_relationships(manifest.package_relationships());
    check(package_rels_xml.find("Target=\"xl/workbook.xml\"") != std::string::npos,
        "minimal workbook package relationship target mismatch");
    check(package_rels_xml.find("Target=\"docProps/core.xml\"") != std::string::npos,
        "minimal workbook package relationships should include core properties");
    check(package_rels_xml.find(
              "http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties")
            != std::string::npos,
        "minimal workbook package relationships should include core properties type");
    check(package_rels_xml.find("Target=\"docProps/app.xml\"") != std::string::npos,
        "minimal workbook package relationships should include extended properties");
    check(package_rels_xml.find(
              "http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties")
            != std::string::npos,
        "minimal workbook package relationships should include extended properties type");

    const auto* workbook_part =
        manifest.find_part(fastxlsx::detail::PartName("/xl/workbook.xml"));
    check(workbook_part != nullptr, "minimal workbook part should exist");
    check(workbook_part->write_mode == PartWriteMode::GenerateSmallXml,
        "minimal workbook part should be generated small XML");
    check(!workbook_part->preserve_original, "minimal workbook part should not preserve original bytes");
    check(workbook_part->dirty, "minimal workbook part should be dirty");
    check(workbook_part->generated, "minimal workbook part should be marked generated");

    const auto* worksheet_part =
        manifest.find_part(fastxlsx::detail::PartName("/xl/worksheets/sheet1.xml"));
    check(worksheet_part != nullptr, "minimal worksheet part should exist");
    check(worksheet_part->write_mode == PartWriteMode::StreamRewrite,
        "minimal worksheet part should use stream rewrite mode");
    check(!worksheet_part->preserve_original, "minimal worksheet should not preserve original bytes");
    check(worksheet_part->dirty, "minimal worksheet part should be dirty");
    check(!worksheet_part->generated, "stream rewrite worksheet should not be marked generated");

    const auto* core_properties_part =
        manifest.find_part(fastxlsx::detail::PartName("/docProps/core.xml"));
    check(core_properties_part != nullptr, "minimal core properties part should exist");
    check(core_properties_part->write_mode == PartWriteMode::GenerateSmallXml,
        "minimal core properties part should be generated small XML");
    check(!core_properties_part->preserve_original,
        "minimal core properties should not preserve original bytes");
    check(core_properties_part->dirty, "minimal core properties part should be dirty");
    check(core_properties_part->generated,
        "minimal core properties part should be marked generated");

    const auto* extended_properties_part =
        manifest.find_part(fastxlsx::detail::PartName("/docProps/app.xml"));
    check(extended_properties_part != nullptr, "minimal extended properties part should exist");
    check(extended_properties_part->write_mode == PartWriteMode::GenerateSmallXml,
        "minimal extended properties part should be generated small XML");
    check(!extended_properties_part->preserve_original,
        "minimal extended properties should not preserve original bytes");
    check(extended_properties_part->dirty, "minimal extended properties part should be dirty");
    check(extended_properties_part->generated,
        "minimal extended properties part should be marked generated");

    const std::string core_properties_xml = fastxlsx::detail::build_core_properties();
    check(core_properties_xml.find("<cp:coreProperties ") != std::string::npos,
        "core properties root mismatch");
    check(core_properties_xml.find("<dc:creator>FastXLSX</dc:creator>") != std::string::npos,
        "core properties creator mismatch");
    check(core_properties_xml.find("<cp:lastModifiedBy>FastXLSX</cp:lastModifiedBy>")
            != std::string::npos,
        "core properties lastModifiedBy mismatch");

    const std::string extended_properties_xml = fastxlsx::detail::build_extended_properties();
    check(extended_properties_xml.find("<Application>FastXLSX</Application>")
            != std::string::npos,
        "extended properties application mismatch");
    check(extended_properties_xml.find("<AppVersion>0.1</AppVersion>") != std::string::npos,
        "extended properties app version mismatch");

    fastxlsx::DocumentProperties custom_properties;
    custom_properties.creator = "Alice & Bob";
    custom_properties.last_modified_by = "Reviewer <QA>";
    custom_properties.title = "Doc <Title>";
    custom_properties.subject = "Subject & Scope";
    custom_properties.description = "Description with <xml> & text";
    custom_properties.keywords = "fastxlsx; metadata";
    custom_properties.category = "Planning";
    custom_properties.application = "FastXLSX Test & Tools";
    custom_properties.app_version = "2.5";

    const std::string custom_core_xml = fastxlsx::detail::build_core_properties(custom_properties);
    check(custom_core_xml.find("<dc:creator>Alice &amp; Bob</dc:creator>") != std::string::npos,
        "custom core properties creator escaping mismatch");
    check(custom_core_xml.find("<cp:lastModifiedBy>Reviewer &lt;QA&gt;</cp:lastModifiedBy>")
            != std::string::npos,
        "custom core properties lastModifiedBy escaping mismatch");
    check(custom_core_xml.find("<dc:title>Doc &lt;Title&gt;</dc:title>") != std::string::npos,
        "custom core properties title escaping mismatch");
    check(custom_core_xml.find("<dc:subject>Subject &amp; Scope</dc:subject>")
            != std::string::npos,
        "custom core properties subject escaping mismatch");
    check(custom_core_xml.find("<dc:description>Description with &lt;xml&gt; &amp; text</dc:description>")
            != std::string::npos,
        "custom core properties description escaping mismatch");
    check(custom_core_xml.find("<cp:keywords>fastxlsx; metadata</cp:keywords>")
            != std::string::npos,
        "custom core properties keywords mismatch");
    check(custom_core_xml.find("<cp:category>Planning</cp:category>") != std::string::npos,
        "custom core properties category mismatch");

    const std::string custom_extended_xml =
        fastxlsx::detail::build_extended_properties(custom_properties);
    check(custom_extended_xml.find("<Application>FastXLSX Test &amp; Tools</Application>")
            != std::string::npos,
        "custom extended properties application escaping mismatch");
    check(custom_extended_xml.find("<AppVersion>2.5</AppVersion>") != std::string::npos,
        "custom extended properties app version mismatch");
}

void test_minimal_workbook_manifest_with_shared_strings()
{
    using fastxlsx::detail::PartWriteMode;

    const auto manifest = fastxlsx::detail::make_minimal_workbook_manifest(1, true);
    check(manifest.size() == 5, "shared string manifest part count mismatch");

    const auto* shared_string_type =
        manifest.content_types().content_type_for(
            fastxlsx::detail::PartName("/xl/sharedStrings.xml"));
    check(shared_string_type != nullptr, "shared string content type should be registered");
    check(*shared_string_type
            == "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml",
        "shared string content type mismatch");

    const auto* workbook_relationships =
        manifest.relationships_for(fastxlsx::detail::PartName("/xl/workbook.xml"));
    check(workbook_relationships != nullptr,
        "shared string workbook relationships should exist");
    check(workbook_relationships->size() == 2, "shared string relationship count mismatch");

    const auto* relationship = workbook_relationships->find_by_id("rId2");
    check(relationship != nullptr, "shared string relationship id mismatch");
    check(relationship->type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings",
        "shared string relationship type mismatch");
    check(relationship->target == "sharedStrings.xml",
        "shared string relationship target mismatch");

    const auto* shared_strings_part =
        manifest.find_part(fastxlsx::detail::PartName("/xl/sharedStrings.xml"));
    check(shared_strings_part != nullptr, "shared strings part should exist");
    check(shared_strings_part->write_mode == PartWriteMode::GenerateSmallXml,
        "shared strings part should be generated small XML");
    check(!shared_strings_part->preserve_original,
        "shared strings part should not preserve original bytes");
    check(shared_strings_part->dirty, "shared strings part should be dirty");
    check(shared_strings_part->generated, "shared strings part should be marked generated");
}

void test_minimal_workbook_manifest_with_styles()
{
    using fastxlsx::detail::PartWriteMode;

    const auto manifest = fastxlsx::detail::make_minimal_workbook_manifest(1, true, true, true);
    check(manifest.size() == 6, "styles manifest part count mismatch");

    const auto* styles_type = manifest.content_types().content_type_for(
        fastxlsx::detail::PartName("/xl/styles.xml"));
    check(styles_type != nullptr, "styles content type should be registered");
    check(*styles_type
            == "application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml",
        "styles content type mismatch");

    const auto* workbook_relationships =
        manifest.relationships_for(fastxlsx::detail::PartName("/xl/workbook.xml"));
    check(workbook_relationships != nullptr, "styles workbook relationships should exist");
    check(workbook_relationships->size() == 3,
        "styles and shared strings relationship count mismatch");

    const auto* shared_strings = workbook_relationships->find_by_id("rId2");
    check(shared_strings != nullptr, "shared strings relationship id should remain before styles");
    check(shared_strings->type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings",
        "shared strings relationship type should remain stable");

    const auto* styles = workbook_relationships->find_by_id("rId3");
    check(styles != nullptr, "styles relationship id mismatch");
    check(styles->type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles",
        "styles relationship type mismatch");
    check(styles->target == "styles.xml", "styles relationship target mismatch");

    const auto* styles_part = manifest.find_part(fastxlsx::detail::PartName("/xl/styles.xml"));
    check(styles_part != nullptr, "styles part should exist");
    check(styles_part->write_mode == PartWriteMode::GenerateSmallXml,
        "styles part should be generated small XML");
    check(!styles_part->preserve_original, "styles part should not preserve original bytes");
    check(styles_part->dirty, "styles part should be dirty");
    check(styles_part->generated, "styles part should be marked generated");
}

void test_minimal_workbook_manifest_without_document_properties()
{
    const auto manifest = fastxlsx::detail::make_minimal_workbook_manifest(1, false, false);
    check(manifest.size() == 2, "manifest without document properties part count mismatch");
    check(manifest.content_types().overrides().size() == 2,
        "manifest without document properties override count mismatch");
    check(manifest.package_relationships().size() == 1,
        "manifest without document properties package relationship count mismatch");

    check(manifest.find_part(fastxlsx::detail::PartName("/docProps/core.xml")) == nullptr,
        "core properties part should be omitted when disabled");
    check(manifest.find_part(fastxlsx::detail::PartName("/docProps/app.xml")) == nullptr,
        "extended properties part should be omitted when disabled");

    const std::string content_types_xml =
        fastxlsx::detail::serialize_content_types(manifest.content_types());
    check(content_types_xml.find("/docProps/core.xml") == std::string::npos,
        "content types should omit disabled core properties");
    check(content_types_xml.find("/docProps/app.xml") == std::string::npos,
        "content types should omit disabled extended properties");

    const std::string package_rels_xml =
        fastxlsx::detail::serialize_relationships(manifest.package_relationships());
    check(package_rels_xml.find("Target=\"xl/workbook.xml\"") != std::string::npos,
        "office document relationship should remain when document properties are disabled");
    check(package_rels_xml.find("docProps/") == std::string::npos,
        "package relationships should omit disabled document properties");
}

} // namespace

int main()
{
    try {
        test_part_name_normalization();
        test_content_types_manifest();
        test_content_type_registry_helper();
        test_relationship_set();
        test_part_index();
        test_relationship_graph();
        test_package_manifest();
        test_package_part_edit_state();
        test_edit_plan_and_rewrite_planner();
        test_minimal_workbook_manifest();
        test_minimal_workbook_manifest_with_shared_strings();
        test_minimal_workbook_manifest_with_styles();
        test_minimal_workbook_manifest_without_document_properties();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
