<!-- awesome-plan-current:start -->
Policy: current
Active Queue: q312 / whole ws014-p009 in-progress; no new HAL change
HAL: every new source change requires prior specific user approval
q306 approval: exact eight amd64 MMIO accessors only; applied and verified
q308 approval: exact device-mapping patch e6ec9e6c2deda41b840fa6f10846438d091f3a20ce782b9251b7979ac7591c8d; applied and verified
No further HAL changes authorized
Git publication: user owns add/commit/push
<!-- awesome-plan-current:end -->

# Guardrail

Current project contribution rules, adopted 2026-09-10. User decisions prevail
when more recent or more specific; archived approvals are not standing authority.

## Scope and architecture

- Work only within the approved Phase scope. Kernel src/, include/, bootloader/,
  libc/, userland/, platform/, build/config and tools have different ownership;
  inspect current code and Phase before adding files or changing module boundaries.
- `include/hal/hal.h` and HAL responsibilities require explicit applicable user
  approval. The old VM ownership move, vmap removal plan and pmem argument expansion
  were superseded by the user's rollback/refactor. Do not replay them. Existing
  `hal_space_*` and shared kernel mapping support are not permission to redesign HAL.
- Keep the manually reviewed driver organization and `drv_` global-symbol policy.
  Trust verified refactoring over stale test assumptions; repair tests as appropriate.
- RTL8822B `.inc` files separate licensing and MUST remain separate. The earlier
  general instruction to merge `.inc` does not apply to these files.
- Base-system implementation/licensing boundaries: [design policy](master-design-policy.md).
  That document also contains historical milestones; current Master and source
  supersede outdated installer/HAL implementation descriptions.
- Do not copy kernel implementation into userland build dependencies. mkfs tools
  must remain independently usable; preserve the user-authorized source-copy split.

## Standards and verification

Full C standard: [coding-style.md](coding-style.md). Use the full applicable
sections before code generation; no unverified condensed substitute is installed.
The user's WS025 instruction allowed approximate style adherence followed by
later cleanup; preserve that scoped exception, not as a blanket exemption for
new WSs. Preserve evaluation order, ownership and behavior in style changes.
Tool coverage/versions: [automation](standards/automation.md).

- No `git commit` or push without user instruction; keep unrelated modifications.
- Do not read repository `.internal/`. No credentials in plans, Issues or sync state.
- Never run aggregate `make check`. Use meaningful focused checks for the Phase.
- Supported build gate: `make -j16` with the selected platform configuration.
  amd64 runtime uses `qemu-system-x86_64`; destructive tests use disposable images.
- User acceptance scope prevails: do not reintroduce rejected exhaustive abnormal
  tests, repeated physical boots, or hardware gates already waived by the user.
  QEMU and physical evidence remain distinct; describe precisely what was observed.
- Explain only actual external approval blockers. Existing exact-scope authorization
  persists; this adoption does not add a confirmation step for routine plan updates.

## Decision sources

User thread: HAL responsibility/hal.h restrictions and subsequent rollback review;
RTL8822B licensing exception; refactor/test trust; no commit; WS025 style flexibility;
normal-path installer acceptance and QEMU-only UAS acceptance. Retained source
records: [post-rollback review](ws025/post-rollback-review.md),
[WS025](ws025/ws.md), [WS019](ws019/ws.md). Older MWP-Q instructions are in old/.
New rules must update this registry, applicable full standard, tool coverage and
impacted plans; do not silently replace agreed architecture or scope.

## WSの単一目標と終了後の扱い（2026-09-12ユーザー指示）

WSは一つの具体的な到達目標を持つ。目標を達成したWS、またはユーザーが終了したWSは再利用・再開して別の目標を追加しない。似た領域だからという理由で一つのWSへまとめない。機種対応などの上位分類・到達点はMGが担い、インストーラ実機動作、PowerPC移植などは別のWSを作る。
一つの目標に必要な依存作業をPhaseへ分解することは可能だが、独立した別目標をPhaseとして混ぜない。WS終了時は子Phaseを全件照合し、未完了は完了に改変せず、ユーザー指定の保留先または別WSへ引き継いで元Phaseを終了する。旧ID、結果、転送先を残す。今回WS003は終了・再利用禁止、PPC移植はWS027へ、その他の未完了はFuture Workへ移す。

