# 当前能力

本文是 FastXLSX **Public API / Internal Foundation / Planned / Explicit Non-goals** 的唯一当前事实源。判断顺序为 public headers → source → tests → 本文；测试 fixture 和 internal hook 不自动形成用户承诺。

## Public API

### Streaming：新建大型 workbook

- `WorkbookWriter`、`WorksheetWriter`、`CellView`。
- 按 worksheet/row 顺序输出，适合大型有序导出。
- 支持当前 public headers 中的 cell、string strategy、style registry、worksheet metadata、table、conditional formatting 和 PNG/JPEG insertion 窄切片。
- 不支持历史行随机修改；不得引入 worksheet DOM 或 dense matrix。

### Small new workbook

- `Workbook`、`Worksheet`、`Cell`。
- 保存前在内存中保留已追加 row/cell，适合小文件便利创建。
- 不是 existing-workbook editor，也不是 Streaming 性能路径。

### Patch：已有 workbook

- `WorkbookEditor::open()` / `save_as()`。
- 支持 sheet catalog 查询、`replace_sheet_data()`、targeted cell patch、窄 sheet rename、formula audit/recalculation request 和已有 PNG/JPEG media bytes replacement。
- 未修改和未知 package part 默认 copy-original；修改 part 才 rewrite/remove。
- `save_as()` 不覆盖 source，也不承诺 atomic in-place save。
- `has_pending_changes()` 表示 retained staged state；成功保存后仍可为 true。
- `has_unsaved_changes()` / `unsaved_change_count()` 表示相对最近一次成功 `save_as()` 的 watermark；成功保存清零，失败 edit/save 不改变。
- Dirty In-memory session 在 package write 前 stage 到 Patch plan，但只在输出成功后提交 handoff 并清除 dirty；写出失败保留 session diagnostics/counts，retry 会用当前值覆盖失败尝试留下的 stale internal projection。
- `request_full_calculation()` 对 workbook XML、calcChain、relationships、content types、manifest 和 edit plan 使用事务式 staging；提交前失败保留调用前计划与输出语义，后续可安全重试。
- `rename_sheet()` 在发布前完成 PackageEditor state、public catalog、pending payload、materialized formula rewrite 和 targeted-cell diagnostics 的 staging；提交前失败不泄漏部分 rename，保留既有 patch 并可安全重试。

### In-memory：小型 worksheet

- `WorkbookEditor::worksheet()` / `try_worksheet()` 返回 borrowed `WorksheetEditor`。
- 使用稀疏 `CellStore`，由 `max_cells` 和 `memory_budget_bytes` guardrail 控制。
- 默认 `WorksheetMaterializationPolicy::RejectKnownLosses`：引用到 rich text、phonetic/extension metadata、formula metadata 或 cached formula result 时，在 session 注册前失败且不污染状态。
- Strict rejection 抛出 `WorksheetMaterializationError`，通过 `WorksheetMaterializationDiagnostic` 提供稳定 loss category、planned worksheet name、1-based row/column，以及可选 zero-based sharedStrings index；无需解析异常文本或依赖 internal parser 状态。
- `worksheet()` 与 `try_worksheet()` 都传播该 typed exception；后者仅在 worksheet 名称不存在时返回 `std::nullopt`。Policy mismatch、malformed XML 和其他加载失败仍是通用 `FastXlsxError`。
- `AllowLossyProjection` 是显式 opt-in，会把支持的 source cell 投影为 plain text/formula text；丢弃语义不可恢复。
- 同一 materialized session 的 policy/guardrail 必须匹配，不能静默切换契约。
- 不是任意 worksheet XML 的无损模型，也不是 large-file low-memory random editor。

### Images

- 默认 `images` feature / `FASTXLSX_ENABLE_IMAGES=ON` 使用 stb。
- `FASTXLSX_ENABLE_IMAGES=OFF` 时 public symbols 仍存在，但调用抛 `FastXlsxError`；`FASTXLSX_HAS_IMAGES=0` 向 consumer 传播。
- `WorksheetWriter::add_image()` 是 new-workbook insertion；`WorkbookEditor::replace_image()` 只替换已有 media bytes。两者都不是完整 drawing 编辑。

### ZIP backend

- Production/default profile 启用 `runtime-minizip` 和 `FASTXLSX_ENABLE_MINIZIP_NG=ON`，支持 stored + DEFLATE package 读写。
- `windows-nmake-release-stored` 是显式 bootstrap profile，只支持 stored entries。

## Internal Foundation

以下类型/模块不是 public API：

- `PackageReader`、`PackageEditor`、`PackageWriter`。
- `EditPlan`、dependency analysis、relationship graph、part index。
- worksheet transformer/event reader、`CellStore`、materialized session registry。
- package-entry chunk/file-backed helpers 和测试 hook。
- Internal `PackageEditor::set_document_properties()` 支持 core/app docProps 与相关 content types/root relationships 的窄事务式 rewrite；它不是 `WorkbookEditor` public API。
- Internal `PackageEditor::remove_part()` 对 edit plan、part/entry replacements、omitted entries、content types 和 manifest 使用事务式 staging；默认只审计并保留 inbound relationships，不是 public 对象删除 API 或自动 orphan cleanup。
- Internal materialized `PackageEditor::replace_part()` 对 small XML replacement、part restore、content types、owned relationships audit、omitted entries 和 manifest 使用事务式 staging；它不是 public arbitrary-part mutation API，也不接受 worksheet 或 stream-rewrite payload。

文档只能在明确的 internal/architecture 语境提及它们。

## Planned

- 扩展 existing-workbook object semantics 前，必须逐对象定义 preserve/audit/fail/edit 和 relationship/content-type side effects。
- 大 worksheet 低内存 rewrite 是独立路径，不通过扩大 `WorksheetEditor` 实现。
- `planned-xml` 中的 zlib-ng、Expat、pugixml 当前未被实现链接；manifest presence 不等于当前能力。
- tracked benchmark evidence 机制已建立；当前为 0 bundle，没有可供 release 引用的性能结论。

## Explicit Non-goals

- 公式求值、cached value 生成、完整 `calcChain.xml` rebuild。
- Atomic in-place save。
- Large-file low-memory random editing。
- 完整 tables/drawings/charts/comments/VBA/pivot/external links/custom XML 语义编辑。
- 因 preservation 测试而宣称上述对象可编辑。
- 因 benchmark instrumentation 或单个本地结果而宣称泛化“高性能/低内存”。

## 事实验证

- Public surface：`include/fastxlsx/`。
- 实现：`src/`。
- Build profiles：`CMakeLists.txt`、`CMakePresets.json`、`vcpkg.json`。
- 行为证据：`tests/` 与 CTest。
- 性能证据：`benchmarks/evidence/` 中通过 validator 的 bundle。
