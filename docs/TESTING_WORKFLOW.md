# 测试流程

## 分级策略

验证前先定级，默认选择能证明本次改动正确的最小集合。小功能不因为位于 public header、Patch 或 package 代码中就自动升级为全量测试。

### T0 文档与 skill

- 适用：只修改 Markdown、Doxygen wording、agent guide 或 project skill，且不改变代码/构建契约。
- 运行：相关 `quick_validate.py`、Markdown links、UTF-8/LF、deleted-doc/high-risk wording、`git diff --check`。
- 不运行：编译、CTest、profile 和 install consumer。

### T1 小功能与窄修复

- 适用：单一窄 public API、小型 feature slice、局部 bugfix、guardrail/diagnostic/test 补强。
- 先只构建 `fastxlsx` 与直接受影响的 test target，运行精确 focused CTest；共享 serializer/helper/transaction 被修改时，再追加证明该共享不变量所需的少量邻接 target。
- 只有改动实际触及相应边界时才追加 targeted stored/no-images/profile、install consumer、OpenPyXL 或 Excel smoke。Public/install smoke 不等于必须运行全量 CTest。
- 默认禁止运行 production 全量 CTest。Focused 结果若暴露跨模块回归，先扩到相关 target；只有风险事实已经扩大为全局时才升级到 T2。

### T2 大功能与里程碑

- 只适用于 active queue 明确标记的大功能收口、release/milestone gate、构建/测试基础设施的大范围变化，或用户明确要求全量验证。
- 运行 production 全量 build + CTest，再按功能实际覆盖追加 stored/no-images/profile/install/consumer 与 ZIP/XML/Office smoke。
- Windows 新链接 executable 的首次运行若仅出现 60 秒冷启动 timeout，记录首轮结果并只串行复跑 timeout target；除非 release gate 明确要求一次 clean full run，否则不为小切片再次重跑整套。

所有层级都必须记录真实命令范围与结果；不得把 focused、分段复跑、被中止的全量测试写成一次 `N/N` 全量通过。

## 命令

T1 focused 示例：

```powershell
cmake --build --preset windows-nmake-release --target <affected-target> -j 1
ctest --test-dir build\windows-nmake-release -R "^<focused-test>$" --output-on-failure -j 1
```

T2 full gate：

```powershell
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release -j 1
ctest --preset windows-nmake-release -j 1
```

普通 CTest timeout 为 60 秒，`noTestsAction=error`；public-state 测试已全部拆为 standalone targets，不再保留专用 120 秒 legacy shard。Benchmark 不进入默认 CTest。

## 关键矩阵

