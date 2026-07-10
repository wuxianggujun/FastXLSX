# Changelog

## Unreleased

- 重构文档治理：删除失效计划/测试流水，保留少量活文档与 Git history。
- Production/default profile 启用 minizip-ng stored+DEFLATE backend；新增 stored-only 与 no-images profiles。
- 拆分 vcpkg features 为 `runtime-minizip`、`images`、`planned-xml`；移除无效 DOM option。
- 图片能力可关闭；关闭态保留 public symbols、传播 `FASTXLSX_HAS_IMAGES=0` 并增加 runtime smoke。
- 新增 `WorkbookEditor::has_unsaved_changes()` / `unsaved_change_count()` 保存水位，保留 `has_pending_changes()` 的 staged-state 兼容语义。
- In-memory materialization 默认拒绝已知有损投影，新增显式 `AllowLossyProjection` policy 与 focused tests。
- 新增 tracked benchmark evidence schema、目录规范和标准库 validator；未提交真实结果时保持 0 bundle，不伪造 claim。
## Versioning Workflow

- `CMakeLists.txt` 的项目版本与 `vcpkg.json` `version-string` 保持一致。
- Release tag 使用 `vMAJOR.MINOR.PATCH`。
- 发布前记录用户可见变化、兼容性、验证证据和已知非目标。
- 在 public header 注释、install/export、CI 和 QA 证据成熟前，不宣称 API 稳定。

## [Unreleased]

### Documentation

- 重构 Markdown 文档体系，以 `docs/CURRENT_CAPABILITIES.md` 作为唯一当前能力事实源，以 `docs/TASK_BREAKDOWN.md` 作为唯一 active queue。
- 删除已失效的长期路线图、产品化计划和测试流水文档；历史内容由 Git history 保存。
- 重写 README、架构、编辑模型、API 门禁、测试、preservation、性能、依赖与开发环境文档，统一 public/internal/planned/non-goal 边界。
- 同步 `AGENTS.md` 和项目 skills，移除失效文档引用与过时 benchmark schema 说明。

### Added

- Basic CMake install/export package support for `FastXLSX::fastxlsx`。
- Release dependency、local environment 和 third-party notices。
- README consumer `find_package()` guidance。

### Validation

- 文档重构使用 Markdown 链接、UTF-8、事实引用、focused `rg` 和 `git diff --check` 验证。
- 既有代码验证入口为 `windows-nmake-release`、`windows-nmake-release-minizip` 和 install/consumer smoke；本次 Markdown-only 变更不重新运行 C++ build/CTest。

### Not Yet Claimed

- Stable public API / ABI。
- 完整 CI release readiness。
- Native chart/VBA generation or semantic editing。
- 完整 existing-workbook object lifecycle、relationship repair/pruning 或 orphan cleanup。

## [0.1.0] - Not Released

- CMake 和 vcpkg manifest 使用的初始项目版本。