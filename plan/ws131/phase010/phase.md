<!-- awesome-plan project=zedbsd record=ws131-p010 -->

# ws131-p010: compositor の拡張の protocol と設定の記録

Status: in-progress（q659、P2、2026-10-04。実装・host の試験・3 OS のうち zedBSD と Linux の build・checker 済み。FreeBSD の build と QEMU の guest の probe は T2 に依頼）
Disposition: normal
Parent: [WS131](../ws.md)、計画の正本 [design.md](../design.md)
Queue: q659（Q1 の「p010 へ」、2026-10-04）
依存: p004 cleared（network と音声が backend にある）。p005 の後なら電源も出す。P2 の BUG-125 の作業の merge 済み（D8 で P2 の終了の後に始めるので満たされる）。判断 D4（決定: compositor は touch IME の UI のために libkeiland を link してよい）・D5・D15
目安: 4〜5h（1 Queue）。実行者: Q1 が割り当てる（high）
所有 path: `userland/desktop/wayland/`（新しい `system.c`・`settings-store.c`・`worker.c`、`protocol.c` の global、`network.c`・`volume.c` の記録と worker）、`userland/desktop/libkeiland/`（新しい `system/`、`keiland.h` の `kl_system_*`、exports.map）、`plan/ws131/tests/`、`plan/ws131/`

## 目的と結果

design.md §4 の拡張 `kl_system_manager_v1`（settings・network・audio・power・devices の枠）と libkeiland の `kl_system_*` を作る。compositor の store（`desktop.conf` の書式・lock・rename）と worker の thread（disk と network の同期の待ちを event loop から外す、review 7）を作る。**毎秒の監視の thread はまだ残す**（Settings が p011 まで直接書くため、review 1）。

## 範囲

1. backend: `kl_backend_peer_uid`（zedBSD は `getpeereid`、`src/libc/openbsd.c:261`。compositor の socket で動くかを最初に確かめる）。device の枠は unsupported。
2. compositor: `system.c`（global の 25 番、registry で同じ uid の client にだけ見せる（`zwl_ime_global_visible` の仕組み）、snapshot・`done`・`request_id`・`result(applied, saved)`・error の列挙、network の要求は一度に一つ・PROFILES は `save_key` の中、design.md §4.1・§4.2）。`settings-store.c`（`libkeiland/preferences.c` の書式を移す）、`worker.c`（store の書き、`save_key`・`get_saved`・`query_details`、250 ms のまとめ、終了・log out の前の flush）。`volume.c` の記録と `network.c:1164`・`:1283` の同期の読み書きを worker へ。
3. compositor と libkeiland（D4 の決定）: compositor は libkeiland の link を続けてよい（touch IME の UI と motion のため）。循環を作らない条件（design.md §9 の D4）: compositor は libkeiland の Wayland の client の部分（`kl_system_*`・`kl_app_*`・`kl_window_*`・file chooser・protocol の wrapper）を使わず、拡張の protocol の定数は `userland/desktop/keiland/` の共有の header から取る。`desktop.conf` の store は compositor の中だけ。
4. libkeiland: `system/`（`kl_system_*`、protocol の `wl_interface` の表は static）、exports.map。
5. 試験（`plan/ws131/tests/`）: host の試験（store の書式・lock・まとめ・flush、protocol の encode と decode、snapshot の途中を見せない、busy）と guest の probe（`kl_system_*` で設定・音量を変える → compositor が記録・別の client に届く・範囲外は invalid・uid の違う client には global が見えない。probe は小さな Wayland の client で、SSH かシリアルで起動し、結果を client の終了の値と guest の file で判定する）。

## 受け入れ

- 3 OS の build（zedBSD の amd64 は exit 0・自前の warning 0、`make keiland-linux` の gcc と clang、FreeBSD の native build と `native-build-audit.py`）と `plan/tools/keiland-os-boundary/check.sh` PASS。
- 新しい host の試験と guest の probe PASS。Settings は旧経路のまま `settings-regress.sh`・`settings-p007.sh` PASS（監視が残るので）。`volume-p004.sh`・`volume-p005.sh` PASS（記録が store を通る）。boot-test、C1・C2・C9。Linux の guest で probe。

## 検証の方法

