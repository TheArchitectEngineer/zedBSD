<!-- awesome-plan project=zedbsd record=ws131-p009 -->

# ws131-p009: backend の GPU の buffer の領域と境界の確定

Status: in-progress（q659-i01、P2 generation7。実装・host 試験・build・checker 済み、QEMU は T2 の結果待ち）
Disposition: normal
Parent: [WS131](../ws.md)、計画の正本 [design.md](../design.md)
Queue: q659 / q659-i01（Q1 2026-10-04: ユーザーの夜の指示「優先度の高い実装を進め、止まれば次の WS へ」の Q1 の解釈、所有 path の委任あり）
依存: p008 cleared。判断 D14（決定済み）
目安: 4〜5h（1 Queue）。実行者: Q1 が割り当てる（high）
所有 path: `userland/desktop/wayland/`（`zedbsd/gpu-*`、`dmabuf/`、`linux/sync-linux.c`・`freebsd/sync-freebsd.c`、`zwl-gpu.h`、`protocol.c`・`objects.c`・`import.c`・`compose.c` の結線、OS の data の `data/` への移動）、`userland/desktop/libkeiland-backend*/`、`plan/ws131/`。（Q1 の委任が要る: Q1 が Phase の前に委任を記録する）: `plan/tools/gpu-boundary/`・`plan/tools/keiland-os-boundary/`・`plan/tools/keiland-freebsd/dmabuf-export-rejected.c`

## 目的と結果

GPU の buffer の protocol（zedBSD の `keiland_gpu_buffer_v1`、Linux・FreeBSD の `zwp_linux_dmabuf_v1`）を backend へ移し（2026-10-03 user の D2）、compositor は wl_buffer の寿命と OS に依らない画像の型だけを持つ。逆向きの依存は compositor が渡す protocol の host の interface で解く（design.md §3.5）。終わると compositor の tree に OS の directory が無く、配置の Guardrail と checker を確定する。

## 範囲

1. 最初の 30 分で `protocol.c:188`・`:632`・`:1560`、`objects.c:571`、`import.c` の import の呼び出しを読み直し、design.md §3.5 の protocol の host の 11 操作に足りない物を足して phase.md に書く。
2. compositor: `struct kl_backend_protocol_host` の実装（wire の object を不透明な resource として渡す）、global の表の 3 番を backend に問う形に。
3. `git mv`: `zedbsd/gpu-buffer-zedbsd.c`・`gpu-zedbsd.*` → backend-zedbsd、`dmabuf/*` → `libkeiland-backend/dmabuf/`（Linux・FreeBSD の system の Vulkan の header で compile）、`linux/sync-linux.c`・`freebsd/sync-freebsd.c` → 各 backend。`zwl_buffer_layout` は backend-zedbsd の中だけ。
4. OS ごとの install の data（`linux/apps.conf.in`・`keiland.desktop`・`freebsd/apps.conf.in`）を `wayland/data/` へ。
5. X server（D14、2026-10-03 user）: `userland/desktop/xserver/keymap.c` の `<uapi/input.h>` の include を除き、Wayland の規格（evdev）の key code の定数を X server の自分の header に持たせる（値は OS に依らない）。所有 path に `userland/desktop/xserver/keymap.c` と新しい header を加える（Q1 の委任が要る）。
6. checker の確定（design.md §3.8、D14 の許可の表）と `v1-check.sh` の改訂。故意の違反で FAIL・exit 1、戻して PASS を確かめる。配置の Guardrail の本文を Q1 に渡す（§3.7）。

## 受け入れ

- 3 OS の build（zedBSD の amd64 は exit 0・自前の warning 0、`make keiland-linux` の gcc と clang、FreeBSD の native build と `native-build-audit.py`）と `plan/tools/keiland-os-boundary/check.sh` PASS。
- `find userland/desktop/wayland -type d` に `zedbsd`・`linux`・`freebsd`・`dmabuf`・`evdev`・`session` が無い。compositor の 3 つの Makefile の source の一覧が一致。`v1-check.sh` PASS。
- zedBSD: boot-test、C1・C2・C9、GPU の境界の試験（zedbsd-commands.md §6、forge・fence の guest）。Linux: `dmabuf-probe`・`dmabuf-forge`・`wsi-check.sh`（90 frame ×4）・acquire-fence。FreeBSD: `dmabuf-export-rejected.c` の native の実行、起動は D11。

