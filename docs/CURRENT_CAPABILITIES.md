# 当前能力

本文是 FastXLSX **Public API / Internal Foundation / Planned / Explicit Non-goals** 的唯一当前事实源。判断顺序为 public headers → source → tests → 本文；测试 fixture 和 internal hook 不自动形成用户承诺。

## Public API

### Streaming：新建大型 workbook

- `WorkbookWriter`、`WorksheetWriter`、`CellView`。
- 按 worksheet/row 顺序输出，适合大型有序导出。
- 支持当前 public headers 中的数字、文本、布尔、公式、显式 blank cell 和稀疏行写入，以及 string strategy、style registry、Excel 1900 date/time serial helper、number-format preset、worksheet metadata、table、conditional formatting 和 PNG/JPEG insertion 窄切片。
- Date/time helper 只生成 numeric cell 所需的 1900 date-system serial；调用方仍需显式注册并附加 number-format style，不支持 1904 date system，也不推断时区。
- 不支持历史行随机修改；不得引入 worksheet DOM 或 dense matrix。
- `StringStrategy::InlineString` 是默认低内存策略；`SharedString` 会保留 workbook 级唯一字符串表，仅适合调用方已知文本高度重复的 workload。当前不提供需要回看、重写或无界缓存的自动策略。
- Worksheet body 使用 256 KiB 有界 batching 写入 file-backed entry；成功 `WorkbookWriter::close()` 后立即删除 worksheet/image 临时文件并释放 row/body buffer、sharedStrings 与 style registry，写包失败则保留状态以支持 retry。

### Small new workbook

- `Workbook`、`Worksheet`、`Cell`。
- 保存前在内存中保留已追加 row/cell，适合小文件便利创建。
- 不是 existing-workbook editor，也不是 Streaming 性能路径。

### Patch：已有 workbook

- `WorkbookEditor::open()` / `save_as()`；`WorkbookEditorSaveOptions::zip_compression_level` 可为 `-1`、`0` 或 `1..9`。
- 支持 sheet catalog 查询、`replace_sheet_data()`、targeted cell patch、窄 sheet rename、formula audit/recalculation request、core/app document properties rewrite 和已有 PNG/JPEG media bytes replacement。
- 未修改和未知 package part 默认 copy-original；修改 part 才 rewrite/remove。
- `save_as()` 不覆盖 source，也不承诺 atomic in-place save。无 options overload 为兼容保留 stored output；显式 save options 的 `0` 为 stored、`-1` 为 active backend default、`1..9` 为 minizip-ng DEFLATE。无效/不可用配置在 dirty session staging 前失败并保留 retry 状态。
- Production minizip-ng 输出对 source/output compression method 匹配的未修改 entry 使用 raw compressed-payload copy，并保留 exact source compressed payload bytes、logical size 与 CRC；不同 DEFLATE level 仍是 method 匹配，因此请求 level 只重新编码 rewritten/generated entries。该路径不复制 source local header、central-directory record、extra fields 或整包布局，也不会仅为重新校验 unchanged payload CRC 而 inflate；损坏的未读取 source payload 会按 preservation 语义原样保留。Stored bootstrap、method-changing save 和 rewritten entry 走既有 logical/encoding 路径。
- `has_pending_changes()` 表示 retained staged state；成功保存后仍可为 true。
- `has_unsaved_changes()` / `unsaved_change_count()` 表示相对最近一次成功 `save_as()` 的 watermark；成功保存清零，失败 edit/save 不改变。
- Dirty In-memory session 在 package write 前 stage 到 Patch plan，但只在输出成功后提交 handoff 并清除 dirty；写出失败保留 session diagnostics/counts，retry 会用当前值覆盖失败尝试留下的 stale internal projection。
- `request_full_calculation()` 对 workbook XML、calcChain、relationships、content types、manifest 和 edit plan 使用事务式 staging；提交前失败保留调用前计划与输出语义，后续可安全重试。
- `rename_sheet()` 在发布前完成 PackageEditor state、public catalog、pending payload、materialized formula rewrite 和 targeted-cell diagnostics 的 staging；提交前失败不泄漏部分 rename，保留既有 patch 并可安全重试。
- `set_document_properties()` 事务式重写 `docProps/core.xml`、`docProps/app.xml` 及缺失的 root relationships/content types；重复调用 last-write-wins，失败保留既有 Patch plan、pending/unsaved 水位与 retry 能力。Custom properties 和未知 entries 保留，但不提供 custom-properties 对象模型。
- 对 DEFLATE source、无 worksheet relationships、目标已存在且具有 top-level dimension 的 strict `replace_cells()`，会将 worksheet 单次 inflate 到 PackageEditor-owned 临时文件，再以 target-only scan + file ranges staging。Missing-cell upsert、relationship-bearing worksheet 与其他 direct-range 不适用场景使用单次 source-order transform，在同一扫描中完成 replacement/insertion、精确 dimension、relationship audit 和 telemetry，再以 file ranges + 小型 memory chunk staging。两条路径都不物化 worksheet DOM/dense matrix；临时文件所有权与 package state 同事务提交，重复 rewrite 提交后立即回收不再引用的旧临时文件，提交前失败由 RAII 清理新资源并保留旧状态。

