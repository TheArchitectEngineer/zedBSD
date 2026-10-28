<!-- awesome-plan project=zedbsd record=ws309-p003 -->

# ws309-p003 io-device: 入力の device の検出と close を thread へ（zedBSD）

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §4.5。Queue: q1662-i01（P1）。
- 状態: in-progress（2026-10-29 P1、実装と host 試験まで。QEMU は p017 にまとめる）。
- 出典: ユーザーの決定 1「デバイスの検出はスレッドに追い出してください。」、3「入力スレッドに入力デバイスの検出があるのはおかしい…I/O系にまとめるのがいい」。
- 依存: p002。前身の P2 の実装（`agent/p2-ws291i` 3d9306f02、backend の中の worker）は merge せず、この Phase の io-device の lane で置き換える（ws.md の未決）。

## 設計（実装した形）

- backend（keiland-backend-evdev.h）: `kl_backend_input_find_supported()`（zedBSD 1、Linux・FreeBSD 0）、`kl_backend_input_find_begin()`（main で: system の event が聞こえるか、
  device の event の回数を写す）、`kl_backend_input_find()`（thread で: readdir・open・capability の ioctl、callback を呼ばない、結果は record の list、既知の path は main の写し）。
- 開けなかった node の記憶（b574fbf6a）は残す。forget は system の event の回数（`input_refused_generation`、main が数える）と開ける側の見た回数（`input_refused_seen`）の比較で、
  開ける側（thread か、thread の無い時の main の scan）だけが bitmap を書く。system の event が無い時は覚えない（P2 の host 試験で見つかった誤りと同じ扱い）。
- compositor: `wayland/io.c` が io-device の lane を持つ。`kwl_input_scan` は lane に finding を出す（1 つずつ、`input_finding`）。taking で main が今の分類（`kwl_backend_input_known`・
  `kwl_input_probe`）を呼び、keep しない node と重複は lane で close。seat が手放した device の close（input.c の detach の後）も lane。終了は seat の後に lane を stop（未取得の node は drop で close）。
- thread の無い時（Linux・FreeBSD、lane が立たない、job の memory が無い）は今の同期の scan。
- 起動: READY の前の scan も lane へ出す。zedBSD では device は READY の直後の pass で attach（P2 の版と同じ振る舞い）。

## 受け入れ

- host 試験 `sh plan/ws309/tests/input-find/run.sh`（ASan+UBSan と TSan、/dev/input を --wrap で偽装）: open が lane の thread だけで起きる、EIO の event4 を覚えて 2 回目は開かない、
  device の event の後に再試行、event が無いと毎回試す、既知の path を開かない、begin の写し。
- `build/amd64/bin/wayland` warning 0、Linux の input-evdev.c が host の cc で `-Wall -Wextra -Werror -fsyntax-only` を通る。
- QEMU（p017）: 起動の後に keyboard・pointer が attach（`KWL INPUT` の行でなく guest の操作で確かめる）、`aat-input` の stop・start で hotplug の attach と close。未実施。

## 記録

- 2026-10-29 P1: host 試験 2 回とも `host-input-find: PASS`。amd64 の `bin/wayland` exit 0・warning 0。Linux の input-evdev.c の syntax の確かめ OK。
- `plan/tools/system-events/run-host-events.sh` は main で既に link が壊れている（notify-unsupported.c の欠け、P2 の記録と同じ）。この Phase では直さない（plan/tools は Q1 の物、Q1 に報告）。
