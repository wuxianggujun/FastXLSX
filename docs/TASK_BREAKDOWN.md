# 执行队列

本文是唯一 active queue。历史阶段、已完成流水和删除的计划文档通过 Git history 查询。

## C0 事实与门禁

- 持续核对 public headers、source、tests、CMake 和 `CURRENT_CAPABILITIES.md`。
- 清理 public/internal 注释中的历史 Phase/future wording。
- Public API 变更必须补模式、内存、状态、失败、side effect 和 non-goal Doxygen。

## C1 Patch Facade

- 扩展现有 workbook 对象前，逐对象定义 preserve/audit/fail/edit。
- 稳定 relationship/content-type/calc metadata 联动和 failure recovery。
- 不公开 package mutation foundation。

## C2 In-memory

- 扩展 strict projection diagnostics，使调用方能审计具体 loss category，而不泄漏 internal parser 状态。
- 继续验证 guardrail、policy identity、dirty flush、move/handle lifecycle。
- 大 worksheet 低内存 rewrite 不进入 `WorksheetEditor`。

## C3 Streaming

- 保持 row-order hot path 与 feature-specific serializer/test 拆分。
- 新增 metadata/media/style 功能时记录 package side effects 与内存增长。

## C4 Complex Objects

- Tables、drawings、comments、VBA、pivot、external links、custom XML 等默认 preserve/audit/fail。
- 只有实现 part + relationship + content type + linked metadata 联动后才能标记 edit。

## C5 Large Worksheet Rewrite

- 单独设计 file-backed/chunked rewrite、行索引和 source-order transformation。
- 不通过完整 worksheet DOM、dense cell map 或扩大 In-memory guardrail 实现。

## C6 Performance / Release Evidence

- 生成真实 benchmark bundle 时提交 machine-readable artifacts、environment、hash 和 claims。
- 为 validator 增加 CI 静态运行；0 bundle 仍应成功。
- 性能结论必须满足 `PERFORMANCE_TARGETS.md`。

## C7 Packaging

- 在可用 MSVC Developer 环境完成 production/stored/no-images build + CTest/smoke。
- 完成三种 profile 的 install/consumer `find_package()` smoke，确认 minizip 条件依赖与 stb 非传播边界。
- 评估 `planned-dev` 是否接入真实 CMake target；未接入前不得称为当前 test/benchmark dependency。