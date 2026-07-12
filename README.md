# FastXLSX

FastXLSX 是一个 C++20 XLSX 创建与编辑库，优先支持 MSVC 2026。项目共享 OpenXML/OPC 底座，但公开三条边界清晰的路径：

- **Streaming**：`WorkbookWriter` / `WorksheetWriter`，面向按行创建大型新 workbook。
- **Patch**：`WorkbookEditor`，面向已有 workbook 的 part-level rewrite；未修改和未知 part 默认保留。
- **In-memory**：`WorksheetEditor`，面向小型 worksheet 的受限稀疏随机编辑。

当前能力的唯一事实源是 [docs/CURRENT_CAPABILITIES.md](docs/CURRENT_CAPABILITIES.md)。

## 安装

FastXLSX 导出 compiled CMake target：

```cmake
find_package(FastXLSX CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE FastXLSX::fastxlsx)
target_compile_features(your_target PRIVATE cxx_std_20)
```

vcpkg 默认 features 为：

- `runtime-minizip`：生产 reader/writer 的 stored + DEFLATE ZIP 支持。
- `images`：基于 stb 的 PNG/JPEG metadata、decode 和当前图片切片。

可关闭 `FASTXLSX_ENABLE_IMAGES`；public image symbols 仍可链接，但调用会抛 `FastXlsxError`，consumer 可通过 `FASTXLSX_HAS_IMAGES` 判断。`planned-xml` 只安装未来候选依赖，当前实现不链接它们。

## 快速开始

### Streaming

```cpp
#include <fastxlsx/fastxlsx.hpp>

int main()
{
    auto workbook = fastxlsx::WorkbookWriter::create("report.xlsx");
    auto sheet = workbook.add_worksheet("Data");
    sheet.append_row({
        fastxlsx::CellView::text("Name"),
        fastxlsx::CellView::number(1.0),
    });
    workbook.close();
}
```

Streaming 按 worksheet/row 顺序写入，不提供历史行随机修改。大规模有序导出优先使用该路径。

### Small new workbook

```cpp
#include <fastxlsx/fastxlsx.hpp>

int main()
{
    auto workbook = fastxlsx::Workbook::create();
    auto& sheet = workbook.add_worksheet("Summary");
    sheet.append_row({
        fastxlsx::Cell::text("Total"),
        fastxlsx::Cell::formula("SUM(B2:B10)"),
    });
    workbook.save("summary.xlsx");
}
```

`Workbook` 在保存前保留 row/cell 数据，适合小文件便利创建。

### Patch / In-memory

```cpp
#include <fastxlsx/fastxlsx.hpp>

int main()
{
    auto editor = fastxlsx::WorkbookEditor::open("template.xlsx");
    fastxlsx::DocumentProperties properties;
    properties.last_modified_by = "Report Service";
    properties.title = "Updated report";
    editor.set_document_properties(properties);
    auto sheet = editor.worksheet("Data");
    sheet.set_cell("A1", fastxlsx::CellValue::text("updated"));
    editor.save_as("output.xlsx");

    if (editor.has_pending_changes() && !editor.has_unsaved_changes()) {
        // staged Patch state 仍保留，但已包含在最近一次成功 save_as() 中。
    }
}
```

`has_pending_changes()` 表示当前仍保留 staged Patch/dirty session 状态；`has_unsaved_changes()` 和 `unsaved_change_count()` 表示相对最近一次成功 `save_as()` 的保存水位。`save_as()` 对 dirty In-memory session 使用 stage → package write → state commit；失败的 edit/save 不推进水位，也不清除 dirty session 诊断。

`WorksheetEditor` 默认使用 `WorksheetMaterializationPolicy::RejectKnownLosses`。遇到 rich text、phonetic/extension metadata、formula metadata 或 cached formula result 等当前模型不能无损表示的 source cell 时，materialization 会在注册 session 前失败。

```cpp
try {
    auto sheet = editor.worksheet("Data");
} catch (const fastxlsx::WorksheetMaterializationError& error) {
    const auto& diagnostic = error.diagnostic();
    // category、worksheet_name、1-based row/column；必要时含 sharedStrings index。
}
```

该异常仍派生自 `FastXlsxError`；已有通用 catch 不需要修改。`try_worksheet()` 对 strict rejection 同样抛出该异常，只有 worksheet 不存在时返回 `std::nullopt`。异常文本仅用于诊断，自动化逻辑应读取结构化字段。

只有明确接受丢失时才使用：

```cpp
fastxlsx::WorksheetEditorOptions options;
options.materialization_policy =
    fastxlsx::WorksheetMaterializationPolicy::AllowLossyProjection;
auto sheet = editor.worksheet("Data", options);
```

这会把受支持对象拍平为 plain text/formula text，丢弃的 source 语义不能由 `save_as()` 恢复。

## 能力边界

- `save_as()` 写到新路径，不是 atomic in-place save。
- `PackageReader`、`PackageEditor`、`EditPlan`、`DependencyAnalyzer`、`RelationshipGraph` 是 internal。
- 公式支持文本、审计、窄重写和请求重算；不求值、不生成 cached value、不完整重建 `calcChain.xml`。
- `replace_image()` 只替换已有 PNG/JPEG media bytes；不编辑 drawing/anchor/relationship。
- `set_document_properties()` 只重写 core/app docProps；不创建或编辑 custom properties。
- Preservation 证据不等于 tables、charts、comments、VBA、pivot 或 custom XML 的语义编辑。
- In-memory 是 small-file 稀疏编辑，不是 large-file low-memory random editing。

## 构建与测试

在 Visual Studio 2026 Developer Command Prompt：

```powershell
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

其他受维护 profile：

```powershell
# stored-only bootstrap（无 DEFLATE）
cmake --preset windows-nmake-release-stored

# production ZIP，关闭 images；CI 运行 disabled-feature smoke
cmake --preset windows-nmake-release-no-images
```

Benchmark 为 opt-in，不进入默认 CTest。release 可引用证据必须提交到 `benchmarks/evidence/<bundle>/` 并通过：

```powershell
py -3 tools/validate_benchmark_evidence.py --root benchmarks/evidence
```

## 文档

- [当前能力](docs/CURRENT_CAPABILITIES.md)
- [架构](docs/ARCHITECTURE.md)
- [编辑模型](docs/EDITING_MODEL.md)
- [API 设计](docs/API_DESIGN_AND_DOCUMENTATION.md)
- [测试流程](docs/TESTING_WORKFLOW.md)
- [性能目标](docs/PERFORMANCE_TARGETS.md)
- [执行队列](docs/TASK_BREAKDOWN.md)
