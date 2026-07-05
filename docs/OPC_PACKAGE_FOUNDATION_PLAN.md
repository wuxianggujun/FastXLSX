# OPC Package Foundation 产品化计划

## 文档定位

本文是 FastXLSX internal OPC / package foundation 的产品化计划，覆盖 `PackageReader`、
`PackageEditor`、`EditPlan`、`DependencyAnalyzer`、`RelationshipGraph`、`PartIndex`、
content types、relationships、write modes 和 package write-out 质量门禁。

本文强调 internal foundation。除非未来经过单独 public API design gate，否则这些类型不能写成
用户可依赖的 public package editing API。

当前能力事实仍以 [CURRENT_CAPABILITIES.md](CURRENT_CAPABILITIES.md) 为准。

所属路径：

- 模式：Internal foundation for Patch / In-memory / Streaming package output。
- Public facade：无；它支撑 `WorkbookWriter`、`Workbook`、`WorkbookEditor`、`WorksheetEditor`。
- 任务入口：`TASK_BREAKDOWN.md` 中的 C1、C2、C5、C6。
- 核心约束：OPC foundation 可以解释实现边界和审计证据，但不能被包装成 public surface。

## 当前基线

当前 internal foundation 已覆盖：

- package entries 读取和写出基础。
- content types manifest 与 relationships ingestion / serialization。
- `PartIndex` 和 `RelationshipGraph` internal views。
- copy-original、generate-small-XML、local-DOM-rewrite、stream-rewrite 的 write-mode metadata。
- registered part removal 和 package-entry audit。
- worksheet replacement、sheetData replacement、docProps generated-small-XML、calc metadata policy。
- unknown entry preservation、linked part preservation fixture、relationship target audit。
- stored bootstrap writer 和 opt-in minizip-ng / DEFLATE backend。

但产品化差距仍在：

- internal write modes、audit reason、output plan 和 public diagnostics 的边界需要持续收敛。
- relationship / content type failure policy 需要覆盖更多 malformed / decoy / unsupported cases。
- staged chunks、temporary files、copy-original bytes 和 CRC / size validation 需要持续硬化。
- 文档必须避免把 internal evidence 写成 broad existing-file support。

## 非目标

以下内容不属于本计划：

- 不公开 public `PackageEditor`、public `EditPlan` 或 public package-entry mutation API。
- 不承诺 full OPC repair、relationship pruning、orphan cleanup 或 linked-part regeneration。
- 不实现完整 XML schema validation 或 malformed XML repair。
- 不承诺 Zip64、data descriptor、encrypted input、multi-disk 或所有 compression method 支持。
- 不把 preservation evidence 写成 tables / drawings / VBA / custom XML semantic editing。
- 不让 public API 依赖 internal package-entry reason 或 output-plan layout。

## 模块边界

Reader：

- 读取 package entries。
- 校验 entry name、payload CRC、supported method 和 metadata root。
- ingest `[Content_Types].xml` 与 `.rels`。
- 构建 internal part / relationship views。

Planner：

- 记录 part-level write modes。
- 记录 package-entry rewrite / omission / preservation audit。
- 生成 conservative dependency summary。
- 不承担 semantic object model。

Editor：

- 对 existing package 做 copy-original / replace / remove / generated-small-XML / stream-rewrite。
- 处理 calc metadata 和 limited workbook / worksheet metadata rewrite。
- 维护 no-state-pollution。

Writer：

- 汇总 source entries 和 generated entries。
- 使用 stored bootstrap 或 opt-in minizip-ng backend 输出 package。
- 保持 backend-neutral OpenXML semantics。

## 数据流

```text
PackageReader::open()
-> entries / content types / relationships ingested
-> PartIndex / RelationshipGraph internal view
-> edit request creates or updates internal plan
-> write modes classify each affected part
-> PackageEditorOutputPlan materializes final entries
-> PackageWriter writes output package
```

write-mode 语义：

- `CopyOriginal`：source entry bytes 原样进入输出。
- `GenerateSmallXml`：由 helper 生成小型 XML part。
- `LocalDomRewrite`：小型 XML part 局部 DOM / bounded rewrite。
- `StreamRewrite`：大型 part 或 worksheet rewrite 走 stream/chunk source。
- `RemovedPart` / omitted entry audit：记录显式移除或由策略省略的 part / package entry。

## 状态流转

推荐状态：

```text
reader opened
-> manifest and indexes built
-> edit plan clean
-> edit preflight
-> plan staged
-> output plan built
-> save_as writes package
```

