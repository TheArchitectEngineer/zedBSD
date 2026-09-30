<!-- awesome-plan project=zedbsd record=ws102-p018 -->

# ws102-p018: クリップボードの履歴

Status: cleared（2026-09-30、QEMU の Venus と host。実機は未実施。password の欄の text-input の purpose による除外は残り（下）、履歴の tab の UI は p016）
Disposition: normal
Parent: [WS102](../ws.md)
Queue: なし（2026-09-30 Q1 の割り当て、サブエージェント P6、worktree `ws035-keiland`（branch `wt/ws035`））
依存: p017（cleared）。keyboard の履歴の tab（一覧の UI）は p016（P3）

## 範囲と受け入れ

- design.md §2.10 の表の「クリップボードの履歴」: compositor の `data.c` が selection の text を記憶の中だけに最近 10 件持つ。password の欄からの複写は残さない。
  lock・Log Out で消す。口は `zwl_clipboard_history_count`・`_get`・`_paste`（項目を selection にして paste の操作を送る）。一覧の UI は p016。
- 確かめ: 3 つの app の間の複写で新しい順、10 件の上限、password の欄の除外、lock で消える。
- keyboard.c・shell.c・desktop.c は触らない（触っていない）。IME の file と seat.c の IME の hook は触らない（触っていない）。

## password の欄の除外（2026-09-30 の判断）

- text-input の purpose（password・PIN・hidden・sensitive）は `text-input.c` の static な list の中にしかなく、外から読む関数が無い
  （`zwl_text_input_current` は secret の欄でも NULL）。読み取りだけの関数 `zwl_text_input_secret_focused` の追加を Q1 経由でユーザーに尋ね、
  **許可は出なかった**（IME の file は人間の作業中）。
- 今の除外: source の MIME に `x-kde-passwordManagerHint`（password manager の約束事、KeePassXC など）がある複写を残さない。
- **残り**: text-input の purpose による除外。再検討のきっかけは「IME の作業がエージェントに戻ったとき」（上の関数 1 つと ime.h の宣言 1 行の案）。

## 変更

- compositor（`userland/desktop/wayland/`）:
  - `clipboard.c`（新規）: 履歴（最近 `ZWL_CLIPBOARD_HISTORY` = 10 件、新しい順、同じ text は上へ移る、`ZWL_CLIPBOARD_TEXT_MAX` = 64 KiB を超える物・空の物は残さない）。
    新しい selection の text を、client と同じ方法（`wl_data_source.send` で pipe に書かせ、event loop の各 pass で待たずに読む、2 秒で諦める）で読む。
    text の型は `text/plain;charset=utf-8`・`text/plain`・`UTF8_STRING`・`TEXT`・`STRING` の順。
    消す時と捨てる時は、free の前に volatile で 0 に塗る。log は長さと FNV-1a の checksum だけで、text を出さない。
    - `zwl_clipboard_history_count(server)`・`zwl_clipboard_history_get(server, index, &length)`・`zwl_clipboard_history_paste(server, index)`
      （項目を zdesktop 自身の selection にして一番上へ移し、`zwl_edit_action(PASTE)`）・`zwl_clipboard_history_clear(server, reason)`。宣言は `zwl.h`。
  - `data.c`: `set_selection` の後に履歴へ知らせる。zdesktop 自身の selection（`selection_offered`）の offer（text の 2 つの型）と、その receive で
    zdesktop が text を書く（`zwl_clipboard_offer_write`）。`zwl_data_send`・`zwl_data_select_offered`。`data.h`。
  - `zwl.h`: server の `selection_offered`、offer の `data_offered`、定数と宣言。
  - `main.c`: 各 pass で `zwl_clipboard_poll`（1 行と include）。
  - `greeter.c`（`zwl_lock`）・`handoff.c`（`zwl_handoff_logout`）: 履歴を消す（各 2 行）。
  - `edit.c`: 試験の口 Super+Alt+H（履歴を log）・Super+Alt+1〜9・0（項目 0〜9 を貼る）。
  - `Makefile`。
- 試験の道具 `userland/base/tests/data-probe/main.c`: `--secret`（source が `x-kde-passwordManagerHint` も出す）。

## 試験（`plan/ws102/tests/`）

- `clip-guest.sh IMAGE OUTDIR`（QEMU の Venus、p015 の inset の image）:
  1. data-probe a（"alpha one"）・Text Editor（"editor text" を全選択して複写）・data-probe c（"gamma three"）の順に複写 → 履歴は c・editor・a（checksum で比べる）。
  2. Super+Alt+2 で editor の text を data-probe c に貼る → c が `DATAPROBE received bytes=11 text=editor text`、editor が一番上へ。
  3. `data-probe --secret` の複写は `ZWL CLIP skip reason=secret`、履歴は 3 件のまま、log に text が無い。
  4. 2 つ目の Text Editor で "item 1"〜"item 12" を複写 → 10 件、"item 12" が先頭、"item 3" が最後。
- `clip-lock.sh IMAGE OUTDIR`（criteria の image、kei の session）: data-probe の複写で 1 件 → Super+L で `ZWL CLIP clear reason=lock count=1` →
  kei の password で解除 → 0 件 → もう一度複写 → App Home の Log Out で `ZWL CLIP clear reason=logout count=1`。

## 結果

| 確認 | 結果 |
| --- | --- |
| build: inset の image（`build/ws099/p018.img`）、criteria の image（`build/ws099/p018-criteria.img`） | どちらも exit 0、`userland/desktop` と data-probe の warning 0、`git diff --check` 0 |
| `clip-guest.sh` 1 回目 | 1〜3 は PASS。4 は試験の誤り（直前の app で Text Editor に戻すつもりが、別の窓に行った）で FAIL。4 を新しい Text Editor で行う形に直した |
| `clip-guest.sh` 2 回目（`build/ws102-p018-shots/run2/`） | **PASS**: 新しい順（c・editor・a）、貼り付け（data-probe c が "editor text" を受け取り、editor が先頭へ）、secret の除外（log に text 無し）、10 件の上限（item 12 〜 item 3） |
| `clip-lock.sh`（`build/ws102-p018-shots/lock2/`） | **PASS**: lock で `clear reason=lock count=1`、解除後 0 件、Log Out で `clear reason=logout count=1`。1 回目は試験の誤り（root の session と思っていたが、criteria の image は kei が自動で login する）で直した |
| host の libkeiui の回帰（`host-input`・`host-widgets`・`host-draw`・`host-chooser`・`host-inset`） | 全て **PASS** |
| C9 | 10 本全て **PASS**（`build/ws099/p018-regress/criteria/results.txt`） |
| WS079 `zdesktop-p010.sh` | **PASS** |
| boot test | **PASS**（`build/ws099/p018-regress/boot/login.png`） |

画面: `build/ws102-p018-shots/run2/three.png`（3 つの app の複写の後）・`pasted.png`（履歴の 2 番目を c に貼った後）・`ten.png`（12 回の複写の後）、
`build/ws102-p018-shots/lock/lock.png`。Q1 に送った。

## 残り

- text-input の purpose（password・PIN・hidden・sensitive の欄）による除外。IME の file の変更が要るので保留（上）。再検討のきっかけは「IME の作業がエージェントに戻ったとき」。
- 履歴の tab の UI（p016、P3）は `zwl_clipboard_history_count`・`_get`・`_paste` を使う。text の中身を描くのは keyboard の側。
