# 性能与发布证据计划

## 文档定位

本文规划 FastXLSX 的 benchmark、OpenXML 结构验证、Excel / openpyxl QA、CI、packaging 和 release
wording 证据链。它不是性能宣传稿，而是发布前判断哪些表述可以写、哪些必须保守的质量门。

当前能力事实仍以 [CURRENT_CAPABILITIES.md](CURRENT_CAPABILITIES.md) 为准；性能目标和历史结果见
[PERFORMANCE_TARGETS.md](PERFORMANCE_TARGETS.md)。

所属路径：

- 模式：Streaming / Patch / In-memory 都适用。
- Public facade：所有 public API 的 release wording gate。
- 任务入口：`TASK_BREAKDOWN.md` 中的 F4、C6、C7。
- 核心约束：没有 benchmark / QA 证据，不宣称高性能、低内存、生产级大文件编辑或完整 Excel parity。

## 当前基线

当前项目已有：

- 默认 CTest 测试入口和 60s 核心边界。
- OpenXML package structure tests。
- opt-in minizip-ng backend。
- streaming writer benchmark 工具和本地 benchmark matrix runner。
- Excel COM / openpyxl / XlsxWriter 本地 QA helper 的若干功能入口。
- package reader/editor、streaming writer、image、conditional formatting、styles、sharedStrings 等结构测试。

当前仍需强化：

- benchmark 矩阵覆盖真实数据形态和多 backend。
- Patch / large worksheet rewrite 的 benchmark 和 memory evidence。
- release wording 与具体证据绑定。
- CMake install / export / package config / vcpkg feature 的发布验证。
- benchmark JSON schema 变更与文档同步，避免 schema 版本漂移。

## 非目标

本计划不做以下事情：

- 不把 benchmark 加入默认 CTest 或默认 CI。
- 不把单机小样本结果写成泛化性能承诺。
- 不用 worksheet-body-only footprint 代替进程 peak memory。
- 不把 openpyxl load 成功写成 Excel 完全兼容。
- 不把 Excel COM smoke 写成跨平台 CI 依赖。
- 不把 benchmark 对比库写成 FastXLSX runtime dependency。

## 证据分层

推荐按四层记录证据：

```text
unit / structure tests
-> local QA helper
-> benchmark matrix
-> release wording approval
```

结构测试：

- ZIP entries、content types、relationships、worksheet XML、styles、sharedStrings、drawing、table、
  conditional formatting、calc metadata。

本地 QA：

- openpyxl load / inspect。
- XlsxWriter reference 对比。
- Excel COM read-only smoke。
- LibreOffice / WPS 可选 smoke。

benchmark：

- explicit opt-in。
- 记录 dataset、backend、compression、string strategy、source mode、rewrite strategy、wall time、
  memory / estimate、output size、temporary footprint、open result。

release wording：

- 只能引用已有 evidence。
- 必须写清限制条件。

## 数据字段

每个 benchmark case 至少记录：

- scenario。
- rows、columns、sheets、cells。
- string ratio / string pattern。
- string strategy：inlineStr / sharedStrings。
- ZIP backend：stored bootstrap / minizip-ng。
- compression setting。
- package entry source mode。
- rewrite strategy：new workbook / transformer / indexed-staged / in-memory projection 等。
- elapsed wall time。
- peak memory 或明确的 estimate 类型。
- output bytes。
- temporary worksheet footprint，并标明是否只覆盖 worksheet body row XML。
- Office / openpyxl / Excel COM 结果。
- build preset、compiler、CPU/RAM/OS、date。
- benchmark JSON schema version；schema 变更必须同步文档和 parser。

## Streaming benchmark matrix

建议覆盖：

- 1M、10M、50M cells。
- numeric-only。
- mixed numeric/text。
- repeated strings。
- high-cardinality unique strings。
- inlineStr vs sharedStrings。
- stored bootstrap vs minizip-ng。
- compression level 0 / default / selected low levels。

发布表述要求：

- 可以说“在某数据集、某 backend、某策略下测得结果”。
- 不能说“总是低内存”“总是最快”“完整生产级大文件支持”。
- sharedStrings 必须同时给出体积收益和内存/临时文件成本。

## Patch / large rewrite benchmark matrix

建议覆盖：

- existing worksheet size：100k、1M、10M rows 或按 cells 计。
- edit count：single cell、1k targets、100k targets。
- rewrite strategy：transformer、indexed-staged internal prototype。
- source mode：stored、minizip if available。
- linked object presence：plain worksheet、with table/drawing/chart fixtures。
- output open result。

发布表述要求：

- internal prototype 不能写成 public 默认算法。
- file-backed / chunked handoff 不能写成完整低内存保证，除非有 peak memory evidence。
- object preservation 不能写成 semantic editing。

## In-memory evidence

建议覆盖：