失败策略：

- reader ingestion 失败应阻止后续 editing。
- edit preflight 失败不能污染 manifest、edit plan、package-entry audit 或 output bytes。
- staged temporary files 必须有明确生命周期。
- save failure 不能破坏 source package，也不能留下 public editor 不可解释状态。

## Workstreams

### OPC-1 Reader guardrails

目标：强化 ZIP / OPC 输入的拒绝边界。

验收：

- illegal entry names、encrypted flags、unsupported methods、Zip64、data descriptor、
  malformed metadata root、duplicate relationship id、conflicting content types 有测试。
- 默认 stored reader 和 opt-in minizip reader 的行为分层清楚。

### OPC-2 Relationship target audit

目标：让 linked part dependency 以 conservative audit 形式稳定传播。

验收：

- owner part、relationship id、type、raw target、normalized target 可用于 internal audit。
- external、URI-qualified、invalid、unresolved targets 分层记录。
- audit 不被写成 repair。

### OPC-3 Content types and relationships rewrite hygiene

目标：确保 generated / removed / restored parts 对 package metadata 的 side effects 可预测。

验收：

- content type default / override 不被无故提升或丢失。
- source-owned `.rels` preserve / omission 策略明确。
- owner-missing `.rels` 和 decoy metadata 有拒绝或 audit。

### OPC-4 Staged chunk and temp-file lifecycle

目标：保证 stream-rewrite / indexed-staged 路径不退化为 full in-memory materialization。

验收：

- chunk range、staged size、source range 和 CRC / size guard 有验证。
- failed staging 清理临时资源。
- benchmark-only instrumentation 不写成 runtime memory guarantee。

### OPC-5 Output plan diagnostics boundary

目标：internal output plan 足够用于测试和 QA，但 public facade 不依赖其结构。

验收：

- tests 可以断言 internal output plan。
- public docs 只暴露 facade diagnostics。
- `CURRENT_CAPABILITIES.md` 明确 internal foundation。

## 阶段顺序

### Phase 0：internal/public 文案清理

- 全文档检查 `PackageEditor` / `EditPlan` 是否被写成 public API。

### Phase 1：reader 和 metadata guardrails

- input validation、content types、relationships。

### Phase 2：edit plan 和 output plan hygiene

- write modes、removed-part audit、relationship audit、no-state-pollution。

### Phase 3：stream/chunk handoff

- large worksheet rewrite、temporary files、package writer。

### Phase 4：release evidence

- backend-neutral OpenXML checks、stored/minizip differences、Excel/openpyxl smoke。

## Definition of Done

OPC foundation 产品化完成的最低标准：

- internal write modes 和 audit reason 在 tests 中可验证。
- reader 对 unsupported / malformed input 有明确拒绝边界。
- content types / relationships rewrite 不静默破坏 unknown parts。
- copy-original bytes 和 staged chunks 有结构测试。
- failures do not pollute edit plan, manifest, package-entry audit, or output bytes。
- public docs 只把这些类型称为 internal foundation。
- backend differences 不影响 OpenXML semantic assertions。

## 任务模板

```text
任务编号：
目标：
模式：Internal OPC foundation
Public facade：无
输入：
输出：
触碰文件：
不触碰文件：
reader guard：
write mode：
content types / relationships side effects：
audit metadata：
failure recovery：
验证命令：
禁止项：
```

## 首批建议任务

### OPC-1 Internal API wording scan

用 `rg` 检查 docs 中 internal 类型是否被 public 化，并修正入口文案。

### OPC-2 Relationship target audit fixture map

整理 worksheet-owned、drawing-owned、external、URI-qualified、invalid、unresolved target 的现有 fixture
和缺口。

### OPC-3 Content type rewrite regression set

补齐 generated / removed / restore 顺序下 content types 和 owner `.rels` 的回归测试表。

### OPC-4 Chunk-source failure hygiene

验证 staged chunk invalid range、stale size、temp-file failure 和 no partial output。

## 文档-only 验证

```powershell
git diff --check -- docs/OPC_PACKAGE_FOUNDATION_PLAN.md docs/TASK_BREAKDOWN.md
rg -n "OPC_PACKAGE_FOUNDATION_PLAN|OPC-|public PackageEditor|public EditPlan|internal foundation" docs/OPC_PACKAGE_FOUNDATION_PLAN.md docs/TASK_BREAKDOWN.md
```

不需要运行 C++ build / CTest，除非同时修改 public header、source、CMake 或测试。