## 検証の方法

[zedbsd-commands.md](../../tools/keiland-linux/zedbsd-commands.md)、[keiland-linux/README.md](../../tools/keiland-linux/README.md)、[keiland-freebsd/README.md](../../tools/keiland-freebsd/README.md)。QEMU の console・serial の log で判定しない（不具合は gdbstub・monitor・QMP）。build は自分の `BUILD=build/ws131-pNNN/`、共有の `build/` を消さない。FreeBSD の起動の確認は判断 D11 の範囲（passthrough なし）。QEMU と実機の証拠を分ける。

## 衝突・危険・rollback

- 衝突: WS113 p004 と同時に流さない。WS103 の道具の path を変える。 順は Q1 が決める（design.md §7.2）。
- rollback: 前へ直すのを基本にし、後ろに同じ file の変更が無い時だけ統合の commit を revert する（design.md §7.4）。

## Resume

依存の Phase の cleared と main への統合、関係する判断の決定の後に、Q1 が Queue を作る。

## 範囲 1 の読み直し（P1 generation12、2026-10-04、読みだけ、main f26a0f4）

compositor から GPU の module（`zedbsd/gpu-buffer-zedbsd.c` 525 行・`gpu-zedbsd.c` 140 行、`dmabuf/gpu-dmabuf.c` 1147 行、`linux/sync-linux.c`・
`freebsd/sync-freebsd.c`）への呼び出しは `protocol.c:188`（request）・`:401-403`（global の名前と版）・`:632`（bind）・`:1560`（commit）、`objects.c:571`
（object の解放）、`compose.c:640`・`:698`・`:710`・`:1039`・`:2015`（extension と frame の fence の型）。module が compositor から使う物を数えると、design.md
§3.5 の host の 11 操作に次が足りない:

| 足りない物 | 今の使い方 | host に足す案 |
| --- | --- | --- |
| object の wire の id | log の `buffer=%u`・`surface=%u`、dmabuf の `created` event の payload（`buffer->id`）、`zwl_emit(…, factory->id, …)` | `uint32_t (*resource_id)(struct kl_backend_resource *)` |
| client の番号 | log の `client=%llu`（試験が grep する `ZWL IMPORT`・`ZWL ACQUIRE_FENCE`・`ZWL VULKAN_IMPORT*`） | `uint64_t (*client_number)(struct kl_backend_resource *)` |
| object の版 | dmabuf の params の opcode 3 は版 2 以上だけ（`gpu-dmabuf.c:178`）、params は factory の版で作る（`:610`） | `uint32_t (*resource_version)(struct kl_backend_resource *)` |
| find の種類の確かめ | `zwl_find` の後に `kind == ZWL_SURFACE`・`kind == ZWL_BUFFER && shm == NULL`（`gpu-buffer-zedbsd.c:258-261`・`:306-309`） | `resource_find(data, any, id, role)` が種類の合わない物と shm の buffer に NULL を返す（role に SURFACE・GPU_BUFFER） |
| alpha だけの変更 | zedBSD の set_alpha の要求は import と別（`:313`）、その後 `server->dirty = 1` | `buffer_register` と別に `void (*buffer_set_alpha)(data, buffer, alpha)`（compositor が dirty にする） |
| fence の列の満杯 | `acquire_count == ZWL_FENCE_MAX` で拒む（zedBSD は要求を拒み、dmabuf は `zwl_error`） | `surface_fence` は満杯で `ENOSPC` を返し fd は呼んだ側が閉じる。generation は引数（zedBSD は要求の値、dmabuf は 1） |
| frame の log の有無 | `server->log_frames` | `int (*log_frames)(void *data)` |
| 上限 | `server->gpu_limits`（max_dimension・memory_type_count） | `struct kl_backend_vulkan` に limits を足すか `(*limits)(data)` |
| 物理 device・instance | dmabuf の format の問い（`gpu_formats(compose)`）、import | `vulkan()` が instance・physical・device・proc addr を返す（案のまま） |

