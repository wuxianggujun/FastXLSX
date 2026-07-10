# Changelog

## Unreleased

### Added

- 新增 `WorksheetMaterializationError`、`WorksheetMaterializationDiagnostic` 和稳定 loss category，使 strict In-memory rejection 可按 worksheet/cell/sharedStrings context 审计，同时保持 `FastXlsxError` catch compatibility。
- 新增 `WorkbookEditor::has_unsaved_changes()` / `unsaved_change_count()` 保存水位，保留 `has_pending_changes()` 的 staged-state 兼容语义。
- 新增 Basic CMake install/export package、`FastXLSX::fastxlsx` consumer target 和 `find_package()` smoke。
- 新增 tracked benchmark evidence schema、目录规范和标准库 validator；未提交真实结果时保持 0 bundle，不伪造 claim。

### Changed

- In-memory materialization 默认拒绝已知有损投影；只有显式 `AllowLossyProjection` 才允许拍平。
- Production/default profile 启用 minizip-ng stored+DEFLATE backend；新增 stored-only 与 no-images profiles。
- vcpkg features 拆分为 `runtime-minizip`、`images`、`planned-xml`；移除无效 DOM option。
- 图片能力可关闭；关闭态保留 public symbols、传播 `FASTXLSX_HAS_IMAGES=0` 并提供 runtime smoke。
- 将 legacy public-state 长运行 shard 的 CTest timeout 从普通 60 秒单独校准为 120 秒；不放宽其他测试上限。
- `WorkbookEditor::save_as()` 的 dirty In-memory handoff 改为 stage → package write → state commit；写出失败不再提前清除 session dirty diagnostics，并可用最新值安全重试。
- `PackageEditor::request_full_calculation()` 改为跨 edit plan、part/entry replacements、omitted entries 和 manifest 的事务式 staging；提交前失败不再泄漏部分 calcChain/content-type/relationship mutation，并支持保留既有计划后重试。
- `WorkbookEditor::rename_sheet()` 与 internal PackageEditor sheet catalog rename 改为跨 package/public state 的事务式 staging；提交前失败不再泄漏部分 catalog、formula session 或 pending diagnostics mutation，并支持保留既有 patch 后重试。

### Documentation

- 重构 Markdown 治理，以 `docs/CURRENT_CAPABILITIES.md` 作为唯一当前能力事实源，以 `docs/TASK_BREAKDOWN.md` 作为唯一 active queue。
- 删除失效路线图、产品化计划和测试流水；历史内容由 Git history 保存。
- 统一 README、架构、编辑模型、API 门禁、测试、性能、依赖、开发环境、`AGENTS.md` 和项目 skills 的 public/internal/planned/non-goal 边界。

### Not Yet Claimed

- Stable public API / ABI。
- 泛化“高性能/低内存”结论；当前没有 tracked benchmark evidence bundle。
- Native chart/VBA generation 或完整 tables/drawings/comments/pivot/custom XML semantic editing。
- Atomic in-place save、公式求值、cached value 生成或完整 `calcChain.xml` rebuild。

## Versioning Workflow

- `CMakeLists.txt` 的项目版本与 `vcpkg.json` `version-string` 保持一致。
- Release tag 使用 `vMAJOR.MINOR.PATCH`。
- 发布前记录用户可见变化、兼容性、验证证据和已知非目标。
- 在 public header 注释、install/export、CI 和 QA 证据成熟前，不宣称 API 稳定。

## [0.1.0] - Not Released

- CMake 和 vcpkg manifest 使用的初始项目版本。
