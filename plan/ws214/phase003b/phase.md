<!-- awesome-plan project=zedbsd record=ws214-p003b -->

# ws214-p003b: kernel の段（UAPI の承認の後）: COMMIT・OVERLAY_INFO・WAIT を眠る形に、overlay の plane

Parent: [WS214](../ws.md)
Status: in-progress（2026-10-12 P2。実装と host・build 済み、Pi の hvs-load の実測と T1 の回帰の前。clear は Q1 の判定）
Phase disposition: normal
Queue: P2

## 範囲

設計は [p001 第 3 版](../phase001/phase.md) の 4.2（UAPI の差分の案、そのとおりに適用）と 4.3 (2)(4)(5)(6)(7)(9) の p003b の分（7 節の表）。HAL の API・toolchain は変えない。i915・Venus の WAIT と present は変えない。

- UAPI（`include/uapi/gpu-display.h`）: `GPU_DISPLAY_HAS_OVERLAYS`、`GPU_DISPLAY_COMMIT`（'G' 44）・`GPU_DISPLAY_OVERLAY_INFO`（'G' 45）と struct・定数、`struct gpu_display_wait` の文（眠る、番号は controller の 1 本）。大きさを `_Static_assert` で確かめる（plane_state 96、commit 552、overlay_info 56）。
- core（`gpu.c`、`gpu-display.h`）: `commit`・`overlay_info` の op（両方か無し、`commit` は UNGATED を要る）、COMMIT の検査（4.2 の EINVAL の場合: plane の重複、COMMIT_WAIT なしの signal_fd、COMMIT_WAIT は primary を名指す時だけ、未定義の flag、count）、plane ごとの wait の fence は lookup の前、IMAGE の resource は pin、COMMIT_WAIT は pin を放した後に採用を待つ、signal の fence。fence は display の node が `GPU_CAP_FENCE` を持つ時だけ（無ければ wait_fd・signal_fd は EOPNOTSUPP）。
- bcm2711: overlay の plane（K=2）の状態と list の符号（unscaled、src の clip、位置）、plane ごとの serial と DROPPED、SHOWN・IMAGE の扱い、primary の状態が無い間の overlay は EINVAL、帯域の予算（仮 1.5 GB/s）と能力を外れた時の ENOSPC（何も変えない）、clock の見積もりに overlay、lease の始めと終わりで overlay を隠し serial を 0 に、外に見せる番号を `state_serial` に（PRESENT・COMMIT・WAIT）、WAIT は `adopted_serial` まで眠る（timeout_ns、1 秒まで、EINTR、失効で EBUSY）、OVERLAY_INFO、`plane_count` を 1＋K、計数（`pair`、overlay の採用の時刻の輪）。
- scaled の plane（PPF・TPZ・LBM、幅 2048 の制限）: 2026-10-12 ユーザーが WS141 の GPL の参照の扱いを WS214 に広げた（Q1 の連絡、guardrail）ので入れた。
- 終わりに: 値の出典の表（[../phase003/hvs-values.md](../phase003/hvs-values.md)）、GPL との類似の監査、Pi の帯域の実測の道具 `hvs-load`（`userland/tests/hvs-load`、test の image だけ）。実測そのものは Pi で T1・Q1 経由。

## 受け入れ

1. host: overlay の list の符号（位置・clip・順）、予算と ENOSPC、COMMIT の原子性と一部 DROPPED、COMMIT_WAIT と WAIT の眠り・EINTR・失効、番号の意味（後の記録の番号が大きく、WAIT(n) は n 以下が全部採用済みで返る）、UAPI の struct の大きさ、ASan・UBSan。既存の display の回帰（p003a の `display-p003a-host.sh`）。
2. build: rpi4・amd64 の vmunix が warning 0。
3. T1: i915・Venus の回帰（WAIT が変わらない）。Pi（T1・Q1 経由）: `hvs-load` で plane の組み合わせごとの underrun（予算と能力を決める）。3 は Q1 が判定。

## コマンドと上限

- host: `timeout 900 sh plan/ws214/tests/display-p003a-host.sh <out>`（p003b の試験もここに足す）。
- build: p003a と同じ。QEMU は起動しない。

