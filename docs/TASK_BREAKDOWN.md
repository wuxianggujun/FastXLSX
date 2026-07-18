# 执行队列

本文是唯一 active queue。历史阶段、已完成流水和删除的计划文档通过 Git history 查询。

## C0 事实与门禁

- 持续核对 public headers、source、tests、CMake 和 `CURRENT_CAPABILITIES.md`。
- 清理 public/internal 注释中的历史 Phase/future wording。
- Public API 变更必须补模式、内存、状态、失败、side effect 和 non-goal Doxygen。
- 保持 public-state standalone tests 的职责边界；结构位移回归按 insertion、formula audit、deletion 分组，新增回归继续进入对应 60 秒 target，不恢复聚合 shard 或专用 timeout。

## C1 Patch Facade

- **已完成基线**：existing-workbook `WorkbookEditor::add_worksheet()` 可事务式追加空白 worksheet，校验名称与 ASCII case-insensitive 重名，分配不冲突的 `sheetId`、relationship id 和 worksheet part path，并协调 stage `xl/workbook.xml`、`xl/_rels/workbook.xml.rels`、`[Content_Types].xml`、新 worksheet part、manifest、public catalog 与 pending/unsaved 状态。提交前失败不污染状态，保存失败保留 retry，成功输出可 reopen；同一 editor 可继续 whole-sheetData replacement、missing-cell Insert upsert 和 rename。
- `add_worksheet()` 不做 worksheet clone，不复制 styles/sharedStrings、tables、drawings、charts、comments、VBA 或其他 linked objects，也不公开 internal package mutation；新增未保存表必须先 `save_as()` 并重新打开，才能进入 In-memory materialization。
- 本切片已通过 production 134/134 CTest、stored-only focused 1/1、no-images runtime smoke，以及 production/stored/no-images 三套独立 install consumer；三份代表输出均通过 OpenPyXL 3.1.2 reopen。Office 未运行，保持 `not_run`。
- **已完成基线**：existing-workbook `WorkbookEditor::remove_worksheet()` 已实现首个关系闭合事务切片。它删除 planned worksheet catalog entry、workbook relationship、worksheet part 和 content-type override，并保留 source catalog、unknown parts、failure-before-commit 与 save retry；`pending_worksheet_edits()` 暴露 `removed` 诊断。最后可见表、`bookViews`/selected tab、definedNames、公式引用、materialized handle、queued worksheet payload、worksheet-owned relationships 和非 workbook inbound relationships 默认 fail。
- 本切片已通过 production 135/135 CTest、stored-only focused 1/1、no-images runtime smoke，以及 production/stored/no-images 三套 install smoke；三个默认 profile 的安装 config 均不含 direct-zlib export dependency。Office 未运行，保持 `not_run`。
- **下一功能主线**：推进 existing-workbook hyperlink/data-validation 等窄 metadata 编辑。每项先定义 preserve/audit/fail/edit、worksheet `.rels`、content types、row-order、formula/structure side effects 和失败恢复，不把 add/remove 的 relationship-closed guardrail扩大成通用对象编辑。
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
- Numeric、mixed inline strings、sharedStrings、formula metadata 与 relationship-bearing hyperlink fixture 已形成 event/action telemetry；relationship scanner 已从 action 级输入迁移为只接收 metadata 的 16 KiB 有界批次，并以 prevalidated worksheet-output 模式减少 namespace/attribute slow path。Schema-v6 evidence 已确认 numeric 5M 的 1 call / 167 bytes / 0 slow tags 与 formula + external hyperlink 1M 的 1 call / 303 bytes / 2 slow tags；schema-v8 记录 aggregate simple wrapper，schema-v10/v4 记录 complete-cell，schema-v11/v5 记录 pass-through batch，schema-v12/v6 记录 canonical inline-string，schema-v13/v7 记录 canonical complete-cell count/bytes/formula/inline-string counters，schema-v14/v8 记录 staged-file prefetch activation/chunks/bytes/peak buffer/read/wait，schema-v15/v9 进一步记录 file IO buffer、writer input peak/call count/maximum call wall time，并保留 formula boundary 与 external hyperlink target。
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
- Schema-v14/v8 staged-read prefetch 已在 Windows production 对至少 4 MiB staged file chunk 使用两个固定 1 MiB buffer；六组双顺序 1,000,000-cell A/B 的前台 input-read median 降低 78.75%–91.79%，completion wait 为 62–135 us，paired process peak working set median 变化不超过 0.02734 MB。Target-entry median 仅五组改善，reverse numeric writer wall time 回退，因此不形成通用 target-entry/total-editor 或物理 OS cache-hit 声明。Production/stored CTest 均为 132/132，no-images runtime smoke 与三类 install consumer 均通过。
- Package-writer schema-v1 升降序 profile 已拆分 write/close wall time、process CPU、调用粒度与 maximum call；512 KiB 相对 1 MiB 的 combined target-entry median 在 numeric/mixed-inline/formula 中降低 4.30%/5.06%/0.85%，maximum call median 降低 41.65%–53.26%，isolated peak working set 减少约 0.99 MiB。Numeric/mixed-inline 两种顺序均改善，formula 反向顺序回退 0.43%；Patch schema-v15/v9 三类代表输出均确认 512 KiB input peak、1 MiB 双 buffer peak、CRC reuse 与 openpyxl 兼容。Production/stored CTest 均为 132/132，no-images runtime smoke 与三类 install consumer 均通过。
- Internal one-pass direct-zlib raw engine 已建立：在 minizip-ng raw-entry handoff 前直接流式调用 zlib，只对至少 4 MiB、非 stored、非 raw-copy 且具备完整 staged CRC metadata 的 entry 生效；small/stored/raw-copy/incomplete-CRC 路径回退 minizip。Engine CPU 只覆盖 zlib API，CRC、staged read、raw-output write 与 entry close 独立计量。Isolated package-writer executable/matrix 已升级为 schema v2，balanced paired runner 使用 schema v1；focused round-trip/guard/CRC mutation、20,000×10 mixed-inline 三 backend matrix 与 paired openpyxl correctness smoke 已通过，public/default backend 保持 minizip-managed DEFLATE。Production、stored 与 direct-zlib profiling CTest 均为 133/133，no-images runtime smoke 与 production/stored/no-images install consumer 均通过；三个默认 profile 的安装 config/targets 均不含 `find_dependency(ZLIB)` 或 `ZLIB::ZLIB`，只有 profiling 安装显式包含两者。
- 当前 dirty revision 的本地 schema-v2/paired-v1 candidate 使用 8 轮 warm-up、16 轮 measured、每个 case/position 各 4 次，覆盖 1,000,000-cell numeric、mixed-inline 与 formula。全部代表输出通过 openpyxl，三类 workload 内各 backend 的 output/worksheet compressed bytes 相同。256 KiB direct-zlib 相对 minizip 的 aggregate pipeline median 为 -0.14%/+0.47%/-0.06%，同轮 paired median 为 +0.49%/-0.19%/+2.35%；32/64 KiB 在三类 aggregate median 均慢 1.10%–4.07%。Zlib API-only CPU 较低，但 overall entry CPU 没有跨 workload 一致改善，CRC 与 raw-output write 基本抵消 encoder 差异；短进程 CPU 以 15.625 ms 粒度跳变，WPR CPU sampling 又被本机 policy `0xc5585011` 拒绝。该 candidate 只用于否定当前采用条件，不是 tracked evidence 或 release claim。
- **Direct-zlib 决策**：保持 internal opt-in 且默认关闭，不生成 tracked bundle、不切换 public/default backend。`FASTXLSX_ENABLE_DIRECT_ZLIB_PROFILING=OFF` 已成为 production/stored/no-images 明确构建边界：默认 library 不包含 direct engine、不直接链接 `ZLIB::ZLIB`，install config 不新增 zlib dependency；benchmark 与 `windows-nmake-release-direct-zlib-profile` 才显式开启。只有 clean revision 上更长时样本同时证明跨 workload wall/overall CPU 收益，并具备可用 ETW/sampling 解释 scheduler/minizip bookkeeping 时才重开采用评估；当前不引入 zlib-ng 或并行压缩。
- Standalone package-writer bundle 必须使用 `package-writer` manifest kind，不能借用 `workbook-editor`；旧的 Patch 合并 evidence 不追溯改类。
- Streaming/Patch 重复矩阵 runner 当前为 schema v3/v15：保留所有 warm-up raw result，分列第一轮 fresh-process observation 与 warmed measured median，并固定标注无 OS cache control、不得声明物理冷缓存。Streaming executable schema v6 新增 generation/package-close/total process CPU 与总账校验；Patch executable schema v20 在 v19 多 worksheet fixture/aggregate telemetry 上增加 rewritten worksheet entry 明细数组，matrix v15 校验 package order、逐字段 sum/max 与 sheet1 legacy 总账；stored backend 数组为空。Internal Patch CRC balanced paired runner 为 schema v3，按反转 rotation 让两个 profile 在两个 position 等频出现，并逐轮验证 logical/compressed fingerprint、CRC、size 与 OpenPyXL。新 runner smoke 均不进入 tracked evidence。
- Dirty revision 的 Streaming v6/v3 本地候选使用 3 warm-up + 7 measured，覆盖 2,000,000-cell numeric/mixed-inline level 1；warmed median 为 731/869 ms、2.736/2.301 million cells/s、约 6.42 MB peak working set。Package close 占 wall 73.1%/68.9%、process CPU 74.4%/67.9%，说明当前 close 主要受真实压缩 CPU 支配而非 file wait；同参数较短 run 已通过 OpenPyXL。
- Patch CRC backend 的 8 warm-up + 16 measured balanced candidate 已完成：相同 minizip package backend 下，minizip CRC 相对 portable 在 2,000,000-cell numeric、1,000,000-cell mixed-inline/formula 的 paired total median 降低 12.05%/10.49%/9.55%，total process CPU 降低 14.51%/9.10%/7.69%，commit 降低 42.70%/29.11%/33.91%。因此 production 保持 minizip CRC，stored-only 保持 portable fallback；`FASTXLSX_ENABLE_PORTABLE_CRC_PROFILING` 仅用于 internal A/B，默认 OFF。
- Patch single-pass 已采用 transform-output fused CRC：在 append 时累计 CRC checkpoints，staged commit 不再二次读取 temporary worksheet，PackageWriter completed-entry CRC 与 mismatch diagnosis 继续保留。相同 balanced protocol 下，numeric/mixed-inline/formula paired total median 降低 28.61%/22.46%/15.50%，total process CPU 降低 24.56%/22.46%/21.30%，commit 从 265/471/230.5 ms 降至 1 ms；CRC 本身 median 为 19.1/30.4/19.8 ms，peak working set 增量不超过约 0.018 MB。Mixed-inline wall 受明显 system load 波动影响、total 只胜出 10/16，但两个 position median、16/16 commit 与 16/16 CPU 均支持保留实现。逐轮 output fingerprint/OpenPyXL、same-size staged mutation output protection/retry、production 133/133、stored focused 2/2、no-images runtime 与三类 install consumer 均通过。这些仍是本机未跟踪结果，不形成 release claim。
- Patch 多 worksheet 顺序 rewrite 当前为 schema-v20/v15，worksheet-scaling runner 为 schema v2。Matched fixed-total 1,000,000-cell、2,000-upsert、6 warm-up + 12 measured 已覆盖 numeric/mixed-inline/formula：4-sheet paired wall ratio 为 1.049/1.088/1.060，CPU ratio 为 1.074/1.081/1.073，peak 降至 8.71/9.54/8.71 MB。1-vs-4 mixed-inline v2 逐 entry 诊断把约 24.5 ms package 增量定位到 writer write envelope：aggregate writer entry 159.824/183.871 ms、read 10.242/11.242 ms、close 0.256/1.089 ms、write 148.518/167.932 ms；四表总 input bytes 更少。三表四 case stored+DEFLATE matrix、scaling-v2 smoke 与代表 OpenPyXL 均通过。系统负载和 fixed-total XML shape 差异仍阻止泛化。
- Clean revision `e85bbd2b` 已增加 validated fixed-shape bundle：mixed-inline/formula 各为 1/2/4-sheet、每表 1,000,000 cells / 1,000 upserts、6 warm-up + 12 measured。四表 paired wall/process CPU ratio 为 3.970/3.898 与 3.803/4.074，rewritten-entry ratio 为 4.288/3.815，paired peak working set 只增至 1.0369×/1.0412×；四表 median 为 3.327 s / 9.36328 MB 与 2.101 s / 8.384765 MB，六个代表输出通过 OpenPyXL 3.1.2。Position 分布仍有明显噪声，Office/第二台机器未运行，不授权并发、direct-zlib 或 public backend 变更。
- **性能后续（非当前功能主线）**：第二台机器的 fixed-shape/fixed-total Patch scaling 复跑作为外部验证 backlog；没有额外机器不阻塞功能开发。只有 release claim 需要时才在当前机器补更大规模 Streaming validated bundle。逐 entry 数据继续只用于解释 DEFLATE restart/write envelope，当前单机分布不授权并发、direct-zlib 或 public backend 变更。Streaming generation/compression overlap 仅在具有 bounded-memory、failure propagation 和 paired wall/CPU/memory/compatibility evidence 方案时重新评估，且不得跳过 staged CRC failure gate。
- 新 bundle 继续提交 machine-readable artifacts、environment、hash、验证状态和 claim-to-artifact 映射；Office 未运行必须保持 `not_run`。
- 性能结论必须满足 `PERFORMANCE_TARGETS.md`。

## C7 Packaging / Dev Tooling

- 评估 `planned-dev` 是否接入真实 CMake target；未接入前不得称为当前 test/benchmark dependency。