[zedbsd-commands.md](../../tools/keiland-linux/zedbsd-commands.md)、[keiland-linux/README.md](../../tools/keiland-linux/README.md)、[keiland-freebsd/README.md](../../tools/keiland-freebsd/README.md)。QEMU の console・serial の log で判定しない（不具合は gdbstub・monitor・QMP）。build は自分の `BUILD=build/ws131-pNNN/`、共有の `build/` を消さない。FreeBSD の起動の確認は判断 D11 の範囲（passthrough なし）。QEMU と実機の証拠を分ける。

## 衝突・危険・rollback

- 衝突: WS113 p005 は同じ manager の version 2（D13）で、この Phase の後。 順は Q1 が決める（design.md §7.2）。
- rollback: 前へ直すのを基本にし、後ろに同じ file の変更が無い時だけ統合の commit を revert する（design.md §7.4）。

## 実施（q659、P2、2026-10-04）

### 範囲の扱い

- 設定の store（`settings-store.c`、desktop.conf の書式・lock・rename）、拡張の `set`・`reset`、system bar の音量の記録、毎秒の監視の除去は **WS135 が済ませた**（session の終わりに merge、監視は既に無い）。この Phase は残りの network・audio・power・devices の object と、libkeiland の `kl_system_*` を作った。範囲 1 の `kl_backend_peer_uid` と registry の uid の制限も WS135 が済み（`settings.c` の `zwl_settings_global_visible`）。
- design.md §4.4 の案の `kl_system_settings_*` は作らず、WS135 の `kl_settings_*` をそのまま使う（設定の API を二つにしない）。
- worker の thread（範囲 2）: `system.c` の中に network 用と power 用の二つの thread（一度に一つの job）を置いた。別の `worker.c` は作らない（store の書きは WS135 の writer の thread が既にある）。

### 実装

- `userland/desktop/keiland/kl-system-protocol.h`: manager の request 2〜5（`get_network`・`get_audio`・`get_power`・`get_devices`）、capability の bit、4 つの interface の opcode と値。
- `userland/desktop/wayland/system.c`（新しい file）:
  - manager の dispatch（`get_settings` は settings.c へ渡す）と、capabilities 0x1f。
  - object を作ると、最初の state と `done` を送る。変化は「変わった event と `done` 一つ」で送る（network の state と scan も `done` 一つにまとめる）。
  - 要求には必ず `result` を一つ返す。errno は protocol の列挙に直す。
  - network の要求は system bar のものと合わせて一度に一つで、他は busy。
  - `save_key` は 3 段を compositor の中で行い、client には result を一つ返す: network の thread が鍵を保存する → PROFILES → join。system bar の要求に塞がれた段は次の pass で送り直す。
  - `query_details` の links・dns・saved は network の thread が読み、`details_done` と `result` の前に送る。
  - power の state（Linux は logind への D-Bus の問い合わせ）は power の thread が読む。power の object を作った時に読み、変われば伝える。action は event loop で行う。backend の `power_asked` が event loop のものだからで、zedBSD の session では unsupported がすぐ返る。
  - devices は枠だけで、eject は unsupported を返す。
  - 鍵は log に出さず、複写は volatile で消す。
- `network.c`:
  - accessor の `zwl_network_watch`・`zwl_network_state`・`zwl_network_scan` を足した。
  - tick で、state と scan の変化を拡張に伝える。
  - DONE は先に `zwl_system_network_done` に渡し、拡張のものなら system bar の slot だけを送る（`network_send_waiting` に分けた）。
- `volume.c`: `zwl_volume_request_channels`（左右と mute、feedback の音は鳴らさない）、`zwl_volume_feedback`、`zwl_volume_audio_state` を足した。
- `zwl.h`: kind 4 つと宣言。`protocol.c` は dispatch と bind を `zwl_system_*` にした。`settings.c` から `zwl_settings_bind` と manager の destroy を除いた。`main.c` に tick と close を足した（backend を閉じる前に thread を join する）。3 つの Makefile に `system.c` を足した。
- libkeiland:
  - `system/system-protocol.c`: 6 つの `wl_interface`。settings.c と共有する。exports.map で外に出さない。
  - `system/system-view.c`: Wayland を知らない view。`done` で一つの状態として適用し、scan・details・devices は一覧ごと置き換える。result は 32 個の ring に溜める。
  - `system/system.c`: `kl_system_*` の 20 個の関数。queue は library 専用のもので、open の時に roundtrip を 3 回行う。
  - `keiland.h`: API と `kl_network_*`・`kl_audio_state`・`kl_power_state`・`kl_device`。
  - `exports.map` に関数を一つずつ挙げた（`kl_system_*` の pattern にすると interface の symbol まで出るため）。
