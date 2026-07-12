# Patch Preservation Coverage

## 文档定位

本文记录 existing-workbook Patch 的对象族 preservation 证据和验证要求。它不声明复杂对象的 public 语义编辑能力；当前能力仍以 [CURRENT_CAPABILITIES.md](CURRENT_CAPABILITIES.md) 为准。

## 默认策略

- 未修改 part：`copy-original`。
- 未知 part：默认保留。
- 修改 part：按 internal plan 执行 stream/local-DOM rewrite 或 remove。
- 受影响但无安全语义编辑能力的对象：audit 或 fail-before-state-change。
- relationship/content-type 变化：必须有显式 side-effect 验证。

## 覆盖矩阵

| 对象族 | 当前证据 | 允许表述 | 禁止表述 |
| --- | --- | --- | --- |
| Core/app/custom docProps | public Patch rewrite、copy preservation、missing-part side effect、failure/retry 测试 | `WorkbookEditor::set_document_properties()` 窄重写 core/app；custom properties 保留 | custom-properties 对象模型或任意 docProps child 无损编辑 |
| Worksheet cells | strict existing-cell Patch、missing-cell upsert、relationship audit、failure/retry 与大 fixture evidence | 无 relationships 的 DEFLATE strict replace 可 one-inflate direct-range；其他已实现场景走 single-pass source-order transform | 任意 random editing、linked metadata repair 或不重写 worksheet part |
| sharedStrings/styles | source preservation、索引读取和窄 cell/style 行为 | 保留未修改 part；支持已实现窄路径 | 完整索引迁移或格式修复 |
| Formulas/defined names/calc metadata | 文本、审计、窄重写、full calculation 请求 | 公式文本与引用策略 | 公式求值、cached values、完整 calcChain rebuild |
| Tables | linked part preservation 与 Streaming 新建窄切片 | existing table parts 默认保留 | existing table semantic editing |
| Drawings/images/charts | drawing/media/relationship preservation；已有 media bytes 窄替换 | 保留 linked parts；替换已有 PNG/JPEG bytes | drawing、anchor、chart 或 relationship 编辑 |
| Legacy/threaded comments/persons | linked-part preservation fixture | 未修改 comments 相关 part 保留 | comments semantic editing |
| VBA | workbook/content-type/relationship preservation fixture | 未修改 VBA part 保留 | VBA 生成、签名或编辑 |
| Pivot/external links | linked-part preservation fixture | 未修改 linked parts 保留 | pivot/external-link semantic editing |
| Custom XML | part、properties、relationship preservation fixture | 未修改 custom XML 保留 | custom XML object model |
| Unknown extensions | copy-original regression | 未识别 part 默认保留 | 任意扩展都可安全编辑 |

## 必测不变量

- Source ZIP entry bytes 或语义结构按策略保留。
- Relationship target、type、target mode 和 content type 没有意外变化。
- 删除或替换操作不会留下明显 dangling relationship/orphan part；未实现自动清理时应 audit/fail。
- 跨 edit plan、replacement、omitted entry、content type 和 manifest 的 mutation 必须先 staging；提交前失败保持调用前输出计划并允许 retry。
- 失败不会增加 pending edit、污染 catalog 或破坏后续 safe save。
- No-op `save_as()` 后可重新打开，并保持未修改对象。
- Preservation fixture 只作为回归证据，不进入 public API 示例。

## 扩展规则

新增对象族或 mutation 时，先在 [TASK_BREAKDOWN.md](TASK_BREAKDOWN.md) 的对应 C1/C4 lane 记录：

1. 对象 part 与 relationship 图。
2. preserve/audit/fail/edit 策略。
3. content type 和 workbook/worksheet side effect。
4. 失败与 retry 行为。
5. ZIP/XML、reopen、Office/openpyxl 验证。

只有 public facade、实现、Doxygen 和验证证据齐全后，才能把对象从 preservation 提升为窄语义编辑支持。
