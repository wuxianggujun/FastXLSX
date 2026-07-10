# Benchmark Evidence

此目录只保存可被 release note 或性能文档引用的、可追溯 benchmark evidence。
原始 benchmark 输出仍由 opt-in 工具生成；只有在结果、环境和哈希都确认后，才把 evidence bundle 提交到仓库。

每个 evidence bundle 使用独立子目录，并至少包含：

- `manifest.json`：符合 `benchmark-evidence-manifest.schema.json`。
- manifest 引用的 machine-readable benchmark JSON / matrix report / summary。
- 如声明 Office 打开结果，附独立 Office report；不得改写 benchmark executable 原始的 `office_open="not_run"`。
- manifest 中每个 artifact 的 SHA-256，路径必须位于该 bundle 内，禁止绝对路径和 `..`。

校验命令：

```powershell
python tools/validate_benchmark_evidence.py --root benchmarks/evidence
python tools/validate_benchmark_evidence.py --self-test
```

空目录表示当前没有可供 release 引用的 tracked evidence，不代表 benchmark 失败，也不能据此形成性能结论。
不要提交本地 build 目录、临时 workbook 或未经核验的数据。