- 試験の道具:
  - `userland/tests/keiland-system/`: probe。`dump`・`scan`・`join`・`save-key`・`details`・`volume`・`feedback`・`power`・`eject`・`watch`。
  - `platform/amd64/vmunix.mk`: probe の link の規則（keiland-settings と同じ形）。
  - `plan/ws131/tests/config-amd64-system.mk`: WS135 の Settings の image に probe を足す。

### 確認（host と build、P2）

- `sh plan/ws131/tests/host-system.sh`: PASS（gcc 3 回、clang 1 回、ASan と UBSan）。試験は 3 部で、compositor の `system.c` と libkeiland の `kl_system_*` を socketpair の上で両端とも本物を動かす。
  1. view の単体: `done` の前に見えない、一覧の置き換え、details、ring、errno。
  2. compositor の単体: 次を確かめた。
     - 新しい ID の無い get、未知の opcode、NUL の無い string は EPROTO になる。
     - 未知の what、短い鍵、名前の無い join、音量 101、action 7 は invalid になる。
  3. 両端: 次を確かめた。
     - 最初の state。
     - power が thread から届く。
     - scan。
     - join が出ている間の 2 つ目の要求は busy。
     - 鍵の無い join は ENODEV。
     - save_key で PROFILES → JOIN(Cafe) と鍵が store に渡る。
     - system bar の要求が出ている間は送らず、その返事は bar に残し、後で PROFILES → JOIN を送る。
     - details（link の 64 bit の counter と hardware の文字列）。
     - state の変化。
     - 音量の左右と mute。
     - feedback。
     - 提供されていない power の action は ENOTSUP、提供されている action は ok。
     - eject は ENOTSUP。
     - protocol error 0。
- zedBSD amd64: `make ZEDBSD_CONFIG=plan/ws131/tests/config-amd64-system.mk BUILD=build/p2-q652 …/bin/wayland …/dynamic/libkeiland.so …/bin/keiland-system` は exit 0、warning 0。
- Linux: `make keiland-linux` は gcc と clang の両方で exit 0、warning 0。`libkeiland.so` は `kl_system_*` の 20 個を出し、interface の symbol は出さない（`nm -D`）。
- checker:
  - `keiland-os-boundary/check.sh` PASS（C1〜C5・L1〜L7・M1・X1・B1・B3・S1）。
  - `makefile-sync.sh` PASS。
  - `gpu-boundary/v1-check.sh` PASS。
  - `style-check.py` の新しい指摘は 0。残る指摘は mutex の critical section の空行の誤検出で、`settings-store.c` と同じ形。

### 未実施（T2 に依頼、2026-10-04）

- FreeBSD の native build と `native-build-audit.py`。
- QEMU の guest で probe を動かす。zedBSD の Settings の image（`SETTINGS_CONFIG=plan/ws131/tests/config-amd64-system.mk plan/ws089/tests/build-settings-image.sh`）で、次を確かめる。
  - `dump`（capabilities 0x1f）。
  - `volume`（左右）と、別の probe の `watch` に届くこと。
  - 範囲外は invalid になること。
  - `power 1` は unsupported になること（zedBSD の session）。
  - `details`。
  - uid の違う client には ENOTSUP になること。
- Linux の guest の probe。
- 回帰: `settings-regress.sh`・`settings-p007.sh`・`volume-p004.sh`・`volume-p005.sh`・boot-test。
- 実機は未実施。

### 残り

- review 7 のうち system bar の鍵の同期の書き（`network.c` の `network_key_submit` の `kl_backend_network_save_key`、`network_key_saved` の `kl_backend_network_get_saved`）は event loop に残っている。Settings を拡張へ移す p011 で、system bar も `system.c` の network の thread を使う形にするのが良い。この Phase では扱っていない。
- 範囲外の file の変更: `platform/amd64/vmunix.mk`（probe の link の規則）と `userland/tests/keiland-system/`（新規）。merge の時に Q1 が判断する。

## Resume

T2 の結果（FreeBSD の build、guest の probe、回帰）を Q1 が判定する。FAIL なら P2 が直す。
