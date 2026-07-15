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
- Single-pass fallback 当前以 256 KiB bounded output batching 合并输出；Patch reader 在 internal opt-in 模式会先把 bounded window 内 exact writer-compatible numeric、simple inline-string 与 formula complete cell 暴露为单个 callback-lifetime exact-byte span，再由 transformer 将连续 row wrapper、raw text 与 untouched span 合并成 pass-through batch。Rich metadata、attribute 变体、unsupported nested markup、replacement/metadata boundary、非连续 offset 与跨窗口 cell 强制走结构 parser 或 flush，并保留原 diagnostics；formula/sharedStrings/style/inline-string audit 通过 event 与 batch summary 保持可见。有序 upsert 使用单游标推进，仅乱序 source 保留 set fallback。
- Numeric、mixed inline strings、sharedStrings、formula metadata 与 relationship-bearing hyperlink fixture 已形成 event/action telemetry；relationship scanner 已从 action 级输入迁移为只接收 metadata 的 16 KiB 有界批次，并以 prevalidated worksheet-output 模式减少 namespace/attribute slow path。Schema-v6 evidence 已确认 numeric 5M 的 1 call / 167 bytes / 0 slow tags 与 formula + external hyperlink 1M 的 1 call / 303 bytes / 2 slow tags；schema-v8 记录 aggregate simple wrapper，schema-v10/v4 记录 complete-cell，schema-v11/v5 记录 pass-through batch，schema-v12/v6 记录 canonical inline-string，schema-v13/v7 进一步记录 canonical complete-cell count/bytes/formula/inline-string counters，并保留 formula boundary 与 external hyperlink target。
- 继续用更大规模和多数据分布 worksheet fixture 验证 rewritten bytes、temporary footprint、process peak working set、retry 和 unknown-part preservation；当前 5,000,000-cell numeric 以及四类 1,000,000-cell 分布证据不能替代其他机器或任意 XLSX。
- 不通过完整 worksheet DOM、dense cell map 或扩大 In-memory guardrail 实现。

## C6 Performance / Release Evidence