## 2026-09-12 HAL変更の承認条件（ユーザー確認）

ユーザーが「HALの改変には許可が必要です」と明示。HAL責務やhal.hの変更に限らず、src/hal/配下の既存宣言への実装追加・補完も、適用可能な明示許可を得てから行う。既存契約の補完を理由に承認不要と解釈しない。レビュー可能な具体差分を用意し、未許可のHAL変更に依存する実装適用・実行試験は待つ。

## q306 HAL変更の承認・再開（2026-09-12）

ユーザーが提示済み差分に「許可します。」と回答した。`plan/ws014/phase003/amd64-mmio-proposal.patch` の8個のamd64 MMIO read/write accessorの適用・検証を許可されたため、同一差分を適用し、build/QEMU検証を再開する。hal.hやHALの責務は変更しない。直前の「HAL変更の許可待ち」は解消済み。今後の別のHAL変更には、その具体差分に対する事前許可を引き続き必要とする。

PCI BARのcapability部分だけをmapして失敗する問題をdriver側で修正し、register BARを一度だけ全体mapして各capabilityに範囲を渡す。driver単体・ASan/UBSan試験は通過済み。実際のVulkan描画は引き続き未検証で、p003/q306はin-progress。

## q308 HAL提示差分の承認（2026-09-13・最新）

