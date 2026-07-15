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
```

重复矩阵默认每个 case 使用 1 次 warm-up 和 3 次 measured run；profiling bundle 可显式增加 warm-up/measured 次数，但必须在 run context 记录。报告保留全部 measured result 与 min/median/max；`--verify-openpyxl` 只验证 median 代表 workbook，Office 仍是独立步骤。当前 validator 应通过 4 个 production Streaming bundle、12 个 Patch bundle 和 1 个 OpenXLSX reference bundle，共 17 个；它们都只能支持 manifest 限定的单机 workload 结论，不能泛化到其他机器或数据规模。`office_open="not_run"` 不得写成 Office 已验证。

当前 Streaming benchmark executable 输出 schema v5；Patch `WorkbookEditor` benchmark executable 输出 schema v11，Patch matrix 输出 schema v5。Streaming 必须分列 generation、package close、throughput、body buffer peak/flush count 和 close 后 active temporary file count；Patch 必须分列 direct-range/single-pass transform、parser/source-callback/coalesced/action traffic、simple inline-string fast-path count/bytes/fallback、complete-cell count/bytes/fallback、pass-through batch count/cells/bytes/peak cells、output append/flush/peak buffer、relationship scanner input calls/bytes/boundary carry/slow-path tags、relationship/temporary IO、package writer target-entry timing、staged CRC reuse/validation、requested compression level、package/target-entry process CPU、DEFLATE writer CPU envelope，以及 raw compressed-copy entry names/count/bytes。历史 schema-v4 至 v10 artifact 仍可读取或验证，但新 Patch evidence 不得删除 v6 relationship scanner、v7 staged CRC、v8 inline-string、v9 compression CPU、v10 complete-cell 或 v11 pass-through batching telemetry。

Patch 矩阵使用 `run_patch_benchmark_matrix.py` 在独立准备进程生成一次 source fixture，warm-up/measured 进程通过 `--reuse-source` 只测 open → mutation → save，避免 source `WorkbookWriter` 污染 editor process peak working set。`--source-compression-level` 与可重复传入的 `--output-compression-level` 分开记录，同一 source 可比较多个 output level。Copied/rewritten bytes 来自 ZIP central-directory 的 logical `file_size` / compressed `compress_size`；所有 copy-original entry 必须保持 source/output CRC 与 logical size 一致。只有 output plan 标记为 raw-copy 的 entry 才进一步比较 exact compressed payload bytes，并核对 telemetry count/bytes；这仍不等于 local header、central directory 或整包 byte preservation。DEFLATE writer CPU 是 minizip writer-write/entry-close 调用的 process CPU envelope，包含 backend bookkeeping；raw-copy entry 必须为 0，且不得把该值写成纯 encoder CPU。

OpenXLSX 只在 `windows-nmake-release-reference-benchmark` opt-in preset 下构建。比较必须使用相同机器、cell count、value/string distribution 与 warm-up/measured protocol，并同时报告 save/compression 设置、output size 与 process peak working set；若双方 public API 无法选择相同 compression，必须明确协议差异，不得伪装为 identical-backend microbenchmark。只允许声明实际覆盖的 workload，不从单一 case 推导总体领先。

## 文档与静态检查

```powershell
rg "旧文件名|高风险措辞" README.md docs AGENTS.md .agents/skills

git diff --check
```

同时检查 Markdown 相对链接、UTF-8/LF、public/internal wording 和 deleted-doc references。
