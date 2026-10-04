<!-- awesome-plan project=zedbsd record=ws142-p002 -->

# ws142-p002: Super（Windows キー）単独で App Home を開く・閉じる

Status: in-progress（2026-10-05 P1 generation17 / q719-i01。q720 の後に再開し、実装・build・host の試験まで。QEMU は T1 に依頼。結果の判定まで cleared にしない）
Disposition: normal
Parent: [WS142](../ws.md)
Queue: q719 / q719-i01（Q1 の投入）
Design: [ws142-p001](../phase001/phase.md) の C

## 範囲と受け入れ

- Super（左右の Meta）を、Shift・Ctrl・Alt を押さずに押して、1 秒以内に、他の key・pointer の button・wheel・タッチ無しに離すと App Home を開く（開いていれば閉じる）。Super+Tab・Super+L・Super+Alt+文字は今のまま。Super の押下・解放は今まで通り client にも渡す（D9 の案）。lock・greeter の間は何もしない。
- build warning 0、host の試験、QEMU（T1）: QMP の key で Super 単独 → `ZWL HOME open via=super`、もう一度 → `ZWL HOME close via=super`、Super+Tab は Wiseview（Home は開かない）、Super を押したまま click → 開かない。

## 実装（2026-10-05）

| 所 | 内容 |
| --- | --- |
| `userland/desktop/wayland/super-tap.c`・`.h`（新） | 純粋な判定。Shift・Ctrl・Alt 無しの Super の押下で arm、他の key の押下（もう一方の Super も）・`zwl_super_tap_cancel` で解除、arm した key の解放が押下から 1 秒以内なら tap。repeat は無視 |
| `seat.c` `zwl_seat_key` | lock の判定の前で全部の key を判定に渡す（Super+L も tap にならない）。tap なら、glass で lock・greeter でない時に `ZWL SUPER home` と `zwl_home_toggle(server, "super")`。key は今まで通り先へ流れる（client に渡る。Home が出ている時の解放は Home が取る） |
| `seat.c` `zwl_seat_button`（押下）・`zwl_seat_axis`、`touch.c` `contact_begin` | `zwl_super_tap_cancel` |
| `home.c`・`zwl.h` | `zwl_home_toggle(server, via)`: 出ている・開いている途中なら `zwl_home_dismiss` で閉じ、それ以外は `home_open`（log `ZWL HOME open via=super`・`ZWL HOME close via=super`） |
| `Makefile`・`Makefile.linux`・`Makefile.freebsd` | `super-tap.c` |

## 確認

| 確認 | 結果 |
| --- | --- |
| zedBSD の compositor（`make … BUILD=build/q713 build/q713/bin/wayland`） | 成功、warning 0 |
| Linux の Keiland（`keiland-linux.mk all`） | 成功、warning 0 |
| `sh plan/tools/keiland-os-boundary/check.sh` | PASS |
| `sh plan/ws142/tests/run-host-super-tap.sh`（ASan・UBSan） | 16 checks passed: 左右の Super の tap、1 秒ちょうどは tap・超えると違う、Super+Tab・Super+L、両方の Super、修飾を押した状態、cancel、repeat、解放だけ、先に押していた key の解放 |
| style-check（新しい file、変えた hunk） | 指摘 0 |
| QEMU（`plan/ws142/tests/p002-guest.sh BUILD`、pen の guest） | **未実施**。T1 に依頼（Q1 経由） |

## QEMU の試験（T1 への依頼の内容）

- image: `plan/tools/guest/test-image.sh plan/ws079/tests/config-amd64-pen.mk BUILD`、起動 `plan/ws079/tests/pen-guest.sh start IMAGE`。
- 試験: `plan/ws142/tests/p002-guest.sh BUILD [OUTDIR]`（BUILD/bin/wayland を写す。QMP の key: meta_l・meta_r・tab・esc・shift、qmp-pointer の click）。
- 合格: 全行 ok（super-opens、super-logged、super-closes、right-super-opens、esc-closes、super-tab-wiseview、super-tab-no-home、click-no-home、long-hold-no-home、shift-no-home、alive、no-error）。`home-open.png` をユーザーに見せる。

## 残り

- QEMU の結果の判定。Wiseview が開いている時の Super 単独は Home を Wiseview の上に開く（Wiseview を閉じない）。p004・p005 で切り替えの UI と合わせて見直す。
