# 复杂对象支持产品化计划

## 文档定位

本文规划 FastXLSX 对 tables、drawings/images、charts、comments、threaded comments/persons、
VBA、custom XML、pivots、external links 等复杂对象的支持路线。

本文的核心原则是先区分 preserve / audit / fail / edit，再决定是否开放 public semantic API。
当前能力事实只以 [CURRENT_CAPABILITIES.md](CURRENT_CAPABILITIES.md) 为准；本文不能把 preservation
证据写成当前 semantic editing。

所属路径：

- Streaming：new-workbook 的窄切片对象输出，例如 tables、images、conditional formatting 等。
- Patch：existing-file 的 preservation / audit / fail / narrow edit。
- In-memory：small-file cell editing，不同步复杂对象。
- 任务入口：`TASK_BREAKDOWN.md` 中的 F2、C2、C3、C7。

## 当前基线

当前 public streaming path 已有若干 new-workbook 窄切片：

- streaming-only tables。
- PNG/JPEG image insertion。
- data validations、hyperlinks、conditional formatting slices。
- 基础 styles。

当前 existing-file path 已有：

- `replace_image()`：只替换已有 PNG/JPEG media part bytes。
- Patch preservation / audit internal evidence：unknown parts、linked drawings/media/charts/tables、
  comments / threaded comments / persons、VBA、custom XML、pivots、external links 等相关 fixture。

这些能力不等于：

- 完整 table editing。
- drawing / anchor / relationship editing。
- chart model editing。
- comments model mutation。
- VBA support beyond preservation。
- custom XML semantic API。
- pivot / external link refresh 或 repair。

## 非目标

除非经过新的 C3 public design gate，否则本计划不承诺：

- existing-file table resize / add / delete。
- existing-file drawing insertion / deletion / anchor editing。
- chart series / axis / style semantic editing。
- comments / threaded comments UI model editing。
- VBA project editing、signing、macro understanding。
- custom XML schema-aware editing。
- pivot cache refresh / pivot table editing。
- external link update / repair。
- relationship pruning / orphan cleanup。
- linked-part regeneration。

## 分层策略

每个复杂对象必须经过五层成熟度，不能跳级：

```text
preserve
-> audit
-> fail policy
-> narrow edit
-> public semantic API
```

定义：

- Preserve：未修改对象 part 和 relationships 原样保留。
- Audit：在 Patch plan / diagnostics 中提示对象依赖和风险。
- Fail policy：当 edit 会破坏对象且无法安全更新时，明确拒绝。
- Narrow edit：只编辑对象中一个边界很窄、可验证的字段或 part bytes。
- Public semantic API：开放用户可依赖的对象级 API，必须有 header、实现、测试、Doxygen、examples。

## 对象族计划

### Tables

当前：

- Streaming new-workbook 可生成 basic table parts、worksheet `<tableParts>`、worksheet `.rels`
  和 content type。
- Existing-file 主要是 preserve / audit evidence。

下一步：

- Patch 中先稳定 table part preservation、tableParts audit、range dependency audit。
- 对 `replace_cells()` / row-column shift / sheet rename 影响 table range 的场景默认 audit 或 fail。
- table resize / add / delete 必须另过 public design gate。

### Drawings and Images

当前：

- Streaming `WorksheetWriter::add_image()` 可插入 PNG/JPEG 到 new workbook。
- Existing-file `WorkbookEditor::replace_image()` 只替换已有 media bytes。

下一步：

- 保持 media bytes replacement 与 drawing semantic editing 分离。
- drawing XML、anchors、drawing `.rels`、worksheet `.rels` 先 preserve / audit。
- existing-file image insertion、anchor editing、one-cell/absolute anchor 支持必须另过设计门。

### Charts

当前：

- 只能作为 linked part preservation / audit evidence 描述。

下一步：

- chart part、drawing relationship、embedded workbook refs、cache data 先 preserve。
- worksheet data replacement 对 chart source range 的影响默认 audit。
- chart API 不进入 public surface，除非定义清楚 series/range/cache/recalc 策略。

### Comments, Threaded Comments, Persons

当前：

- internal preservation fixture 已覆盖 comments / threaded comments / persons 相关场景。

下一步：

- 继续以 preserve / audit / fail 为主。
- 不凭空创建 comments owner `.rels`。
- comments add/delete/edit、threaded comments 和 persons model 必须另过 public gate。

### VBA

当前：

- 只能作为 existing-file preservation 对象。

下一步：

- 保留 `vbaProject.bin`、content type、relationships。
- 不解析、不修改、不签名、不生成宏。
- 任何会移除 workbook-level macro relationships 的操作必须 fail 或 audit。

### Custom XML

当前：

- 只能作为 preservation / audit 对象。

下一步：

- 保留 custom XML parts、properties 和 relationships。
- 不做 schema-aware editing。
- 删除或替换相关 owner part 时必须保守 audit。

### Pivots and External Links

当前：

