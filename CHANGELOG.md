# Changelog

## Unreleased

### Changed

- Streaming worksheet 热路径改为 256 KiB 有界 body batching，减少逐行文件写调用；成功 `close()` 后立即释放 worksheet 临时文件、row/body buffer、sharedStrings 与 styles 等不再使用的资源，失败时仍保留可重试状态。
- Patch missing-cell upsert、relationship-bearing worksheet 与其他 direct-range 不适用场景改为单次 source-order scan；同一扫描完成 replacement/insertion、dimension、relationship audit 与 telemetry，再以 file ranges + 小型 memory chunk staging 输出。
- 重复 Patch rewrite 在新事务提交后立即删除已被替代且不再引用的 owned temporary file，提交前失败仍由 RAII 清理新资源并保留旧状态。
- Benchmark JSON 升级为 schema v5，分列 Streaming generation/package-close/body-buffer/resource-lifecycle 指标，以及 Patch single-pass scan、match/insert、staged bytes 与 transform/commit 指标。
- 新增 3 个 validated evidence bundle：schema-v5 Streaming、Patch single-pass 与 OpenXLSX 0.4.1 reference。限定在同机 1,000,000-cell numeric/mixed public writer workload，FastXLSX 吞吐约为 OpenXLSX 的 2.01×/2.64×，同时保留输出大小、peak working set、协议差异与非泛化边界。
- Production minizip-ng Patch save 对 compression method 匹配的 unchanged entries 使用 raw compressed-payload copy；rewritten、method-changing 与 stored-bootstrap 路径保持原有编码语义。Public/internal 文档明确该能力不复制 local header、central directory、extra fields 或整包布局。
- Patch schema-v5 telemetry 增加 raw-copy entry names/count/bytes，矩阵逐 entry 验证 exact source/output compressed payload；新增 Streaming compression 与 Patch raw-copy 两个 validated evidence bundle，记录 level 1/3/6 的吞吐、输出大小、peak working set 和 openpyxl 结果。

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