### In-memory：小型 worksheet

- `WorkbookEditor::worksheet()` / `try_worksheet()` 返回 borrowed `WorksheetEditor`。
- 使用稀疏 `CellStore`，由 `max_cells` 和 `memory_budget_bytes` guardrail 控制。
- 默认 `WorksheetMaterializationPolicy::RejectKnownLosses`：引用到 rich text、phonetic/extension metadata、formula metadata 或 cached formula result 时，在 session 注册前失败且不污染状态。
- Strict rejection 抛出 `WorksheetMaterializationError`，通过 `WorksheetMaterializationDiagnostic` 提供稳定 loss category、planned worksheet name、1-based row/column，以及可选 zero-based sharedStrings index；无需解析异常文本或依赖 internal parser 状态。
- `worksheet()` 与 `try_worksheet()` 都传播该 typed exception；后者仅在 worksheet 名称不存在时返回 `std::nullopt`。Policy mismatch、malformed XML 和其他加载失败仍是通用 `FastXlsxError`。
- `AllowLossyProjection` 是显式 opt-in，会把支持的 source cell 投影为 plain text/formula text；丢弃语义不可恢复。
- 同一 materialized session 的 policy/guardrail 必须匹配，不能静默切换契约。
- Dirty save 仅在 workbook 具有单一有效 sharedStrings relationship/content type，且 target 经合法 percent-decoding 后可规整为包内 part 时做 append-only sharedStrings projection；重复、外部、fragment/query、非法 percent escape、错误 content type、缺失或 malformed sharedStrings 等不安全元数据会回退为 inline strings，并保留原 relationship/content type/part 状态而不做 repair。
- `WorksheetEditor::insert_rows()` / `delete_rows()` / `insert_columns()` / `delete_columns()` 只移动或删除已表示的 sparse cell 记录；会随记录保留 `CellValue` 和 materialized source `StyleId`，并对所有 surviving formula records 使用同一窄 structural rewriter，只调整插入/删除轴实际影响的引用，而不会因 formula cell 自身移动而套用 copy/move delta translation。Dirty `save_as()` 刷新 sparse dimension；失败必须保留 dirty diagnostics/counts 且可重试。它不是 worksheet metadata repair、sharedStrings/styles 全量迁移、relationships/drawings/tables 修复或 calcChain rebuild。
- `WorksheetEditor::copy_cells()` 在同一 materialized worksheet 内执行 sparse overlay copy：只复制 source 范围中已表示的记录，按目标左上角位移公式文本并保留 workbook-local `StyleId`；source 空洞不清除目标记录，重叠复制读取稳定 pre-edit snapshot。目标 footprint 越界或 CellStore guardrail 失败时不发布部分状态。它不复制或同步 row/column metadata、merged cells、tables、filters、validations、conditional formatting、hyperlinks、drawings/charts/VBA、defined names、relationships、sharedStrings/styles metadata 或 calcChain，不是完整 Excel copy/paste。
- `WorksheetEditor::copy_cells_from()` 将同一 `WorkbookEditor` 中另一个已 materialize worksheet 的当前 sparse snapshot overlay 到目标 session：跨 sheet 相同坐标仍执行复制，source 保持不变，仅 destination 在有效复制后 dirty；目标 guardrail 失败或不同 owner handle 拒绝不发布部分状态。CellValue、公式窄位移和同 workbook 已验证 StyleId 的语义与 `copy_cells()` 一致，但不克隆 worksheet 或 linked metadata，也不接受跨 workbook source。
- `WorksheetEditor::copy_cell_values()` / `copy_cell_values_from()` 提供 same-sheet 与同 workbook cross-sheet 的 sparse value-only overlay：复制 represented source payload 并按坐标位移公式，但已有 mapped target 保留 pre-edit StyleId，missing target 插入为 unstyled，source StyleId 不复制。重叠读取稳定 source/value 与 target-style snapshot，目标 bounds/guardrail 失败不发布部分状态；不迁移 sharedStrings/styles metadata 或 linked objects。
- `WorksheetEditor::move_cell_values()` / `move_cell_values_from()` 提供 same-sheet 与同 workbook cross-sheet 的 sparse value-only move：represented source records 先按 `clear_cell_value()` 语义变成显式 blank 并保留 source StyleId，再把 pre-edit payload/formula overlay 到 mapped destination，因此 same-sheet 重叠 target 可以覆盖 source blank；destination 保留 pre-edit StyleId，missing target 插入为 unstyled。Same-sheet 使用单候选 CellStore，cross-sheet 使用双候选 + noexcept swap；bounds/guardrail/owner/session 失败不发布半边状态。它不移动 style handles、worksheet metadata 或 linked objects，也不迁移 sharedStrings/styles metadata。
- `WorksheetEditor::move_cells()` 复用同一 sparse transfer preflight：从稳定 snapshot 读取 source 已表示记录，在候选 store 中删除这些 source records，再以公式位移和原 StyleId overlay 到目标；重叠移动不会被中途写入污染，source/target 越界或 guardrail 失败不发布 source 删除或目标写入。目标中没有对应 source record 的现有记录默认保留，但位于 source 范围内的 represented records 仍按 move 语义移除。它不是完整 Excel cut/paste，也不移动或同步上述 worksheet metadata/linked objects。
- `WorksheetEditor::move_cells_from()` 在同一 `WorkbookEditor` 的两个已 materialize sessions 间移动 represented sparse records：先独立构造并验证 source-removal 与 destination-overlay CellStore candidates，再通过 noexcept swap 同时发布；目标 guardrail、formula translation、owner/session 或 bounds 失败时两边 active state 都不变。有效移动使两个 sessions 同时 dirty，失败 `save_as()` 保留双边状态供重试。它不是 worksheet relocation 或完整跨 sheet cut/paste，不移动 linked metadata。
- `WorksheetEditor::copy_cell_style()` / `copy_cell_styles()` / `clear_cell_style()` / `clear_cell_styles()` 提供同一 materialized worksheet 内的窄 style-only mutation：复制只复用 materialization 已从 source styles.xml 校验过的 workbook-local StyleId，单 cell source/target 和 range copy 的每个 represented source 对应 target 都必须已表示，unstyled source 会清除 target style，重叠 range copy 使用稳定 source-style snapshot 且缺失 mapped target 不发布部分状态；clear 对 missing/unstyled target clean no-op。它们保留目标 CellValue、cell count 和 styles.xml bytes，不接受任意 caller StyleId，不创建、合并、验证或迁移 style table，也不是 conditional/theme formatting 或 dense range-style API。
- `WorksheetEditor::move_cell_style()` / `move_cell_styles()` 在同一 materialized worksheet 内移动 represented source 的 optional StyleId：先在候选状态清除参与 source styles，再把稳定 pre-edit style snapshot overlay 到已表示的 mapped targets，因此重叠移动确定、unstyled source 会清除目标样式且所有 CellValue/cell count 保持不变。Missing mapped target、bounds 或 CellStore guardrail 失败不发布部分 source clear/target update；dirty save 只改变相关 `s` attributes 并保留 styles.xml bytes。它不是 value/formula move、style creation/registry、cross-sheet/cross-workbook style migration 或 dense formatting。
- `WorksheetEditor::copy_cell_styles_from()` 将同一 `WorkbookEditor` 中另一个已 materialize worksheet 的 current source-style snapshot 映射到目标 represented records；source 保持不变，跨 sheet 相同坐标仍执行样式比较与应用，missing mapped target 或不同 owner 拒绝不发布部分状态。目标 CellValue、cell count 和 styles.xml bytes 保持不变，不接受跨 workbook source，也不形成 style table migration。
- `WorksheetEditor::move_cell_styles_from()` 在同一 `WorkbookEditor` 的两个 materialized sessions 间移动 represented optional StyleId：先冻结 source snapshot 并 preflight 所有已表示 mapped targets，再分别构造 source-clear 与 destination-overlay candidates；两边 guardrail 均通过后才以 noexcept swap 发布实际变化的 session。Unstyled source 可只让 destination dirty，同坐标跨 sheet 仍执行移动，失败 save 保留双边 retry 状态；所有 CellValue/cell count/styles.xml bytes 保持不变。它不接受跨 owner handle，不移动 linked metadata，也不形成 style table migration。
- 不是任意 worksheet XML 的无损模型，也不是 large-file low-memory random editor。

