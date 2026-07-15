# FastXLSX

FastXLSX 是一个 C++20 XLSX 创建与编辑库，优先支持 MSVC 2026。项目共享 OpenXML/OPC 底座，但公开三条边界清晰的路径：

当前版本为 **v0.1.0 Preview**。Public API/ABI 尚未承诺稳定，`0.x` 升级后应重新编译 consumer；首版支持边界以 [当前能力](docs/CURRENT_CAPABILITIES.md) 和 [Changelog](CHANGELOG.md) 为准。

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

Streaming 按 worksheet/row 顺序写入，不提供历史行随机修改。大规模有序导出优先使用该路径；worksheet XML 使用 256 KiB 有界 body batching 降低小写入调用，成功 `close()` 后立即释放临时文件与构建期缓存。

字符串策略会直接影响吞吐、内存和文件大小：默认 `InlineString` 不维护 workbook 级唯一字符串表，适合字符串基数未知或接近全唯一的大型导出；只有调用方已知文本高度重复时，才应评估 `SharedString`。Streaming 不提供 `Auto` 策略，因为在单遍写入中准确判断未来字符串基数需要无界缓存、回看或重写，会破坏当前低内存边界。

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
    fastxlsx::WorkbookEditorSaveOptions save_options;
    save_options.zip_compression_level = 1;
    editor.save_as("output.xlsx", save_options);

    if (editor.has_pending_changes() && !editor.has_unsaved_changes()) {
        // staged Patch state 仍保留，但已包含在最近一次成功 save_as() 中。
    }
}
```

`has_pending_changes()` 表示当前仍保留 staged Patch/dirty session 状态；`has_unsaved_changes()` 和 `unsaved_change_count()` 表示相对最近一次成功 `save_as()` 的保存水位。`save_as()` 对 dirty In-memory session 使用 stage → package write → state commit；失败的 edit/save 不推进水位，也不清除 dirty session 诊断。

`save_as(path)` 为兼容既有行为写 stored ZIP；需要 production DEFLATE 时使用 `WorkbookEditorSaveOptions`。`zip_compression_level=-1` 选择 active backend 默认值，`1..9` 请求 DEFLATE，`0` 明确请求 stored output。Production minizip-ng 输出会对 compression method 匹配的未修改 entry 直接复制 source compressed payload；不同 DEFLATE level 仍属于同一 method，因此请求 level 只作用于 rewritten/generated entries。Stored 或 method-changing 输出走 logical copy。该优化不改变 Patch preservation 语义。

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

- `save_as()` 写到新路径，不是 atomic in-place save。Production minizip-ng 对 method 匹配的未修改 entry 保留 exact compressed payload bytes；它不保留 source local header、central-directory record、extra fields 或整包布局。Rewritten entry、stored bootstrap 和 method-changing save 正常重新编码。
- 当前 reader/writer 不支持 Zip64 或 multi-disk ZIP；单 entry 超过 ZIP32 size、entry count 超过 ZIP32 上限或 source 使用 Zip64 时会拒绝，不承诺接近/超过 4 GiB 的 XLSX。
- `PackageReader`、`PackageEditor`、`EditPlan`、`DependencyAnalyzer`、`RelationshipGraph` 是 internal。
- 公式支持文本、审计、窄重写和请求重算；不求值、不生成 cached value、不完整重建 `calcChain.xml`。
- `replace_image()` 只替换已有 PNG/JPEG media bytes；不编辑 drawing/anchor/relationship。
- `set_document_properties()` 只重写 core/app docProps；不创建或编辑 custom properties。
- Preservation 证据不等于 tables、charts、comments、VBA、pivot 或 custom XML 的语义编辑。
- In-memory 是 small-file 稀疏编辑，不是 large-file low-memory random editing。

## 性能现状

- **创建**：production Streaming 使用 256 KiB 有界 body batching、file-backed worksheet body、chunked package entry 和 minizip-ng DEFLATE；成功关闭后释放 worksheet 临时文件和构建期缓存。最新同机 warmed compression profile 每个场景写入 1,000,000 cells：numeric/mixed-inline 的 level 1 median 为 0.322/0.406 秒，level 6 为 0.955/0.981 秒；level 1 输出分别增大 9.62%/21.63%，全部 measured process peak working set 位于 6.28–6.33 MB。吞吐优先的这两个 workload 推荐先评估 level 1，文件大小优先再提高 level。
- **字符串选型**：在最新矩阵中，20% mixed workload 的 `InlineString`/`SharedString` median 为 1.248/1.359 秒；1,000,000 个字符串全部唯一时，`InlineString` 为 1.498 秒与 6.92578 MB，`SharedString` 为 3.356 秒与 123.039 MB。因此默认保持 `InlineString`，只有调用方已知文本高度重复时才评估 `SharedString`。
- **已有文件编辑**：Patch 按 part-level rewrite 工作，未修改 part 默认 copy-original。最新同机 raw-copy profile 中，1,000,000-cell source 的 no-op save raw-copy 7 个 entry / 3,128,791 compressed bytes，level 1/3/6 median save 为 19/21/25 ms；仅改 document properties 时 raw-copy 5 个 entry / 3,128,295 bytes，median save 为 15/24/41 ms。Exact compressed-payload equality 和 12 个代表输出的 openpyxl reopen 均由矩阵验证。
- **OpenXLSX 对比**：同机、同 1,000,000-cell numeric/mixed 数据分布与 public writer API 协议下，FastXLSX Streaming median 为 1.583/1.248 秒，OpenXLSX 0.4.1 workbook API 为 3.180/3.292 秒，即该两个 workload 的吞吐约为 2.01×/2.64×；FastXLSX peak working set 为 6.87/6.89 MB，OpenXLSX 为 395.26/403.96 MB。OpenXLSX 使用 `document.save()` 默认设置，FastXLSX 使用 DEFLATE level 6，输出大小同时记录，因此这不是所有功能、机器或 backend 的泛化胜出声明。
- **有界高吞吐 Patch**：对 DEFLATE source、无 worksheet relationships、目标已存在且具有 top-level dimension 的 strict targeted replace，路径只 inflate 一次到事务式临时文件，再做 target-only scan 和 file-range staging。Missing-cell upsert、relationship-bearing worksheet 与其他 fallback 使用单次 source-order scan，并以 256 KiB 有界 output batching 合并事件碎片后完成 relationship scan 与临时文件写入；两条路径都不构建 worksheet DOM/dense matrix。
- **Patch rewrite 现状**：最新同机 schema-v10/v4 profile 中，1,000,000-cell numeric/mixed-inline/formula + external hyperlink level-1 upsert 的 total median 为 1.075/1.205/1.007 秒，transform median 为 0.611/0.691/0.591 秒，较紧邻同协议 baseline 分别降低 51.18%/33.13%/25.85% 和 55.56%/34.50%/29.14%。Median process peak working set 为 8.816/9.832/8.820 MB，每类 profile 至少合并 999,982 个完整 source cell、最多 18 个 fallback；这些结果只证明记录 workload 的顺序 rewrite 与有界内存，不是任意随机编辑承诺。
- **Patch compression 选型**：最新同机 schema-v9 profile 中，1,000,000-cell numeric/mixed-inline upsert 的 level 1 median save 为 0.488/0.408 秒，level 6 为 1.033/1.244 秒；level 1 输出增大 9.73%/14.51%，process peak working set median 约 8.84/9.81 MB。吞吐优先先显式选择 level 1；文件大小优先再提高 level。Public 默认保持兼容，当前证据不支持直接切换 backend 或引入并行压缩。
- **Patch 下一瓶颈**：在吞吐配置 level 1 下，三类 profile 的 source scan/action median 仍约 0.574–0.668 秒，transform action 与 output append 仍约 120 万次；下一轮先用 profile 区分 parser scan、action dispatch 与 file-range emission，再决定是否做 row/pass-through run 合并。只有 size-oriented workload 在 matched level 下仍受压缩限制时，才评估其他 backend。
- **随机编辑/处理**：`WorksheetEditor` 有意限定为 small-file sparse editing。大型 worksheet 的顺序 Patch 可以使用上述 file-backed bounded path；任意 random editing 仍需要独立设计，不能通过放宽 In-memory guardrail 冒充高性能。

精确环境、重复测量协议、min/median/max 和验证结果见 [性能目标与证据](docs/PERFORMANCE_TARGETS.md)。

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
