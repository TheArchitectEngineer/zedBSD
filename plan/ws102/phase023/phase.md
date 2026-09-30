<!-- awesome-plan project=zedbsd record=ws102-p023 -->

# ws102-p023: Text Editor の本当の編集の状態

Status: cleared（2026-09-30、サブエージェント P6、worktree `ws035-keiland`（branch `wt/ws035`）。QEMU の Venus と host。実機は未実施）
Disposition: normal
Parent: [WS102](../ws.md)
Queue: なし（2026-09-30 Q1 の割り当て）
依存: p017（cleared）

## 範囲と受け入れ

- Text Editor が `kui_window_edit_state`（KUI_VERSION 8、p017）で、選択がある・貼れる・取り消せる・やり直せるを言い、keyboard の編集の button の灰色（`zwl_edit_state`）を正しくする。
- 版は上げない（KEILAND_VERSION 19・KUI_VERSION 8 のまま）。

## 変更

- `userland/desktop/textedit/main.c`: `main_edit_state`（新規、static）。main loop の各 pass で、menu と title bar の状態（`main_state`）から
  選択（`selected`）・取り消し（`can_undo`）・やり直し（`can_redo`）を、`kui_window_can_paste` から貼れるかを取り、`kui_window_edit_state` で言う。
  libkeiui は、変わった時だけ compositor へ送る。

## 試験

- 新規 `plan/ws102/tests/edit-state-guest.sh IMAGE OUTDIR`（QEMU の Venus、p015 の inset の image）: 空の clipboard の compositor と、"abc" の Text Editor で次を確かめる。
  | 手順 | state の flags | 試験の口（Super+Alt+Q）で見る enabled |
  | --- | --- | --- |
  | 開いた時 | `0x0` | `0x60` |
  | 全選択 | `0x1` | — |
  | 複写 | `0x3` | — |
  | x を打つ | `0x6` | `0x6c` |
  | 取り消し | `0xb`（やり直せる） | — |
- `edit-guest.sh`（p017）の期待値を、本当の状態に合わせて直した。選択の途中は `0xbb` → `0xa3`（取り消し・やり直し・貼り付けが灰色）、複写の後は `0x7f` → `0x67`。

## 結果

| 確認 | 結果 |
| --- | --- |
| build（`build/ws099/p023.img`） | exit 0、`userland/desktop` の warning 0、`git diff --check` 0 |
| `edit-state-guest.sh`（`build/ws102-p023-shots/run1/`） | **PASS**（上の表の全て） |
| `edit-guest.sh`（直した期待値、`build/ws102-p023-shots/edit/`） | **PASS**（`0xa3`・`0x67`、"HELLO"、Terminal `0x5`、wlshm `0x3f`） |
| `inset-guest.sh`（p015） | **PASS**（中央から 0.54 行） |
| Text Editor の host の試験 `plan/tools/textedit/host-core.sh` | **PASS** 34/34 |
| C9・WS079-p010・boot test | compositor は p018 から変わっていないので、p018 の実行（全て PASS）を使う。Text Editor は C9 の image に無い |

画面: `build/ws102-p023-shots/run1/selected.png`（全選択）・`typed.png`（x を打った後、Undo が有効で Redo が灰色）・`undone.png`。

## 残り

- 無し。Text Editor 以外の libkeiui の app は、言わなければ p017 の既定のまま（選択・取り消し・やり直しを「ある」とする）。