- Streaming 重复策略矩阵已形成同机比较基线；继续补不同数据规模/机器证据前，不把 1,000,000-cell Windows/MSVC 结果泛化。
- Production minizip-ng 已实现 method-matching unchanged entry 的 raw compressed-payload copy；继续维护 exact compressed bytes、logical payload/CRC、unknown-part preservation、method-changing fallback、stored profile 与失败输出不污染回归。不得写成 local-header/central-directory/整包 byte passthrough。
- Targeted strict replace 已采用 DEFLATE one-inflate + target-only direct-range staging；missing-cell upsert、relationship-bearing worksheet 与其他 fallback 已改为 256 KiB output-batched single-pass source-order transform，并保持精确 dimension、relationship audit、retry 与 bounded memory 语义。
- Compression profile 已验证 level 1 在当前 numeric/mixed Streaming 和完整 worksheet rewrite workload 上以约 9.6%–21.6% 输出增长换取更低 save/close CPU；保持 level 选择由 caller 显式控制，不根据单一数据集静默更改 public 默认值。
- Event/action coalescing 已在同机 5,000,000-cell numeric level 1 workload 将 transform 与 residual median 分别降低 11.53%/14.21%，owned output buffer 与 process peak working set 保持有界；该结果只用于记录 workload，不泛化。
- Relationship scanner metadata batching 的 schema-v6 bundle 已完成，确认 input calls/bytes、boundary carry、slow-path tags、formula/hyperlink preservation、256 KiB output buffer 与约 8.26–8.29 MB median process peak working set；该轮 system-load-sensitive total elapsed 不用于提升声明。
- Staged file chunks 的 expected CRC32 已在 production minizip-ng 正常路径按长度合并，并以 completed-entry CRC 校验；失败时才重读定位具体 chunk，不完整 metadata 继续走 per-chunk fallback。Schema-v7 同机 numeric level-6 profile 将 isolated entry residual median 从 131,892 us 降至 603 us，median CRC validation 为 152 us，约 8.34 MB process peak working set 未出现膨胀。
- Simple inline-string fast path 已在 schema-v8 同机 1,000,000-cell mixed-inline level-1 workload 将 parser/source-callback/action traffic 分别降低 22.73%/17.24%/31.23%，transform/residual median 分别降低 21.10%/21.48%，process peak working set median 只变化约 0.004 MB；该结论只覆盖 manifest 记录的 workload。
- Schema-v9/v3 compression CPU profile 已在同一 source 上分离 requested level、package/target-entry process CPU、DEFLATE writer CPU envelope、输出大小与兼容性；numeric/mixed-inline/formula-hyperlink 的 level 1 对 level 6 median save 分别降低 52.76%、67.20%、71.31%，输出分别增大 9.73%、14.51%、18.38%，代表输出均通过 openpyxl，hyperlink target 保留。Public 默认保持不变，由 caller 按 workload 显式选择 level。
- Schema-v10/v4 complete-cell profile 已把 numeric、mixed-inline 与 formula + external hyperlink 中至少 999,982 个完整 source cell 合并为单 callback，source callback 降低 62.50%/62.50%/66.67%，total editor median 降低 51.18%/33.13%/25.85%，median process peak working set 变化小于 0.03 MB。
- Schema-v11/v5 pass-through batching 已把三类 1,000,000-cell level-1 workload 的 transform action/output append 降低 99.82%，batching 至少覆盖 999,482 个 untouched source cell；numeric/formula total median 再降低 21.21%/15.69%，mixed-inline 从 1205 ms 变为 1222 ms，不声明提升。Production/stored CTest 均为 132/132，no-images runtime smoke 与 production/stored/no-images install consumer 均通过。
- Schema-v12/v6 canonical inline-string literal path 已在同机同刻 1,000,000-cell mixed-inline level-1 A/B 将 total/transform/source scan median 降低 14.36%/24.71%/22.62%；333,327 个 payload 全部命中，6 个跨窗口 candidate 保留原 parser，median process peak working set 只增加约 0.004 MB。Numeric/formula-hyperlink guard canonical count 为 0，代表输出通过 openpyxl 且 target 保留；guard elapsed 不用于性能声明。
- Schema-v13/v7 canonical complete-cell path 已在 numeric/mixed-inline/formula-hyperlink 双顺序 1,000,000-cell A/B 中将 parsed events 降低 73.53%–76.92%，source scan/action median 在两种顺序中分别降低 30.01%–54.85%、27.99%–47.39%、36.34%–37.18%，paired peak working set 未增加。Package writer/temporary IO 随执行顺序波动，因此不形成通用 total-editor 提升声明。
- **下一优先级**：在隔离负载下 profile staged temporary output → package target-entry handoff，区分 temporary write、staged read、writer write/close、process CPU 与 OS cache，并保持 stage → package write → state commit、retry、CRC 和 bounded-memory 契约。只有 size-oriented workload 明确要求更高 level 且 minizip-ng 仍不满足时，才建立 matched-level backend comparison；不在无独立依赖、兼容性、CPU、输出大小和内存证据时引入 zlib-ng、并行压缩或自定义 backend。
- 为 Streaming/Patch 增加至少一个更大规模和一台不同机器的 validated bundle；记录冷热启动差异，release claim 使用 warmed repeated protocol，不以单次局部计时替代 bundle。
- 新 bundle 继续提交 machine-readable artifacts、environment、hash、验证状态和 claim-to-artifact 映射；Office 未运行必须保持 `not_run`。
- 性能结论必须满足 `PERFORMANCE_TARGETS.md`。

## C7 Packaging / Dev Tooling

- 评估 `planned-dev` 是否接入真实 CMake target；未接入前不得称为当前 test/benchmark dependency。