## 設計からの違い（Q1 の判断）

- **cached の overlay の画像を受ける**（2026-10-12 Q1 が案 B を採った）: 設計 4.3 (5) は overlay の画像を uncached だけにしていたが、uncached の領域は Vulkan の memory からしか作れず、UAPI を直に呼ぶ道具 `hvs-load` が書けない。kernel は cached の画像も受け、**新しい画像を名指す commit の時だけ** thread の文脈で clean する。位置だけの commit と IRQ の再発行は clean しない（M6 の懸念は守れる）。libvulkan の画像は uncached なので普段の費用は変わらない。UAPI の `struct gpu_display_plane_state` の文は「uncached でなければならない」から「cached は commit の時に clean する、借りている間は書かない」に緩めた（受ける側への緩め、互換）。
- **libvulkan は p004 まで overlay を列挙しない**（2026-10-12 Q1）: query が plane_count＝3・HAS_OVERLAYS を返すようになったので、今の libvulkan（`wsi-display.c`）が overlay を普通の plane として列挙しないように、HAS_OVERLAYS の出力は primary だけを数える 7 行を足した（91b119330）。p004 で overlay の列挙に置き換える。束で kernel だけを入れ替える時は libvulkan も入れる（Q1、place の注記）。
- adoption_wait の形を `(opaque, session, lease, *sequence, *adopted)` に変えた（PRESENT と COMMIT_WAIT の共通）。

## 結果（2026-10-12 P2）

commit: a5a42e08a（UAPI・core・unscaled の overlay）、91b119330（libvulkan の 7 行、main に merge 済み）、15f37cd9a（scaled）、続く commit（cached の受け入れ、hvs-load、記録）。

### 実装

- UAPI: 4.2 のとおり（`GPU_DISPLAY_HAS_OVERLAYS` 2048、`GPU_DISPLAY_COMMIT` 'G' 44、`GPU_DISPLAY_OVERLAY_INFO` 'G' 45、定数、WAIT の文）。大きさは `gpu.c` の `_Static_assert` と ILP32・LP64 の compile で 96・552・56。
- core: `commit`・`overlay_info` の op（組、commit は UNGATED を要る）。`gpu_commit_ioctl`: 検査（framing、lease・generation、出力の欄が 0、flag、count 1〜5、plane の重複、serial 0、wait・signal の fence の形、COMMIT_WAIT は primary を名指す時、signal_fd は COMMIT_WAIT の時、IMAGE の形と src が image の中、未使用の欄）→ fence（`GPU_CAP_FENCE` の無い node では EOPNOTSUPP）を lookup の前に待つ → admission の検査 → IMAGE の resource を pin（BLOB・SCANOUT だけ、行が storage の中）→ backend の記録 → unpin → COMMIT_WAIT なら `adoption_wait` → signal → copyout。OVERLAY_INFO は ungated、overlay だけ、出力の欄は 0。
- bcm2711: overlay 2 枚（plane 1・2、primary の上、cursor の下）。`bcm2711_display_flip_commit` は guard の下で view を作って検査し（plane の serial 以下は DROPPED、IMAGE は SHOWN の時だけ、primary は出力の全体・unscaled・不透明、overlay は primary の上だけ、image と src・dst の範囲、拡大縮小の限界）、guard の外で予算（1.5 GB/s 仮）・clock の上限の ENOSPC、clock を上げ、新しい cached の画像を clean し、guard の下で view が古くない（`planes_generation`）ことを確かめて 1 つの番号で記録する。番号は `state_serial`（PRESENT・COMMIT・WAIT 共通、lease をまたいで増える）。WAIT は `bcm2711_display_flip_wait_number`（timeout は 1 秒まで、EAGAIN・ETIMEDOUT・EINTR・EBUSY、timeout で uncertain にしない）。scaled: PPF・TPZ の word、LBM は overlay ごとに 8192 word の固定の区画、縦の拡大縮小は 1 行 2048 pixel まで、縮小は 32 倍未満、clock は scaled を 2 pixel/clock、帯域は縦の縮小の比の切り上げ倍。lease の始めに overlay と serial を戻し、restore は overlay を隠して借りを全部解く。OVERLAY_INFO は max_src_width ≦ 2048 と SCALE_UP・DOWN_X・DOWN_Y。query は plane_count＝3・HAS_OVERLAYS。計数（v3d.perf=1）: flip perf に overlay・dropped・pair（窓と cursor が同じ list で動いた数/窓が動いた数）、`bcm2711: overlay times`（overlay の記録ごとの plane:serial:記録から採用までの ms、採られずに置き換えられた数）、cadence に overlay。
- `userland/tests/hvs-load`（新、test の image だけ）と `platform/common/userland-dynamic.mk` の link の規則。

