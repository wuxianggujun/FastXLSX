# API 设计与文档规则

## 事实与入口

- 当前能力唯一事实源：[CURRENT_CAPABILITIES.md](CURRENT_CAPABILITIES.md)。
- 当前执行入口：[TASK_BREAKDOWN.md](TASK_BREAKDOWN.md)。
- 只有 `include/fastxlsx/` 中明确暴露的符号才能描述为 public API。
- `include/fastxlsx/detail/`、`src/` 和测试 hook 属于 internal foundation。

## API 模式门禁

设计 API 前必须声明所属模式：

- **Streaming**：新建 XLSX、大数据导出、row-order append。
- **Patch**：existing-file part-level rewrite、preservation 和 audit。
- **In-memory**：small-file sparse random editing。

跨模式 API 必须写清入口、数据流、状态所有权、保存行为和性能后果。

## Public / Internal 边界

- public facade 保持面向 workbook/worksheet/cell 的用户语义。
- `PackageReader`、`PackageEditor`、`EditPlan`、dependency analysis 和 relationship graph 不公开。
- 测试覆盖、internal hook 或 preservation fixture 不能自动成为用户承诺。
- planned 能力必须标记为 planned，不写成路线图保证或当前支持。

## 设计原则

- 易用 API 不能让 large worksheet 隐式进入 DOM、dense matrix 或无界 cell map。
- `Cell` / `CellValue` 可以作为 owning 边界值和 small-file 存储，不作为 million-row 热路径长期模型。
- Patch edit 必须明确 copy/rewrite/remove/audit/fail 行为，以及 sharedStrings、styles、formulas、relationships、content types 和 calc metadata 策略。
- In-memory API 必须提供 cell count、内存估算和 guardrail，并定义失败前状态不污染。
- Existing-workbook style-only API 在 style registry/migration contract 建立前，只能复用同一 materialized workbook 中已校验的 source StyleId 或清除现有句柄；range mutation 必须定义 sparse mapping、missing-target、overlap snapshot 与 batch preflight 语义。不得接受任意 caller non-default StyleId，并必须声明 styles.xml 是 preserve 而不是 edit。
- Style-only move 必须明确 source-clear 与 destination-overlay 的顺序：从 pre-edit snapshot 冻结 optional StyleId，在候选 CellStore 中先清除 represented sources、再覆盖全部已表示的 mapped targets，比较最终状态并通过 guardrail 后一次发布；不能先修改 active source，也不能借 move 合成 cell、移动 CellValue 或引入 style table migration。
- Cross-worksheet In-memory API 必须验证 borrowed handle 属于同一当前 `WorkbookEditor`，明确 source/destination dirty ownership、live snapshot 时点、同坐标行为和目标 guardrail；不得把 same-workbook sparse copy 描述为 worksheet clone、cross-workbook migration 或 linked-object copy。
- Value-only copy 必须明确 source StyleId 被忽略、existing destination StyleId 从 pre-edit snapshot 保留、missing destination 插入 unstyled，以及公式仍按 source-to-target delta 平移；不得用 full-cell copy 或 style migration 语义替代。
- Value-only move 必须同时定义 source 与 destination 的样式所有权：source 采用 `clear_cell_value()` 的显式 blank 并保留 source StyleId，destination 保留 pre-edit StyleId，missing destination 插入 unstyled；跨 worksheet 发布必须使用双 CellStore candidate + noexcept commit。
- Row/column structural edit 必须将 formula-cell 坐标移动与 formula-reference 重写分开：所有 surviving formulas 都按插入/删除轴做 structural rewrite，不能因公式记录自身被移动就退化为 copy/move delta translation；`$` 只保留标记，不阻止结构调整。
- Cross-worksheet move 是双状态 mutation：实现必须在 active sessions 外构造 source-removal 与 destination-overlay candidates，验证两边 guardrail 后只以 noexcept commit 发布，并覆盖 destination preflight failure、save failure retry 与 reopen；禁止先删 source 再尝试写 destination。
- Cross-worksheet style-only mapping 还必须要求 mapped target 已表示，先完成 source optional StyleId snapshot 与全目标 preflight，再发布 destination-only batch；source gaps 不得合成 target，unstyled source 的 clear 语义和 styles.xml preserve 边界必须显式记录。
- Public structured diagnostics 只暴露稳定业务/语义分类与调用方可理解的上下文；XML token、parser state、part path、relationship id 和 internal type 不得成为 public contract。Typed exception 应保留 `FastXlsxError` 基类兼容性，并明确哪些相邻失败仍是通用错误。
- 数值写入必须拒绝非 finite 值，不能序列化 `nan`、`inf` 或 `-inf`。
- 第三方库只承担 ZIP、XML、图片等通用能力；XLSX 语义留在 FastXLSX。

## Doxygen 要求

Public API 注释至少说明：

- API 所属模式和适用数据规模。
- 参数生命周期、所有权及 `std::span`/view 的有效期。
- 顺序要求和随机访问限制。
- 内存随 row、cell、string、style、rule、range、image bytes 或 decoded pixels 的增长关系。
- OpenXML side effect：worksheet `.rels`、drawing `.rels`、content types、styles、sharedStrings、docProps、calc metadata 等。
- 错误类型、失败是否发生在状态变更前、失败后是否可重试。
- 若错误提供 typed diagnostic，逐字段说明索引基数、可选条件、稳定性和 text message 的非契约属性。
- 不支持项和容易被误解的边界。

发现 Doxygen 缺口时在 `TASK_BREAKDOWN.md` 的 C0/C3 记录任务；本文件不维护逐方法覆盖流水。

## Public API 提案模板

每个提案必须回答：

1. 属于 Streaming、Patch 还是 In-memory？
2. public facade 和 internal implementation 分别是什么？
3. 输入、输出、所有权和错误契约是什么？
4. 内存随什么增长，是否触碰热路径？
5. 涉及哪些 OpenXML part、relationship 和 content type？
6. Existing-file 场景采用 preserve、audit、fail 还是 edit？
7. 需要哪些 unit、OpenXML、Office、preservation 或 benchmark 证据？
8. 哪些能力明确不在本次范围？

## Wording Gate

- “高性能”“低内存”必须附 benchmark 数据集与内存口径。
- “支持对象”必须区分创建、读取、保留、审计、替换和语义编辑。
- “编辑图片”必须区分 new-workbook insertion 与 existing media bytes replacement。
- “支持公式”必须区分文本、审计、重写、重算请求和求值。
- “保存”必须区分 `save()`、`close()` 和 non-atomic `save_as()`；`WorkbookEditor::save_as()` 不是 commit/close，成功后仍可能保留 staged Patch state。

## 验证清单

- public 名称可在 public headers 中找到。
- internal 名称只出现在明确的 internal/架构语境。
- 没有把 planned 或 preservation 写成当前 public support。
- README、事实源、架构和任务入口没有复制互相漂移的长矩阵。
- 文档链接、UTF-8 和 `git diff --check` 通过。
