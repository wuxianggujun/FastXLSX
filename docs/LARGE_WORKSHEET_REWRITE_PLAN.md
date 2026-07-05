# Large Worksheet 低内存 Rewrite 计划

## 文档定位

本文是大 worksheet 低内存 rewrite 路径的设计计划，用于把已有 internal worksheet reader /
transformer / chunk handoff evidence 收敛为独立的工程主线。它解决的是“已有 workbook 中的大型
worksheet 如何低内存重写”，不是 `WorksheetEditor` 的小文件随机编辑，也不是 `WorkbookWriter`
的新建 workbook streaming export。

本文不是当前能力事实源。当前状态只以
[CURRENT_CAPABILITIES.md](CURRENT_CAPABILITIES.md) 为准。本文中的 public transformer / rewrite API
均属于 planned / design-gate 内容，不能写成当前 public API。

所属路径：

- 模式：Patch + low-memory worksheet rewrite。
- 当前 facade：没有稳定 public low-memory transformer facade。
- 任务入口：`TASK_BREAKDOWN.md` 中的 C5，并受 C3 public editor 扩展决策门约束。
- 核心约束：大型 worksheet 不进 DOM，不构造 dense matrix，不把 internal transformer 或 chunk helper
  写成 public API。

## 当前基线

当前项目已经有 internal foundation：

- worksheet event reader。
- worksheet transformer action model。
- targeted-cell replacement planning。
- source-offset cell index foundation。
- chunked / file-backed handoff。
- staged payload helper 和 package-entry chunk emitter。
- internal PackageEditor 支持 stream/chunk rewrite 的基础路径。

这些证据说明低内存 rewrite 是可推进方向，但当前不能宣称：

- public large worksheet transformer API 已完成。
- public Patch 默认算法已经是 indexed staged-chunk。
- source-entry ZIP seek 已完成。
- 大文件性能已经有可发布承诺。
- random cell editing 可以低内存覆盖大 worksheet。

## 非目标

以下能力不属于本计划的第一阶段：

- 不提供 dense cell matrix 或 arbitrary random access。
- 不把 `WorksheetEditor` 升级成大文件随机编辑器。
- 不公开 internal `WorksheetTransformer`、`PackageEntryChunk`、`PackageEditor` 或 `EditPlan`。
- 不做 complete worksheet object semantic sync。
- 不做 sharedStrings / styles broad migration。
- 不做 relationship repair / pruning / orphan cleanup。
- 不承诺 Zip64、true package streaming 或所有压缩输入场景。

## 模块边界

Reader 层：

- 以 event reader 读取 worksheet XML。
- 只保留当前 rewrite 所需的小状态。
- 对 malformed XML、invalid cell references 和 unsupported payload 做前置失败或结构化 audit。

Planner / transformer 层：

- 把 caller edit request 转为 ordered transform actions。
- 对 duplicate targets、missing targets、insert policy、source offsets 和 tail pass-through 做确定性处理。
- 不直接暴露为 public stable ABI。

Writer / chunk 层：

- 输出 rewritten worksheet XML 到 file-backed / chunk-backed source。
- 不物化整份 worksheet XML 到内存。
- 与 PackageEditor handoff 时记录 write mode、staged chunks 和 audit。

Patch / package 层：

- 更新目标 worksheet part。
- 保守处理 workbook calc metadata、content types、relationships 和 linked parts。
- 未修改 part copy-original。

## 数据流

目标数据流：

```text
source package
-> PackageReader
-> worksheet entry chunk source
-> WorksheetEventReader
-> ordered transform planner
-> stream/chunk worksheet emitter
-> PackageEditor staged worksheet rewrite
-> save_as output package
```

关键边界：

- 事件流中只保留 bounded state。
- 对 target ranges 的索引可以是有限、可估算的结构，不是完整 worksheet map。
- staged output 可以写临时文件或 chunks，但不能要求最终 public API 依赖内部 chunk 结构。
- linked objects 仍走 preserve / audit / fail，不静默同步。

## 状态流转

推荐状态：

```text
request received
-> source worksheet validated
-> transform plan built
-> emit starts
-> source events consumed
-> staged output sealed
-> package edit queued
-> save_as writes package
```

失败策略：

- plan 阶段失败不能创建 staged output。
- emit 阶段失败不能污染 active PackageEditor plan。
- staged-size、range、CRC 或 source mismatch 失败必须丢弃 staged chunks。
- failure recovery 需要证明 no partial output package。

## Workstreams

### LWR-1 Source event reader hardening

目标：确保大型 worksheet 输入可按事件读取，且错误可解释。

验收：

- cell、row、sheetData、tail XML 的 pass-through 行为稳定。
- invalid source 坐标、duplicate source cells、malformed tags 有失败路径。
- 测试不依赖 Excel UsedRange 反推。

### LWR-2 Transform action model

