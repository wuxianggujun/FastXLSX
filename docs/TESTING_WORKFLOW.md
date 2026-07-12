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
- Streaming：row order、无 DOM/dense matrix、strings/styles/media/metadata package side effects。
- No-images：编译 `image_disabled.cpp`，consumer 宏为 0，runtime smoke 确认 public call 抛错。

## Benchmark evidence

Benchmark 是 opt-in 本地工具。原始结果不自动成为 release evidence。可引用结果必须位于 `benchmarks/evidence/<bundle>/`，包含 manifest、artifact hash、环境和 claim-to-artifact 映射：

```powershell
py -3 tools/validate_benchmark_evidence.py --self-test
py -3 tools/validate_benchmark_evidence.py --root benchmarks/evidence
py -3 tools/run_benchmark_matrix.py --self-test
py -3 tools/run_patch_benchmark_matrix.py --self-test
```

重复矩阵默认每个 case 使用 1 次 warm-up 和 3 次 measured run，报告 min/median/max 并保留全部 raw result；`--verify-openpyxl` 只验证 median 代表 workbook，Office 仍是独立步骤。当前 validator 应通过 2 个 production Streaming bundle 和 1 个 production Patch bundle；它们都只能支持 manifest 限定的单机 workload 结论，不能泛化到其他机器或数据规模。`office_open="not_run"` 不得写成 Office 已验证。

Patch 矩阵使用 `run_patch_benchmark_matrix.py` 在独立准备进程生成一次 source fixture，warm-up/measured 进程通过 `--reuse-source` 只测 open → mutation → save，避免 source `WorkbookWriter` 污染 editor process peak working set。`--source-compression-level` 与 `--output-compression-level` 分开记录。Copied/rewritten bytes 来自 ZIP central-directory 的 logical `file_size` / compressed `compress_size`；copy-original entry 必须保持 source/output CRC 与 logical size 一致，copied source/output compressed bytes 分列，不能误写成 raw compressed-byte copy。

## 文档与静态检查

```powershell
rg "旧文件名|高风险措辞" README.md docs AGENTS.md .agents/skills

git diff --check
```

同时检查 Markdown 相对链接、UTF-8/LF、public/internal wording 和 deleted-doc references。
