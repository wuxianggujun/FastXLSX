# 测试流程

## 顺序

1. 运行与改动最接近的 focused target/test。
2. 运行 production preset CTest。
3. 依赖/profile 变更时运行 stored、no-images 和 install/consumer smoke。
4. OpenXML 输出变更再做 ZIP/XML/Office smoke。

## 命令

```powershell
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

普通 CTest timeout 为 60 秒，`noTestsAction=error`；public-state 测试已全部拆为 standalone targets，不再保留专用 120 秒 legacy shard。Benchmark 不进入默认 CTest。

## 关键矩阵

- Patch：failure-before-state-change、retry、reopen、unknown part preservation、relationships/content types/calc metadata side effects；calc metadata 需注入提交前失败，验证既有 plan/manifest/replacements 不变且 retry 成功。
- Save transaction/watermark：验证 stage → package write → state commit；post-stage/write failure 保留 dirty session、pending/unsaved count 和 `last_edit_error()`，retry 写入最新值；successful save 清零 unsaved，retained staged state 仍可 pending，move 转移 watermark；invalid/unavailable compression 必须在 dirty-session staging 前失败，production DEFLATE 与 stored-only retry 分 profile 验证。
- In-memory：guardrail、strict rejection category/context、`worksheet()`/`try_worksheet()` typed propagation、explicit lossy opt-in、generic policy mismatch、malformed-source precedence、no-state-pollution、`last_edit_error()` preservation、dirty flush/recovery。
- Streaming：row order、无 DOM/dense matrix、strings/styles/media/metadata package side effects、body buffer 上限、成功 close 后 temporary resource count 为零。
- Patch large worksheet：direct-range 与 single-pass fallback 分别验证 scanned/matched/inserted counts、精确 dimension、relationship audit、retry；重复 rewrite 必须证明被替代的临时文件立即删除且当前 staged output 仍可保存。
- Test artifacts：每个测试进程使用 system temp 下独立的 PID 子目录，正常退出时清理自己的 XLSX/PNG/ZIP 工件；不得恢复跨进程共享的 flat artifact directory 或让全量 CTest 持续累积历史文件。
- No-images：编译 `image_disabled.cpp`，consumer 宏为 0，runtime smoke 确认 public call 抛错。

`windows-nmake-release-no-images` preset 当前关闭 tests，不存在对应 CTest preset；验证方式是完成该 profile build 后直接运行 `build\windows-nmake-release-no-images\fastxlsx_image_disabled_smoke.exe`，再执行 install/consumer smoke。

Install/consumer smoke 使用 manifest profile 时，独立 consumer 的 `CMAKE_PREFIX_PATH` 必须同时包含 FastXLSX install prefix 与该 profile `CMakeCache.txt` 中 `VCPKG_INSTALLED_DIR/<triplet>`；动态 triplet 运行 consumer 时还需把同一安装树的 `<triplet>/bin` 加入 `PATH`。不要误用仓库根部或其他 preset 的同名 `vcpkg_installed` 目录。

## Benchmark evidence

Benchmark 是 opt-in 本地工具。原始结果不自动成为 release evidence。可引用结果必须位于 `benchmarks/evidence/<bundle>/`，包含 manifest、artifact hash、环境和 claim-to-artifact 映射：

```powershell
py -3 tools/validate_benchmark_evidence.py --self-test
py -3 tools/validate_benchmark_evidence.py --root benchmarks/evidence
py -3 tools/run_benchmark_matrix.py --self-test
py -3 tools/run_patch_benchmark_matrix.py --self-test
py -3 tools/run_patch_worksheet_scaling_benchmark.py --self-test
py -3 tools/run_package_writer_benchmark_matrix.py --self-test
py -3 tools/run_package_writer_paired_benchmark.py --self-test
py -3 tools/run_patch_crc_paired_benchmark.py --self-test
```

重复矩阵默认每个 case 使用 1 次 warm-up 和 3 次 measured run；profiling bundle 可显式增加 warm-up/measured 次数，但必须在 run context 记录。当前 runner 保留全部 warm-up/measured result 与 measured min/median/max，并把第一轮 fresh-process warm-up observation 与 warmed measured median 分列；该 observation 必须保持 `cache_control=none`、`cold_cache_claim=false`，因为 OS cache 未清空且 Patch source 被复用，不能写成受控物理冷缓存。`--verify-openpyxl` 只验证 warmed median 代表 workbook，Office 仍是独立步骤。当前 validator 应通过 4 个 production Streaming bundle、17 个 Patch bundle 和 1 个 OpenXLSX reference bundle，共 22 个；它们都只能支持 manifest 限定的单机 workload 结论，不能泛化到其他机器或数据规模。`office_open="not_run"` 不得写成 Office 已验证。

当前 Streaming benchmark executable/matrix 输出 schema v6/v3；Patch `WorkbookEditor` benchmark executable/matrix 输出 schema v20/v15；isolated package-writer executable/matrix 输出 schema v2，其 balanced paired runner 输出 schema v1；internal Patch CRC/fusion paired runner 输出 schema v3。最新 tracked Streaming general matrix、Patch general matrix、Patch worksheet-scaling 与 isolated package-writer evidence 分别为 v5、v9、v20/v2、v1；未纳入 evidence 的新 runner/package-writer smoke 不得形成 release claim。Streaming 必须分列 generation、package close、total wall/process CPU、throughput、body buffer peak/flush count 和 close 后 active temporary file count，并校验 total process CPU 等于 generation + package close；Patch 必须分列 direct-range/single-pass transform、parser/source-callback/coalesced/action traffic、aggregate/canonical inline-string fast-path count/bytes/fallback、complete-cell count/bytes/fallback、canonical complete-cell count/bytes/formula/inline-string counters、pass-through batch count/cells/bytes/peak cells、output append/flush/peak buffer、relationship scanner input calls/bytes/boundary carry/slow-path tags、relationship/temporary IO、CRC backend、fused CRC wall/segment count、single-pass commit、package writer target-entry timing、staged CRC reuse/validation、staged-file prefetch activation/chunks/bytes/peak buffer/read/wait、file IO buffer、writer input peak/call count/maximum call wall time、requested compression level、open/materialize/mutation/save/total editor process CPU、package/target-entry process CPU、DEFLATE writer CPU envelope，以及 raw compressed-copy entry names/count/bytes；多 worksheet profile 还必须核对每表 names/value/dimension、按表 edits/inserted counts、single-pass/CRC segments aggregate、rewritten worksheet entry aggregate，以及 schema-v20 明细数组的 package order、逐字段 sum/max 与 sheet1 legacy 总账。Stored backend 不伪造 minizip entry 明细。Total editor process CPU 必须等于四个 editor phase 之和。Windows production 的至少 4 MiB staged file chunk 必须报告 active prefetch、固定 512 KiB input peak 与 1 MiB 双 buffer peak；小 chunk、raw-copy、stored-only 与非 Windows guard 必须保持同步路径且 prefetch traffic 为零。历史 Streaming schema-v5 与 Patch schema-v4 至 v19 artifact 仍可读取或验证，但新 evidence 不得删除当前相应 telemetry。

Patch worksheet scaling runner 使用独立 schema v2。默认 1/2/4-sheet variants 的 warm-up/measured round 数必须为 `2 × variant count` 的倍数，以反转 rotation 保证每个 variant 在每个 position 等频出现；`fixed-shape` 固定每表 rows/cols/edits，`fixed-total` 固定总 rows/edits并均分。后者必须明确不同 sheet partition 会改变 row-number 宽度与每个 XML/package part 大小，不是 byte-identical 对照。报告要保留全部 raw result、position statistics、同轮 baseline ratio、逐 worksheet entry 跨 measured runs 的 min/median/max 与代表文件 OpenPyXL 状态。Windows process CPU 存在约 15.625 ms 量化，快速 level-1 entry 即使略大于 1 MiB 也可能合法记录为 0；正 CPU gate 只对至少 4 MiB uncompressed rewritten target 强制执行。

旧的 Patch 合并 bundle 继续使用 `workbook-editor` manifest kind；新的 standalone package-writer bundle 必须使用 `package-writer`，不得借用 Patch 分类。

Patch 矩阵使用 `run_patch_benchmark_matrix.py` 在独立准备进程生成一次 source fixture，warm-up/measured 进程通过 `--reuse-source` 只测 open → mutation → save，避免 source `WorkbookWriter` 污染 editor process peak working set。`--source-compression-level` 与可重复传入的 `--output-compression-level` 分开记录，同一 source 可比较多个 output level。Copied/rewritten bytes 来自 ZIP central-directory 的 logical `file_size` / compressed `compress_size`；所有 copy-original entry 必须保持 source/output CRC 与 logical size 一致。只有 output plan 标记为 raw-copy 的 entry 才进一步比较 exact compressed payload bytes，并核对 telemetry count/bytes；这仍不等于 local header、central directory 或整包 byte preservation。DEFLATE writer CPU 是 minizip writer-write/entry-close 调用的 process CPU envelope，包含 backend bookkeeping；raw-copy entry 必须为 0，且不得把该值写成纯 encoder CPU。

Package-writer schema-v2 backend 对照必须复用同一 staged payload 与 compression level；paired runner 的 schedule 要让每个 case 在每个 position 等频出现。One-pass direct-zlib 必须验证至少 4 MiB/完整 staged CRC gate、small/stored/raw-copy/incomplete-CRC fallback、output buffer 边界、CRC mutation rejection、失败时既有 output 不变和 openpyxl reopen。`direct_zlib_engine_process_cpu_us` 只覆盖 `deflateInit2`/`deflate`/`deflateEnd`；CRC、staged read、minizip raw-output write 与 entry close 不得混入。

Patch CRC/fusion A/B 使用 `run_patch_crc_paired_benchmark.py`，measured rounds 必须是 4 的倍数；反转 rotation 让 baseline/candidate 在两个 position 等频出现。每轮必须核对 target worksheet logical/compressed SHA-256、CRC、logical/compressed size 与 package bytes，代表输出再做 OpenPyXL。Fused path 还必须验证 CRC segment 连续覆盖、dimension memory chunk、completed-entry CRC mismatch diagnosis、既有 output protection 和修复 staged bytes 后 retry；不能因 commit 不再重读 temporary file 而删除 PackageWriter failure gate。

Production/stored/no-images preset 显式设置 `FASTXLSX_ENABLE_DIRECT_ZLIB_PROFILING=OFF`，并由 `fastxlsx.package_writer_direct_zlib` 验证 unavailable selection 在写出前失败且保留既有输出。One-pass focused correctness 使用 `windows-nmake-release-direct-zlib-profile`；manual benchmark preset 也显式开启该 option。默认 install consumer 不应要求 FastXLSX 自己的 `ZLIB::ZLIB` export dependency，只有 profiling install 才允许增加它。

Production/stored/no-images 也显式设置 `FASTXLSX_ENABLE_PORTABLE_CRC_PROFILING=OFF`。CRC backend A/B 只使用 `windows-nmake-release-patch-crc-minizip-profile` 与 `windows-nmake-release-patch-crc-portable-profile`；后者保持 minizip package backend 不变，仅强制 PackageEditor portable CRC。该 private option 不得出现在 install targets 或 consumer compile definitions。

OpenXLSX 只在 `windows-nmake-release-reference-benchmark` opt-in preset 下构建。比较必须使用相同机器、cell count、value/string distribution 与 warm-up/measured protocol，并同时报告 save/compression 设置、output size 与 process peak working set；若双方 public API 无法选择相同 compression，必须明确协议差异，不得伪装为 identical-backend microbenchmark。只允许声明实际覆盖的 workload，不从单一 case 推导总体领先。

## 文档与静态检查

```powershell
rg "旧文件名|高风险措辞" README.md docs AGENTS.md .agents/skills

git diff --check
```

同时检查 Markdown 相对链接、UTF-8/LF、public/internal wording 和 deleted-doc references。