wire の object の型（`ZWL_FACTORY`・`ZWL_GPU_OBJECT`・`ZWL_BUFFER`）の判定（`gpu-dmabuf.c:156`・`:166`）は backend が自分で作った resource に付ける private の
tag で行える（`resource_private`）ので host には足さない。

着手（範囲 2〜6）は Q1 の確認待ち（p008 の未 cleared のまま着手するか、Queue ID、所有 path の委任、WS113 p004 との衝突）。

## 着手（Q1、2026-10-04）

q659（P2）。ユーザーの夜の自律の指示（master の記録）で着手。p008 は cleared（T2-013・T1-054）。所有 path の委任は Q1 が許可。WS113 p004 は planned で動いていない。p007 は touch・pen の demo-s8-s9 だけ未実行で uncleared（T1 に依頼）、p009 の前提の入力の backend は統合済み。


## 実装（q659-i01、P2 generation7、2026-10-04）

- **interface**（新）`libkeiland-backend/keiland-backend-gpu.h`: `struct kl_backend_protocol_host`（design.md §3.5 の 11 操作に P1 の読み直しの 9 個を足した: resource の
  create・create_server・find（role で種類と shm を確かめる）・destroy・role・id・version・client_number・private、emit、post_error（`KL_BACKEND_ERROR_INVALID` で
  generic）、take_fd、buffer_adopt、buffer_set_alpha（次の frame を描き直す）、surface_fence（満杯で ENOSPC）・surface_fence_full、device（instance・physical・device と
  上限）、log_frames）と、backend の出す `kl_backend_gpu_*`（global の名前と版・bind・request・commit・resource_free・instance/device の extension・frame の fence の型）。
  design の案からの違い: `struct kl_backend *backend` の引数は無い（GPU の module は process に一つの状態で backend を使わない）。`void *data` も無く、host の関数は
  resource から compositor を得る（device・log_frames も resource を取る）。
- **compositor**: `wayland/gpu-host.c`（新）が host を実装（resource は `struct zwl_object`）。`protocol.c`（request・global の名前と版・bind・commit）・`objects.c`（解放）・
  `compose.c`（extension・fence の型、`server->gpu_device` に instance・physical・device と上限）を `kl_backend_gpu_*` に。`zwl-gpu.h` と `server->gpu_limits` を除いた。
- **git mv**: `wayland/zedbsd/gpu-buffer-zedbsd.c`・`gpu-zedbsd.{c,h}` → `libkeiland-backend-zedbsd/`、`wayland/dmabuf/{gpu-dmabuf.c,sync.h}` → `libkeiland-backend/dmabuf/`、
  `wayland/linux/sync-linux.c` → `libkeiland-backend-linux/`、`wayland/freebsd/sync-freebsd.c` → `libkeiland-backend-freebsd/`。install の data は
  `wayland/data/{apps-linux.conf.in,keiland.desktop,apps-freebsd.conf.in}`。**compositor の tree に OS の directory は無い**（`find userland/desktop/wayland -type d` は
  `shaders`・`data` だけ）。module は host 越しに書き直し（log の文字列は同じ）、dmabuf は呼び出しの間 file-scope の `gpu_host`、`zwl_dmabuf_export_read` →
  `kl_backend_dmabuf_export_read`（backend の未定義の symbol に `zwl_` が無い）。
- **build の一覧**: zedBSD は `sources.mk` の `KL_BACKEND_ZEDBSD_SOURCES` に GPU の 2 file、Linux・FreeBSD は backend の static library に dmabuf と sync。compositor の
  3 つの Makefile の compositor の source の一覧は一致（新 M1）。
