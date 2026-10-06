<!-- awesome-plan project=zedbsd record=ws131-p021 -->

# ws131-p021: compositor の内部の名前を kwl_・KWL_ に（log の文字列は変えない）

Status: in-progress（q814、P1。実装と host の確認まで済み、T1 の結果待ち）
Disposition: normal
Parent: [WS131](../ws.md)、計画の正本 [design.md](../design.md)
Queue: q814（2026-10-06 ユーザー「全部進める」）
依存: p009・p011 cleared。compositor に他の作業が無い時（P2・WS113・WS099 と同時に流さない）。判断 D15
目安: 3〜4h（1 Queue）。実行者: Q1 が割り当てる（high）
所有 path: `userland/desktop/wayland/`（全 file、`zwl.h`・`zwl-*.h` の名前）、D15 を採る時は libkeiland の protocol の wrapper・`userland/desktop/keiland/wayland/` と libwayland の protocol の header・keiland-ime、`plan/ws131/`。（Q1 の委任が要る: Q1 が Phase の前に委任を記録する）: compositor の source を compile する道具（`plan/tools/gpu-boundary/`・`plan/tools/titlebar/`・`plan/ws035/tests/`・`plan/ws102/tests/`・`plan/tools/keiland-freebsd/` の各 1 file）

## 目的と結果

compositor の内部の名前 `zwl_`（494）・`ZWL_`（271）を [rename-map-kwl.md](../rename-map-kwl.md) のとおり `kwl_`・`KWL_` にし、`zwl.h` を `kwl.h` にする。log の文字列 `"ZWL "` は変えない（p022）。D15 を採れば Keiland の Wayland の protocol の名前（`keiland_*_v1`）と生成の定数も `kl_*_v1`・`KL_*_V1_*` に（compositor の global の表、libkeiland、libwayland の header、ime を同時に）。

## 範囲

1. rename-map-kwl を作り直し（backend へ移った名前は既に無い）、機械的に改名。
2. D15: wire の名前の変更は compositor と全 client を同じ build で。

## 受け入れ

- 3 OS の build（zedBSD の amd64 は exit 0・自前の warning 0、`make keiland-linux` の gcc と clang、FreeBSD の native build と `native-build-audit.py`）と `plan/tools/keiland-os-boundary/check.sh` PASS。
- compositor に `zwl_`・`ZWL_` の識別子が 0（log の文字列を除く）。
- zedBSD: boot-test、C1・C2・C9、GPU の境界（§6）。Linux: compositor と app の PNG。log を読む試験は文字列が変わらないので今のまま PASS すること。

## 検証の方法

[zedbsd-commands.md](../../tools/keiland-linux/zedbsd-commands.md)、[keiland-linux/README.md](../../tools/keiland-linux/README.md)、[keiland-freebsd/README.md](../../tools/keiland-freebsd/README.md)。QEMU の console・serial の log で判定しない（不具合は gdbstub・monitor・QMP）。build は自分の `BUILD=build/ws131-pNNN/`、共有の `build/` を消さない。FreeBSD の起動の確認は判断 D11 の範囲（passthrough なし）。QEMU と実機の証拠を分ける。

## 衝突・危険・rollback

- 衝突: compositor の全 file に触れる。 順は Q1 が決める（design.md §7.2）。
- rollback: 前へ直すのを基本にし、後ろに同じ file の変更が無い時だけ統合の commit を revert する（design.md §7.4）。

## Resume

依存の Phase の cleared と main への統合、関係する判断の決定の後に、Q1 が Queue を作る。

## q814（P1、2026-10-06）

Q1 の確認: compositor を触る他の担当は無し（P2 には merge まで触らないよう Q1 が連絡）。compositor の source を compile する道具の直しは Q1 の委任。

### 変えた所

- [rename-map-kwl.md](../rename-map-kwl.md) を作り直した（`zwl_` 677・`ZWL_` 406、計 1083。backend の `zwl_gpu_*` なども含む）。
- `zwl_`→`kwl_`・`ZWL_`→`KWL_`: `userland/desktop/wayland/` の全 file と libkeiland-backend の 3 tree の識別子と comment（文字列の中は対象外。文字列に `zwl_` は無かった）。`zwl.h`→`kwl.h`（`git mv`、include と header guard）。生成の `shaders.h` の名前と `shaders/regenerate.py`。log の `"ZWL "` は変えない（p022）。
- compositor の source を compile する host の試験 17 本と、source の名前を読む script（`keiland-os-boundary/check.sh` の C3 の pattern、`ws075/tests/guard/run.sh` の shader の名前、comment の参照）。shell の試験の補助関数 `zwl_app_client(s)`・`ZWL_RUN`（`plan/tools/guest/zwl-clients.sh`）は compositor の名前ではないので p022 に残す。
- D15: Keiland の protocol の名前 `keiland_*_v1`→`kl_*_v1`、`KEILAND_*_V1_*`→`KL_*_V1_*`（wire の interface の名前を含む、978 か所・63 file）: compositor、libwayland（`*-protocol.c`・`zed-*-client-protocol.h`・exports.map・event.c）、libkeiland、keiland-ime、libvulkan の WSI、libkeiland-backend-zedbsd、Text Editor・Image Viewer、`userland/tests/`（titlebar-probe・acquire-fence・gpu-forge）、`docs/architecture/keiland.md`、試験（`ws094/tests/desktop-probe.c`・`desktop-guest.sh`、`ws102/tests/edit-guest.sh`・`inset-guest.sh`、`ws114/tests/decoration-wire.py`）。独立の試験の protocol（`ws035/tests/p075` の `keiland_generic_test_v1`）と古い Phase の記録は変えない。

### 確認

- build: zedBSD amd64 の libwayland-client・libkeiland・libvulkan・wayland・textedit・imageview・files・settings・terminal・notes・calendar・mailer・pdfviewer・browser・xserver・keiland-ime・titlebar-probe・acquire-fence-test・gpu-forge-test（exit 0、warning 0）。全 binary と .so の未定義の `*_v1` の symbol は全て libwayland-client.so にある（`llvm-nm -D` の比較）、旧名の未定義 0、compositor の wire の名前は `kl_*_v1` だけ。
- `make keiland-linux` の gcc と clang（exit 0、warning・error 0）、`keiland-os-boundary/check.sh` PASS、`exports.py --check` OK。
- host: run-host-role、host-store (43)、p078 run-host、host-network-info (31)、host-power-layout (21)、host-emoji、host-keyboard、host-scanout-rules (17)、host-system、host-lid (29)、ws142 の host-apps (32)・layout (57)・gesture (62)・super-tap (16)・swipe (15)・switcher (31)、host-touchpad (25)、p034-bar-host、gpu-zedbsd-host と host-pointer-accel（script は host の rm を含むので、同じ compile を worktree の build の中で直に）、icons-host の compile。全て PASS。
- 未実施: FreeBSD（環境なし）、`ws075/tests/guard/run.sh`（Mesa の道具が要る）、QEMU（T1）: boot-test、C1・C2・C9、GPU の境界（§6）、log を読む試験（文字列は変わらない）、titlebar-probe・edit-guest・inset-guest・desktop-guest（wire の名前が変わった口）、Linux の compositor と app の PNG。
