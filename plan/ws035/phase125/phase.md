<!-- awesome-plan project=zedbsd record=ws035p125 -->

# ws035-p125: guest の試験の道具の前提を demo の image と guest の止め方に合わせる

Phase ID: `ws035-p125`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-29、サブエージェント。QEMU の Venus）
Phase disposition: normal
Queue: なし（2026-09-29 main の割り当て。plan/ws035/tests・plan/tools/files の道具の変更の許可）

## 範囲と原因

p121〜p123 で見つかった 2 つ（[p121](../phase121/phase.md) の検証、main への一覧の 5・6）:

1. demo の image（graphical login）では greeter の service（sessiond と `/bin/wayland --greeter`）が画面を持つ。compositor を自分で
   起こす試験（zdesktop-p0xx・files-p0xx）は lean な image を前提にしていて、swapchain が作れず FAIL した。また、5 本の試験は止める process を
   改名前の名前（comm `zdesktop`）で探していて、前の compositor が残った。
2. `zdesktop-guest.sh stop` は GUEST_RUNTIME が無いと `build/ws035-sq-run` を止めるので、files-guest.sh（`build/ws071-run`）の guest は止まらない。
   さらに start は同じ runtime で動いている guest を確かめず、その disk に image を複写して新しい pid を記録するので、前の QEMU が記録されない
   まま残った（2 回目の start は disk の lock で失敗し、stop は失敗した方の pid を止めた）。

## 実装（2026-09-29）

1. `plan/ws035/tests/*.sh`・`plan/tools/files/*.sh` の `stop_all` を持つ 56 本の試験: `stop_all` の先頭に
   `service stop greeter >/dev/null 2>&1;` を足した（lean な image では service が無く、何もしない）。`zdesktop-p052.sh`（`stop_all` が無い）は
   最初の片付けの行に同じもの。comm で探す 5 本（p064・p065・p070・p071・p072）の `zdesktop` を `wayland` に。
   greeter を扱う試験のうち stop_all を持つのは p095 だけで、p095 は sessiond を手で起こすので、service の greeter を止めても影響は
   無いと読んだ（p095 は再実行していない）。demo-walk は stop_all を持たない。`zdesktop-p097.sh`（quiet boot の console、専用の image）は変えていない。
2. `plan/ws035/tests/zdesktop-guest.sh`: start は同じ runtime の guest を先に `guest.py stop` で止め、使った runtime を
   `build/.zdesktop-guest-runtime` に記録する。GUEST_RUNTIME を与えない stop はその記録の guest を止める。files-guest.sh は GUEST_RUNTIME を
   与えて呼ぶので、そのまま自分の guest を止める。`plan/tools/guest/guest.py`（範囲外）は変えていない。

## 検証（2026-09-29、QEMU の Venus、demo の Venus の image）

- demo-walk（最後に greeter へ戻る）の直後に、greeter を手で止めずに: `zdesktop-p078.sh` PASS、`zdesktop-p071.sh` PASS、
  `zdesktop-p090.sh … demo` PASS、`zdesktop-p064.sh` PASS、`plan/tools/files/files-p002.sh` PASS。
- `env -u GUEST_RUNTIME plan/ws035/tests/zdesktop-guest.sh stop` が files-guest.sh の guest（`build/ws071-run`）を止めた（QEMU 0）。
- `files-guest.sh start` を 2 回続けて: 2 回目の前に 1 回目が止まり、QEMU は 1 つ、`qemu.log` に lock の誤りなし。
- 未実施: 変えた 56 本の残りの試験の実行（同じ一行の追加で、代表の 5 本を走らせた）。lean な image での再実行。

## 残り

- `plan/tools/titlebar/`・`plan/ws079/tests/` 等の他の道具の試験（範囲外）にも同じ前提がある。必要なら同じ一行を足す。
- `guest.py start` 自身が動いている guest を確かめると、zdesktop-guest.sh を通らない起動でも守れる（`plan/tools/guest/`、範囲外）。
