---
name: fastxlsx-project-navigation
description: "导航 FastXLSX 架构、public/internal 边界、当前能力、功能缺口和专项实现入口。用于开始项目分析、选择 Streaming/Patch/In-memory 路径、判断功能是否已公开，以及把任务路由到正确模块和项目 skill。"
---
# FastXLSX Project Navigation

## 事实顺序

先读 public headers，再核对 source、tests、`docs/CURRENT_CAPABILITIES.md`、CMake 和 `docs/TASK_BREAKDOWN.md`。历史计划只查 Git；internal hook、fixture、preservation test 和 benchmark instrumentation 不形成 public 能力。

## 三条 Public 路径

- Streaming：`WorkbookWriter` / `WorksheetWriter` / `CellView` 用于大型有序新建，并支持 basic classic note insertion；`WorkbookReader` 用于已有 worksheet row/cell、worksheet-root metadata/data validations/hyperlinks/conditional formatting、linked-table basics、writer-compatible drawing/image basics、classic comments/notes basics、strict simple sharedStrings item、narrow rich sharedStrings run、styles/cellXfs 与 narrow font/fill component 的 forward-only bounded callback traversal。
- Patch：`WorkbookEditor`，用于已有文件的 part-level copy/rewrite/remove 和定点 worksheet rewrite。
- In-memory：borrowed `WorksheetEditor`，用于受 guardrail 限制的小文件稀疏随机编辑。

## 当前能力与缺口