### 確かめ（host・build、QEMU・実機ではない）

- `timeout 900 sh plan/ws214/tests/display-p003a-host.sh build/p2-ws214/p003b-host` PASS（ordinary・ASan/UBSan）: core の COMMIT・OVERLAY_INFO（op の組の検査、framing・重複・flag・signal・COMMIT_WAIT の検査、fence の EOPNOTSUPP、ordinary の storage の IMAGE は EINVAL、COMMIT_WAIT の後の adoption_wait、OVERLAY_INFO の検査）、flip の overlay（list の word、2 枚を 1 つの記録で、DROPPED、cached の画像は commit の時に 1 回 clean・位置だけは clean しない、範囲の外・拡大縮小の限界・予算の ENOSPC で何も変わらない、hide で借りが採用の後に解ける、番号の WAIT の EAGAIN・採用・別の lease の EBUSY、pair と記録の時刻、lease の終わりで overlay が消え借りが解ける、次の lease は serial が 1 から、PPF・TPZ の word の値）、device（番号は記録の番号、WAIT の EAGAIN と ETIMEDOUT、lease をまたいだ番号、plane_count・HAS_OVERLAYS、OVERLAY_INFO）。
- `plan/ws141/tests/display-host-test.sh`・`vulkan-cursor-host.sh` PASS、UAPI の layout の C89 の compile（ILP32・LP64）。
- build: rpi4 の vmunix・amd64 の vmunix・rpi4 の libvulkan.so・hvs-load が warning 0。
- 値の出典と類似の監査: [../phase003/hvs-values.md](../phase003/hvs-values.md)。

### Pi で帯域を実測する手順（焼き直しの後、T1・Q1 経由）

1. image: rpi4 の config に `ZEDBSD_USER_PROGRAMS += hvs-load`（または `--file /bin/hvs-load=build/<dir>/bin/hvs-load`）、kernel と libvulkan は同じ main の組。boot の行に `v3d.perf=1`。
2. compositor を止めた状態（console の login、または desktop を終えて）で `hvs-load --seconds=10`（display の node は自動で探す。見つからなければ `--node=/dev/gpuN`）。scene は primary、one-full、two-full、up-2x、down-x-2、down-y-2、down-y-3、down-xy-3/4、full-and-down-y-2 の 9 つ、各 10 秒、約 90 秒。
3. 読む物: stdout の `HVS-LOAD scene=… start_ms=… result=applied|nospace` と `end_ms`、kernel の log の `bcm2711: flip perf … underruns=N`（2 秒ごと、scene の時刻で突き合わせる）。`result=nospace` は今の予算（1.5 GB/s 仮）が refuse した組み合わせ。
4. 判定: applied の scene で underruns が 0 なら、予算は少なくとも安全。underrun が出た scene があれば、その帯域（src の面積×縦の比の切り上げ×4×refresh の和）より下に予算を下げる（`FLIP_BANDWIDTH_BUDGET` を直し、OVERLAY_INFO の能力も合わせる）。予算を上げる方向の実測は、予算が refuse する組み合わせを流せないのでこの道具ではできない（要るなら別の段で）。
5. 可能なら mode を変えて（800x480・1024x600・1920x1080 のうち選べる物）繰り返す。

### 未実施・残件

- Pi の実測（上の手順）と T1 の i915・Venus の回帰（WAIT・present の振る舞いが変わらないこと）。
- 差し替えで HVS が完全な list の一方だけを採ること、scaled の word と LBM の区画が実機で正しく出ることは、host の模型では確かめられない（Pi の目視と underrun）。
- p004（libvulkan の overlay の plane）。
