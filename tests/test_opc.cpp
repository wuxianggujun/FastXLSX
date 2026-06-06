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

    const std::string xml = fastxlsx::detail::serialize_content_types(manifest);
    check(xml.find("<Default Extension=\"xml\" ContentType=\"application/xml\"/>")
            != std::string::npos,
        "content types serializer should include defaults");
    check(xml.find(
              "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>")
            != std::string::npos,
        "content types serializer should include overrides");
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

void test_minimal_workbook_manifest()
{
    const auto manifest = fastxlsx::detail::make_minimal_workbook_manifest(2);
    check(manifest.size() == 3, "minimal workbook manifest part count mismatch");
    check(manifest.content_types().overrides().size() == 3,
        "minimal workbook manifest override count mismatch");
    check(manifest.package_relationships().size() == 1,
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

    const std::string package_rels_xml =
        fastxlsx::detail::serialize_relationships(manifest.package_relationships());
    check(package_rels_xml.find("Target=\"xl/workbook.xml\"") != std::string::npos,
        "minimal workbook package relationship target mismatch");
}

} // namespace

int main()
{
    try {
        test_part_name_normalization();
        test_content_types_manifest();
        test_relationship_set();
        test_package_manifest();
        test_minimal_workbook_manifest();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