目标：形成可组合的 ordered rewrite actions。

验收：

- replace、insert、strict missing fail、tail pass-through 语义明确。
- source-order plan 和 target-order plan 冲突时有确定规则。
- duplicate target 和 duplicate source 的处理可测试。

### LWR-3 Indexed staged rewrite prototype

目标：把 targeted-cell replacement 的 indexed / staged 路径变成可验证内部实现候选。

验收：

- index build 成本、matched/source counts、staged output bytes 可观测。
- stale range、stale staged size、invalid chunk 被拒绝。
- benchmark 仍标记 internal prototype，不写成 public 默认算法。

### LWR-4 Package handoff

目标：确保 rewritten worksheet 能以 stream/chunk source 进入 package write-out。

验收：

- write mode 正确标记为 stream rewrite。
- calc metadata policy 清楚。
- linked parts copy-original / audit 不丢失。
- failed handoff 不污染 edit plan。

### LWR-5 Public design gate

目标：决定是否开放 public low-memory rewrite API。

必须回答：

- public facade 是 `WorkbookEditor` 上的新方法，还是独立 builder / transformer？
- caller 如何表达 edits：ordered updates、row callback、range patch，还是 template transform？
- missing target、duplicates、sharedStrings、styles、formulas、relationships 如何处理？
- API 如何说明内存上界、temporary files、failure recovery 和 save_as handoff？

## 与其他路径的关系

Streaming writer：

- Streaming writer 用于 new-workbook export。
- Large worksheet rewrite 用于 existing-file worksheet rewrite。
- 两者可共享 XML encoding helper，但 public API 和 data flow 不混用。

Patch facade：

- Large rewrite 是 Patch 的内部或后续 public 扩展。
- 未修改 parts 仍由 Patch package foundation copy-original。

In-memory editor：

- In-memory editor 是 small-file random editing。
- Large rewrite 是 low-memory sequential transformation。
- 不用 In-memory API 包装大 worksheet rewrite。

## 阶段顺序

### Phase 0：internal evidence 收敛

- 梳理 reader / transformer / chunk / PackageEditor 现有测试。
- 标出哪些只是 prototype benchmark。

### Phase 1：targeted-cell rewrite 硬化

- strict replace / insert / missing fail。
- source offset、tail pass-through、no partial output。

### Phase 2：package integration

- file-backed staged chunks。
- calc policy、relationship audit、save_as failure recovery。

### Phase 3：benchmark evidence

- 大 worksheet targeted-cell edit matrix。
- transformer vs indexed-staged prototype 对比。
- 记录 input size、target count、source mode、rewrite strategy、memory estimate、output size、open result。

### Phase 4：public gate

- 只有证据充分后，才决定是否开放 public API。

## Definition of Done

大 worksheet rewrite 路径可称为产品化候选的最低标准：

- event reader 和 transformer 可处理大 worksheet，不需要 DOM 或 dense matrix。
- targeted-cell rewrite 有 no-partial-output 验证。
- chunk / staged handoff 有 failure recovery。
- unknown / linked parts 保留或 audit。
- benchmark 记录 source mode、rewrite strategy、耗时、内存估算、输出大小和打开结果。
- public API 尚未开放时，所有文案都明确 internal / planned。
- 若开放 public API，必须通过 C3 design gate 并补 Doxygen、tests、examples、docs。

## 任务模板

```text
任务编号：
目标：
模式：Patch / large worksheet rewrite
Public facade：无 / 待 C3 决策
输入：
输出：
触碰文件：
不触碰文件：
source worksheet 读取策略：
rewrite strategy：
temporary file / chunk 策略：
dependency audit：
失败恢复：
性能验证：
验收标准：
禁止项：
验证命令：
```

## 首批建议任务

### LWR-1 Current evidence inventory

整理 worksheet reader、transformer、cell index、chunk emitter、PackageEditor C5 tests 和 benchmark prototype
的证据边界。

### LWR-2 No-partial-output failure suite

补齐 emit 阶段、staged chunk、stale range、invalid source 的失败状态验证。

### LWR-3 Targeted-cell benchmark matrix

为 transformer 和 indexed-staged internal prototype 形成 opt-in benchmark 矩阵，明确不能进入 release
wording 之前的条件。

### LWR-4 Public API decision brief

用 C3 模板写 public facade 候选方案和不开放方案的取舍。

## 文档-only 验证

```powershell
git diff --check -- docs/LARGE_WORKSHEET_REWRITE_PLAN.md docs/TASK_BREAKDOWN.md
rg -n "LARGE_WORKSHEET_REWRITE_PLAN|LWR-|large worksheet|public transformer|dense matrix" docs/LARGE_WORKSHEET_REWRITE_PLAN.md docs/TASK_BREAKDOWN.md
```

不需要运行 C++ build / CTest，除非同时修改 public header、source、CMake 或测试。
