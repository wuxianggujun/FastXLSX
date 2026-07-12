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
- Internal `PackageEditor::set_document_properties()` 改为跨 edit plan、part/entry replacements、omitted entries 和 manifest 的事务式 staging；同时纠正文档中将该 internal helper 误写为 `WorkbookEditor` public 能力的表述。
- Internal `PackageEditor::remove_part()` 改为跨 edit plan、part/entry replacements、omitted entries、content types 和 manifest 的事务式 staging；提交前失败不再发布部分 removal 状态，既有 replacement 可保存并可安全重试。
- Internal materialized `PackageEditor::replace_part()` 改为跨 edit plan、part/entry replacements、omitted entries、content types 和 manifest 的事务式 staging；提交前失败不再取消既有 removal 或发布部分 replacement，后续 retry 可恢复 source part。
- Internal non-worksheet `PackageEditor::replace_part_chunks()` 改为跨 edit plan、part/entry replacements、omitted entries 和 manifest 的事务式 staging；提交前失败不再取消既有 removal 或发布部分 chunk replacement，后续 retry 可恢复 source part。
- Internal worksheet chunk replacement 改为跨 worksheet/workbook rewrite、calc metadata、relationship/content-type side effects、edit plan、part/entry replacements、omitted entries 和 manifest 的事务式 staging；generic/direct/by-name routing notes 与 audits 在同一次 commit 发布，提交前失败保留既有 replacement、输出语义和 retry 能力。
- Internal complete-worksheet chunk-source wrapper 将 PackageEditor 临时文件所有权与 worksheet/package state 一起在副本中 staging，再以 `noexcept` swap 发布；提交前失败由 RAII 删除未发布 staged file，direct/by-name/prevalidated wrapper notes 不会单独泄漏。
- Internal bounded sheetData replacement 将最终 `LocalDomRewrite` mode、file-backed staged output ownership、preservation/dependency audits 与 direct/by-name notes 纳入同一 worksheet transaction；提交前失败不再泄漏 StreamRewrite 中间态、notes 或指向已删除临时文件的 chunks，并可在同一 editor 上 retry。
- Internal worksheet cell transformer fallback 将 file-backed output ownership 与 transform diagnostics 随 worksheet state 一次发布；indexed direct-range fast path 将 structured telemetry 与 notes 写入 staged replacement/edit-plan 副本。两条路径的提交前失败不再泄漏 temp chunks、telemetry 或 notes，并支持保留既有 patch 后 retry。
- 继续拆分 legacy public-state 超大 translation unit：source StyleId rejection/public-view 与 source-style clear/erase 场景迁入 style-focused target，materialized dirty state、save/reopen、multi-sheet aggregate 与 move lifecycle 场景迁入 materialized-session target；移除两个对应 legacy shard，专用 120 秒 shard 从 7 个降为 5 个，standalone tests 保持普通 60 秒上限。
- 将 `public-state-reacquire-guards` 的 13 个测试迁入 materialized-session 与 coordinate-guards 独立目标，删除重复的 legacy shard 调度入口；专用 120 秒 shard 从 5 个降为 4 个，迁入目标继续使用普通 60 秒上限。
- 将 `public-state-reacquire` 的 32 个测试按 renamed、saved 与 retry/failure 责任拆为三个独立 60 秒目标，共享 test-only support helper；删除 legacy shard 调度入口，专用 120 秒 shard 从 4 个降为 3 个。
- 将 `public-state-edits` 的 17 个 clear/erase 与 memory-budget 回归迁入独立 60 秒目标，并将 reacquire helper 通用化为 public-state test support；删除 legacy shard 调度入口，专用 120 秒 shard 从 3 个降为 2 个。
- 将 `public-state-formula-audits` 的 52 个 renamed/full-calculation、saved-reacquire 与 shift-after-rename 回归迁入独立 60 秒目标，并提取 formula-audits test-only support；删除 legacy shard 调度入口，专用 120 秒 shard 从 2 个降为 1 个。

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
