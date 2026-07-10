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

普通 CTest timeout 为 60 秒，`noTestsAction=error`；benchmark 不进入默认 CTest。

## 关键矩阵

- Patch：failure-before-state-change、retry、reopen、unknown part preservation、relationships/content types/calc metadata side effects。
- Save watermark：successful save 清零 unsaved；failed save 保留；retained staged state 仍可 pending；move 转移 watermark。
- In-memory：guardrail、strict rejection、explicit lossy opt-in、policy mismatch、no-state-pollution、dirty flush/recovery。
- Streaming：row order、无 DOM/dense matrix、strings/styles/media/metadata package side effects。
- No-images：编译 `image_disabled.cpp`，consumer 宏为 0，runtime smoke 确认 public call 抛错。

## Benchmark evidence

Benchmark 是 opt-in 本地工具。原始结果不自动成为 release evidence。可引用结果必须位于 `benchmarks/evidence/<bundle>/`，包含 manifest、artifact hash、环境和 claim-to-artifact 映射：

```powershell
py -3 tools/validate_benchmark_evidence.py --self-test
py -3 tools/validate_benchmark_evidence.py --root benchmarks/evidence
```

当前 0 bundle 是合法状态，表示没有 tracked release evidence。`office_open="not_run"` 不得写成 Office 已验证。

## 文档与静态检查

```powershell
rg "旧文件名|高风险措辞" README.md docs AGENTS.md .agents/skills

git diff --check
```

同时检查 Markdown 相对链接、UTF-8/LF、public/internal wording 和 deleted-doc references。