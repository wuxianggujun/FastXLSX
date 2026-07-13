# 编辑模型

FastXLSX 将三条路径分开，避免便利 API 破坏 Streaming 热路径或 existing-file preservation。

## Streaming

```text
caller rows -> CellView encoding -> worksheet stream -> package writer -> new XLSX
```

- 新建 workbook。
- 顺序 row/cell 输出。
- 大型 worksheet 不进入 DOM/dense matrix。
- close/save 阶段完成 package assembly。

## Patch

```text
source package -> part index/relationships -> staged edits -> part-level rewrite -> save_as
```

- Public facade 是 `WorkbookEditor`。
- Unchanged/unknown part 默认 copy-original。
- Production minizip-ng 且 source/output compression method 匹配时，unchanged entry 直接复制 exact compressed payload；stored bootstrap、method-changing 与 changed entries 走 logical/encoding 路径。该优化不保留完整 ZIP record 或 package layout。
- Changed part 选择 stream rewrite、small-part rewrite 或 remove。
- Relationship-free DEFLATE worksheet 的 strict existing-cell replace 可走 one-inflate target-only direct-range：解压后的 worksheet 放在 owned temporary file，未触碰 XML 以 file ranges replay，replacement payload 使用小型 memory chunks。
- Missing-cell upsert、relationship-bearing worksheet 与其他 direct-range 不适用场景走 single-pass source-order transform：一次扫描完成 replacement/insertion、精确 dimension、relationship audit 和 telemetry，输出由 transformed temporary file ranges 与 bounded dimension memory chunk 组成。该路径同样不物化 DOM/dense matrix，但仍重写完整 worksheet part。
- 每个功能必须明确 preserve/audit/fail/edit，以及 relationships/content types/calc metadata 联动。
- `save_as()` 成功后 staged plan 可继续用于另一个输出或后续编辑；因此 `has_pending_changes()` 不等于“未保存”。
- `has_unsaved_changes()` 是保存水位：只表示最近一次成功保存之后的新变化。

## In-memory

```text
source worksheet events -> strict/lossy projection -> sparse CellStore -> edits -> Patch stage -> package write -> state commit
```

