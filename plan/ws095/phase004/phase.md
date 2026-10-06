<!-- awesome-plan project=zedbsd record=ws095-p004 -->

# ws095-p004: protocol の記述、zdesktop の仲介、IME の program、guest の試験

Status: cleared（2026-09-29）
Disposition: normal
Parent: [WS095](../ws.md)
Queue: main が割り当て（2026-09-29、worktree `.claude/worktrees/ws095-ime`、branch `wt/ws095`）。Queue の ID は main が記録する

## 範囲と受け入れ

[design.md](../design.md) §2〜§5・§8・§12 の p004 の部分:
- protocol の client の記述: text-input-v3・input-method-v2・virtual-keyboard-v1・私的な `keiland_ime_status_v1`。
- zdesktop の仲介: 起動と信頼、text input の状態、key の経路、Alt+Space、watchdog。
- IME の program の骨。
- ime-probe と guest の試験。

main の指示（2026-09-29）: zdesktop の変更は新しい file にまとめ、既存の file への差し込みは最小にする。Alt+Space を Terminal より先に取る。

受け入れ（全て満たした、下）:
- libwayland・zdesktop・IME・probe が guest の toolchain で -Werror で build できること。
- guest（QEMU、Venus）で次を確かめること。
  - IME の起動と global を他の client から隠すこと
  - 直接入力の素通し
  - Alt+Space の切り替え
  - 変換と確定
  - done の serial
  - password の field で IME を通さないこと
  - watchdog
  - 起こし直し
- 起動の確認（boot-test）が通ること。

## 結果

### 変えた file

- **新規（zdesktop）**
  - `userland/desktop/wayland/ime.h`: 共有の型と宣言
  - `text-input.c`: zwp_text_input_v3 の状態・enter と leave・secret の判定・done の serial
  - `input-method.c`: 起動・信頼・再起動、input_method・grab・popup の受け・VK・status、key の経路、watchdog
- **新規（libwayland）**
  - 記述: `text-input-protocol.c`・`input-method-protocol.c`・`virtual-keyboard-protocol.c`・`ime-status-protocol.c`
  - 公開の header: `include/libc/wayland/{text-input-unstable-v3,input-method-unstable-v2,virtual-keyboard-unstable-v1}-client-protocol.h`
  - 私的な header: `userland/desktop/libwayland/zed-ime-status-v1-client-protocol.h`
- **新規（IME の program）**: `userland/desktop/ime/{main.c,method.c,keys.c,program.h,Makefile}`（package `keiland-ime`、`/usr/libexec`、既定では選ばない）
- **新規（試験の client）**: `userland/tests/ime-probe/{main.c,Makefile}`（package `ime-probe`）
- **新規（試験）**: `plan/ws095/tests/{config-amd64-ime.mk,build-ime-image.sh,ime-guest.sh,ime-p004.sh}`
- **既存の file への差し込み（最小）**
  - `wayland/zwl.h`: 種類 10、`client->ime`、`server->ime`、`zwl_seat_key_deliver` の宣言
  - `seat.c`: key の hook 3 つ、`zwl_seat_key_deliver` の切り出し、modifiers の hook、focus の hook
  - `protocol.c`: global 4 つ、registry と bind の filter、dispatch
  - `objects.c`: object の hook、client の hook
  - `main.c`: 起動と tick
  - `wayland/Makefile`: source 2 つ
  - `libwayland/Makefile`・`exports.map`
  - `include/libc/wayland/API-PROVENANCE.md`: 3 つの XML の pin
  - `platform/amd64/vmunix.mk`: `keiland-ime` と `ime-probe` の link の規則（既存の Wayland の試験の client と同じ形）

design から変えた点と細部（design §15）:
- modifiers は client にいつも物理のものを送り、VK の modifiers は捨てる。
- 日本語の engine の結線を p005 から p004 に移した。
- watchdog の 500 ms に加え、100 ms を超えた答えの遅れを log に出す。IME の起動の時に engine を温める。
- `keiland_ime_status_v1` の `languages` は p005 に回した。

## 実行したコマンドと結果

