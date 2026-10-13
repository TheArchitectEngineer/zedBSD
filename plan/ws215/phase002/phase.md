<!-- awesome-plan project=zedbsd record=ws215-p002 -->

# ws215-p002: arm64 で Bluetooth の userland と input bridge を build する

Parent: [WS215](../ws.md)
Status: in-progress（2026-10-13 P3: 実装・build・host 試験まで済み。T1 の QEMU raspi4b の起動の回帰と、Pi の実機の確かめ（下の「Pi で見る点」）の結果を Q1 が判定するまで cleared にしない）
Phase disposition: normal
Queue: P3 の lane（2026-10-13 Q1 の割当。承認: 2026-10-12 ユーザー「RPi4のWiFiとBluetoothを実装し」、2026-10-13 朝の D1〜D4 の答え）

## 目的・範囲

設計 [p001](../phase001/phase.md) の B17。bluetoothd・bt・bt-probe を arm64 で build し、kernel の `CONFIG_INPUT_BRIDGE`（/dev/input/bridge、Bluetooth の HID の keyboard・mouse が通る）を rpi4 でも build する。BT の driver（PL011 の HCI の transport）は p003。

受け入れ条件:

1. rpi4 の vmunix（`config/ci/config-rpi4.mk`）と bluetoothd・bt・bt-probe が arm64 で warning 0。`CONFIG_INPUT_BRIDGE=y` が rpi4 の既定。
2. amd64 の vmunix と bluetoothd・bt が壊れない（warning 0）。
3. 既存の host 試験（bt-hci-proto、bluetoothd）が PASS。
4. Pi 4 の既定の image に bluetoothd を入れる（D1「Bluetooth はデフォルト有効」）。BT の driver が入るまで、bluetoothd は controller を見つけず静かに待つ（起動を害さない、log が溢れない）。
5. QEMU raspi4b の起動の回帰（input bridge の入った kernel）が通る（T1）。

## 調べて分かった事（設計の 1 節の訂正）

- **bluetoothd と bt は既に arm64 で build され、Pi 4 の既定の image に入っている。** `ZEDBSD_USERLAND_PACKAGE` の 3 番目の引数（platform）は、menu が `base`・`desktop` などの package では `*` に置き換わる（Makefile 273 行）。bluetoothd・bt の `amd64` は使われていなかった。`config/ci/config-rpi4.mk` の `ZEDBSD_USER_PROGRAMS` に `bluetoothd`・`bt` があり、main の checkout の `build/arm64/rootfs/` に `/sbin/bluetoothd`・`/bin/bt`・`/etc/service.d/bluetoothd`・`_bluetooth` の account がある（2026-10-13 13:40 の build）。ユーザーが「実機動作OK」とした main 88a488457 の image にも同じ規則で入っている（bluetoothd の log を実機で見たわけではない）。
- **bt-probe は `tests` の package なので platform の制限が効き（Makefile 368〜370 行）、amd64 だけだった。**
- `CONFIG_INPUT_BRIDGE` は Makefile の既定が amd64 だけ y、kernel の規則も amd64 の vmunix.mk にだけあった。呼び出し（`src/kern/vfs.c` の `drv_input_bridge_register()`）は arch に依らない。

## 変えた所

| file | 変更 |
| --- | --- |
| `Makefile` | `CONFIG_INPUT_BRIDGE` の既定を `amd64 rpi4` で y |
| `platform/arm64/vmunix.mk` | amd64 の 349〜355 行と同じ形の規則: `input-bridge.c`・`input-bridge-setup.c`。`CONFIG_DRIVER_USB_HID` が n の時は `hidraw-describe.c` と HID の glue（`hid-report.c`・`hid-digitizer.c`・`hid-touch.c`・`hid-input.c`。arm64 では usb-hid と一緒にしか build されないので、amd64 の `AMD64_HID_SOURCES` の分もここで足す） |
| `userland/tests/bt-probe/Makefile` | platform を `amd64 rpi4` |
| `userland/base/bluetoothd/Makefile`・`userland/base/bt/Makefile` | platform の引数を `*`（他の base の package と同じ。挙動は変わらない、読み違いを防ぐだけ） |
| `plan/ws215/tests/config-rpi4-bt.mk` | 試験の image: `config/ci/config-rpi4.mk` + bt-probe（p003 の smoke で使う） |

