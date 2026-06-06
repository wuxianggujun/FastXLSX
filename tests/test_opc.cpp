#include <fastxlsx/detail/opc.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

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
        test_minimal_workbook_manifest();
        test_minimal_workbook_manifest_with_shared_strings();
        test_minimal_workbook_manifest_without_document_properties();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