- build（自分の BUILD の `build/ws095/img`、config は Files の lean な image と `plan/ws095/tests/config-amd64-ime.mk`、background）
  - libwayland の .so、`/bin/wayland`（zdesktop）、`keiland-ime`、`ime-probe` は全て exit=0、-Werror で warning 0。
  - 最初は `keiland-ime` と `ime-probe` の link が未定義の symbol で失敗した。vmunix.mk に link の規則を足して通した。
- `plan/ws095/tests/build-ime-image.sh build/ws095/img <自分>/build/ws095/p003/dist` → disk image が exit=0 で作れた。
  - 辞書と外部の package の tarball は自分の directory に取得した（共有の distfiles には書いていない）。
- guest（`plan/ws095/tests/ime-guest.sh start`、Venus、QEMU）で `/usr/libexec/keiland-ime` と `/usr/share/kei/ime/ja/SKK-JISYO.{X,kei}`（0644）が入っていることを SSH で確かめた。
  - p003 で未実施だった image への組み込みも、これで確かめた。
- `plan/ws095/tests/ime-p004.sh`（判定は guest の file を SSH で読む。log を host の grep で見る。画面は `zdesktop-check.py` で撮る）
  - 1 回目: 試験の側の 2 つの誤りで失敗（UTF-8 の pattern を guest の grep に渡した、閉じた窓の後の focus）。試験を直した。
    - 同じ run で、日本語に切り替えた直後に意図しない bypass が 1 回出た。IME の起動の時に engine を温める処理と、遅れの log を足した。
  - 直した後の 2 回: **いずれも `ime-p004: status=0`**。bypass は試験で IME を止めた段の 1 回だけで、100 ms を超えた答えは 0 回。確かめたこと:
    - IME の起動と READY。IME の global（input method・virtual keyboard）が probe に見えない。
    - 直接入力で a・b が probe の key として届き、preedit は無い。
    - Alt+Space で `language=ja`。`kanji` で preedit かんじ、Space で 漢字、Enter で確定 漢字。`watasi` Space Enter で 私。
    - done 16 回の serial が全て probe の commit の数と一致。
    - Alt+Space で direct に戻り、x が key として届く。
    - password の probe では日本語の時も a が key として届き、preedit は無い。
    - IME を SIGSTOP すると 500 ms で bypass し、z が probe に届く。SIGCONT で answering。
    - SIGKILL で `ZWL IME lost`、起こし直し、2 回目の READY、新しい probe に再び activate。
    - `ZWL ERROR` は 0。
    - 日本語の時に押した Alt は IME を通って virtual keyboard で probe に戻った（probe の log の key=56）。
  - 画面: `build/ws095-shots/p004-run1/{preedit,converted,password,restarted}.png`
    - probe は文字を描かない窓。候補の窓と indicator は p005 で作る。
- `OUTPUT=build/ws095/boot-test plan/tools/boot-test.sh build/ws095/img/hdd-image.img` → **PASS**（`build/ws095/boot-test/login.png`）。
- `sh plan/ws095/tests/host-engine.sh` → 150 passed, 0 failed。
- coding-style の機械の見直しで見つかった所を直した（閉じ括弧の後の空行、条件の中の fcntl）。`git diff --check` は問題なし。

## 未実施・制限

- 実機は未実施。確かめたのは QEMU（Venus）の証拠だけ。
- 次は p005 以降で行う。
  - 候補の窓の描画と配置
  - indicator
  - IME の中の key の repeat（変換中の BackSpace の押しっぱなし）
  - titlebar の field（p008）
- 既定の image への `keiland-ime` の組み込みは、main が決める（入れる image は辞書を取得する）。
- 変換中に window の menu・tab の key を後に回す経路（composing_only）は、guest では直接には試していない（menu のある app を試験に使っていない）。
- 気づいたこと（WS095 の外）: 手前の窓を閉じた後、残った窓に keyboard の focus が戻らなかった。zdesktop の focus の動きで、意図したものかを main に確かめたい。
- build で worktree の中に `build/amd64/sysroot`（header の写し）と `build/amd64/packages/toolchain`（package の cross の wrapper）が作られた。
  - disk image の通常の手順で作られるもの。共有の toolchain（`build/llvm` など）と toolchain の source は変えていない。

## Resume point

p004 は cleared。次は p012（補いの辞書の千語への拡張と活用の注釈、held-out の計測）か p005（候補の窓と indicator）。