## bluetoothd が controller の無い Pi で静かに待つこと（受け入れ 4、code を読んだ確かめ）

- `btd_open()`（main.c 916 行〜）: 親（privsep）が `/dev/bluetooth0`〜`15` を順に open し、無ければ ENOENT。ENOENT と EBUSY（2 回目から）は log に出さない。session を作らず、`btd_failures` も数えない（止まらない）。
- 主の loop（main.c 740・817 行）: poll の上限 1 s、node の探し直しは `BTD_RETRY_MS` = 2 s ごと。CPU は 2 s に 1 回 16 個の open の失敗だけ。
- `bluetoothd.service`: `restart=on-failure`、`required=NO`。controller が無くても daemon は終わらないので再起動の loop にならない。
- p003 の後: driver が attach しない時（serial の console が y、`disable-bt`、QEMU、BT_ON の読みの ENOTSUP、電源の投入の失敗）も node が出ないので同じ待ち方になる。

## 実行したコマンドと結果（agent/p3、main 752de34d2 を merge の後）

| コマンド | 結果 |
| --- | --- |
| `make -j8 -o <wt>/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk vmunix` | rc 0、warning 0。`input-bridge.c`・`input-bridge-setup.c` が build され、`build/arm64/vmunix` に "register input bridge" の文字列（vfs.c の `#ifdef CONFIG_INPUT_BRIDGE` の中）がある |
| 同上 + `CONFIG_DRIVER_USB_HID=n` | rc 0、warning 0、`hidraw-describe.c` と HID の glue 4 file がこの規則から build されて link が通る（確かめの後、既定の config で build し直した: rc 0、warning 0） |
| `make ... ZEDBSD_CONFIG=plan/ws215/tests/config-rpi4-bt.mk build/arm64/bin/bluetoothd build/arm64/bin/bt build/arm64/bin/bt-probe` | rc 0、warning 0（`-Wall -Wextra -Werror`）、3 つとも aarch64 の PIE、`check-dynamic-elf.py` を通る |
| `make ... ZEDBSD_CONFIG=config/ci/config-amd64.mk vmunix build/amd64/bin/bluetoothd build/amd64/bin/bt` | rc 0、warning 0 |
| `OUT=build/ws215-p002/bt-hci-host sh plan/ws143/tests/bt-hci-proto-host-test.sh` | `bt-hci-proto: all checks passed` |
| `OUT=build/ws215-p002/bt-daemon-host sh plan/ws143/tests/bt-daemon-host-test.sh` | daemon 90・pair 161・link 56・hid 75・hidhost 96・hog 24 の checks が PASS（この worktree に intelbt の cache が無いので、Intel の実 file の段は走っていない） |
| `make menuconfig-host-test` | PASS（6 targets） |

log は worktree の `build/ws215-p002/`（git に入れない）。

## 未実施・T1 への依頼

- **QEMU raspi4b の起動の回帰（T1）**: `config/ci/config-rpi4.mk` の image（input bridge の入った kernel）で `plan/tools/boot-test.sh` の login prompt。担当は QEMU を起動していない。
- **Pi で見る点（T1・実機、Q1 が届ける image で）**: (1) 起動して login prompt（今と同じ）。(2) `service status bluetoothd`（または `ps`）で bluetoothd が動いている。(3) `bt show` が controller 無し（`no-controller` の類）を返す。(4) syslog に bluetoothd の行が繰り返し出ていない（起動の行の後に増えない）。(5) `ls /dev/input/bridge` がある。(6) `top` で bluetoothd の CPU がほぼ 0。USB の BT dongle を挿せば既存の usb-bt で動くはずだが、今の受け入れの外。
- 実機の証拠はまだ無い（上は code と build の確かめだけ）。

## 残件

- p003 以降（PL011 の transport、config.txt の生成、D1 の答えで `CONFIG_DRIVER_BCM2711_BT` の既定は y）。設計の review-2 への対応は [p001](../phase001/phase.md) の「第 3 版」の節。
