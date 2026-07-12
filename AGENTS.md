# FastXLSX Agent Guide

## 事实源

FastXLSX 是 C++20 / MSVC 2026 优先的 XLSX 创建与编辑库，公开 Streaming、Patch、In-memory 三条路径。当前能力唯一事实源是 `docs/CURRENT_CAPABILITIES.md`，active queue 唯一入口是 `docs/TASK_BREAKDOWN.md`。历史计划和测试流水只查 Git history。

推荐读取顺序：public headers → source → tests → `CURRENT_CAPABILITIES.md` → 设计文档。

## Public / Internal

- Public new-workbook：`WorkbookWriter` / `WorksheetWriter` / `CellView`，以及 small-file `Workbook` / `Worksheet` / `Cell`。
- Public existing-workbook：`WorkbookEditor` Patch facade、borrowed `WorksheetEditor` In-memory editor。
- Internal：`PackageReader`、`PackageEditor`、`PackageWriter`、`EditPlan`、dependency/relationship graph、worksheet transformer/event reader、`CellStore`、materialized session registry。
- 不把 internal hook、fixture 或 preservation test 写成 public API。

## 当前关键契约

- Production/default profile 启用 minizip-ng，支持 stored + DEFLATE package；`windows-nmake-release-stored` 才是 stored-only bootstrap。
- `save_as()` 写新路径，不是 atomic in-place save。
- `has_pending_changes()` 表示 retained staged state；`has_unsaved_changes()` 表示最近成功保存后的 watermark delta。
- Dirty In-memory session 使用 stage → package write → state commit；写出失败必须保留 dirty diagnostics、pending/unsaved count 和 retry 能力。
- `WorksheetEditorOptions` 默认 `RejectKnownLosses`；rich/phonetic/extension、formula metadata、cached result 等已知损失抛 `WorksheetMaterializationError`，提供稳定 category 与 worksheet/cell/sharedStrings context。只有显式 `AllowLossyProjection` 才能拍平，且 policy 是 session identity 的一部分。
- In-memory 是 small-file sparse random editing，不是 large-file low-memory random editing。
- Patch 默认 copy-original；unknown part 默认保留；existing-file 功能必须写清 preserve/audit/fail/edit。
- Calc metadata、sheet rename、internal document-properties、internal part removal 与 materialized small-part replacement 联动必须先在 plan/replacements/omitted entries/manifest/public diagnostics 副本完成，再以 noexcept commit 发布；提交前失败保留调用前状态并可重试。
- 公式不求值、不生成 cached value、不完整重建 calcChain。
- `replace_image()` 只替换已有 media bytes；`add_image()` 是 new-workbook insertion，均不是完整 drawing 编辑。
- `FASTXLSX_ENABLE_IMAGES=OFF` 不需要 stb，public image symbol 调用抛错，`FASTXLSX_HAS_IMAGES=0` 传播给 consumer。

## 架构约束

- 大型 worksheet/sharedStrings、批量导出和大型模板 rewrite 禁止完整 DOM/dense matrix。
- 小型 workbook metadata part 可局部结构化处理，但不自动形成对象语义支持。
- Streaming hot path 不承担所有 feature 细节；独立 feature 优先独立 serializer/helper/test。
- 新 `.cpp`、test、example、benchmark 或 smoke target 必须同步 CMake。

## 依赖

- vcpkg 默认 features：`runtime-minizip`、`images`。
- `planned-xml` 只包含当前未链接的 zlib-ng/Expat/pugixml。
- `planned-dev` 当前未自动接线 Catch2/Google Benchmark。
- OpenXLSX、xlnt 等只作 reference benchmark。
- 默认不用 FetchContent 或 vendoring。

## 构建

```powershell
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

Stored bootstrap：`windows-nmake-release-stored`。No-images：`windows-nmake-release-no-images`，并运行 `build\windows-nmake-release-no-images\fastxlsx_image_disabled_smoke.exe`。

普通终端缺少 `cl`/`nmake` 时必须先加载 VS `vcvars64.bat`；不要把环境失败误判为代码失败。

## 验证

- 代码：focused test → production CTest → 相关 profile → install/consumer smoke。
- Patch：failure-before-state-change、retry、reopen、unknown part、relationship/content type/calc side effects，以及跨状态 mutation 的提交前故障注入。
- In-memory：typed strict diagnostics、explicit lossy、generic policy mismatch、guardrail、no-state-pollution、two-phase save handoff、post-stage failure retry、move/handle lifecycle。
- Streaming：row order、无 DOM/dense matrix、package side effects。
- CTest 普通上限 60 秒；public-state 测试已全部拆为 standalone targets，不再保留专用 120 秒 legacy shard。
- Benchmark：只有 `benchmarks/evidence/` 中通过 validator 的 bundle 可用于 release claim；0 bundle 表示当前无 tracked evidence。
- 文档：Markdown links、UTF-8/LF、deleted-doc refs、high-risk wording、`git diff --check`。

## 项目 Skills

位于 `.agents/skills/`：project navigation、API design/docs、CMake build、dependency policy、Streaming worksheet、OPC editing、test quality，以及各 feature-specific skills。使用前先读对应 `SKILL.md`，但 skills 不能覆盖 public headers/source/tests 的事实优先级。
