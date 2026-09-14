# zedBSD — Awesome Plan

Read and follow `docs/agent/awesome-plan/awesome-plan.md` (pinned upstream skill),
then `plan/config.md`, `plan/guardrail.md`, and `plan/README.md`.
This applies throughout this repository. GitHub mode is already approved and
configured; do not repeat adoption or create another Master/Project.

At session start inspect `plan/.sync/state.json`, pending outbox/conflicts and
working-tree changes; fetch current Master, Guardrail, Queue, Past Log and the
selected WS/Phases including decision comments. Use `plan/tools/sync.py` and
`plan/tools/README.md`. Reconcile before edits; never overwrite remote human work.
Read only relevant plans/history, not the whole plan tree on every session.

Mandatory synchronization (user instruction, 2026-09-11): after every authorized
planning update, publish Master and all affected WS/Phase/Board records, decision
comments and Project projections to the configured GitHub project, then read
them back before reporting completion. Do not leave an updated master.md only
in the local cache or defer synchronization because earlier publication drafts
were pending. For this update, the user explicitly requested Master synchronization.
Publish the authorized records; do not expand a specific synchronization request
into new Issue creation without applicable scope authorization. If an actual tool/permission/network
block prevents publication, preserve the outbox and report the exact unsynced
records and blocker; never claim success or treat local edits as synchronized.
Do not repeat a publication-permission question for already authorized work.
This rule does not authorize git commit/push or broader implementation.


Current handoff: no active Queue or Priority list. q303 finished/stopped.
2026-09-11 user decision: WS025 p029/p030/p032/p038 cleared; WS025, WS019,
WS006, WS022 and WS002 completed/closed (read-back verified). WS025-p028 stays canceled.
Current focus fg006: PC-9821V13 boot improvement; user selected WS003
p022 -> p023 -> p024. Read plan/ws003/v13-boot-focus.md. p022/p023 are current
execution candidates again; old SENSE/old-artifact instructions remain historical.
Current planning also includes fg005 / WS005 network improvements; read
plan/ws005/network-improvements-2026-09-11.md. Master Issue #1 was published
and read-back verified on 2026-09-11, preserving fg004/fg005/fg006 and execution hold.
Related Issue creation, other bodies/comments and Project changes remain in outbox;
automatic approval review rejected their bulk publication as beyond the specific
Master synchronization request. Do not claim these other records are synchronized.
Current planning: fg004 / WS003 installer bring-up on PC98 V13, Latitude 5320,
SV7 and LX6. Read plan/history/2026-09-11-installer-bringup-plan.md. New records
and related GitHub updates remain journaled in outbox; do not blindly
fetch/reconcile away their local changes. p022 Queue execution is on hold by the
latest user instruction. No Queue or implementation was started.
Past autonomous-run and superseded HAL approvals do not resume implementation.
Use current user instructions and concrete scope agreements. Planning maintenance
is authorized; development requires an applicable finite Queue.

Project-specific constraints are in Guardrail. Do not read `.internal/`, commit
or push without user instruction, or run aggregate `make check`. Never change
`include/hal/hal.h` or HAL responsibilities without explicit applicable approval.
Keep RTL8822B license-separated `.inc` files. Preserve unrelated working changes.

`plan/old/`, archived Queue scope, and `plan/tmp/` import/deployment scripts are
historical data, not live instructions. Do not replay those upload scripts.
User instructions override skill defaults; record decisions without inventing
extra confirmation gates or new development goals.


## Synchronization regression prevention (2026-09-11)