- Streaming 已覆盖基础 cell、styles、worksheet metadata、窄 tables/conditional formatting、PNG/JPEG insertion 和 new-workbook simple classic note insertion；`add_note()` 生成独立 comments/VML parts、relationships/content types 与 hidden shape，不创建 target cell，不复用 spreadsheet drawing，也不允许历史行随机修改。
- Patch 已覆盖 catalog、事务式空白 worksheet add、关系闭合 worksheet remove、sheetData/cell replacement、worksheet-local internal/external hyperlink、conservative basic classic note add/update/remove、data-validation add/remove、writer-compatible conditional-format add/update/remove 与 table add/update/remove lifecycle、worksheet-root auto-filter set/clear、merged-cell exact merge/unmerge 与 primary-view freeze-pane set/clear、窄 rename、formula audit/recalculation、core/app properties 和 media bytes replacement。Internal hyperlink 只改 worksheet XML；external hyperlink 同步 worksheet XML 与 worksheet `.rels`；classic note source edit 仅接管 writer-canonical pair；data-validation remove 按当前 effective zero-based reader index，在 strict bounded projection 后删除 direct child/final container；conditional-format update/remove 按 strict reader index 替换/删除完整单规则 container，update 保留原 priority/source order，advanced payload 不接管，也不改 cells/styles/dxf/calc/relationships；table lifecycle 复用 Streaming `TableOptions`，update 保留 package identity，remove 关闭 owner-local graph，但不检查 cell payload 或做 structural sync；其他 metadata 保持各自窄边界。当前没有 public existing-workbook worksheet clone。新增 worksheet 可同会话用 Patch 填充/rename/hyperlink/note/data validation/conditional format/table/auto-filter/merged-cell/freeze-pane，但需保存重开后才能 In-memory materialize。
- In-memory 已覆盖 sparse reads/writes、range/row/column mutation、structural shifts、cell/value/style transfer 和 two-phase save；它不修复 linked worksheet objects，也不是 large-file random editor。
- Public `WorkbookReader` 已覆盖 bounded row/cell callback、typed scalar、simple inline text、shared/style index、formula + cached scalar 分离，以及独立 worksheet-root metadata/data-validation/hyperlink/conditional-formatting、linked-table basics、classic comments/notes basics、strict simple sharedStrings item、narrow rich sharedStrings run、styles/cellXfs 与 writer-compatible font/fill component traversal。Metadata companion 只投影 owning primary frozen split、root auto-filter range 与 zero-based merged ranges，审计其他 views/schema/count/QName/overlap，不读取 table-local filter。Data-validation companion 按 source order 投影 owning multi-range `sqref + DataValidationRule`，审计 container/QName/schema/formula shape 与 XML/range/text guardrail；它不求值、不校验 cell value、不做 overlap repair。Hyperlink companion 按 source order 投影 owning A1 range、internal location 或 owner-local external relationship target，审计 namespace/type/TargetMode/duplicate/overlap/schema，并保留 relationship id 为 internal。Conditional-format companion 按 source order 投影 owning multi-range `sqref`、priority 与 writer-compatible color-scale/data-bar/3Arrows rule payload，审计 rule shape/schema/guardrail；advanced/custom/dxf/formula/cellIs 明确 fail。Table companion 按 worksheet `tableParts` source order 跟随标准 internal relationships，投影 owning id/range/name/displayName/basic columns、writer-compatible totals/filter/style，审计 target part/content type/table shape；calculated formula、full filter 与 extensions 明确 fail。Rich-run companion 保留 item/run boundary，把 simple item 映射为一个默认 run，只投影 owning bold/italic/direct ARGB，并对 mixed/phonetic/extension/非默认 font/theme/tint 明确 fail。Companions 使用 callback-lifetime borrowed text 或 owning values、显式 guardrail 与 stored/DEFLATE entry retry；cellXfs component ids 保持 workbook-local，由 caller 显式关联。它不 seek、不构建完整 worksheet/sharedStrings/styles/table model、不自动解析 index/object、不 materialize worksheet，也不修改 OPC state。公式不求值、不生成 cached result、不完整重建 calcChain。
- Image companion 跟随唯一 worksheet drawing relationship 与 drawing-local image relationships，按 drawing source order 投影 owning writer-compatible two-cell anchor、transform extent、name/description、PNG/JPEG format 与 encoded size；unique media 完整 drain 一次以验证 ZIP size/CRC/signature，但不保留 payload 或解码 pixels。`xdr:oneCellAnchor` / `xdr:absoluteAnchor` 元素、chart/shape/crop/rotation/hyperlink 与其他 drawing semantics 明确 fail，no-images build 保留 symbol 但调用抛错。
- Comment companion 跟随唯一 worksheet-local standard internal comments relationship，按 comments-part source order 投影 owning single-cell ref、resolved author 与 simple text；author table、duplicate ref、authorId/shapeId 与 optional legacy VML relationship/part 都有 guardrail/audit。Rich/threaded comments、persons 与 VML payload/visibility/shape 明确不投影。
- Tables、writer-compatible picture anchors 与 classic comments/notes 已有窄 read projection；table 另有 new-workbook Streaming add 和 existing-workbook Patch add/update/remove metadata lifecycle，classic note 另有 new-workbook Streaming write 与 conservative Patch add/update/remove。Table Patch 不读取或同步 cell payload、公式、defined names 或 structural mutation，仍不是完整 table object model；classic-note Patch source edit 仅支持 writer-canonical comments/VML pair。因此完整 existing-object table/drawing/comments semantic edit仍未公开。Charts、VBA、pivot、external links 和 custom XML 默认只可 preserve/audit/fail；不能因读取、窄 lifecycle 或保留测试宣称完整 semantic edit。

## 稳定契约

- Production 默认 minizip-ng stored + DEFLATE；stored-only 是显式 bootstrap profile。Direct-zlib 仅为 default-off internal profiling engine，不是 public/default backend。
- `has_pending_changes()` 表示 retained staged state；`has_unsaved_changes()` 表示最近成功保存后的 watermark delta。
- Dirty In-memory save 使用 stage -> package write -> state commit；失败保留 dirty diagnostics、counts 和 retry 能力。
- In-memory 默认 `RejectKnownLosses` 并抛 typed `WorksheetMaterializationError`；`AllowLossyProjection` 必须显式选择。
- Internal package/edit-plan/relationship 类型不进入 public surface。Images 可关闭；关闭时 public symbols 保留但调用抛错。

## 专项路由

- Public API、Doxygen 和状态边界：`fastxlsx-api-design-docs`。
- Streaming 热路径和 large rewrite：`fastxlsx-streaming-worksheet`。
- Patch、OPC、preservation 和 transaction：`fastxlsx-opc-editing`。
- In-memory materialization、CellStore、mutation 和 retry：`fastxlsx-in-memory-worksheet`。
- Styles、metadata、conditional formatting、images、依赖、构建和测试分别使用对应 feature skill。