- 只能作为 linked parts preservation / audit 对象。

下一步：

- pivot caches、cache records、pivot table parts、external link parts 默认 preserve。
- worksheet cell edits 不刷新 pivot cache，也不更新 external link metadata。
- refresh / repair 属于未来 semantic API，必须另过 gate。

## 数据流

复杂对象在 Patch 中的推荐处理流：

```text
edit request
-> identify direct target part
-> discover known inbound / outbound relationship-bearing objects
-> classify each object family as preserve / audit / fail / edit
-> execute direct edit only if policy is safe
-> carry audit into output plan / public diagnostics where appropriate
-> save_as preserves unknown and non-target parts
```

Streaming 中的对象流：

```text
public streaming object API
-> validate bounded metadata
-> record worksheet-local / workbook-local object state
-> close-time XML part generation
-> relationships and content types emitted
```

两条路径不能混淆：streaming object generation 不等于 existing-file object editing。

## Workstreams

### OBJ-1 Object-family policy registry

目标：为每个对象族记录 preserve / audit / fail / edit 状态。

验收：

- tables、drawings/images、charts、comments、threaded comments/persons、VBA、custom XML、
  pivots、external links 都有一页策略。
- public docs 引用策略，不复制长矩阵。

### OBJ-2 Existing-file preservation fixture map

目标：把当前 internal preservation tests 映射到对象族和 gap。

验收：

- 每个 fixture 说明验证的是 preservation、audit、fail 还是 narrow edit。
- 不把 fixture 名称升级成 public support。

### OBJ-3 Relationship-bearing object audit

目标：统一 relationship target audit 在复杂对象上的传播。

验收：

- owner part、relationship id/type、target part、external/invalid target 都可审计。
- audit 不等于 repair。

### OBJ-4 Narrow edit gate

目标：为未来对象级窄编辑定义 gate。

必须回答：

- 编辑哪个 part / XML element / attribute？
- 是否需要同步 content types、relationships、workbook metadata、worksheet metadata？
- sharedStrings、styles、formulas、calcChain 是否受影响？
- 失败时如何回滚状态？

### OBJ-5 Streaming object polish

目标：把已有 streaming 窄切片做成稳定文档和示例。

验收：

- tables、images、conditional formatting、validations、hyperlinks 的 side effects 明确。
- examples 不暗示 existing-file support。

## 阶段顺序

### Phase 0：对象族分类

- 建立 policy registry。
- 清理文档中“完整支持”的模糊表述。

### Phase 1：preserve / audit 补齐

- 每个对象族至少有 preserve evidence 或明确缺口。
- linked relationship target audit 可追踪。

### Phase 2：fail policy

- 对会破坏对象但不能同步的 edit 增加 conservative fail。
- 失败前置验证 no-state-pollution。

### Phase 3：narrow edit

- 只选择一个对象族的一处小边界。
- 先 internal tests，再 public gate。

### Phase 4：semantic API

- 只有用户价值、策略闭环和测试证据都足够时才公开。

## Definition of Done

复杂对象支持计划完成的最低标准：

- 每个对象族都有 preserve / audit / fail / edit 状态。
- 文档明确哪些是 streaming-only new-workbook，哪些是 existing-file preservation。
- Patch edits 不静默破坏 known linked parts。
- public API 不承诺未实现 semantic editing。
- Release wording 不用“完整支持 Excel 对象”这类模糊表述。

## 任务模板

```text
任务编号：
对象族：
模式：Streaming / Patch / In-memory
当前层级：preserve / audit / fail / narrow edit / public semantic API
目标：
输入：
输出：
触碰 parts：
relationships / content types side effects：
sharedStrings / styles / formulas / calcChain 影响：
失败策略：
验收标准：
禁止项：
验证命令：
```

## 首批建议任务

### OBJ-1 Complex object policy registry

新增或整理对象族策略索引，链接当前 preservation fixture 和待补缺口。

### OBJ-2 Table existing-file policy gate

围绕 tableParts、table ranges、relationship 和 worksheet edits 写清 preserve / audit / fail。

### OBJ-3 Drawing/image split wording

统一 `WorksheetWriter::add_image()` 与 `WorkbookEditor::replace_image()` 的文档边界，避免混淆。

### OBJ-4 Comments/VBA/custom XML preservation audit

把 comments、threaded comments、persons、VBA、custom XML 的 preservation evidence 和 non-goals
整理为 release gate 输入。

## 文档-only 验证

```powershell
git diff --check -- docs/COMPLEX_OBJECTS_SUPPORT_PLAN.md docs/TASK_BREAKDOWN.md
rg -n "COMPLEX_OBJECTS_SUPPORT_PLAN|OBJ-|semantic editing|preserve / audit / fail" docs/COMPLEX_OBJECTS_SUPPORT_PLAN.md docs/TASK_BREAKDOWN.md
```

不需要运行 C++ build / CTest，除非同时修改 public header、source、CMake 或测试。
