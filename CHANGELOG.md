# Changelog

## Unreleased

### Changed

- Patch single-pass reader 在 generic tag parser 前增加 canonical complete-cell fast path：bounded window 内 exact writer-compatible value、formula、cached-formula 与 simple inline-string cell 直接作为一个 callback-lifetime exact-byte event 输出，并保留 coordinate、formula、sharedStrings、style 与 inline-string audit；attribute 变体、rich/unsupported metadata、malformed markup 和跨窗口 candidate 保留原结构 parser 与 diagnostics。Patch benchmark JSON 升级为 schema v13、matrix 升级为 v7；双顺序 1,000,000-cell A/B 的 parsed events 降低 73.53%–76.92%，三类 source scan/action median 在两种顺序中均下降，paired peak working set 未增加。Package writer/temporary IO 随顺序波动，因此不声明通用 total-editor 提升。
- Patch single-pass reader 对 canonical `<is><t>…</t></is>` 与 `<is><t xml:space="preserve">…</t></is>` 增加 literal terminator/suffix fast path，不复制 payload；attribute/self-closing/rich/unsupported/malformed/跨窗口 candidate 保留原结构 parser 与 diagnostics。Patch benchmark JSON 升级为 schema v12、matrix 升级为 v6，并增加 canonical count/bytes telemetry；同机同刻 1,000,000-cell mixed-inline A/B 的 total/transform/source scan median 分别降低 14.36%/24.71%/22.62%，median process peak working set 只增加约 0.004 MB，结论不泛化到其他 workload。
- Patch transformer 在 reader window 内把连续 row wrapper、raw text 与 untouched complete-cell exact-byte span 合并为 pass-through batch，并用 cell count、coordinate extrema 与 sharedStrings/style/formula flags 保留 dimension/dependency audit；replacement、metadata、非连续 offset 与窗口结束强制 flush。Patch benchmark JSON 升级为 schema v11，matrix 升级为 v5，并新增 batch count/cells/bytes/peak cells telemetry；同机 1,000,000-cell level-1 evidence 中 transform action/output append 降低 99.82%，numeric/formula total editor median 降低 21.21%/15.69%，mixed-inline 按持平记录，median process peak working set 降低 0.21–0.22 MB。
- Patch single-pass reader 新增 internal opt-in complete-cell coalescing：bounded window 内结构完整的 numeric、simple inline-string 与 formula cell 作为一个 callback-lifetime exact-byte span 交给 transformer；rich metadata、unsupported nested markup 与跨窗口 cell 保留详细事件流，formula audit 通过显式 metadata 保持可见。有序 upsert 使用单游标推进，仅乱序 source 保留 set fallback。Patch benchmark JSON 升级为 schema v10，matrix 升级为 v4，并新增 complete-cell count/bytes/fallback telemetry；同机 1,000,000-cell level-1 numeric/mixed-inline/formula + external hyperlink evidence 中 total editor median 分别降低 51.18%/33.13%/25.85%，median process peak working set 变化小于 0.03 MB，结论不泛化到任意 workbook 或全功能胜出。
- Patch benchmark JSON 升级为 schema v9，matrix 升级为 v3，可在同一 source 上比较多个 output compression level，并新增 requested level、package/target-entry process CPU 与 DEFLATE writer CPU envelope；raw compressed-copy entry 的 DEFLATE writer CPU 固定为 0。新增 validated compression CPU bundle 覆盖 1,000,000-cell numeric、mixed-inline 与 formula + external hyperlink targeted upsert；记录 workload 中 level 1 明显降低 save/CPU，public 默认保持不变，结论不泛化为纯 encoder、backend 或任意 workbook 性能。
- Patch coalesced reader 对当前 bounded window 内完整的简单 `<is><t>…</t></is>` 使用 exact-byte fast path；rich/phonetic/extension、namespace alias、跨窗口和 malformed candidate 继续走原 parser，formula markup 保持可见。Patch benchmark JSON 升级为 schema v8，并新增 fast-path count/bytes/fallback telemetry；同机 1,000,000-cell mixed-inline level-1 evidence 中 transform/residual median 分别降低 21.10%/21.48%，process peak working set median 基本不变，结论不泛化到其他 workload。
- Production minizip-ng writer 对具备完整 expected size/CRC32 metadata 的 staged file chunks 使用 entry-level CRC 合并校验，不再在正常写出路径重复逐字节计算 CRC；不完整 metadata 保留原 per-chunk fallback，combined mismatch 才重读并恢复 entry/chunk/file diagnostics，raw-copy 与 stored-bootstrap 语义不变。
- Patch benchmark JSON 升级为 schema v7，新增 target-entry staged CRC reuse、reused file-chunk count 与 validation timing；同机 1,000,000-cell level-6 evidence 中 isolated entry residual median 从 131,892 us 降至 603 us，numeric/formula profile 的 process peak working set median 均约 8.34 MB。该证据不泛化为跨机器或任意 workbook 吞吐结论。
- Patch single-pass relationship audit 不再接收 cell XML action traffic，改为只把 metadata 片段经 16 KiB 有界 buffer 输入 prevalidated scanner；保留 split tag/ignored markup 状态与 namespace shadowing，新增 formula/hyperlink preservation 回归。
- Patch benchmark JSON 升级为 schema v6，在既有 schema-v5 指标上增加 relationship scanner input calls/bytes、boundary carry 与 slow-path tag telemetry；新增 validated v6 bundle 覆盖 5,000,000-cell numeric 与 1,000,000-cell formula + external hyperlink workload，并明确排除受系统负载影响的总耗时提升声明。
- 测试工件改为每进程独立的 system-temp PID 子目录并在正常退出时清理，避免全量 CTest 在共享 flat 目录累积数千个历史 XLSX/PNG/ZIP 文件或产生跨进程路径冲突。
- Streaming worksheet 热路径改为 256 KiB 有界 body batching，减少逐行文件写调用；成功 `close()` 后立即释放 worksheet 临时文件、row/body buffer、sharedStrings 与 styles 等不再使用的资源，失败时仍保留可重试状态。
- Patch missing-cell upsert、relationship-bearing worksheet 与其他 direct-range 不适用场景改为单次 source-order scan；同一扫描完成 replacement/insertion、dimension、relationship audit 与 telemetry，再以 file ranges + 小型 memory chunk staging 输出。
- 重复 Patch rewrite 在新事务提交后立即删除已被替代且不再引用的 owned temporary file，提交前失败仍由 RAII 清理新资源并保留旧状态。
- Benchmark JSON 升级为 schema v5，分列 Streaming generation/package-close/body-buffer/resource-lifecycle 指标，以及 Patch single-pass scan、match/insert、staged bytes 与 transform/commit 指标。
- 新增 3 个 validated evidence bundle：schema-v5 Streaming、Patch single-pass 与 OpenXLSX 0.4.1 reference。限定在同机 1,000,000-cell numeric/mixed public writer workload，FastXLSX 吞吐约为 OpenXLSX 的 2.01×/2.64×，同时保留输出大小、peak working set、协议差异与非泛化边界。
- Production minizip-ng Patch save 对 compression method 匹配的 unchanged entries 使用 raw compressed-payload copy；rewritten、method-changing 与 stored-bootstrap 路径保持原有编码语义。Public/internal 文档明确该能力不复制 local header、central directory、extra fields 或整包布局。
- Patch schema-v5 telemetry 增加 raw-copy entry names/count/bytes，矩阵逐 entry 验证 exact source/output compressed payload；新增 Streaming compression 与 Patch raw-copy 两个 validated evidence bundle，记录 level 1/3/6 的吞吐、输出大小、peak working set 和 openpyxl 结果。
- Patch single-pass rewrite 使用 256 KiB 有界 output batching 合并事件碎片，复用 transformer 已解析 cell coordinate，并减少 relationship scanner 的 namespace-scope 复制；事务、精确 dimension、relationship audit、retry 与 temporary ownership 契约保持不变。
- Patch benchmark telemetry 增加 transform residual、append/flush/peak buffer、relationship/temporary IO 与 package writer target-entry 分解；新增 1,000,000/5,000,000-cell validated rewrite batching bundle。记录 workload 中 level 1 的 1,000,000-cell transform median 降至 0.767 秒，5,000,000-cell process peak working set 为 8.80859 MB；结论不跨机器或泛化到任意 XLSX。
- Patch worksheet reader 在 single-pass rewrite 中合并相邻非公式 value 事件，transformer 合并 pass-through action 并对有序 upsert 复用 next-target；新增 parser/callback/coalesced/action telemetry 与 numeric、mixed inline、sharedStrings、formula + hyperlink evidence。记录的 5,000,000-cell numeric level 1 workload 中 transform/residual median 较紧邻同机基线降低 11.53%/14.21%，process peak working set 保持约 8.79 MB；不泛化到其他机器或任意 XLSX。

