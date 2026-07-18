---
name: fastxlsx-in-memory-worksheet
description: "实现或审查 FastXLSX In-memory `WorksheetEditor`。用于 strict/lossy materialization、typed diagnostics、CellStore guardrails、cell/range/row/column mutation、结构插删、same-/cross-sheet value/style transfer、borrowed handle 生命周期、dirty handoff、保存失败恢复和 retry。"
---

# FastXLSX In-memory Worksheet

## 必读文件

- `docs/CURRENT_CAPABILITIES.md`、`docs/EDITING_MODEL.md`、`docs/API_DESIGN_AND_DOCUMENTATION.md`
- `include/fastxlsx/workbook_editor.hpp`
- `src/cell_store.cpp`、`src/cell_store_materialization_loss.hpp`
- `src/workbook_editor_materialized_edits.cpp`、`src/workbook_editor_worksheet_access.cpp`、`src/workbook_editor_worksheet_facade.cpp`
- 对应 `test_workbook_editor_public_state_*`、materialization、session、retry 和 transfer tests

## 边界

- 将 `WorksheetEditor` 保持为同一 `WorkbookEditor` 所有的 borrowed handle；明确 owner、move/reacquire 和 session identity。
- `WorkbookEditor::add_worksheet()` 新增但尚未保存的 worksheet 没有 source payload，必须明确拒绝 materialization；`save_as()` 后重新打开，generated worksheet 成为新 source catalog 的一部分才可 materialize。
- 只做受 `max_cells` / `memory_budget_bytes` 约束的 small-file sparse random editing。Large worksheet rewrite 必须走 Patch/C5，不扩大 guardrail 冒充低内存随机编辑。
- `CellStore` 不承载 row/column metadata、merged cells、tables、filters、validations、conditional formatting、hyperlinks、drawings、charts、VBA、defined names、relationships 或 calcChain。

## Materialization

- 默认使用 `RejectKnownLosses`。Rich text、phonetic/extension、formula metadata 和 cached result 等已知损失，在注册 session 前抛 `WorksheetMaterializationError`。
- Public diagnostic 只暴露稳定 category、worksheet、row/column 和可选 sharedStrings index；不泄漏 XML token、part path、relationship id 或 parser state。
- `AllowLossyProjection` 必须显式 opt-in，且 policy/guardrail 是 session identity 的一部分。Policy mismatch、malformed XML 和其他加载失败保持通用 `FastXlsxError`。

## Mutation

- 使用 active snapshot -> candidate state -> coordinate/style/formula/guardrail preflight -> effective-change comparison -> noexcept publish。任一失败不得发布部分记录或虚假 dirty state。
- Batch duplicate 采用 later-wins 后再比较最终记录。全等 mutation 保持 clean，并在 CellStore guardrail 前返回。
- 区分 full-cell、value-only 和 style-only 语义。任意 caller non-default StyleId 在 style registry/migration contract 建立前必须拒绝；同 workbook 已验证 StyleId 只按当前 public 契约复用。
- Structural insert/delete 使用窄 structural formula rewriter；copy/move 使用 source-to-target translation。不要把两种公式语义混用。
- Cross-sheet move 是双状态事务：先构造并验证 source/destination candidates，再以 noexcept swap 发布；失败不得留下半边 mutation。

## Save 与状态

- Dirty save 使用 materialized stage -> package write -> public state commit。Package write 失败必须保留 dirty session、diagnostics、pending/unsaved counts、watermark 和最新值 retry 能力。
- `has_pending_changes()` 不等于 `has_unsaved_changes()`；成功保存清除 unsaved delta，但允许 retained staged state 继续存在。
- SharedStrings projection 只在关系和 content type 唯一且安全时 append-only；不安全 metadata 回退 inline string 并保留原 package 状态，不做隐式 repair。

## 验证

- 覆盖 strict typed diagnostics、explicit lossy、policy mismatch、guardrail、clean no-op、duplicate later-wins 和 failure-before-state-change。
- 结构/transfer 覆盖 overlap snapshot、formula boundaries、style ownership、cross-owner rejection 和双边 dirty ownership。
- 保存覆盖 post-stage failure、output protection、retry 最新值、reopen、move/reacquire 和 unknown-part preservation。