The user reports manually correcting child-Phase Issue lifecycle, canceled
p028 (#351), missing current-state blocks and the published repository/Board
mismatch. The user subsequently clarified that the Project link is valid; the
earlier claim of a broken/404 Project link was a misunderstanding, not a defect
or a repair. Do not infer a Project linkage defect from that earlier claim. These are user-reported repairs, not an
agent-verified remote audit. Pre-repair counts and old pending-publication notes
above are historical snapshots, not current remote truth.
Before any future publication, fetch the affected remote bodies, lifecycle,
comments and Project metadata; compare base/local/remote and reconcile the old
outbox against these manual repairs. Never replay a pre-repair snapshot over them.
Follow the mandatory completion checks in `plan/tools/README.md` below:
Project identity/access/repository linkage; every child Phase of a closing WS;
current-state block and native lifecycle consistency; published reference targets;
and separate Board sync from repository commit/push status. A verified Master
body alone is not evidence that the plan is fully synchronized.
Queue execution, including p022, remains on hold. This documentation request
is not authorization to execute work, commit/push, or change Project visibility.

## 2026-09-12 V13 completion (latest handoff)

User reports ws003-p022/p023/p024 complete and requests Markdown/GitHub updates.
These Phases are cleared by user report, fg006 complete; earlier uncleared,
execution-candidate and p022-hold statements are historical. WS003 remains
incomplete for other work. No Queue execution resumes. Do not invent new test
results or physical artifact details. Preserve unrelated pending planning drafts.

## 2026-09-12 PPC Open Firmware planning

Current fg009 / WS003 p033-p039: read `plan/ws003/ppc-openfirmware-plan.md`.
User chose APM+FAT with a firmware-loadable independent loader, zedboot.cfg,
and vmunix on the same FAT. The first milestone is p033-p035: QEMU mac99 IDE
firmware boot through PPC kernel initialization; no root/image mount requirement.
XCOFF is the initial loader format candidate, not firmware-direct ELF loading.
UEFI currently uses zedbsd.cfg; retain its name and reuse its grammar for the
explicitly requested PPC zedboot.cfg. Later phases cover amd64 OHCI, PPC user ABI,
and USB boot/rootfs.img/data.img integration. No Queue execution is authorized
by this planning request. Preserve fg006 completion and unrelated user changes.


## WSの単一目標と終了後の扱い（2026-09-12ユーザー指示）

WSは一つの具体的な到達目標を持つ。目標を達成したWS、またはユーザーが終了したWSは再利用・再開して別の目標を追加しない。似た領域だからという理由で一つのWSへまとめない。機種対応などの上位分類・到達点はMGが担い、インストーラ実機動作、PowerPC移植などは別のWSを作る。
一つの目標に必要な依存作業をPhaseへ分解することは可能だが、独立した別目標をPhaseとして混ぜない。WS終了時は子Phaseを全件照合し、未完了は完了に改変せず、ユーザー指定の保留先または別WSへ引き継いで元Phaseを終了する。旧ID、結果、転送先を残す。今回WS003は終了・再利用禁止、PPC移植はWS027へ、その他の未完了はFuture Workへ移す。

Current PPC plan: plan/ws027/ws.md. Old WS003 references are historical; follow new WS027 p001-p007.

## 2026-09-12 latest installer handoff

WS028 / fg004 now owns installer hardware acceptance and the NVMe failure report.
The menuconfig-absence hypothesis is recorded, while current source contains
CONFIG_DRIVER_PCI_NVME for amd64/i386; deployed config/image is not verified.
Installer portion of Future F-004 is transferred to WS028; other items stay deferred.
WS003 remains closed, never reusable. WS027 owns PowerPC porting. No Queue started.

## 2026-09-12 GPU planning handoff

WS014/p001 architecture discussion resumed by user; first target is QEMU virtio-gpu, superseding i915-first/manual design hold. Vulkan display API is a proposal, not a frozen ABI. Linux DRM compatibility is not required, but OS memory/sync/display/permission machinery remains necessary. No implementation Queue. Other WS holds stay unchanged. See WS014 and p001; old review cases remain design inputs, not runtime tests.

## 2026-09-12 GPU phase sequence

WS014 p001 supplies design decisions; p002 implements only the GPU framework; p003 integrates virtio-gpu/Venus and the capture/debug loop on Linux i915+ANV host, refining API gaps; p004 reviews final API and full standards. Native guest i915 is the separate WS029 after WS014. Read plan/ws014/qemu-venus-debug-loop.md. No Queue started. User will git add/commit documentation; do not add/commit/push.

## q304 GPU framework completion（2026-09-12 historical handoff）

User authorized and executed only WS014 p002. q304 / q304-i01 finished/cleared;
no active Queue. Read plan/ws014/gpu-framework.md and plan/history/queue-q304.md.
GPU/PCI/cdev framework tests and amd64 build passed. WS014 stays incomplete;
p001/p003/p004 remain planning. Venus/native i915 have not started. Earlier
no-implementation statements above are historical. Do not resume p003 without
an applicable finite Queue. Changes remain uncommitted; user owns add/commit.

q304 sync read-back caught the standing Queue Issue closing after generic
Project Status=Done. Use Awesome Plan Status=finished for the cycle; keep the
standing Queue Issue open and its generic Status unset. Read back both. See
plan/tools/README.md; do not change Project workflows as a workaround.

## q305 GPU registration correction（2026-09-12 latest handoff）

WS014 p002 correction completed: q305 / q305-i01 finished/cleared; no active Queue.
Use drv_gpu_register(ops, private_data, **device) / drv_gpu_unregister(device).
Preserve the user's struct drv_gpu_ops name. GPU has no PCI-specific public
service table, registration wrapper, deferred publish API, or fixed device count.
Common cdev/devfs registry and directory snapshots are dynamic; VFS mount must
preserve earlier registrations. Successful unregister consumes the handle;
EBUSY retains handle/backend until a successful retry. See
plan/ws014/gpu-framework.md and plan/history/queue-q305.md for ownership and tests.
40 GPU / 80 cdev host tests, sanitizers, common registry analyzer, ILP32/LP64 ABI
and amd64 build passed. WS014 remains incomplete; p001/p003/p004 are planning.
Venus/native i915 are not started. Keep changes uncommitted for the user.
The standing Queue Issue remains open with generic Project Status unset;
use Awesome Plan Status=finished for q305 and verify both Issue and Project.

## 2026-09-12 HAL変更の承認条件（ユーザー確認）

ユーザーが「HALの改変には許可が必要です」と明示。HAL責務やhal.hの変更に限らず、src/hal/配下の既存宣言への実装追加・補完も、適用可能な明示許可を得てから行う。既存契約の補完を理由に承認不要と解釈しない。レビュー可能な具体差分を用意し、未許可のHAL変更に依存する実装適用・実行試験は待つ。

## q306 completion / p005 next（2026-09-13 latest handoff）

WS014 p003/q306 cleared/finished: Venus driver, independent Vulkan frame client, and remote QEMU loop passed 2D/Vulkan patterns1/2 on awe@10.0.10.25. QMP controls and captures console; GL frames use egl-headless readback plus Unix VNC. Current amd64 aperture is bounded to8MiB. GPU ops v2/UAPI v1. See plan/ws014/phase003/results.md and plan/history/queue-q306.md. User next requests new p005: userland/base/vkdemo textured rotating cuboid to exercise vertex/fragment shaders, before p004. Prepare a separate Queue; q306 remains closed. p001/p004 planning, WS014 incomplete, native i915 is WS029. User explicitly approved GitHub publication; git add/commit/push remains user-owned. Only the exact eight reviewed amd64 MMIO accessor additions were approved; other HAL changes still require prior specific approval.

## q307 active（2026-09-13 latest handoff）

User requested new WS014 p005 at userland/base/vkdemo: textured rotating cuboid exercising vertex/fragment shaders before p004. Queue q307 selects only p005; plan/Issue/Project are synchronized before implementation. p003/q306 remain cleared/finished. Reuse existing GPU UAPI and independent Venus userland codec, adding only demonstrated missing API. No additional HAL change is authorized. Runtime uses the existing private host and transfer approval. Apply full coding-style and finite focused verification; no aggregate make check or git add/commit/push.

## q307完了（2026-09-13 latest handoff）

WS014 p005/q307 cleared/finished. userland/base/vkdemo renders a textured rotating cuboid with original vertex/fragment shaders and depth. q307-vkdemo-002 passed six GPU/readback/VNC/independent-oracle frames, normal cleanup and a second ordinary two-second/twelve-frame run in the same VM. Shared Venus client and graphics U operations added; the existing K transport now honors its ten-second deadline while clocks advance and uses its poll cap only for stalled clocks. No new ioctl or HAL change. See plan/ws014/phase005/results.md and plan/history/queue-q307.md. No active Queue; p004 planning is next, p001 planning and WS014 incomplete, native i915 remains WS029. GitHub synchronization is approved; git add/commit/push remains user-owned. HAL changes beyond the exact p003 eight-accessor approval still require specific prior permission.

## q308 active（2026-09-13 latest handoff）

User explicitly continues full Vulkan1.0 (137core) and direct-display WSI (KHR_surface/display/swapchain/display_swapchain), with standard headers libc/include/vulkan/, independent userland/base/libvulkan/ and /lib/libvulkan.so. New single-goal WS030 owns the standard library. Queue q308 selects WS030 p001→p002→p003→WS014 p005 standard-API correction→WS030 p004; estimate720 active minutes with120-minute reviews and bounded commands/retries. p005 old clearance is invalidated; q307 historical evidence/attempt is preserved. WS014 p002/p003 cleared, p004 unqueued, WS029 native i915 deferred. EGL canceled for this work, future GLES-on-Vulkan only; Wayland future backend. No additional HAL edits, no aggregate make check, no git add/commit/push; current private-host transfer approval remains. Read plan/ws030/ws.md, its design/Phases and Queue. Begin source implementation only after q308 plan/body/comment/native-parent/dependency/Project readback is complete and implementation-authorized.json exists.

## q308開始時に具体化したHAL前提（未承認）

現行amd64の静的調査で、hal_space_map()はHAL_SPACE_DEVICEでもRAM aliasを要求してMMIOを拒否し、hal_space_map_device()は16MiB固定PCI windowに限られることが分かった。標準Vulkanのcoherent user mappingと十分なHOST_VISIBLE blob容量に必要な出力は、現行の有限8MiB driver subsetだけでは供給できない。

既存HAL契約内のdevice usermapとkernel可変device windowについて、rootがレビュー可能な具体差分を準備し、ユーザーの適用許可を別途確認する。現時点でHAL source変更はなく、q308の承認をその具体差分の適用許可として扱わない。未承認差分に依存するsource適用・build/runtimeは待つ。独立した公開header/dispatch/library/codec等のU作業は計画同期後に進められる。

coherent memoryをCPU copyで代用してその宣言を維持したり、FIFOやlimitsの未達を無視して全1.0をcompleteとしない。必要HAL出力と承認・適用・検証の実際の状態をp002から後続へ引き渡す。公式rendererの固定参照版はvirglrenderer1.1.0（ローカル調査cache: /tmp/q308-virglrenderer-1.1.0）。EGLはゲスト実装を今回cancelしたまま、既存QEMUホストのegl-headless captureとは区別する。

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

## WS014 p006追加: kernel handle・GPU共有・最小Wayland（2026-09-13）

ユーザー指定により[WS014 p006](https://github.com/awemorris/zedBSD/issues/393)を一つのplanned Phaseとして追加した。kernel_handle/handle_fd_*とSCM_RIGHTS、GPU/Venusの別context共有、GPU画像を扱えるWSI、VK_KHR_wayland_surface、最小client library、全画面zwl、標準APIのwltestを本Phaseで実装・検証する計画。コード配置はlibc/include/wayland/、userland/base/libwayland/・zwl/・wltest/、公開libraryは/lib/libwayland-client.so。

中核のK/driver実装を先に進め、Wayland通信/WSI/試験アプリを接続して実測から設計を改善する。新経路はCPU readbackを必須にせず、GPU allocationの実共有と同期・寿命を確認する。linux-dmabuf-v1、ゲストdma-buf/DRM、EGL、一般DEは採用しない。内部の段取りは別Phaseへ分割しない。

順序はp005 cleared → p006 planned → p004 planning。p004はp006の最終ソース/API/検証を受けて規約確認する。WS030 completedとq308 finished、既存Phaseのclearを維持。今回作成したのは計画であり、active Queue・新しい実装/試験結果はない。HALの追加差分は従来どおり個別承認、git add/commit/pushはユーザー担当。

## q309開始: WS014 p006を単一項目で実行（2026-09-13）

ユーザーの「では、実行してください。」により、[p006](https://github.com/awemorris/zedBSD/issues/393)全体をq309-i01として実行する。kernel handle/fd/SCM_RIGHTS、GPU/Venusの別context allocation共有・GPU内表示、VK_KHR_wayland_surface、最小libwayland-client.so・zwl・wltest、限定検証と実QEMU受入を一つのPhase/項目に含める。中核K/driverを先に実装し、通信/WSI/アプリを接続して実測から改善する。p004は含めない。

時間枠は720 active minutes見積、120分ごとに進捗・残件を確認。各command/VMを有限化し、同条件無変更retryは3回まで。達成の保証や無限継続ではなく、未達は証拠と再開条件を残す。全規約を適用し、既存成果/履歴を保持する。HAL追加変更とgit add/commit/pushは許可されたとは解釈しない。private host・image/source転送の承認を維持する。

q309はactive、q309-i01とp006はin-progress、WS014はincomplete。q308 finished・WS030 completed・既存clearanceは維持。新経路でCPU readbackを必須にせず、実GPU allocation共有と同期/寿命/Wayland protocolを確認する。実装成功・Phase受入はまだ記録していない。

## q309完了: GPU handle共有・Wayland WSI・virtio scanout（2026-09-13）

WS014 p006 / q309-i01をcleared、q309をfinishedとする。active Queueなし。WS014はincomplete、p001/p004 planning、p004未queue。WS030 completedと既存Phaseのclearanceを維持し、native i915は別WS029のまま。

kernel_handle/handle_fd_*と共通fd参照・SCM_RIGHTS、GPU/Venusの独立process/context共有、VK_KHR_wayland_surface、最小libwayland-client.so、全画面zwl、標準Wayland/Vulkanアプリwltestを実装した。共有GPU imageはGPU copyと所有権同期を経て別processへ渡り、SET_SCANOUT_BLOB/RESOURCE_FLUSHで表示する。通常の新WSI経路にCPU readbackや再uploadを必須としない。旧copy経路もGOPではなくvirtio 2D scanoutであり、直接表示の互換経路として保持する。

GET_DISPLAY_INFOとGET_EDIDのbase/CTA progressive DTDで表示・モードを列挙。実QEMUでは1280×800、74,994mHz、320×200mmを取得した。custom framebuffer寸法はEDID寸法と独立に扱い、1〜100Hzのguest nominal pacingを検証する。virtioは物理pixel clockを設定しないため、物理vblank同期の保証とは区別する。

最終q309-wayland-004は43.869秒、QEMU exit0でPASS。FIFO/MAILBOX各6枚の320×240実VNC画像、計921,600画素が独立期待値と全画素一致し、12回の表示が実import資源とBLOB scanoutに対応した。生成元終了後の独立renderer import/GPU copyも検証専用readbackの1024画素が一致。swapchain再作成、client中断・再open、compositor通常終了・SIGKILL・再起動、実SURFACE_LOST/cleanup=0、強制終了直後の640×480 console復帰とechoによる画面更新を確認した。

同じ最終kernelのq309-direct-002も42.705秒、QEMU exit0でPASS。標準vkdemoの回転直方体6枚をGPU readback/VNC/独立oracleで照合し、通常終了とSIGINT後の再open、console復帰、表示競合拒否とowner完走を確認した。K/fd/SCM・GPU/EDID・Wayland/WSIの実コード限定fixtureとsanitizer、157 Vulkan dispatch/export、両ABIの公式header照合、Noct再生成、rootfs配置、対象buildと全適用規約を確認した。正式CTSや全Wayland SDK互換は主張しない。

途中のharness起動待ち不足とEDID/custom mode回帰を修正し、失敗証拠と再実行理由を保存した。最終reviewのMSG_PEEK二重put疑義は、rights付きpeekを既存guardが拒否するため到達不能と確認し、本体変更を戻して拒否後の参照寿命を追加検証した。formatterは規約と設定の不一致によりexit1でありPASSとは扱わず、全文確認とdiffcheckを記録した。HAL追加変更なし。ローカル結果はplan/ws014/phase006/results.md、技術資料3件、conformance.mdとfinal-evidence/verification.json、Queue履歴はplan/history/queue-q309.md。これらsource/doc/imageのgit add/commit/pushはユーザーが行う。本同期はGitHub Issues/Projectの計画・受入結果である。

## q310開始: GPUレビュー改善p007（2026-09-13）

ユーザーの新Phase作成・実行指示により、[WS014 p007](https://github.com/awemorris/zedBSD/issues/394)全体を単一項目q310-i01として実行する。直接VK_KHR_displayにもGPU copy/blit→共有linear画像→BLOB scanoutを使い、通常vkdemoのCPU readbackを除く。完了通知・command batch/reply mmap・controller排他の短縮・非同期present・同一open並行性・安全なtransport回復・buffer/optimal allocation共有を改善し、実QEMUで描画/寿命と転送数を検証する。

ユーザーはzwlの1パス1surface同期presentとlibwaylandの限定protocolをテストドライバとして承認した。一般Wayland環境、複数window合成・入力・既存Toolkit対応・常駐化は本Phaseへ入れない。reviewの推奨は仕様と照合し、送信受理、Venus decoder応答、VkFence完了、scanoutを区別する。以前のp006でGPU内表示を実証したのはWayland経路であり、直接表示にCPU経路が残った対応不足を訂正する。

p006/q309は実際の受入範囲のcleared/finishedと証拠を保持する。順序はp006 cleared → p007 in-progress → p004 planning/未queue → WS029 native i915。WS030 completed、p001の未決定と既存clearanceは維持。q310は720 active minutes見積/120分レビューの有限項目。HAL追加変更、VFIO/ホスト表示停止、git add/commit/pushは含めず、private image/source転送とGitHub計画同期の既存承認を使用する。開始時点では新実装・試験の成功を主張しない。

## q310追補: 標準external memory/fence fdと描画・表示の組（2026-09-13）

ユーザーの追加review2 B/C/E/F/Gを[WS014 p007](https://github.com/awemorris/zedBSD/issues/394)へ追加し、q310-i01の同じ目標で実行する。KERNEL_HANDLE_FENCE/POLLIN・command/present wait/signal、VK_KHR_external_fence_fdとVK_KHR_external_memory_fdのOPAQUE_FD、必要な標準問い合わせ/拡張依存、libvulkanでの描画nodeと表示nodeの組、driverによるscanout import可否/制約照会、swapchain作成時の一度だけの共有またはCPU fallback判断を含める。

OPAQUE_FDはLinux dma-buf/SYNC_FDを要求しないが、同じdeviceUUID/driverUUIDの互換条件を守る。別描画GPUからの無条件importは前提にしない。表示専用foreign importはdriverが実backingを検証した場合のみで、未対応は拒否する。通常VenusのBLOB直接表示を受入条件として維持する。別node/制約/fallbackは実コードfixture、標準共有・描画・寿命は実Venusで検証する。

zwlの同期1surface/passとlibwayland限定protocolはユーザー承認のテストドライバ範囲として維持。一般Wayland/Toolkit、external_semaphore_fdやdisplay_controlの全API、native i915、新HAL/物理foreign-DMA受入、git add/commit/pushは自動追加しない。720 active minutes見積/120分レビューを維持。新規機能の実装・受入成功はまだ記録していない。p004はこの追加を含む最終APIを後続で確認し、未queueのまま。

## q310完了: BLOB直接表示・標準fd共有・GPU同期改善（2026-09-13）

WS014 p007 / q310-i01をcleared、q310をfinishedとする。active Queueなし。WS014はincompleteでp001/p004はplanning、p004とWS029 native i915は未queue。p006/q309・WS030の既存clearanceを維持する。

直接VK_KHR_displayもGPU copy/blit→共有linear画像→SET_SCANOUT_BLOBへ移行し、通常vkdemoのCPU画像readback/uploadを除いた。IRQ完了通知、vkCmd batchingとmapped reply、表示待ち中のcontroller排他短縮、所有jobによる非同期present、同一openの待機admission、安全なtransport回復を実装した。

review2 B/C/E/F/Gも反映: KERNEL_HANDLE_FENCE/POLLINとcommand/present wait/signal、標準VK_KHR_external_memory_fdとexternal_fence_fdのOPAQUE_FD、描画node＋表示nodeの組、driverによるscanout import判定とswapchain作成時に一度だけの経路選択。GPU_DISPLAY_EVENTS/POLLPRIのQUERY→列挙→exact ACKと、GPU_BLOB_CREATE_PLACEDによる物理配置要求を追加した。GPU ABI v1の旧要求layoutを保ち、内部drv_gpu_opsはv6。実backingで満たせない配置はENOTSUP、OOMやdevice lossはfallbackで隠さない。

OPAQUE_FDはguestにdma-buf/SYNC_FDを要求しない。標準memory共有は同じdeviceUUID/driverUUIDの互換範囲、別GPUの任意importは未対応。表示専用foreign import・DMA/cache/placement・別node組合せは実コードfixtureによる検証であり、異種実機DMAや新HAL allocatorの受入ではない。Venusは非零physical placementを拒否する。

| 最終実QEMU | 結果 |
| --- | --- |
| q310-wayland-008 | PASS、46.531秒、QEMU exit0。128回同一fd並行操作、標準fence・linear/buffer/optimal共有、初期topology QUERY/ACK、FIFO/MAILBOX各6画面・異常終了/reopen/console |
| q310-direct-004 | PASS、41.83秒、QEMU exit0。直接表示6画面の独立oracle、通常readback0の動く2実画像、QEMU BLOB trace/対象寸法のlegacy経路なし、SIGINT/reopen/lease競合/console |
| q310-fence-exit-003 | PASS、4.424秒、QEMU exit0。pending producer終了後DEVICE_LOST、独立30秒fault期限と実測、最終waitpid |
| q310-recovery-004 | PASS、13.807秒、QEMU exit0。所有rendererだけを停止、10000ms timeoutとpeer error/旧参照gate、再開後checked resetと新contextの4096byte/decoder |

最終buildと限定K/U/driver/WSI fixtureの通常・ASan/UBSan、170公開APIのdispatch/両ABI、Noct生成8file一致を確認した。通常デモは約2秒で13frame程度という実測を残し、速度倍率や物理vblank保証・CTS適合を主張しない。zwlの同期1surface/passとlibwayland限定protocolはユーザー承認のテストドライバ制約として維持する。

optimal共有はstock virglrenderer1.1.0 proxyのOPAQUE attach不足を補ったisolated paired library/serverで受入した。exact168B capsetとINIT handshakeで合意した場合だけ選択し、private WSIのnative DMA経路を保持。system library/packageは変更していない。patch/library/server hashと手順はlocal/uncommitted plan/ws014/phase007/renderer-opaque/に保存した。stockだけでoptimal共有が通るとは扱わない。

失敗履歴wayland001–005、direct001、recovery001、fence-exit002を保存。fence-exit002はconsumer/driver双方10秒の期限競合でVK_TIMEOUTが先行した。fault専用期限を30秒に分け、final closeはpending fenceをerrorへしてからcallback drainを待つ順序へ改善した。修正前FAIL・修正後normal/sanitizer PASSの因果fixtureも保存し、実行中ioctl/file参照によるfinal close入口までの遅延とは区別する。

資料はlocal/uncommitted plan/ws014/phase007/results.md、gpu-uapi-contract.md、display-wsi.md、transport-sync.md、各verification JSONとQueue履歴plan/history/queue-q310.md。新HAL変更・GDM/VFIO操作・git add/commit/pushなし。GitHub Issues/Projectへの計画/受入同期と、ユーザー担当のrepository公開を区別する。

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


## q312完了: GPUレビュー対応とフレームワーク共通化（2026-09-13）

WS014 p009 / q312-i01をcleared、q312をfinishedとする。active Queueなし。p008/q311の受入を保持し、WS014はincomplete、p001/p004はplanning、p004と別WS029 native i915は未queueのまま。

R1: 容量不足をOOMにせず、`GPU_JOB_CAPACITY` QUERY/WAIT（ioctl 37）と`GPU_JOB_POLICY`（38）を追加。libvulkanはnative準備→QUERY→回収→非待機RESERVEとし、EAGAINではqueue/device/context mutexを外して待つ。R3: 予約10秒・実行60秒・停止10秒をmake/menuconfigの設定と実効値照会にし、session単位のsticky errorと`drv_gpu_recovery_ops`（stop_begin/stop_poll/fault/reset）でcontext単位の停止確認を導入。Venusはflags7のquiescence契約（全native VkDeviceWaitIdleを確認したCPU0 ACK）を持つisolated pairで実停止を証明し、確認不能なら従来のquarantine/全体resetへ進む。R4: direct acquireは画像返却・故障・topologyをwaiter固有pipeと`ppoll`で待ち、10 ms周期起床を除いた。R5: terminal private fenceを最大64本ずつ一括resetしREADYを再利用。R2はstrict（flags7）維持、stock 1.1.0の情報欠落をstock-compat/で記録。fenceとjob監督はdrv_gpu内に保持し、汎用kernへの追加なし。

最終8VMは同一最終artifactでPASS/QEMU exit0: direct-003 41.935秒、wayland-002 47.342秒、submit-load-005 11.833秒（2process 576 submit、OOM 0）、completion-delay-003 25.316秒（15秒遅延完了、peer継続）、context-timeout-003 25.138秒（短縮期限でDEVICE_LOST、peer継続）、producer-stop-002 19.438秒（SIGSTOP中に7770 msで終端）、producer-exit-002 4.165秒、recovery-002 14.051秒（10000 ms watchdog後checked reset）。限定fixture（K 12 suite、U 10+5 job、transport/host/console）、170 API/両ABI/Noct、6platform×GPU有無のbuild入力、GPUなしamd64実ELF、規約確認を完了。失敗履歴（submit-load-001の能力bit漏れ、producer-stop-001の旧期待値、recovery-001のerrno期待値）を保持し、初回成功とは扱わない。

新libvulkanのVkDevice作成にはflags7（OPAQUE+STRICT+QUIESCE）のisolated paired rendererが必要で、stock/旧pairは初期化で拒否する。実行期限60秒は正当な長時間computeにも適用される。任意GPU間DMA、native i915、一般Wayland/toolkit、CTSは未受入。HAL・host system package・git add/commit/pushは行っていない。

受入記録: local `plan/ws014/phase009/results.md`、`runtime-verification/summary.json`。受入記録: [p009結果コメント](https://github.com/awemorris/zedBSD/issues/396#issuecomment-5655170913)。

## q313開始: GPU監督の共通化仕上げと局所隔離（2026-09-14）

ユーザー指示により[WS014 p010](https://github.com/awemorris/zedBSD/issues/397)をq313-i01の単一Phaseとして実行する。前提はp009の自己レビュー（plan/ws014/gpu-stack-review5.md、SHA256 `05435c48bdfd9168184fc026a1a628ba5d392c91522fa0556b9ed662fa2a200c`）と、その後のframework側実装可否・Venusから移せる処理の回答。S1 停止期限の起点をstop_begin実呼出しへ（D1/B3）、S2 close時のcommit済みjob監督継続（D2）、S3 停止shortcut・fault cancel後のsession失敗・RESERVED回収・停止flag・control期限定数のframework移管、S4 monitor起床の限定とrecovery_ready除去（D4/B6）、S5 停止未確認contextのsession隔離とidle時reset回収（D3/B4/B5）の順に、各段階を限定fixtureで固定してから進める。

既存UAPIのlayout/ioctl番号/sizeは変えず、内部opsは版9へ進める。実QEMUは既存7件の回帰に加え、producer-exit-delayed（既定policyで15秒jobを持つproducer終了後にconsumer fenceが成功）とproducer-exit-hang（event待ちjobで実行期限DEVICE_LOST、他sessionの継続、idle時のreset回収）を新設し、producer-exitは実行期限ERRORへ期待値を更新する。

p009/q312のcleared/finishedを保持し、順序はp010 → p004 planning/未queue → 別WS029。720 active minutes見積・120分レビュー、fixture120秒/build・転送1200秒/VM180秒（hang系300秒）で有限化。追加HAL、stock互換、一般DE/native i915、git add/commit/push、system package/GDM/VFIO変更は含めない。private host/転送・隔離依存build・GitHub同期は既存承認を使用する。開始時点では新実装・試験の成功は主張しない。GitHub Issues/Projectへのq312完了とq313開始の公開は、このsessionでは自動承認レビューにより保留され、outbox/draftsに記録した。

## q313完了: GPU監督の共通化仕上げと局所隔離（2026-09-14）

WS014 p010 / q313-i01をcleared、q313をfinishedとする。active Queueなし。p009/q312の受入を保持し、WS014はincomplete、p001/p004はplanning、p004と別WS029 native i915は未queueのまま。

S1: 停止期限の起点を`stop_begin`実呼出しへ移し、実行期限内のjobが残る間は停止しない。S2: graceful closeはcommit済みjobを終端せず実結果をfenceへ公開する。S3: native仕事の有無判定、fault cancel後のsession失敗、RESERVED回収、admission拒否、control期限定数を共通層へ移した。S4/B6: monitor起床の限定と`drv_gpu_recovery_ready`除去。S5: `recovery->isolate`（ops版9）で停止未確認contextをsession隔離し、device全体は継続、idle時のchecked resetで回収。libvulkanは自contextのPOLLERRだけでdevice lossをlatchする。UAPI/HALは不変。

最終10VMは同一最終sourceの2 build（既定policy・短縮policy）でPASS/QEMU exit0: exit-delayed-003 19.698秒（producer終了後にconsumer fenceが14750 msでSUCCESS）、exit-hang-006 28.178秒（8000 msでDEVICE_LOST、context隔離、peer継続、idle openでreset回収と通常試験PASS）、producer-exit-002 24.447秒（hostの実結果を公開）、direct-002 41.796秒、wayland-002 47.258秒、submit-load-002 10.886秒、completion-delay-002 25.213秒、context-timeout-002 25.296秒、producer-stop-002 19.341秒、recovery-002 13.926秒。限定fixture（GPU core 10種、Venus 5種、libvulkan 5種、build selection）を通常＋sanitizerでPASS。失敗履歴（exit-hang-001のU側latch、exit-hang-004のreset中open拒否、producer-exit-001の旧期待値）を保持し、初回成功とは扱わない。

隔離で失った容量はidle時のresetまで戻らず自動escalationは無い。隔離contextの表示状態はresetまで残る。closeはcommit済みjobの退役まで待つ。git add/commit/pushはユーザー担当。受入記録: [p010結果コメント](https://github.com/awemorris/zedBSD/issues/397#issuecomment-5655171051)。

受入記録: local `plan/ws014/phase010/results.md`、`runtime-verification/summary.json`、`gpu-supervision-contract.md`。

## q314準備: WS029 i915ネイティブGPU driver（2026-09-14）

ユーザー指示により[WS029](https://github.com/awemorris/zedBSD/issues/386)へp001–p007を作成し、Queue q314を準備状態で作成した。実行は次の指示で開始し、開始時にp001の移植方針承認とp006のhost操作許可をjournalへ記録する。計画の正本は plan/ws029/i915-design.md（事実、決定、ファイル構成、関数一覧、初期化/実行/割込みの手順、native stream、試験設計、受入）。

決定: 対象はDell Latitude 5330のAlder Lake-P（8086:46a8）、execlists/ELSQ（GuC不使用）、表示は対象外（IGD UPTは画面出力なし）、LLC coherent前提、Linux i915のMIT定義/テーブルを出典付き`.inc`へ分離転記し論理はzedBSD規約で新規実装、HAL/UAPI不変、GEM backingは物理連続16 MiBまで。p001–p005はhost fixtureとamd64 buildで固定し、p006でVFIO passthroughのtest loop（GDM停止→i915 unbind→vfio-pci→QEMU→復旧）、p007で実機のcopy/fill/store/jobとhostのpmemsave照合、静的レビュー、規約全文確認。

Linux参照10ファイルのMIT表記を確認済み（p001で固定tagの全ファイルを機械監査）。host事実: IOMMU group 0単独、vfio-pci module、CONFIG_VFIO_PCI_IGD=y、QEMU 10.0.11、sudo -n可。見積1440 active minutes、120分ごとにレビュー、同条件retry 3回、VM 300秒/build・転送1200秒/host attach・restore各120秒。host reboot・package導入・cmdline変更は許可外。git add/commit/pushはユーザー担当。

## q314開始: WS029 i915ネイティブGPU driver（2026-09-14）

ユーザーの「では、実装してください。」により q314 を開始し、[WS029 p001](https://github.com/awemorris/zedBSD/issues/398) を q314-i01 として実行する。提示済みの計画（plan/ws029/i915-design.md、p001–p007）に対する開始指示を、p001 の移植方針（MIT 定義/テーブルの分離転記＋論理の新規実装）と p006 の host 操作（GDM 停止、i915 unbind、VFIO passthrough）の承認として journal に記録する。firmware が必要になった場合は userland/firmware/<機種>/ に置く（現計画では GuC/HuC 不使用で firmware なし）。順序は p001 → p002 → … → p007。見積 1440 active minutes、120 分ごとに点検、同条件 retry 3 回。HAL/UAPI 不変、host reboot・package 導入・cmdline 変更は許可外、git add/commit/push はユーザー担当。開始時点で新実装・試験の成功は主張しない。

## q314 p001 完了: 対象確定・ライセンス境界・移植方針の固定（2026-09-14）

[WS029 p001](https://github.com/awemorris/zedBSD/issues/398)（q314-i01）を cleared にし、[p002](https://github.com/awemorris/zedBSD/issues/399)（q314-i02、driver 骨格）を in-progress にする。source 変更なし、host 状態変更なし。

成果物（すべて local）:
- `plan/ws029/phase001/approval.json`: 開始指示「では、実装してください。」を、MIT 定義/テーブルの分離転記＋論理新規実装、GDM 停止・i915 unbind・VFIO passthrough の承認として記録（設計資料 SHA256 付き）。firmware は必要時に `userland/firmware/<機種>/`。
- `plan/ws029/i915-license-audit.md`（`plan/ws029/tests/fetch-linux-refs.sh v6.19`）: 参照 29 ファイル（`drivers/gpu/drm/i915/` 26、`include/drm/intel/pciids.h`、`include/drm/intel/i915_drm.h`、`include/uapi/drm/i915_drm.h`）を tag v6.19 で取得し SHA256 を記録。判定は全ファイル MIT（SPDX MIT または MIT/X11 permission notice）。GPL のファイルは参照していない。script は MIT 以外があれば非 0 で終了する。
- `plan/ws029/phase001/host-facts.json`（`plan/ws029/tests/host-i915-facts.sh`、読取専用）: Latitude 5330、kernel 6.19.13+deb13-amd64、`00:02.0` = `8086:46a8` rev 0c、i915 bound、iommu group 0 単独（全 17 group）、`CONFIG_VFIO_PCI_IGD=y`、vfio 系 module 解決可、`/dev/vfio` は `vfio` のみ、GDM active、QEMU 10.0.11 に `vfio-pci` あり、RMRR `0x6c000000–0x707fffff`。設計資料 §1 と差異なし。
- `plan/ws029/i915-symbols.md`（`plan/ws029/tests/gen-symbols.py v6.19` が生成）: p002–p005 が転記・参照する 257 symbol を 10 群（補助 macro、device ID/PCI、forcewake、reset、engine register、割込み、command、LRC、GTT、MOCS）に分け、出典 file:line と gen12/ADL-P での有効条件を列挙。値は書いていない。未検出 0。論理の参照元関数（ggtt/ppgtt/uncore/irq/engine/lrc/execlists/reset/emission/mocs）を「転記せず挙動を新規実装」として別表に列挙。
- `plan/ws029/i915-vfio-plan.md`: IGD UPT（画面出力なし）、iommu group 0 単独、RMRR relaxable、`vfio-pci` module、QEMU `-device vfio-pci,host=0000:00:02.0`、`sudo -n` 起動の理由（memlock）、`host-igd.sh attach/restore/status` の手順、許可済み操作（GDM 停止、i915 unbind、VFIO passthrough、restore）と許可外操作（reboot、package 導入、cmdline/modprobe.d/udev/limits 変更、BIOS、他プロセス kill）、既知 risk の扱いを記載。
- `plan/ws029/phase001/host-facts-after.json` と `host-state-diff.txt`: p001 の前後で driver/GDM/`/dev/vfio`/drm node/cmdline が同一であることを示す。

判明した事項:
- `SNB_GMCH_CTRL`/`BDW_GMCH_GGMS_*` は v6.19 では `include/drm/intel/i915_drm.h`（MIT）にあり、`intel_pci_config.h` にはない。`I915_MOCS_PTE` は `include/uapi/drm/i915_drm.h`（MIT）の enum。両ファイルを参照一覧と監査に追加した。
- gen12 の CSB 判定は `GEN12_CSB_SW_CTX_ID_MASK`/`GEN12_IDLE_CTX_ID`/`GEN12_CTX_STATUS_SWITCHED_TO_NEW_QUEUE` を使い、`GEN8_CTX_STATUS_*` は使わない。CSB entry が `-1` のままの場合は `GEN8_EXECLISTS_STATUS_BUF`/`GEN11_EXECLISTS_STATUS_BUF2` の mmio mirror から読む（tgl HSDES 22011327657 相当）。
- LRC image の per-context batch pointer は gen12 では offsets 表の index 0x12（`lrc_ring_wa_bb_per_ctx`）で、`CTX_BB_PER_CTX_PTR` という define は存在しない。p004 では 0 を書く。

検証: 監査 script exit 0、generator 未検出 0、host 前後 diff すべて same。次: p002（`src/drivers/gpu/i915/` 骨格、PCI attach、MMIO/forcewake、GGTT、割込み）。

## q314 p002 完了: driver 骨格（PCI attach、MMIO/forcewake、GGTT、割込み）（2026-09-14）

[WS029 p002](https://github.com/awemorris/zedBSD/issues/399)（q314-i02）を cleared にし、[p003](https://github.com/awemorris/zedBSD/issues/400)（q314-i03、メモリ）を in-progress にする。実機は未使用。HAL/UAPI 変更なし。git add/commit/push はユーザー担当。

新規 source（すべて local、Zlib）:
- `include/drivers/i915.h`: `drv_i915_pci_driver_register()` のみ公開。
- `src/drivers/gpu/i915/internal.h`: `struct i915_device`/`i915_ggtt`/`i915_session`、定数（engine slot、forcewake domain、timeout、予約 GGTT page、IRQ bank）、cross-file prototype。
- `src/drivers/gpu/i915/i915.c`: ID 表（ADL-P/ADL-N/RPL-U/RPL-P、`8086:46a8` を含む 54 ID）、attach → start（stage: dma-provider、save-pci-state、enable-pci-memory、bar0、map-registers、uncore、gt-reset、ggtt-probe/scratch/bitmap/fill、enable-bus-master、irq、gpu-publication）→ publish（`drv_gpu_ops` v9、capabilities=0、open/close/get_info のみ）、stop/detach の逆順解放。失敗時 `i915: attach stopped at <stage>: <errno>`。
- `src/drivers/gpu/i915/uncore.c`: `drv_i915_read32/write32`（BAR0 窓の範囲検査）、`drv_i915_wait32`（`sched_ticks` 上限）、`drv_i915_forcewake_get/put`（GT/RENDER の 2 domain、masked write、ACK poll 50 ms、参照計数）、`drv_i915_uncore_init`（全 domain 解放、GDRST 進行中なら待つ）、`drv_i915_gt_reset`（GRDOM_FULL、1 s）。
- `src/drivers/gpu/i915/ggtt.c`: `drv_i915_ggtt_start`（GMCH 0x50 の GGMS から entry 数、BAR0 上半分を map、scratch page、bitmap、全 PTE を scratch で埋めて flush）、`alloc/free`（first-fit、先頭 1 MiB 予約）、`insert/clear`（PTE=phys|PRESENT、`GFX_FLSH_CNTL`）、`stop`。
- `src/drivers/gpu/i915/irq.c`: `drv_i915_irq_start/stop/reset`（MSI 1 本、RENDER_COPY enable に user/CS error/context switch/semaphore、RCS0/BCS0 mask、他 class は disable/mask、master enable）、`drv_i915_irq_handler`（master disable→bank→selector→identity valid 待ち→class/instance/intr→counter→ack→master enable）。今は counter（user/context switch/error/unknown/identity timeout）だけを更新し、p004 で request 処理へ接続する。
- `src/drivers/gpu/i915/linux/i915-regs.inc`（163 定義）、`linux/i915-ids.inc`（4 表）: `plan/ws029/tests/gen-inc.py v6.19` が Linux v6.19 の MIT ファイルから `#define` だけを機械転記（`_MMIO` 除去、`REG_BIT`→`I915_INC_BIT` 等、U suffix）。header に各出典の copyright 行、MIT permission notice、出典 path と SHA-256、変換規則を記載。generator は出典 SHA-256 が `i915-license-audit.md` の値と一致し判定が MIT であることを検査し、転記本文が未転記 symbol を参照していれば失敗する。
- build: `Makefile`（`CONFIG_DRIVER_PCI_I915`/`_SELFTEST` 既定 n、-D、`KERN_GPU_BACKENDS` に追加）、`platform/amd64/vmunix.mk`（`AMD64_I915_SOURCES`）、`src/kern/platform/pcat.c`（登録）、`config/drivers/pci.drivers`、`config/kernel-options.list`（selftest bool）、`plan/ws029/tests/config-i915-amd64.mk`。

検証（agent-1）:
- `make BUILD=build/i915-amd64 ZEDBSD_CONFIG=plan/ws029/tests/config-i915-amd64.mk vmunix`: PASS（warning 0、`amd64 vmunix check: PASS`、`drv_i915_*` 14 symbol link）。
- 同 config で I915 := n: PASS、`drv_i915_`/`drv_gpu_register` symbol 0。
- `sh plan/ws029/tests/run-i915-host-tests.sh`（uncore/gtt/irq、通常＋ASan/UBSan）: PASS。forcewake 参照計数と timeout 時の巻き戻し、GDRST self-clear/stuck、範囲外 MMIO 拒否、GGTT 1M entry の scratch fill・first-fit・insert/clear/free・不正引数、IRQ enable/mask 値、bank/identity decode（RCS0/BCS0/未知 class）、CS error の EIR 記録、identity timeout。
- `python3 plan/ws029/tests/run-i915-build-selection-test.py`: 6 platform × Venus/i915 各 y/n で PASS（GPU core はどちらかの backend が y のときだけ、i915 object は amd64 かつ y のときだけ）。
- `git diff --check`: PASS。規約 checklist（forward declaration、purpose comment、`Succeeded:` return、条件分割、for 初期化子なし）を自己確認。

制限: 実機未接続のため hardware 動作は未証明。engine/LRC/submission は p004。`GPU_CAP_*` は 0 のため `/dev/gpu0` は open/get_info しかできない。

## q314 p003 完了: メモリ（GEM object、48-bit PPGTT、CPU view）（2026-09-14）

[WS029 p003](https://github.com/awemorris/zedBSD/issues/400)（q314-i03）を cleared にし、[p004](https://github.com/awemorris/zedBSD/issues/401)（q314-i04、実行）を in-progress にする。実機は未使用。HAL/UAPI 変更なし。git add/commit/push はユーザー担当。

新規・変更 source（すべて local、Zlib）:
- `src/drivers/gpu/i915/ppgtt.c`: `drv_i915_ppgtt_create`（scratch page → scratch PT/PD/PDP の連鎖、PML4 は scratch PDP で充填）、`destroy`、`va_alloc`（bump、開始 0x1_0000_0000、2 MiB 揃え、再利用なし）、`insert`（4-level walk、欠けた table を 4 KiB page で確保し下位 scratch entry で充填）、`clear`（leaf を scratch に戻す）、`lookup`（試験用）。encode: page/table = `GEN8_PAGE_PRESENT|GEN8_PAGE_RW`（PAT index 0）、scratch table は Linux の `gen8_pde_encode(I915_CACHE_NONE)` と同じ `PAT0|PAT1`。
- `src/drivers/gpu/i915/gem.c`: `drv_i915_gem_create`（`kern_pmem_alloc_limited` 4 KiB 揃え・39-bit 上限・zero fill、device list に登録）、`destroy`（bound/quarantined なら保持）、`bind/unbind_ggtt`、`bind/unbind_vm`、`read/write`（範囲再検査、barrier）。
- `src/drivers/gpu/i915/internal.h`: `struct i915_ppgtt`/`i915_ppgtt_page`/`i915_gem_object`、session は `vm` を別 allocation で保持（quarantine 時に close で device の `quarantined_vms` に移し、reset/stop で解放）、device の object list と counter。
- `src/drivers/gpu/i915/i915.c`: capabilities = `GPU_CAP_RESOURCE|GPU_CAP_TRANSFER`、`open`（session 番号、PPGTT 作成）、`close`（PPGTT 破棄または quarantine 保持）、`resource_create`（`GPU_RESOURCE_USAGE_STORAGE` のみ、1..16 MiB、VM へ bind、log `i915: resource session=%u slot=%u bytes=%llu phys=0x%llx va=0x%llx`）、`resource_destroy`（quarantine 時は保持）、`resource_read/write`（mutex 下で copy）、`i915_stop` が残存 object と quarantined VM を解放。
- build: `platform/amd64/vmunix.mk` に `ppgtt.c`/`gem.c`。監査に `gt/intel_gtt.c`（MIT）を追加（29→30 ファイル）。
- fixture: `plan/ws029/tests/i915-gtt-test.c` に PPGTT 3 試験（scratch 連鎖と encode 値、index bit ごとの walk と table 確保数、VA allocator）、`i915-backend-test.c`（新規: 登録→attach→publish→open/get_info→create/write/read/destroy→close→unpublish→detach、6000 B が 2 page に丸められ VA 0x1_0000_0000、16 MiB 上限、EINVAL/ENOMEM 巻戻し、quarantine 保持と detach での回収、lease 全解放）、`i915-fixture.inc` に mutex/PCI attach/drv_gpu 登録の stub と 32 MiB の偽 page pool。

検証（agent-1）:
- `make BUILD=build/i915-amd64 ZEDBSD_CONFIG=plan/ws029/tests/config-i915-amd64.mk vmunix`: PASS（warning 0、`amd64 vmunix check: PASS`、`drv_i915_*` 28 symbol）。I915 := n: PASS、symbol 0。
- `sh plan/ws029/tests/run-i915-host-tests.sh`（uncore/gtt/irq/backend、通常＋ASan/UBSan）: PASS。
- `python3 plan/ws029/tests/run-i915-build-selection-test.py`: 6 platform × Venus/i915 各 y/n PASS（i915 object 6 個は amd64 かつ y のときだけ）。
- `git diff --check`: PASS。

判明した事項: quarantined session の close で page table が漏れる設計穴を fixture が検出し、VM を別 allocation にして device 側 list へ移す形に直した（close は失敗できない契約のため close 時に allocation しない）。

制限: 実機未接続。`GPU_CAP_COMMAND`/JOB は p004–p005。resource 上限は 1 object 16 MiB、物理連続。

## q314 p004 完了: 実行（engine/LRC/execlists、request/seqno、engine reset、selftest）（2026-09-14）

[WS029 p004](https://github.com/awemorris/zedBSD/issues/401)（q314-i04）を cleared にし、[p005](https://github.com/awemorris/zedBSD/issues/402)（q314-i05、drv_gpu 統合）を in-progress にする。実機は未使用（selftest の実行は p006）。HAL/UAPI 変更なし。git add/commit/push はユーザー担当。

新規・変更 source（すべて local、Zlib。転記 `.inc` は MIT 表示付き）:
- `linux/i915-commands.inc`（63 定義: MI_*/XY_*/PIPE_CONTROL）、`linux/i915-lrc-offsets.inc`（`gen12_xcs_offsets`/`gen12_rcs_offsets` と NOP/LRI/REG/REG16/END の encode macro を `I915_LRC_*` に改名して verbatim 転記）、`linux/i915-mocs.inc`（LE_/L3_/L4_ macro、`MOCS_ENTRY`、`GEN11_MOCS_ENTRIES`、`gen12_mocs_table`）。generator `plan/ws029/tests/gen-inc.py` に verbatim block 転記と `BUILD_BUG_ON_ZERO` 除去を追加。`i915-regs.inc` に `GEN11_GRDOM_RENDER`、`GEN9_LNCFCMOCS`、`BLIT_CCTL_*_MOCS_MASK`、`GEN12_GFX_PREFETCH_DISABLE` を追加（168 定義）。
- `engine.c`: `drv_i915_engines_start`（forcewake GT+RENDER を device 寿命で保持、global MOCS 64 entry と LNCFCMOCS 32 pair を書込み、RCS0/BCS0 の HWSP object と kernel context を作成し `i915_engine_program`: HWSTAM、`GEN11_GFX_DISABLE_LEGACY_MODE`、STOP_RING 解除、HWS_PGA、EMR/EIR/ESR、BCS の `BLIT_CCTL` を uncached MOCS index 3、CSB pointer reset）、`drv_i915_engine_reset`（STOP_RING+PREFETCH_DISABLE→MODE_IDLE 待ち→`RESET_CTL` request/ready→`GDRST` engine domain（2 回書き）→cancel→再 program）、`drv_i915_engine_interrupt`（irq_lock 内で CSB 消費→seqno retire→次 request 投入、lock 外で完了 callback）、`drv_i915_engine_idle`。
- `lrc.c`: `drv_i915_lrc_create`（RCS 14 page / BCS 2 page の image を GGTT に bind、context ごとに 64 KiB ring、offset 列を `MI_LOAD_REGISTER_IMM|LRM_CS_MMIO(|FORCE_POSTED)` に展開、CONTEXT_CONTROL の inhibit、PDP0=PML4、MI_MODE pair の STOP_RING 解除、RING_START/HEAD/TAIL/CTL、末尾 `MI_BATCH_BUFFER_END|1`、descriptor low=64B addressing|VALID|PRIVILEGE|GGTT、high=sw_id<<5|class<<29|instance<<16）、`submit`（image tail 更新→ELSQ port1=0/port0=desc|FORCE_RESTORE→`EL_CTRL_LOAD`）、`reset_csb`、`csb_consume`（HWSP write pointer、entry -1 なら mmio mirror、gen12 parse: away 無効または new queue で promotion、それ以外 completion）、`ring_space`/`ring_emit`（末尾 NOOP 詰めで wrap）。
- `request.c`: slot 32、FIFO queue、`kick`（engine idle 時のみ emit+submit: preparser disable→TLB invalidate flush(BCS: MI_FLUSH_DW、RCS: PIPE_CONTROL)→extra dwords→`MI_BATCH_BUFFER_START_GEN8|NON_SECURE`(48-bit PPGTT)→breadcrumb（BCS: flush + `MI_FLUSH_DW` post-sync store to HWSP seqno via GGTT、RCS: PIPE_CONTROL flush + QW_WRITE）→`MI_USER_INTERRUPT`→ARB enable→ARB_CHECK/NOOP）、`retire`（HWSP seqno 一致）、`fail`（queue/active を error で回収）、`complete_list`（`drv_gpu_complete` を lock 外で呼び slot 解放、`drv_gpu_capacity_changed`）。
- `selftest.c`（`CONFIG_DRIVER_PCI_I915_SELFTEST=y` のみ link）: kernel context で BCS0 に `MI_STORE_DWORD_IMM|USE_GGTT`（HWSP scratch dword に 0xdeadbeef）を含む request を投入し 100 ms 以内に値・seqno・user interrupt 増加を確認、`i915: selftest bcs0 store=%s irq=%u seqno=%u/%u`。失敗は attach 失敗。
- `irq.c` が engine へ転送、`i915.c` は attach で engines start（+selftest）、open で engine ごとの context 作成、close で破棄（quarantine 時は保持）、stop で GT reset→engines stop→object 回収。`uncore.c` に `drv_i915_domain_reset`。`platform/amd64/vmunix.mk` に engine/lrc/request と条件付き selftest、`plan/ws029/tests/config-i915-selftest-amd64.mk`。
- fixture: `i915-lrc-test.c`（新規: image layout が Linux の CTX_* index と一致、descriptor、ELSQ 書込み、CSB 判定と mirror fallback、ring wrap/space）、`i915-fixture.inc` に execlists emulator（ELSQ load を記録し `fixture_run_engines()` が image→ring を parse: MI_STORE_DWORD_IMM(GGTT/PPGTT)、MI_FLUSH_DW store、PIPE_CONTROL QW write、MI_BATCH_BUFFER_START を PPGTT 経由で追跡、MI_SEMAPHORE_WAIT で hang、CSB event 2 件と割込み identity を作り handler を呼ぶ; MI_MODE/RESET_CTL/GDRST の応答）、`i915-backend-test.c` に engine 初期化 register 値、selftest 成功、batch による resource 書込みと完了 callback、hang→`request_fail(EIO)`→engine reset→再実行を追加、`i915-irq-test.c` に engine 転送の確認。

検証（agent-1）:
- 3 構成 build PASS（i915、i915+selftest、GPU なし: `drv_i915_` symbol 0）、warning 0、`amd64 vmunix check: PASS`。
- `sh plan/ws029/tests/run-i915-host-tests.sh`（uncore/gtt/irq/lrc/backend、通常＋ASan/UBSan）: PASS。
- `python3 plan/ws029/tests/run-i915-build-selection-test.py`: PASS。`git diff --check`: PASS。

設計との差分: ring は engine 共有ではなく context ごと（execlists は context save で RING_HEAD を image に書き戻すため、共有 ring では head/tail が食い違う。Linux と同じ構成）。forcewake は attach 後に恒久保持（IRQ 文脈からの ELSQ 書込みで ACK 待ちを避けるため）。`CTX_R_PWR_CLK_STATE` は 0（RCS の 3D 利用は WS029 後続）。

制限: 実機での CSB/割込み挙動は未証明（fixture の model は Linux の parse 規則に基づく）。1 engine 1 request 直列。`GPU_CAP_COMMAND`/JOB/recovery ops は p005。

## q314 p005 完了: drv_gpu 統合、native stream、生 UAPI 試験クライアント（2026-09-14）

[WS029 p005](https://github.com/awemorris/zedBSD/issues/402)（q314-i05）を cleared にし、[p006](https://github.com/awemorris/zedBSD/issues/403)（q314-i06、VFIO passthrough テストループ）を in-progress にする。実機は未使用。HAL/UAPI 変更なし。git add/commit/push はユーザー担当。

新規・変更（すべて local）:
- `plan/ws029/i915-native-stream.md`: native stream の確定版（header 32 byte、relocation 16 byte、engine/timeline の対応、job の意味、試験 batch、harness 向け handle 対応付け）。
- `src/drivers/gpu/i915/i915.c`: `drv_i915_stream_parse`（magic/version/engine/count/dwords/flags/reserved/bytes 一致/末尾 `MI_BATCH_BUFFER_END`/relocation 範囲を検査）、`i915_submit_stream`（session の batch pool（最大 32、空き object を再利用）へ copy、relocation を handle→session object の VA で patch、request を投入）、`i915_submit_marker`、`i915_command`（同期受理）、`i915_command_submit`（bytes 0 は marker、timeline 0/1=BCS0、2=RCS0）、`i915_command_drain`（`retire_waitq` で pending 0 まで待つ）、jobs（`reserve`: slot 確保＋callback 保持、`commit`: marker 投入、`cancel(0)`: slot 解放、`cancel(fault)`: RETAINED で保持、`capacity`: 空き slot 数）、recovery（`stop_begin`/`stop_poll`(pending で EAGAIN)/`isolate`(session の request を EIO で回収し、active なら engine reset して他 session を継続)/`fault`(全 request 回収、`failed=1`)/`reset_device`(GT reset、engine 再 program、quarantined object/VM 解放)）。capabilities = RESOURCE|TRANSFER|COMMAND|NOTIFICATION|JOB|JOB_CAPACITY。resource log に `handle=` を追加。
- `request.c`: RESERVED/RETAINED slot も `request_fail` で回収、retire 時に batch を pool へ戻し `retire_waitq` を起こす。`engine.c`: `drv_i915_engine_recover`（irq_lock 外で engine reset、`resetting` 中は handler と kick が待つ）。`internal.h`: stream 定数、session の object/batch list、`retire_waitq`。
- `userland/base/tests/gpu-i915/{main.c,Makefile}`（`/bin/gpu-i915-test`、libvulkan 非依存）: `GPUI915 START` → GET_INFO（driver_name/capabilities）→ 64 KiB resource ×2 → pattern write → copy（`XY_SRC_COPY_BLT`）→ 照合 → fill（`XY_COLOR_BLT`）→ 照合 → store（`MI_STORE_DWORD_IMM`）→ 照合 → job（RESERVE/COMMIT/WAIT）→ `GPUI915 PASS copy=1 fill=1 store=1 job=1 src_handle=<h> dst_handle=<h>`、失敗は `GPUI915 FAIL stage=<s> errno=<e>`。両 config の `ZEDBSD_USER_PROGRAMS` に追加。
- fixture: `i915-stream-test.c`（新規、正常 3 形と不正 13 形）、`i915-fixture.inc` に waitq stub と `XY_SRC_COPY_BLT`/`XY_COLOR_BLT` の emulation、`i915-backend-test.c` に ops 経由の stream（relocation patch、copy/fill の結果照合、不正 handle/長さ拒否）、marker、drain、jobs（capacity 32→31、rollback、commit 完了、fault cancel の保持）、stop_begin/poll、isolate（hang した session を engine reset で切り離し peer の request が継続）、fault→open ENODEV→isolate→close→reset_device→再 open 成功。

検証（agent-1）:
- `sh plan/ws029/tests/run-i915-host-tests.sh`（uncore/gtt/irq/lrc/stream/backend、通常＋ASan/UBSan）: PASS。
- kernel build 3 構成 PASS（warning 0）、`make ... disk-image`（i915 config）PASS、`build/i915-amd64/rootfs/bin/gpu-i915-test` を確認。
- `run-i915-build-selection-test.py` PASS、`git diff --check` PASS。

制限: 実機での blit/interrupt は未証明（p006/p007）。RCS0 の 3D state は対象外（`MI_STORE_DWORD_IMM`/`PIPE_CONTROL` 経路のみ）。

## p006 実機結果（VFIO passthrough テストループ、2026-09-14）

Latitude 5330（`awe@10.0.10.25`）の IGD（`0000:00:02.0`、`8086:46a8` rev 0c）を `host-igd.sh` で vfio-pci に切替え、i915 selftest 付き image を QEMU で起動して attach → selftest → `/dev/gpu0` 公開まで到達した。各 attempt の後に i915 と GDM を復旧した。

### 復旧 rehearsal（QEMU なし）

`plan/ws029/phase006/host-rehearsal.json`。`attach` で driver=vfio-pci、`/dev/vfio/0` 出現、GDM inactive。`restore` 後 driver=i915、GDM active、`driver`/`gdm`/`dev_vfio`/`drm_nodes` が開始前と一致（`restored=true`）。復旧手順が成立することを確認。

### attempt 履歴（同条件の修正と再実行）

| attempt | mode | 結果 | 原因・修正 |
| --- | --- | --- | --- |
| boot-001 | boot-only | fail | harness の import 依存 `venus_rfb.py` を転送していなかった → 転送一覧に追加 |
| boot-002 | boot-only | fail | UEFI loader が GOP framebuffer 無しで `Locate GOP` 停止 → QEMU 引数を `-vga none` から `-vga std`（表示 backend なし）に変更 |
| boot-003 | boot-only | fail(attach) | i915 が `map-registers` で EINVAL → BAR0 を `drv_pci_device_claim_bar` してから map するよう修正（PCI は claim した BAR しか map させない） |
| boot-005 | boot-only | 進捗 | BAR0 は正しく map（regs va=0xffffffffe0000000, bus=0x380010000000）、GGTT/MSI まで到達、engine bring-up で停止 → 段階 log 追加 |
| boot-006 | boot-only | 進捗 | 両 engine init と `engines started` まで到達、selftest で停止 → selftest に log 追加 |
| **boot-007** | boot-only | **pass** | `i915: selftest bcs0 store=ok irq=1 seqno=1/1`、`registered native GPU node`。実機の BCS0 が store を実行し user interrupt を上げた |

### 受入

- attach 全段階（PCI enable、BAR0 claim/map、forcewake、GT reset、GGTT、MSI、engine×2、selftest、publish）を通過。
- `boot-007` boot-pass: guest.log に `attach stopped` なし、`selftest bcs0 store=ok`、`registered`。
- host 復旧: attempt 後 driver=i915、GDM active（`host_restored=true`）。attach 中は driver=vfio-pci、`/dev/vfio/0`、GDM inactive。
- 証拠: `plan/ws029/temp/remote/q314-i915-boot-007/`（result.json、guest.log、qemu.log、qmp.jsonl、console）、`plan/ws029/phase006/host-rehearsal.json`。

実機で GPU が実際に命令を実行した最初の到達点。copy/fill/store/job と host 側 RAM 照合は p007。

## q314完了: WS029 i915ネイティブGPU driver（2026-09-14）

q314 finished、ws029-p001..p007 全 cleared。実機（Latitude 5330、IGD 8086:46a8、VFIO passthrough）で attach → selftest（BCS0 が store を実行し user interrupt）→ /dev/gpu0 公開まで到達し、userland /bin/gpu-i915-test が copy/fill/store/job を実行、host が QMP pmemsave で guest RAM を独立照合して PASS（test-002）。Linux i915（MIT）の定義/テーブルは出典付き .inc へ転記、driver 論理は zedBSD 規約で新規実装。HAL/UAPI 不変。静的解析 gcc -fanalyzer / clang --analyze 0 件、規約 §14 確認、host fixture・GPU core 回帰・build 3 構成 PASS。制限: cold VFIO attach の bring-up 間欠ハング（最優先の後続）、hang 注入の実機 peer 継続未達、display/scanout は対象外。後続は WS029 registry の planning 行に列挙。source/doc の git add/commit/push はユーザー担当。