## [0.1.0] - 2026-07-13

首个公开 Preview 版本。Public API/ABI 尚未承诺稳定；升级后应重新编译 consumer。

### Added

- 提供三条明确的 public 路径：`WorkbookWriter` Streaming 大型有序创建、`WorkbookEditor` existing-workbook Patch，以及 `WorksheetEditor` 小型稀疏 In-memory 编辑。
- Streaming 支持数字、布尔、字符串、公式文本、日期时间、styles、document properties、data validations、hyperlinks、窄 tables、conditional formatting 和图片插入等当前公开切片。
- Patch 支持 worksheet catalog、targeted cell replace/upsert、sheet rename、document properties、full-calculation request、sheetData replacement、已有 media bytes replacement，以及 unknown package parts 默认 preservation。
- In-memory 支持稀疏单元格读写、行列结构变换、cell transfer、受限 styles 与显式 guardrails；默认以 typed diagnostic 拒绝已知有损 materialization，只有显式 `AllowLossyProjection` 才允许拍平。
- 提供 `WorkbookEditorSaveOptions`、pending/unsaved 双状态水位、失败后 retry，以及 dirty In-memory session 的 stage → package write → state commit 保存契约。
- 提供 Basic CMake install/export package、`FastXLSX::fastxlsx` consumer target、vcpkg manifest、stored-only 与 no-images profiles。