- **X server**（D14）: `xserver/keymap.c` の `<uapi/input.h>` を除き、使う 62 個の evdev の key code を `xserver/keycodes.h`（新）に（値は uapi と同じ、Wayland の keyboard の規格）。
- **checker**（委任あり）: `plan/tools/keiland-os-boundary/check.sh`: common は compositor と libkeiland の全体、C3 は `zwl_buffer_layout`・`gpu_image_descriptor`・
  `uapi/gpu` が compositor・libkeiland・backend の共有と Linux・FreeBSD の tree に無い、L2・L3 の path を backend に、新 L7（compositor に OS の directory が無い）・
  M1（3 つの Makefile の compositor の source の一致）・X1（X server が OS の header を include しない）、B3 は `keiland-backend*.h` に広げた。
  `plan/tools/gpu-boundary/v1-check.sh`: backend を `libkeiland-backend-zedbsd/gpu-zedbsd.c` に、毒入りの header の compile は compositor の source の全体（backend を除く）。
  `run-gpu-zedbsd-host.sh`・`gpu-zedbsd-host.c`・`keiland-freebsd/{backend-test.sh,README.md,dmabuf-export-rejected.c}` の path。
  ついでに `plan/tools/keiland-linux/makefile-sync.sh` の `.c` の正規表現（`apps.conf` を `apps.c` と読んで main でも FAIL していた）に `\b`。

## 検証（host と build）

- build: zedBSD の compositor・X server（`ZEDBSD_CONFIG=plan/tools/settings/config-amd64-settings.mk`）warning 0、`make keiland-linux`（gcc）と `CC=clang` とも warning 0。
- `nm -u libkeiland-backend.a`（Linux）に `zwl_`・`kwl_` 無し。
- `keiland-os-boundary/check.sh` PASS（C1〜C5・L1〜L7・M1・X1・B1・B3・S1）。故意の違反（`wayland/zedbsd/x.c` に `zwl_buffer_layout`）で C3 と L7 が FAIL、戻して PASS。
- `v1-check.sh build/amd64` PASS（52 source を毒入りで compile）。故意の違反（`wayland/os.c` に `#include <uapi/gpu.h>`）で FAIL 2 件、戻して PASS。
- `run-gpu-zedbsd-host.sh` PASS（ordinary・sanitize）、`run-dedicated-host.sh` PASS、`makefile-sync.sh` PASS、`style-check.py` の新しい・変えた file に違反 0。
- 未実施（T2 に依頼）: zedBSD の boot-test、C1・C2・C9（criteria）、forge・fence の guest。Linux の dmabuf-probe・dmabuf-forge・wsi-check・acquire-fence、FreeBSD の native build と
  `dmabuf-export-rejected` の実行（backend-test.sh）。

## Guardrail の本文（Q1 が `plan/guardrail.md` に適用、design.md §3.7）

design.md §3.7 の「配置」の段落をそのまま（X server の key code は `xserver/keycodes.h`、checker は `check.sh` の L7・M1・X1・C3）。「compositor は libvulkan だけ」は
「OS 固有の部分は `libkeiland-backend-zedbsd/` に閉じる（`gpu-zedbsd.c` が kernel の GPU の型を読む唯一の file）。確かめは `plan/tools/gpu-boundary/v1-check.sh`」に。

## Linux・FreeBSD の結果（Q1、2026-10-04、T1-060）

FreeBSD: main 71ffdf3 では include の相対 path の誤り（git mv の後）で build が FAIL、P2 の 098082c で直し、backend-test 9 項目 PASS（build warning 0・install・audit・host-seat・session・power・sync-rejected・dmabuf-export-rejected）。Linux: host の gcc・clang warning 0、wsi-check 4 mode × 90 frame PASS、Debian 13 の QEMU+KVM guest で wltest 600 frame（`ZWL ACQUIRE_FENCE` 601）、dmabuf-forge PASS（out_of_bounds で拒否、compositor 継続、`ZWL EXIT error=0 cleanup_failed=0`）。zedBSD の分は T2-019 待ち。
