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

- 维护现有 one-inflate direct-range 与 single-pass source-order transform 的支持/拒绝 metadata 清单、failure-before-state-change、dimension 和 relationship audit 契约；新增 worksheet metadata 前先扩展 audit/fail 边界。
- Single-pass fallback 当前以 256 KiB bounded output batching 合并事件碎片，并复用 transformer 已解析 coordinate；后续优化必须继续保持 exact output、retry、unknown-part preservation 和 temporary-file ownership，不把顺序 rewrite 包装成任意随机编辑。
- 下一实现批次聚焦 source XML event parsing/action residual；先用 numeric、mixed strings、sharedStrings、formula metadata 与 relationship-bearing fixture 判断热点是否一致，再决定 parser、action representation 或 scanner 优化。
- 继续用更大规模和多数据分布 worksheet fixture 验证 rewritten bytes、temporary footprint、process peak working set、retry 和 unknown-part preservation；当前 5,000,000-cell numeric 证据不能替代其他分布或机器。
- 不通过完整 worksheet DOM、dense cell map 或扩大 In-memory guardrail 实现。

## C6 Performance / Release Evidence

- Streaming 重复策略矩阵已形成同机比较基线；继续补不同数据规模/机器证据前，不把 1,000,000-cell Windows/MSVC 结果泛化。
- Production minizip-ng 已实现 method-matching unchanged entry 的 raw compressed-payload copy；继续维护 exact compressed bytes、logical payload/CRC、unknown-part preservation、method-changing fallback、stored profile 与失败输出不污染回归。不得写成 local-header/central-directory/整包 byte passthrough。
- Targeted strict replace 已采用 DEFLATE one-inflate + target-only direct-range staging；missing-cell upsert、relationship-bearing worksheet 与其他 fallback 已改为 256 KiB output-batched single-pass source-order transform，并保持精确 dimension、relationship audit、retry 与 bounded memory 语义。
- Compression profile 已验证 level 1 在当前 numeric/mixed Streaming 和完整 worksheet rewrite workload 上以约 9.6%–21.6% 输出增长换取更低 save/close CPU；保持 level 选择由 caller 显式控制，不根据单一数据集静默更改 public 默认值。
- **下一优先级**：level 6 完整 worksheet rewrite 已测得 package writer 高于 transform，优先比较现有 minizip-ng/zlib 与可选 backend/参数的 encode CPU、输出大小、兼容性和依赖成本；同时继续拆 source scan/action residual。只有证据显示 CPU 可并行且事务/内存边界可控时才评估并行压缩。
- 为 Streaming/Patch 增加至少一个更大规模和一台不同机器的 validated bundle；记录冷热启动差异，release claim 使用 warmed repeated protocol，不以单次局部计时替代 bundle。
- 新 bundle 继续提交 machine-readable artifacts、environment、hash、验证状态和 claim-to-artifact 映射；Office 未运行必须保持 `not_run`。
- 性能结论必须满足 `PERFORMANCE_TARGETS.md`。

## C7 Packaging / Dev Tooling

- 评估 `planned-dev` 是否接入真实 CMake target；未接入前不得称为当前 test/benchmark dependency。