- materialization size vs `max_cells` / `memory_budget_bytes`。
- sparse store mutation count。
- batch failure no-state-pollution。
- dirty-session `save_as()` success / failure recovery。
- row/column shift with formulas and style handles。

发布表述要求：

- 只说 small-file random editing。
- 不说 large-file low-memory random editing。
- memory estimate 不写成 RSS。

## CI 和本地 QA

默认 CI：

- 配置、构建、默认 CTest。
- opt-in minizip job 可独立运行。
- 不依赖 Excel COM。

本地 QA：

- Excel COM read-only smoke。
- openpyxl / XlsxWriter comparison。
- benchmark matrix runner。
- package unzip / XML diff。

文档要求：

- 每个 helper 写清是否进入 CI。
- 每个 helper 写清它验证什么、不验证什么。
- QA 输出不能反写 benchmark case 的原始结果，除非 schema 明确支持。

## Packaging / release evidence

发布前应验证：

- CMake target `fastxlsx` 和 alias `FastXLSX::fastxlsx`。
- install / export / package config 是否可用。
- headers 安装路径。
- vcpkg manifest feature，尤其 opt-in minizip-ng。
- license 文件和第三方 license 记录。
- examples 是否能构建。
- README quick start 与 public headers 一致。
- Doxygen public API 注释齐全。

## Workstreams

### PRE-1 Benchmark schema governance

目标：防止 benchmark JSON schema、runner、文档互相漂移。

验收：

- schema version、字段、parser、report 文档同步。
- 旧结果如何读取或废弃有说明。
- `office_open` / sidecar QA 的写入规则清楚。

### PRE-2 Streaming matrix

目标：形成 release 可引用的 streaming evidence。

验收：

- 至少覆盖 1M / 10M cells。
- inlineStr / sharedStrings 和 stored / minizip 有对比。
- openpyxl 或 Excel smoke 结果记录。

### PRE-3 Patch matrix

目标：为 existing-file edit 的性能和保真建立证据。

验收：

- targeted-cell、sheetData replacement、large rewrite prototype 分开记录。
- linked object fixture 的 preservation evidence 单独记录。
- 不把 prototype 写成 public default。

### PRE-4 Release wording gate

目标：发布前逐句审查 README、API docs、release notes。

验收：

- 每一句性能或兼容性声明都能追溯到证据。
- 未实现能力保持 planned / non-goal。
- 模糊词如“完整支持”“生产级”“低内存”必须带条件或删除。

## 阶段顺序

### Phase 0：证据字段收敛

- 统一 benchmark 字段和 QA 输出。
- 文档同步 schema 变更。

### Phase 1：Streaming release evidence

- new-workbook export 是当前最适合优先形成证据的主线。

### Phase 2：Patch / In-memory evidence

- save_as、preservation、targeted-cell、small-file random editing。

### Phase 3：Packaging

- install/export、vcpkg、examples、Doxygen。

### Phase 4：Release wording approval

- 审查 README、CURRENT_CAPABILITIES、API docs、release notes。

## Definition of Done

发布证据完成的最低标准：

- 所有性能声明都有 benchmark case 支撑。
- 所有兼容性声明有结构测试或 QA helper 支撑。
- benchmark JSON schema 与 docs / runner 同步。
- 默认 CTest 和 opt-in benchmark 边界清楚。
- Excel COM 只作为本地 read-only smoke，不是 CI 强依赖。
- release wording 不扩大 current capabilities。

## 任务模板

```text
任务编号：
目标：
模式：Streaming / Patch / In-memory / Packaging
Public facade：
数据集：
backend / compression：
string strategy：
rewrite strategy：
metrics：
QA 工具：
release wording：
触碰文件：
验证命令：
禁止项：
```

## 首批建议任务

### PRE-1 Benchmark schema audit

检查 benchmark runner、JSON 字段、`PERFORMANCE_TARGETS.md` 和 AGENTS / skill 文本是否存在 schema
版本或字段漂移，形成修正任务。

### PRE-2 Streaming matrix runbook

写清 stored/minizip、inline/shared、1M/10M cells 的本地运行命令和结果记录模板。

### PRE-3 Release wording checklist

为 README、API docs、release notes 建立逐句证据检查表。

### PRE-4 CMake install/export validation plan

补齐发布前 packaging 验证任务，不混入性能 benchmark。

## 文档-only 验证

```powershell
git diff --check -- docs/PERFORMANCE_AND_RELEASE_EVIDENCE_PLAN.md docs/TASK_BREAKDOWN.md
rg -n "PERFORMANCE_AND_RELEASE_EVIDENCE_PLAN|PRE-|benchmark JSON schema|release wording" docs/PERFORMANCE_AND_RELEASE_EVIDENCE_PLAN.md docs/TASK_BREAKDOWN.md
```

不需要运行 C++ build / CTest，除非同时修改 public header、source、CMake 或测试。