- Public facade 是 borrowed `WorksheetEditor`。
- 适合 small-file random editing；由 cell count 与 estimated memory guardrail 限制。
- 默认 strict：`RejectKnownLosses` 在 session 注册前拒绝 rich/phonetic/extension、formula metadata 和 cached result 等已知损失。
- Strict loss 通过 `WorksheetMaterializationError` 暴露稳定 category 与 worksheet/cell/sharedStrings context；public diagnostic 不携带 XML token、part name、relationship id 或 parser state。
- 显式 `AllowLossyProjection` 才允许拍平为 plain text/formula text。
- Policy 是 session identity 的一部分；重复获取必须使用相同 policy/guardrail。
- Dirty session 在 `save_as()` 写包前 stage 到 Patch plan；只有 package write 成功后才提交 public handoff、清除 dirty 并推进 watermark。失败 retry 会按当前 CellStore 重建 projection，覆盖失败尝试留下的 stale internal stage。
- `copy_cells()` 是同一 session 内的 sparse overlay：source 已表示记录先从稳定快照复制到目标 footprint，内部样式句柄随记录保留，公式使用窄 A1 translator 按位移重写；source 空洞不代表目标删除。全部 mapped records 已与目标相同则 clean no-op；否则候选 sparse map 通过 guardrail 后才替换 active store，因此越界和预算失败不发布部分复制。
- `move_cells()` 使用同一 transfer staging，但在候选 map 中先移除 snapshot 中的 represented source records，再 overlay 目标记录；重叠 source/target 不读取中途状态，失败不发布部分 source removal 或 target write。它仍只移动 CellStore 记录，不同步 worksheet metadata。
- Style-only mutation 只允许在同一 materialized session 内复制/移动已验证的 source StyleId 或清除 target style；单 cell 与 sparse range copy/move 都要求每个 mapped target 已表示。Range overlap 从稳定 source-style snapshot 读取；move 在候选 store 中先清除 represented source styles，再 overlay mapped targets，并在全量 preflight/guardrail 后一次发布。目标 CellValue 保持不变，dirty projection 只改变 cell `s` attribute，styles.xml 继续 copy-original。它不开放 style registry、任意 StyleId 写入、dense range formatting 或跨 workbook migration。
- Cross-sheet sparse copy 只接受同一当前 `WorkbookEditor` 的两个已 materialize borrowed handles，读取 source 的 live sparse snapshot 并只发布到 destination store；同 workbook StyleId 可随 CellValue 复用，全等映射保持两边 clean，存在实际差异时才 dirty destination，目标 guardrail 在替换前检查。它不是 worksheet clone，不跨 workbook 迁移 style/sharedStrings，也不复制 linked metadata。
- Value-only sparse copy 将 payload/公式位移与样式所有权分离：source styles 被忽略，existing destination records 保留 pre-edit StyleId，missing mapped targets 以 unstyled records 插入；same-sheet overlap 和 cross-sheet source 都从发布前稳定 snapshot 读取，按该样式归属得到的 mapped values 全等时 clean no-op，目标 CellStore guardrail 仍控制整批发布。
- Value-only sparse move 在 copy 语义上增加 source clear：represented source records 先变成显式 blank 并保留 source StyleId，再 overlay destination，因此 same-sheet 重叠 target 可以覆盖 source blank；destination 仍保留 pre-edit StyleId，公式按位移平移。Same-sheet 通过单候选 store 发布，cross-sheet 先验证 source/destination 两份候选再 noexcept swap；每个 session 只在自己的最终候选实际变化时 dirty，显式 blank source 可保持 source clean，全部最终记录全等时 clean no-op。失败不得泄漏半边状态。
- Row/column insert/delete 中 formula record 的坐标 shift 与引用 rewrite 是两件事：无论公式单元格本身是否移动，surviving formula 都使用同一 structural rewriter，只调整被插入/删除轴实际影响的引用；普通 copy/move delta translation 仅用于 cell transfer API。
- Cross-sheet sparse move 同样只接受同一 owner，但属于双 session mutation：source removal 和 destination overlay 必须先在各自 CellStore candidates 中通过 coordinate/guardrail preflight，随后以 noexcept swap 发布实际变化的 session。Full-cell move 的 represented source removal 必然改变 source；destination 是否 dirty 由最终 overlay 是否改变其 sparse state 决定。任一候选失败不得泄漏 source 删除或 destination 写入，失败 save 必须保留所有 dirty session 的 retry 状态。
- Cross-sheet style-only copy 遵循同一 owner/session identity：先冻结 source 的 live optional StyleId snapshot 并验证所有 mapped destination records，再批量替换目标 `s` attribute；destination values 与 styles.xml 保持不变。不同 owner、missing target 或发布前 guardrail 失败不污染任一 session。
- Cross-sheet style-only move 在同一 owner/session identity 上增加双状态提交：source optional StyleId snapshot 与全部 mapped targets 冻结后，独立构造 source-clear/destination-overlay candidates，两边 guardrail 都成功才 noexcept 发布。每个 session 只在自身最终样式变化时 dirty；unstyled source 因此可以只清除 destination。同坐标跨 sheet 不是 no-op，CellValue 与 styles.xml 保持不变。
- Strict rejection 不注册 session、不排队 edit、不改变 pending/unsaved count，也不覆盖 `last_edit_error()`。
- Worksheet metadata、relationships、tables、drawings、validations、comments 等不进入 `CellStore`，不能假设会随 cell structural edit 语义同步。

## DOM 边界

- 禁止：大型 `worksheet.xml`、大型 `sharedStrings.xml`、批量导出和大型模板填充的完整 DOM。
- 可选：`workbook.xml`、`.rels`、`[Content_Types].xml`、`docProps`、较小 `styles.xml` 等小 part 的局部结构化处理。
- 使用局部 DOM 不代表已实现完整对象编辑。

## 状态与失败

- Edit/materialization 先 preflight，成功后才注册或修改状态。
- `request_full_calculation()`、`rename_sheet()`、internal document-properties rewrite、internal part removal、materialized small-part replacement 与 file-backed worksheet rewrite 的跨 plan/replacements/omitted entries/manifest/public diagnostics/临时文件所有权变更先在副本完成，再以 noexcept swap 提交；staging 失败不能留下部分 mutation，未发布临时文件由 RAII 清理，既有 patch 必须保持可保存、可重试。提交后的 ownership 集合只保留当前 replacement 引用的路径，被后续 rewrite 取代的旧临时文件立即删除。
- Malformed source、非法值和 session option mismatch 保持通用 contract/load failure；不得伪装成 strict projection loss。
- Failed edit/save 不清除 staged state 或 dirty session diagnostics，也不改变 pending/unsaved count、`last_edit_error()` 或 save watermark。
- Successful `save_as()` 清除 unsaved watermark，但保留可复用 staged state。
- Move construction/assignment 转移 watermark 与 session state；moved-from editor 视为未打开且无 pending/unsaved state。

## Unknown Part Preservation

Unknown part preservation 是默认安全策略，不是语义支持声明。只要功能没有明确更新对象的 part、relationship 和 content type，就只能承诺 preserve/audit/fail，不能承诺 edit。
