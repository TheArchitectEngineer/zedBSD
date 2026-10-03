<!-- awesome-plan project=zedbsd record=ws102-p020 -->

# ws102-p020: QWERTY の補助の key の列

Status: cleared（2026-09-30、QEMU の Venus と host。実機は未実施）
Disposition: normal
Parent: [WS102](../ws.md)
Queue: main の依頼（2026-09-30、p021 の後。worktree `.claude/worktrees/ws102-keyboard`、branch `wt/ws102`）

## 範囲と受け入れ

design §2.10 の QWERTY の補助の key の列を足す（ユーザー「Termux の補助キーは QWERTY の方がいいかも」）。

- key: Esc・Tab・Ctrl・`|`・`~`・矢印。
- 一度だけ効く修飾（Ctrl・Alt）。

受け入れ（ws.md の行）: 補助の列の key が働き、Ctrl・Alt が次の 1 key にだけ効く（guest の Text Editor と log）。

## 実装

| file | 内容 |
| --- | --- |
| `keyboard.h`・`keyboard-layout.c` | QWERTY の両方の面の一番上に、補助の列（`ZWL_QWERTY_EXTRA_ROW`）を足した。行は 6 になった（`ZWL_QWERTY_ROWS`）。<br>並び: Esc・Tab・Ctrl・Alt（4 単位）、`\|`・`~`・`/`・`-`・Home・End・PgUp・PgDn（3 単位）。<br>key の code: Esc 1・Tab 15・Home 102・End 107・PgUp 104・PgDn 109。<br>`ZWL_FLICK_CTRL`・`ZWL_FLICK_ALT` |
| `keyboard.c` | 補助の列の高さは、他の行の 0.7。QWERTY の panel の高さの割合は 38% から 42%（最大 460 px）にした。そうしないと、行が増えて key が低くなる。<br>Ctrl・Alt は、押すと次の 1 key にだけ modifiers を足し（`held`）、その key で消える。もう一度押すと取り消す。押している間は淡い青。<br>`keyboard_send_key` は、Shift と held を合わせた modifiers で key を送り、送った後に戻す。log は `send via=key code= shift= held=`、`ZWL OSK held=`。panel を閉じると held は消える |

design との差: 矢印は p006 から space の行にあるので、補助の列には Home・End・PgUp・PgDn と `/`・`-` を置いた（Termux の補助の列に近い）。IME の file と seat.c は変えていない。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make … bin/wayland …` | rc 0、warning 0 |
| style | `plan/tools/style-check.py keyboard.c keyboard-layout.c keyboard.h`、`git diff --check` | 0 件 |
| host | `sh plan/ws102/tests/host-keyboard.sh` | PASS（6 行が 40 単位・12 key 以内、全ての文字に US の key） |
| guest（pen image の複写、1280x800） | `osk-guest.sh build/ws102-shots/p020-final2 install start pointer flick edges touch send close qwerty hand extra` | PASS（新しい手順 extra と、それまでの全ての手順） |
| guest（1920x1080） | `… install large` | PASS（QWERTY は `x=0 y=627 width=1920 height=453`） |
| 回帰 | WS079-p010、WS099 の C9（10 本）、boot test | PASS |

手順 extra で確かめたこと（Text Editor）:

- b・c・Home・a・End・`|`（補助の列）・Tab を打つと、file が `abc|<tab>` になった。
- Ctrl を押すと `held=4` になり、次の a は `code=30 held=4`（全選択）で送られた。その次の z は `held=0` で送られ、全選択を置き換えて file は `z` になった。
- Alt を 2 回押すと取り消された。Alt・Esc で、Esc は `held=8` で送られた。
- QWERTY の 30 文字の手順（p006）も、行が増えた後で 30 文字 4.9〜5.1 秒・誤り 0 のまま。

画面: `build/ws102-shots/p020-final2/extra.png`。最大化の画面（ユーザー向けの依頼）: `build/ws102-shots/maximized/maximized-qwerty-*.png`。

## 見つけたこと・制限

- 試験を直した: guest の file を SSH で読むと、まれに空が返る（1 回、手順 send で起きた）。読み直す `read_file` と、key の場所の読み直しを足した。
- held は key として送るときだけ効く。text-input の commit（かな）には効かない。
- 実機・Windows の上の QEMU は未実施。

## Resume point

2026-09-30: cleared。次は p007（作業の領域）。
