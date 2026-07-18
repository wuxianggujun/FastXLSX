---
name: fastxlsx-project-navigation
description: "导航 FastXLSX 架构、public/internal 边界、当前能力、功能缺口和专项实现入口。用于开始项目分析、选择 Streaming/Patch/In-memory 路径、判断功能是否已公开，以及把任务路由到正确模块和项目 skill。"
---
# FastXLSX Project Navigation

## 事实顺序

先读 public headers，再核对 source、tests、`docs/CURRENT_CAPABILITIES.md`、CMake 和 `docs/TASK_BREAKDOWN.md`。历史计划只查 Git；internal hook、fixture、preservation test 和 benchmark instrumentation 不形成 public 能力。

## 三条 Public 路径

- Streaming：`WorkbookWriter` / `WorksheetWriter` / `CellView`，用于大型有序新建和低内存导出。
- Patch：`WorkbookEditor`，用于已有文件的 part-level copy/rewrite/remove 和定点 worksheet rewrite。
- In-memory：borrowed `WorksheetEditor`，用于受 guardrail 限制的小文件稀疏随机编辑。

## 当前能力与缺口

- Streaming 已覆盖基础 cell、styles、worksheet metadata、窄 tables/conditional formatting 和 PNG/JPEG insertion；不允许历史行随机修改。
- Patch 已覆盖 catalog、事务式空白 worksheet add、关系闭合 worksheet remove、sheetData/cell replacement、窄 rename、formula audit/recalculation、core/app properties 和 media bytes replacement；当前没有 public existing-workbook worksheet clone。新增表可同会话用 Patch 填充/rename，但需保存重开后才能 In-memory materialize。
- In-memory 已覆盖 sparse reads/writes、range/row/column mutation、structural shifts、cell/value/style transfer 和 two-phase save；它不修复 linked worksheet objects，也不是 large-file random editor。
- 当前没有 public bounded-memory worksheet reader。公式不求值、不生成 cached result、不完整重建 calcChain。
- Tables、drawings、charts、comments、VBA、pivot、external links 和 custom XML 默认只可 preserve/audit/fail，不能因保留测试宣称 semantic edit。

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