### Images

- 默认 `images` feature / `FASTXLSX_ENABLE_IMAGES=ON` 使用 stb。
- `FASTXLSX_ENABLE_IMAGES=OFF` 时 public symbols 仍存在，但调用抛 `FastXlsxError`；`FASTXLSX_HAS_IMAGES=0` 向 consumer 传播。
- `WorksheetWriter::add_image()` 是 new-workbook insertion；`WorkbookEditor::replace_image()` 只替换已有 media bytes。两者都不是完整 drawing 编辑。

### ZIP backend

- Production/default profile 启用 `runtime-minizip` 和 `FASTXLSX_ENABLE_MINIZIP_NG=ON`，支持 stored + DEFLATE package 读写。
- `windows-nmake-release-stored` 是显式 bootstrap profile，只支持 stored entries；public Patch save 的正数 DEFLATE level 会在状态 staging 前拒绝。

## Internal Foundation

以下类型/模块不是 public API：

- `PackageReader`、`PackageEditor`、`PackageWriter`。
- `EditPlan`、dependency analysis、relationship graph、part index。
- worksheet transformer/event reader、`CellStore`、materialized session registry。
- package-entry chunk/file-backed helpers 和测试 hook。
- Internal `PackageEditor::set_document_properties()` 是 public `WorkbookEditor::set_document_properties()` 使用的 core/app docProps 事务式 staging foundation；internal plan、part name 和 relationship mutation 仍不公开。
- Internal `PackageEditor::remove_part()` 对 edit plan、part/entry replacements、omitted entries、content types 和 manifest 使用事务式 staging；默认只审计并保留 inbound relationships，不是 public 对象删除 API 或自动 orphan cleanup。
- Internal materialized `PackageEditor::replace_part()` 对 small XML replacement、part restore、content types、owned relationships audit、omitted entries 和 manifest 使用事务式 staging；它不是 public arbitrary-part mutation API，也不接受 worksheet 或 stream-rewrite payload。
- Internal non-worksheet `PackageEditor::replace_part_chunks()` 对 stream-rewrite chunks、part restore、edit plan、part/entry replacements、omitted entries 和 manifest 使用事务式 staging；它是 package writer 的 internal handoff，不是 public arbitrary-part streaming API。
- Internal worksheet chunk replacement 对 worksheet/workbook rewrite、calc metadata、relationship/content-type side effects、edit plan、part/entry replacements、omitted entries、manifest 和 file-backed 临时文件所有权使用统一 staging，并在提交前完成 routing notes 与 dependency audits；完整 worksheet chunk-source wrapper 只在 `noexcept` commit 时发布临时文件所有权，提交前失败由 RAII 删除未发布 staged file。失败保留既有计划、输出语义和 retry 能力。它仍是 internal worksheet rewrite foundation，不是 public arbitrary worksheet XML API。
- Internal bounded sheetData replacement 会把最终 `LocalDomRewrite` mode、file-backed staged output ownership、preservation/dependency audits 与 direct/by-name notes 纳入同一 worksheet transaction；提交前失败删除临时输出并保留调用前 package state。它仍不是 large-file transformer、任意 cell random editing 或 linked metadata repair。
- Internal worksheet cell replacement 的 single-pass fallback 会把 file-backed output ownership、精确 dimension、relationship audit、scan/match/insert telemetry 与 transform notes 纳入同一 staged transaction；事件输出先进入 256 KiB 有界 batch，再按 batch 做 relationship scan 与 temporary-file write，transformer 已解析的 source coordinate 直接传给 dimension/audit，避免重复解析。Indexed direct-range fast path 支持 stored source package ranges，以及 DEFLATE source 的单次 inflate + owned temporary-file ranges。两条路径的提交前失败都保留调用前状态与 retry 能力；事务提交会删除已被新 replacement 取代且不再引用的旧临时文件。

