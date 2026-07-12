# 执行队列

本文是唯一 active queue。历史阶段、已完成流水和删除的计划文档通过 Git history 查询。

## C0 事实与门禁

- 持续核对 public headers、source、tests、CMake 和 `CURRENT_CAPABILITIES.md`。
- 清理 public/internal 注释中的历史 Phase/future wording。
- Public API 变更必须补模式、内存、状态、失败、side effect 和 non-goal Doxygen。
- 保持 public-state standalone tests 的职责边界；结构位移回归按 insertion、formula audit、deletion 分组，新增回归继续进入对应 60 秒 target，不恢复聚合 shard 或专用 timeout。

## C1 Patch Facade

- 扩展现有 workbook 对象前，逐对象定义 preserve/audit/fail/edit。
- 将 calc metadata 已采用的副本 staging + noexcept commit 边界扩展到其他跨 relationship/content-type/manifest 的多状态 mutation，并逐项补 failure recovery 回归。
- 不公开 package mutation foundation。

## C2 In-memory

- 新增可投影语义时同步扩展 strict/lossy 分类、typed diagnostic 和 failure-before-state-change tests，不把 parser/XML 细节加入 public contract。
- 新增 In-memory mutation/projection 时复用 guardrail、policy identity、two-phase save handoff、move/handle lifecycle 回归矩阵。
- 大 worksheet 低内存 rewrite 不进入 `WorksheetEditor`。

## C3 Streaming

- 保持 row-order hot path 与 feature-specific serializer/test 拆分。
- 新增 metadata/media/style 功能时记录 package side effects 与内存增长。

## C4 Complex Objects

- Tables、drawings、comments、VBA、pivot、external links、custom XML 等默认 preserve/audit/fail。
- 只有实现 part + relationship + content type + linked metadata 联动后才能标记 edit。

## C5 Large Worksheet Rewrite

- 先建立 source-order transform contract、支持/拒绝的 worksheet metadata 清单和 failure-before-state-change 语义，再公开 file-backed/chunked rewrite；不要先承诺任意随机编辑。
- 用实际大型 worksheet fixture 验证 rewritten bytes、temporary footprint、process peak working set、retry 和 unknown-part preservation。
- 不通过完整 worksheet DOM、dense cell map 或扩大 In-memory guardrail 实现。

## C6 Performance / Release Evidence

- Streaming 重复策略矩阵已形成同机比较基线；继续补不同数据规模/机器证据前，不把 1,000,000-cell Windows/MSVC 结果泛化。
- 下一优先级是 Patch evidence：no-op copy、targeted cell patch、small metadata rewrite、large worksheet replacement，并区分 copied/rewritten bytes 与 source/output package size。
- 新 bundle 继续提交 machine-readable artifacts、environment、hash、验证状态和 claim-to-artifact 映射；Office 未运行必须保持 `not_run`。
- 性能结论必须满足 `PERFORMANCE_TARGETS.md`。

## C7 Packaging / Dev Tooling

- 评估 `planned-dev` 是否接入真实 CMake target；未接入前不得称为当前 test/benchmark dependency。