- Patch：failure-before-state-change、retry、reopen、unknown part preservation、relationships/content types/calc metadata side effects；calc metadata 需注入提交前失败，验证既有 plan/manifest/replacements 不变且 retry 成功。
- Save transaction/watermark：验证 stage → package write → state commit；post-stage/write failure 保留 dirty session、pending/unsaved count 和 `last_edit_error()`，retry 写入最新值；successful save 清零 unsaved，retained staged state 仍可 pending，move 转移 watermark；invalid/unavailable compression 必须在 dirty-session staging 前失败，production DEFLATE 与 stored-only retry 分 profile 验证。
- In-memory：guardrail、strict rejection category/context、`worksheet()`/`try_worksheet()` typed propagation、explicit lossy opt-in、generic policy mismatch、malformed-source precedence、no-state-pollution、`last_edit_error()` preservation、dirty flush/recovery。
- Streaming：row order、无 DOM/dense matrix、strings/styles/media/metadata package side effects、body buffer 上限、成功 close 后 temporary resource count 与 feature construction state 为零。
- Streaming classic notes：one-based coordinate/Excel bounds、non-empty author/text、duplicate-cell rejection、unwritten target cell、author first-use dedup/source order、UTF-8/XML escaping/`xml:space`、simple comments XML、hidden legacy VML coordinates、compact per-worksheet part numbering、comments/VML content types，以及 external hyperlink/spreadsheet drawing/VML/comments/table relationship id 与 worksheet suffix schema ordering。覆盖 stored/production DEFLATE、本库 comments reader round-trip、close 后 construction-state release、失败不污染/retry、OpenPyXL 与本机 Excel reopen；不把成功 reopen 扩大为 rich/threaded/visibility/shape 或 existing-workbook edit 支持。
- Patch classic notes：one-based coordinate/Excel bounds、non-empty author/text、duplicate/missing target、same-session add/update/remove regeneration、identical update no-op、final/add/update/remove diagnostics、author first-use dedup、UTF-8/XML escaping/`xml:space`、hidden VML、compact paired part numbering、relationship id/content types 与 suffix ordering。Add 另测 source-owned classic/threaded comments 和任何 VML relationship 拒绝；update/remove 另测 comments/VML serializer exact audit、rich/threaded/unknown VML rejection、其他 worksheet/part/package-root 入边拒绝，并覆盖 percent-encoded path 与 query/fragment URI alias，最后一条 `<legacyDrawing>`/relationships/parts/content-types 清理和其他 VML preservation。共同覆盖 unknown entry/unrelated relationship/cell preservation、planned rename/added worksheet、worksheet removal guard、failure-before-state-change、failed save retry、stored/production DEFLATE bounded-reader reopen、OpenPyXL/Excel reopen；不得扩大为任意 source merge、rich text、shape customization 或通用 VML 编辑。
- Patch conditional-format add：覆盖 two-/three-color scale、basic data bar、basic `3Arrows` icon set、三类 API 的 single/`span`/initializer-list multi-range、existing/same-session priority continuation、planned rename/added worksheet、`conditional_format_count`、schema-safe append 和 source order。另测 invalid rule/range、root/QName/nesting/schema/direct-child/priority failure-before-state-change，以及 advanced/custom/dxf/formula/cellIs/multiple-rule source payload exact preservation；同时核对 cells、relationships、content types、styles/dxf、`calcPr`/`calcChain` 与 unknown parts，覆盖 failure hook、failed save retry/reopen。T1 focused gate 只运行 `fastxlsx.workbook_editor_public_conditional_formatting`、`fastxlsx.streaming.conditional-formatting`、`fastxlsx.worksheet_conditional_format_reader` 与静态门禁；focused 未暴露跨模块风险时不跑 production 全量 CTest。
- Patch table lifecycle：复用 Streaming `TableOptions`/serializer，add 覆盖 missing/existing/self-closing `tableParts`、existing/missing worksheet `.rels`、`xmlns:r` 与 relationship/id/part/content-type/manifest coordination；update 覆盖 source identity/order 保留、same-session add、identical no-op、missing/name/range conflict；remove 覆盖 single/final table cleanup、missing、name/range reuse与 id/part session reservation。共同覆盖 source/planned rename/same-session added worksheet、effective overlay、workbook-wide ASCII-insensitive name/id/part uniqueness、same-sheet range non-overlap、bounded projection、无 child relationship、percent-encoded/query/fragment URI alias inbound ownership rejection、cell/calc/unknown-part preservation、final/add/update/remove diagnostics、failure-before-state-change、failed save retry/reopen 和 structural sync 非目标。T1 focused gate 只运行 `fastxlsx.workbook_editor_public_tables`、`fastxlsx.worksheet_table_reader`、`fastxlsx.streaming.metadata` 与静态门禁，除非结果暴露跨模块风险才扩大。
- Streaming read：stored + production DEFLATE、row/cell callback order、typed number/boolean/text/error/shared index、formula/cached split、style index、borrowed view 复制、callback exception 原样传播和 entry retry；覆盖 XML window/active-cell text guardrail、missing/duplicate/out-of-order coordinate、shared/style relationship、rich/formula metadata rejection 与 malformed XML diagnostics。
- Bounded simple sharedStrings：stored + production DEFLATE、simple/empty/entity decode、zero-based source order、borrowed copy、跨 package chunk token、callback exception retry、XML window/item-text guardrail、relationship target/content type，以及 rich/phonetic/extension/extra metadata 和 malformed XML rejection。
- Bounded sharedStrings runs：stored + production DEFLATE、item start/run/item end 顺序、simple 单 run compatibility、rich run boundary、borrowed text copy、owning bold/italic/direct-ARGB、三类 callback exception retry、跨 package chunk token、XML window/item/run/runs-per-item/nesting guardrail、relationship target/content type，以及 mixed shape、phonetic/extension、非默认 font/theme/tint、unsupported property 和 malformed QName/boundary rejection。
- Bounded cell formats：stored + production DEFLATE、custom number-format/cellXfs source order、format-code borrowed copy/entity decode、number-format 与 cell callback exception retry、跨 package chunk token、XML window/format-code/nesting/custom-id guardrail、container count/duplicate id、styles relationship target/content type，以及 enabled border/base-style/protection/quote/pivot、nested/unsupported alignment 和 malformed XML rejection。
- Bounded style components：stored + production DEFLATE、zero-based font/fill source order、owning bold/italic/direct-ARGB 与 none/gray125/solid values、font/fill callback exception retry、XML window/nesting/component-count guardrail、container count、styles relationship target/content type，以及 non-default font/theme、unsupported color/pattern/gradient 与 malformed nesting rejection。
- Bounded worksheet metadata：stored + production DEFLATE、primary/other sheetView audit、frozen pane/worksheet-root auto-filter/zero-based merged-range source order与 owning copy、callback exception retry、XML window/nesting/reference/view/merge-count guardrail、container/count/QName/schema/range-overlap rejection，以及 source package/relationships/content types 无副作用。
- Bounded worksheet data validations：stored + production DEFLATE、zero-based rule/source order、owning multi-range `sqref` + `DataValidationRule` copy、entity decode、formula1/formula2 与 prompt/error projection、callback exception retry、absent/empty container，以及 XML window/nesting/rule/range/sqref/formula/metadata-text guardrail；另测 count/direct-child/QName/schema、boolean/enum、unsupported attribute/extension/prompt-window rejection 和 package no-side-effect。
- Bounded worksheet hyperlinks：stored + production DEFLATE、zero-based source order、owning internal location/external target/range/display/tooltip、internal no-`.rels`、callback exception retry、absent/empty container，以及 XML window/nesting/hyperlink/ref/relationship-id/target/metadata-text guardrail；另测 relationship missing/type/`TargetMode`/target、namespace scope、duplicate semantic id、container/direct-child/QName/schema、unsupported attribute/child、duplicate/range overlap、foreign extension disambiguation 和 source package no-side-effect。
- Bounded worksheet conditional formatting：stored + production DEFLATE、zero-based source order、owning multi-range `sqref`、priority、two-/three-color color scale、basic data bar、basic `3Arrows` icon set、show/reverse flags、callback exception retry、absent container，以及 XML window/nesting/format/range/`sqref` guardrail；另测 missing/invalid `sqref`、priority、rule shape、`cfvo`/color/icon thresholds、ARGB、schema order、QName/namespace scope、advanced/custom metadata、`dxf`、formula/cellIs、foreign extension disambiguation 和 source package no-side-effect。
- Bounded worksheet tables：stored + production DEFLATE、`tableParts` zero-based source order、owning id/range/name/displayName/basic column、writer-compatible totals function/label、table-local auto-filter/style flags、callback exception retry、absent/foreign-extension container，以及 XML window/nesting/table/id/target/name/column/range guardrail；另测 count/direct-child/QName/relationship namespace/schema、missing/wrong/external/duplicate relationship target、percent decode、part/content type、table root/header/totals/filter/style audit、calculated/other totals formula/full-filter/extension rejection 和 source package no-side-effect。
- Bounded worksheet images：stored + production DEFLATE、drawing zero-based source order、owning `editAs`/from-to marker/EMU offset/transform extent/name/description/format/encoded size、unique media reuse、callback exception retry、absent drawing，以及 XML/nesting/image/id/target/name/description/numeric/media guardrail；另测 worksheet/drawing QName/namespace/schema、duplicate semantic drawing id、relationship missing/wrong/external/percent target、part/content type、PNG/JPEG signature、stored CRC、`xdr:oneCellAnchor` / `xdr:absoluteAnchor` 元素、chart/shape/group/connector、crop/rotation/position transform、picture hyperlink rejection 和 source package no-side-effect。
- Bounded worksheet comments：stored + production DEFLATE、comments-part zero-based source order、owning one-based row/column/author/simple text、entity decode、self-closing author/text、callback exception retry、absent comments 与 optional legacyDrawing，以及 XML/nesting/comment/author/per-author/aggregate-author/text/ref/authorId/shapeId/relationship id/target guardrail；另测 duplicate/external/percent target/part/content type、worksheet-local threaded-comment relationship rejection/persons no-follow、VML relationship missing/wrong/external/part/content type、rich/phonetic/extension、duplicate/invalid ref、invalid authorId/shapeId 和 source package no-side-effect。VML payload 不解析，因此不把 presence audit 写成 visibility/shape compatibility test。
- Patch large worksheet：direct-range 与 single-pass fallback 分别验证 scanned/matched/inserted counts、精确 dimension、relationship audit、retry；重复 rewrite 必须证明被替代的临时文件立即删除且当前 staged output 仍可保存。
- Test artifacts：每个测试进程使用 system temp 下独立的 PID 子目录，正常退出时清理自己的 XLSX/PNG/ZIP 工件；不得恢复跨进程共享的 flat artifact directory 或让全量 CTest 持续累积历史文件。
- No-images：编译 `tools/feature_smoke/image_disabled_smoke.cpp`，consumer 宏为 0，runtime smoke 确认 image helper 与 `read_worksheet_images()` public call 均抛错。

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