文档只能在明确的 internal/architecture 语境提及它们。

## Performance Evidence

- tracked benchmark evidence 机制已建立；当前有 4 个 production Streaming bundle、6 个 production Patch bundle 与 1 个 OpenXLSX reference bundle。每个 bundle 只支持 manifest 限定的单机同数据集结论，warm-up/measured 次数以各自 run context 为准。
- 最新 compression profile 在同机 1,000,000-cell numeric/mixed InlineString workload 中，Streaming level 1 median 为 322/406 ms、level 6 为 955/981 ms；level 1 输出增大 9.62%/21.63%，全部 measured process peak working set 为 6.28125–6.32812 MB。该结论不泛化到其他数据分布。
- 最新 Patch event/action profile 中，5,000,000-cell numeric level 1 upsert 的 total/mutation/transform/residual median 为 8004/6210/5180.113/4059.595 ms，较紧邻同协议 bundle 分别降低 5.77%/10.30%/11.53%/14.21%，process peak working set 为 8.79297 MB，owned output buffer 保持 262,144 bytes。1,000,000-cell profile 另覆盖 numeric、mixed inline、sharedStrings、formula metadata 与 external hyperlink relationship；这些数据只覆盖记录的顺序 upsert workload，不是大文件任意随机编辑承诺。
- 同机 1,000,000-cell numeric/mixed public writer workload 中，FastXLSX Streaming median 为 1583/1248 ms 与 6.87109/6.88672 MB peak working set，OpenXLSX 0.4.1 workbook API 为 3180/3292 ms 与 395.258/403.957 MB。该证据只说明两个已记录 workload 的 2.01×/2.64×吞吐比，不形成全功能或跨机器“总体超越”承诺。

## Planned

- 扩展 existing-workbook object semantics 前，必须逐对象定义 preserve/audit/fail/edit 和 relationship/content-type side effects。
- 大 worksheet 低内存 rewrite 是独立路径，不通过扩大 `WorksheetEditor` 实现。
- `planned-xml` 中的 zlib-ng、Expat、pugixml 当前未被实现链接；manifest presence 不等于当前能力。

## Explicit Non-goals

- 公式求值、cached value 生成、完整 `calcChain.xml` rebuild。
- Atomic in-place save。
- Zip64、multi-disk ZIP，以及接近/超过 ZIP32 package 边界的读写。
- Large-file low-memory random editing。
- 完整 tables/drawings/charts/comments/VBA/pivot/external links/custom XML 语义编辑。
- 因 preservation 测试而宣称上述对象可编辑。
- 因 benchmark instrumentation、单机矩阵或局部架构优势而宣称泛化“高性能/低内存”。

## 事实验证

- Public surface：`include/fastxlsx/`。
- 实现：`src/`。
- Build profiles：`CMakeLists.txt`、`CMakePresets.json`、`vcpkg.json`。
- 行为证据：`tests/` 与 CTest。
- 性能证据：`benchmarks/evidence/` 中通过 validator 的 bundle。