ユーザーが「この差分の適用と検証を許可する」と回答した。[承認記録](https://github.com/awemorris/zedBSD/issues/390#issuecomment-5647471812) の対象は `plan/ws030/phase002/amd64-device-mapping-proposal.patch`、SHA256 `e6ec9e6c2deda41b840fa6f10846438d091f3a20ce782b9251b7979ac7591c8d`。既存MMIO APIのamd64補完と明示DEVICE usermap・protection/cache検査、hal.hの説明コメントに限り適用と検証を進める。これより前の「HAL未承認・適用待ち」はこの差分について解消した。適用・試験成功はまだ記録していない。別のHAL変更とgit add/commit/pushは許可されたと解釈しない。

## q308 checkpoint001（実装・限定検証の中間結果）

[承認HAL差分の適用・限定試験と実装進捗](https://github.com/awemorris/zedBSD/issues/390#issuecomment-5647774479) を記録。HAL対象・amd64 kernel統合build、HAL/GPU資源寿命/memory共有map/sync/WSIの限定host試験がPASS。全体は未完了で、Phaseのclearanceは変更しない。公開headerは固定Khronos由来1.3.269 headerから1.0 core137＋WSI18をNoctで選択する方式に具体化し、両ABIの配置/定数を照合済み。HAL追加APIなし。256MiB apertureのguest runtime、全entrypoint link/dispatch、残りAPI family、/lib設置と標準vkdemo直接表示の統合受け入れは未検証。以前の「未適用・試験成功なし」はこのcheckpointで述べた範囲について履歴となる。local証拠 `plan/ws030/phase002/checkpoint001.json`。未commitのsourceをGitHub repositoryで読めるとは扱わず、git add/commit/pushはユーザーが行う。

## q308 checkpoint002／第1回時間境界レビュー

[256MiB QEMU受入・PCI cache契約修正・全Vulkan symbol link](https://github.com/awemorris/zedBSD/issues/390#issuecomment-5647977365) を記録。既存Venus経路の49,152画素一致、実PCI/VM回帰試験、memory/descriptor/pipeline/sync/WSIの限定試験がPASS。全137 core＋18 WSIを含むlibvulkan.soと標準vkdemoがlinkし、SONAME/155 exports/依存を検証した。標準アプリのゲスト直接表示、/lib設置、残るAPI peer、最終規約照合は未完了で、各Phaseのclearanceは変更しない。承認HAL差分以外のHAL改変なし、720 active minutes枠内で継続。local証拠 `plan/ws030/phase002/checkpoint002.json`。source/docは未commitのままユーザー担当。

## q308 checkpoint003／標準APIの実ゲスト描画と終了条件

[標準Vulkan6枚描画・通常再起動・155 API検証とconsole復帰の未達](https://github.com/awemorris/zedBSD/issues/392#issuecomment-5648174368) を記録。`q308-standard-vkdemo-002` は /lib/libvulkan.so を使い、実VNC/GPU readback/独立ray-texture oracleを6枚で通過した。SIGINT後の再openも通るが、物理console復帰は `q308-lifecycle-001` で失敗したため修正中。全API peer/dispatch・Noct再生成・能力/破棄失敗レビューは進み、155行の検証台帳を作成した。最終sourceのbuild/実表示・競合・console・規約受入は残っており、clearanceは変更しない。詳細と履歴は `plan/ws030/phase004/checkpoint003.json` と同evidence資料。HALは既承認差分のみ、source/docのgit公開はユーザー担当。

## q308完了: 標準Vulkan・直接表示libraryと標準APIデモ（2026-09-13）

WS030 p001/p002/p003/p004とWS014 p005の標準API訂正をclearedとし、WS030 completed、q308 finished、active Queueなしとする。WS014はincomplete、p001/p004 planning、p004未queue、native i915は別WS029のまま。q307の旧scopeの実測と履歴は保持する。

`libc/include/vulkan/` にVulkan1.0の公開header、`userland/base/libvulkan/` に独立した全137 core＋選択direct-display WSI18の実装を提供し、`/lib/libvulkan.so` に配置した。vkdemoは標準Vulkan/WSIだけを使い、GPU ioctl/Venus codecをアプリへ持ち込まない。ABI、Noct再生成、155実exportとproc-address、全familyの限定意味論試験、U/Kの所有権・権限・失敗回収、適用C規約の独立レビューを実施した。正式CTS認証は主張しない。

最終 `q308-lifecycle-003` は実QEMU10.0.11/virglrenderer1.1.0/Intel ANVで6枚の回転直方体を描画し、実VNC/GPU readback/独立ray-texture oracleが一致（評価対象不一致0）。通常終了後6frame再起動、SIGINT後6frame再起動、640×480文字画面への復帰とechoによる画面更新、別processの表示競合拒否とowner35frame/DONEを確認した。42.671秒、QEMU exit0。最終書式変更後のkernel/appは実行済みbinaryと一致する。

承認済みHAL patch SHA256 `e6ec9e6c2deda41b840fa6f10846438d091f3a20ce782b9251b7979ac7591c8d` のみを適用し、既存hal_space_map_device/device usermapを補完した。追加HAL APIはない。PCI cache属性、queue総数63、allocator破棄、console/query/通知の修正と、先行失敗・再実行理由を保存した。公開coherent HOST_VISIBLE、256MiB aperture、native watchdog等の制約は能力監査へ記録した。

結果は `plan/ws030/results-q308.md`、155行の台帳は `plan/ws030/phase004/api-verification.md`、最終証拠は `plan/ws030/phase004/final-evidence/verification.json`、p005訂正は `plan/ws014/phase005/results-q308.md`、履歴は `plan/history/queue-q308.md`（いずれもlocal/uncommitted）。GitHubは計画Issue/Project/結果コメントの同期であり、source/doc/imageのgit add/commit/pushはユーザーが行う。EGLは今回cancel、Waylandは将来VK_KHR_wayland_surface backendとして追加する。

## q309開始: WS014 p006を単一項目で実行（2026-09-13）

ユーザーの「では、実行してください。」により、[p006](https://github.com/awemorris/zedBSD/issues/393)全体をq309-i01として実行する。kernel handle/fd/SCM_RIGHTS、GPU/Venusの別context allocation共有・GPU内表示、VK_KHR_wayland_surface、最小libwayland-client.so・zwl・wltest、限定検証と実QEMU受入を一つのPhase/項目に含める。中核K/driverを先に実装し、通信/WSI/アプリを接続して実測から改善する。p004は含めない。

時間枠は720 active minutes見積、120分ごとに進捗・残件を確認。各command/VMを有限化し、同条件無変更retryは3回まで。達成の保証や無限継続ではなく、未達は証拠と再開条件を残す。全規約を適用し、既存成果/履歴を保持する。HAL追加変更とgit add/commit/pushは許可されたとは解釈しない。private host・image/source転送の承認を維持する。

q309はactive、q309-i01とp006はin-progress、WS014はincomplete。q308 finished・WS030 completed・既存clearanceは維持。新経路でCPU readbackを必須にせず、実GPU allocation共有と同期/寿命/Wayland protocolを確認する。実装成功・Phase受入はまだ記録していない。

## q310開始: GPUレビュー改善p007（2026-09-13）

ユーザーの新Phase作成・実行指示により、[WS014 p007](https://github.com/awemorris/zedBSD/issues/394)全体を単一項目q310-i01として実行する。直接VK_KHR_displayにもGPU copy/blit→共有linear画像→BLOB scanoutを使い、通常vkdemoのCPU readbackを除く。完了通知・command batch/reply mmap・controller排他の短縮・非同期present・同一open並行性・安全なtransport回復・buffer/optimal allocation共有を改善し、実QEMUで描画/寿命と転送数を検証する。

ユーザーはzwlの1パス1surface同期presentとlibwaylandの限定protocolをテストドライバとして承認した。一般Wayland環境、複数window合成・入力・既存Toolkit対応・常駐化は本Phaseへ入れない。reviewの推奨は仕様と照合し、送信受理、Venus decoder応答、VkFence完了、scanoutを区別する。以前のp006でGPU内表示を実証したのはWayland経路であり、直接表示にCPU経路が残った対応不足を訂正する。

p006/q309は実際の受入範囲のcleared/finishedと証拠を保持する。順序はp006 cleared → p007 in-progress → p004 planning/未queue → WS029 native i915。WS030 completed、p001の未決定と既存clearanceは維持。q310は720 active minutes見積/120分レビューの有限項目。HAL追加変更、VFIO/ホスト表示停止、git add/commit/pushは含めず、private image/source転送とGitHub計画同期の既存承認を使用する。開始時点では新実装・試験の成功を主張しない。

## q310追補: 標準external memory/fence fdと描画・表示の組（2026-09-13）

ユーザーの追加review2 B/C/E/F/Gを[WS014 p007](https://github.com/awemorris/zedBSD/issues/394)へ追加し、q310-i01の同じ目標で実行する。KERNEL_HANDLE_FENCE/POLLIN・command/present wait/signal、VK_KHR_external_fence_fdとVK_KHR_external_memory_fdのOPAQUE_FD、必要な標準問い合わせ/拡張依存、libvulkanでの描画nodeと表示nodeの組、driverによるscanout import可否/制約照会、swapchain作成時の一度だけの共有またはCPU fallback判断を含める。

OPAQUE_FDはLinux dma-buf/SYNC_FDを要求しないが、同じdeviceUUID/driverUUIDの互換条件を守る。別描画GPUからの無条件importは前提にしない。表示専用foreign importはdriverが実backingを検証した場合のみで、未対応は拒否する。通常VenusのBLOB直接表示を受入条件として維持する。別node/制約/fallbackは実コードfixture、標準共有・描画・寿命は実Venusで検証する。

zwlの同期1surface/passとlibwayland限定protocolはユーザー承認のテストドライバ範囲として維持。一般Wayland/Toolkit、external_semaphore_fdやdisplay_controlの全API、native i915、新HAL/物理foreign-DMA受入、git add/commit/pushは自動追加しない。720 active minutes見積/120分レビューを維持。新規機能の実装・受入成功はまだ記録していない。p004はこの追加を含む最終APIを後続で確認し、未queueのまま。

## q310 checkpoint 01: 実QEMUで並行操作・標準fd共有を確認（2026-09-13）

[WS014 p007](https://github.com/awemorris/zedBSD/issues/394) / q310-i01 は実装・検証を継続中。p007は未完了であり、p004/native i915には進んでいない。

- 実QEMU `q310-wayland-005` で同一GPU fdの4 pthread×32回（128 allocation lifecycle）の書込/読出全4096 byte照合がPASS。
- 別processの標準Vulkan OPAQUE_FD fenceのexport/import/reset・実GPU signal・参照寿命がPASS。linear共有と標準buffer OPAQUE_FDのproducer終了後import/GPU copy/pixel検証もPASS。
- 同じ実行はoptimal image capability queryでVK_ERROR_FORMAT_NOT_SUPPORTEDとなりFAIL。対応が必要なnative dedicated allocation条件を確認し、必要なVK_KHR_get_memory_requirements2 / VK_KHR_dedicated_allocationを標準APIで補う。対応型の能力を偽って成功扱いにしない。Vulkan coreは1.0を維持する。
- 限定fixtureの通常版/ASan・UBSanではtyped fence/poll/SCM_RIGHTS、signal fd close/reuse競合、K allocation rollback・元所有者終了後の保持、display-only foreign backingのDMA/cache拒否と最後の解放、非同期presentの所有権、作成時に一度だけのfallback選択、command batching、mapped reply、IRQ out-of-order/制御用slot確保を確認した。fixtureと実GPUの証拠は区別する。
- 実試験で見つかったQUERY出力欄をRESET/WAIT入力へ再利用する不具合を修正。次に露呈したAMD64 pthread初期SPのC ABI不一致はlibcで修正し、005で例外が消えた。HAL変更は行っていない。
- 失敗履歴001（旧診断不足）、002（harnessで未対応の`;`を入力）、003（RESET EINVAL）、004（workerのmovapsでstack alignment例外）、005（上記optimal profile拒否）を保存。失敗を過去の成功へ書き換えない。

残件はoptimal allocationの標準API受入、direct BLOB/Waylandの画面・寿命・性能検証、故障/回復の有限試験、最終API/規約/宣言再生成確認。zwlとlibwaylandの承認済みテストドライバ制約は維持する。資料・コードは作業treeにありgit add/commit/pushはユーザー担当。GitHub Issues/Projectの同期とrepository公開を混同しない。

ローカル証拠: `plan/ws014/temp/remote/q310-wayland-001`〜`005`（実行ごとのsource/image hash、build/transfer/guest/renderer log）。UAPI対応表は `plan/ws014/phase007/gpu-uapi-contract.md`。同表は下記Phase本文にも掲載する。720 active minutes見積・120分レビューを維持し、今回のcheckpoint後もq310を継続する。

q310 checkpoint 02: paired OPAQUE rendererでstandard buffer/optimal/fence、Wayland12画面、direct6画面と通常readback0、producer終了error、10000ms timeout後のchecked recoveryを実QEMUでPASS。最後のtopology POLLPRI/ACK通知と統合確認を継続中。詳細は[WS014 p007](https://github.com/awemorris/zedBSD/issues/394)。

## q310最終引継ぎ

q310 finished、[WS014 p007](https://github.com/awemorris/zedBSD/issues/394) cleared。BLOB直接表示・標準OPAQUE memory/fence・同期/batch・topology/placementを受入済み。最終APIは170 commands / drv_gpu_ops v6。active Queueなし、p004とWS029は未queue。optimal共有には隔離したpaired renderer差分を使用。source/docのgit公開はユーザー担当。

## q311開始: GPU完了責任・fence所属と描画資源の改善（2026-09-13）

ユーザーがレビュー回答を承認し、独立Phaseの作成・実行を指示した。[WS014 p008](https://github.com/awemorris/zedBSD/issues/395)を単一項目q311-i01で実行する。p007/q310のcleared/finishedを保持し、p008 → p004 planning/未queue → 別WS029 native i915の順とする。

A1の局所表示エラー分離、A3のGPUドライバによるfence終端、A4のqueue容量、A5のacquire待機、A6の同時進行slot別pool/cb再利用、A7のexternal worker撤去・console通知、A2/A8の検証補強を含む。fenceはdrv_gpuフレームワークへ移し、kernには不透明handle/fd/refcount/poll/SCM_RIGHTSを残す。共通DRIVER分類＋ops識別とGPU組込時だけのbuildを用いる。

isolated paired rendererのSTRICT_QUEUE能力を合意し、実GPU成功だけを正常retireへ流す。失敗はsticky化して後続まとめretireを抑止し、K watchdogでERROR終端する。native投入前の予約も期限管理し、U停止による未監督仕事を残さない。通常BLOB表示と標準OPAQUE_FDを維持する。

720 active minutes見積・120分レビュー、有限fixture/build/VMで完了まで進める。HAL追加変更・一般Wayland・native i915・git add/commit/pushは含めない。既存private host/転送とGitHub同期の承認を使用する。開始時点で新実装や検証成功は主張しない。詳細はp008本文と承認回答コメント、local plan/ws014/phase008/に残す。


## q311完了: GPU完了責任・driver fence・描画資源改善（2026-09-13）

WS014 p008 / q311-i01をcleared、q311をfinishedとする。active Queueなし。p007/q310の受入を保持し、WS014はincomplete、p001/p004はplanning、p004と別WS029 native i915は未queueのまま。

承認回答A1–A8とfence所属を実装した。fenceはdrv_gpuフレームワークへ移し、kernは不透明handle/fd/refcount/poll/SCM_RIGHTSを保持する。6platformでGPU共通層＋fenceをbackend選択時だけbuildし、GPUなしamd64実ELFでGPU symbol/object不在とgeneric handle/fd残存を確認した。

native投稿前のGPU_JOB予約から独立watchdogが監督し、strict paired rendererの実submission VkFence成功でKがexact generationを終端する。U-only/未commitのpendingをK内部で無期限に待たない。slotは最大64descriptor/32chain（28job＋4control）、外部fenceごとのworkerを撤去。acquireはmonotonic condition、present pool/cbは同時slotごとに再利用し、consoleは文字・所有権変更で起床する。局所表示エラーと全device故障も分離した。

最終5VMは同じkernel/base imageでPASS/QEMU exit0: direct-002 41.345秒、wayland-002 46.011秒、producer-stop-004 14.117秒、producer-exit-002 4.214秒、recovery-002 13.823秒。直接/Waylandの通常BLOB表示・独立画像oracle・複数process・再open/consoleを確認。SIGSTOP中fdを開いたproducerは9970msでDEVICE_LOST、renderer停止は10000msで故障通知後にchecked reset・新context往復を確認した。

K/U/transport/host/consoleの限定normal・sanitizer、170API/両ABI/Noct8file、対象build・規約・独立レビューを完了。U回収競合2件は修正前FAIL→修正後PASS。static analyzerの4警告は実callee/有効入力の前提と照合して記録し、全警告0とは扱わない。初期のbuild/harness失敗も保持する。

新libvulkanのVkDevice作成にはSTRICT_QUEUE対応のisolated paired rendererが必要。stock/旧pairは初期化で拒否する。host system packageとHALの追加変更なし。通常2秒sceneはp007再測定13frameからp00815frameだが、QEMU CPU時間は0.36秒から0.48秒の単発観測で、CPU削減や速度倍率は主張しない。一般Wayland/Toolkit、任意GPU間DMA、native i915、CTSは未受入。source/doc/patchのgit add/commit/pushはユーザー担当。


受入記録: [p008結果コメント](https://github.com/awemorris/zedBSD/issues/395#issuecomment-5652374702)。local/uncommittedの資料は plan/ws014/phase008/、Queue履歴は plan/history/queue-q311.md。

## q312開始: GPUレビュー対応とフレームワーク共通化（2026-09-13）

ユーザー指示により[WS014 p009](https://github.com/awemorris/zedBSD/issues/396)をq312-i01の単一Phaseとして実行する。承認範囲は[review4回答](https://github.com/awemorris/zedBSD/issues/395#issuecomment-5652742665)とGPU共通化の協議。job/fenceの状態・容量待機・期限・session故障と参照保持をdrv_gpuへ寄せ、backendは実資源の予約・投稿・完了と停止/DMA退役確認を担う。R1のU排他と容量通知、R3の期限/障害範囲、R4のdirectAcquire通知、R5のprivate fence reset再利用・測定、R6の寿命を改善する。

R2はstrictを当面維持し、stock互換の能力と退役条件を限定検証する。安全性が成立しなければstrictと具体的な不足・制約を記録する。context停止も能力と実確認が前提で、停止不能時はquarantine/全体resetを維持する。通常BLOB表示・GPU内共有・標準APIとzwl/libwaylandのテストドライバ範囲を保持する。

p008/q311のcleared/finishedを保持し、順序はp009 → p004 planning/未queue → 別WS029。720 active minutes見積・120分レビュー、有限fixture/build/VMで実装・受入する。追加HALや一般DE/native i915、git add/commit/push、system package/GDM/VFIO変更は含めない。private host/転送・隔離依存build・GitHub同期は既存承認を使用する。開始時点では新実装・試験の成功は主張しない。