### Changed

- Production/default profile 使用 minizip-ng stored + DEFLATE backend；images 可独立关闭并向 consumer 传播 `FASTXLSX_HAS_IMAGES=0`。
- Calc metadata、sheet rename、document properties、part removal/replacement、worksheet rewrite 与 dirty In-memory save 使用提交前 staging；失败不发布部分状态并保留 retry 能力。
- DEFLATE strict existing-cell Patch 在满足窄前提时使用 one-inflate、target-only scan、owned temporary file 与 file-range staging；minizip writer 复用同路径输入句柄，避免 sparse ranges 反复打开文件。
- 重构 Markdown 治理，以 `docs/CURRENT_CAPABILITIES.md` 为当前能力唯一事实源，以 `docs/TASK_BREAKDOWN.md` 为唯一 active queue。

### Performance Evidence

- Tracked evidence 当前包含 2 个 production Streaming bundle 与 2 个 Patch bundle；均通过标准库 validator，并只支持各自 manifest 限定的单机 workload 结论。
- Windows/MSVC、1,000,000-cell、DEFLATE level 6 Streaming 重复矩阵中，numeric 与重复/混合字符串场景 median 为 1.488–2.562 秒；除 unique sharedStrings 外，process peak working set median 约为 6 MB。
- 相同机器与数据集上的 1,000-cell targeted replace total/mutation median 为 1.529/0.489 秒，process peak working set median 为 7.80859 MB；该结论不覆盖 missing-cell upsert、relationship-bearing worksheet、其他机器或任意 XLSX。

### Known Limitations

- Public API/ABI 尚不稳定；`0.x` 版本可能调整接口。
- 不支持 Zip64、多磁盘 ZIP、atomic in-place save 或接近/超过 ZIP32 边界的 package release claim。
- 不求值公式、不生成 cached values，也不完整重建 `calcChain.xml`。
- Tables、drawings、comments、VBA、pivot、external links 与 custom XML 等 existing-workbook 对象默认 preserve/audit/fail，不等于完整语义编辑。
- `WorksheetEditor` 是 small-file sparse random editing，不是 large-file random editing；大 worksheet 使用 Streaming 创建或有界 Patch 路径。
- Copy-original 保证 logical payload/CRC preservation，不等于 raw compressed-byte passthrough。
- 性能数据只覆盖 tracked manifests 中的一台 Windows/MSVC 机器、指定数据集和 compression 配置，不形成跨机器泛化承诺。

## Versioning Workflow

- `CMakeLists.txt` 的项目版本与 `vcpkg.json` `version-string` 保持一致。
- Release tag 使用 `vMAJOR.MINOR.PATCH`。
- 发布前记录用户可见变化、兼容性、验证证据和已知非目标。
- 在 public header 注释、install/export、CI 和 QA 证据成熟前，不宣称 API 稳定。
