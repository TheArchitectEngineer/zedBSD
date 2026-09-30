<!-- awesome-plan project=zedbsd record=ws102-p015 -->

# ws102-p015: keyboard の inset の知らせ

Status: cleared（2026-09-30、QEMU の Venus と host。実機は未実施。p007（作業の領域）の configure の前の呼び出しは p007 の仕事）
Disposition: normal
Parent: [WS102](../ws.md)
Queue: なし（2026-09-30 Q1 の割り当て、サブエージェント P6、worktree `ws035-keiland`（branch `wt/ws035`））
依存: p007（作業の領域）。p007 はまだで、p015 は keyboard の開閉の時に知らせる。p007 は configure の前に同じ関数を呼ぶ（下）

## 範囲と受け入れ

- design.md §2.8「keyboard による大きさの変更の知らせ」。ユーザー（2026-09-30）:「スクリーンキーボードの表示でサイズが変更されるとき、念のためウィンドウに
  特殊なXDGメッセージを送りましょう。対応しているウィンドウであれば、現在のキャレットを画面の中心など見やすい位置にセンタリングできる、という寸法です。
  libkeiuiの機能にしましょう。」
- 受け入れ: Text Editor の長い文書（100 行以上）の最後の方に caret を置いて QWERTY を開き、caret が keyboard の上の範囲の中央付近（±1 行）に見える
  （log と画面）。対応していない app（wlshm）は今までどおり動く。
- 回帰: host の libkeiui の試験、C9、WS079-p010、boot test。
- keyboard.c は P3・P4 も触るので、開閉の所で呼ぶ 2 行だけ。IME の file（ime.h・text-input.c・input-method.c）と seat.c の IME の hook は触らない（触っていない）。

## protocol（`keiland_keyboard_inset_v1`、version 1）

| object | request / event | 引数 |
| --- | --- | --- |
| `keiland_keyboard_inset_manager_v1`（global、zdesktop の 22 番） | 0 `destroy` | — |
| | 1 `get_inset` | `new_id keiland_keyboard_inset_v1`、`object xdg_toplevel` |
| `keiland_keyboard_inset_v1` | 0 `destroy`（request） | — |
| | 0 `inset`（event） | `int right`、`int bottom`、`uint reason`（0 なし・1 右の列（flick）・2 下の行（QWERTY・手書き）） |

- right・bottom は、その窓の本体のうち keyboard が覆う幅を、窓の右の辺・下の辺からの窓の画素で表す。覆わない窓と、閉じた時は 0。
- 送る時: keyboard が開く・閉じる時に、inset を作った全ての窓へ（今は keyboard.c の `keyboard_open`・`zwl_keyboard_close` から）。
  **p007 は、作業の領域の変化で窓の大きさ・位置を変える configure の前に `zwl_keyboard_inset_notify(server, panel)` を呼ぶ**（panel は keyboard の矩形、閉じる時は NULL）。
- 対応していない窓（inset を作らない app）には何も送らない。

## 変更

- compositor（`userland/desktop/wayland/`）:
  - `inset.c`・`inset.h`（新規）: request の処理、`zwl_keyboard_inset_notify`、窓ごとの覆う幅の計算（`zwl_glass_body_origin` と `zwl_surface_size`）。
    log `ZWL INSET create client= inset= toplevel=`・`ZWL INSET client= surface= right= bottom= reason=`。
  - `protocol.c`: global と dispatch。`zwl.h`: object の種類 2 つと `zwl_keyboard_inset_notify` の宣言。`objects.c`: toplevel が消える時に inset から外す。`Makefile`。
  - `keyboard.c`: 2 行（`keyboard_open` と `zwl_keyboard_close` で `zwl_keyboard_inset_notify`）。
- libwayland: `keyboard-inset-protocol.c`・`zed-keyboard-inset-v1-client-protocol.h`（新規、手書き）、`Makefile`、`exports.map`。
- libkeiland（**KEILAND_VERSION 18**。17 は ws075-p029 の `keiland_glass_set_blur` が先に使った）: `keyboard-inset.c`（新規）の
  `keiland_keyboard_inset_create(display, toplevel, callback, data)`・`keiland_keyboard_inset_destroy`。compositor に無ければ ENOTSUP で何もしない。
  `include/libc/keiland.h`、`Makefile`、`exports.map`。
- libkeiui（**KUI_VERSION 7**）:
  - `window.c`・`window.h`: `kui_window` が toplevel を作る時に inset を作り、event を受ける。app の callback（任意）`kui_window_on_keyboard_inset(window, fn, data)`
    （1 を返すと既定の動きをしない）、今の値の `kui_window_keyboard_inset(window, &right, &bottom)`。
  - `ui.c`・`internal.h`: 既定の動き。keyboard が来た・変わった（reason が 0 でない）後の次の `kui_ui_end` で、その frame の text の view
    （`kui_ui_text_region`。focus のある物、無ければ最後の物）の scroll を、caret の行が keyboard の残す範囲の縦の中央に来るよう動かす。
    caret は app が `kui_window_text_cursor` で伝えた矩形（無ければ view の `caret_rect` と指の caret）。文書の先頭・末尾を越えない、scroll しない view は動かさない。
    app の変更は要らない（Text Editor は変更なしで動いた）。
  - `include/libc/keiui.h`。

## 試験（`plan/ws102/tests/`）

- `host-inset.sh`・`host-inset.c`（host、ui.c）: 下の行・右の列・閉じる・一度だけ・末尾で止まる・app の caret が無い時の view の caret・scroll しない view の 10 項目。
- `inset-guest.sh IMAGE OUTDIR`（QEMU の Venus、`build-inset-image.sh` の image = WS079 の demo の image と Text Editor（`config-amd64-inset.mk`））:
  200 行の文書（150 行目だけ M の列）の 150 行目に caret を置き、QWERTY を開いて画面の M の行と、keyboard の上に見える text の最初と最後の行の中点を比べる（±1 行）。
  閉じた時の `reason=0`、wlshm（inset を作らない）が動き続け inset が作られないこと、error 0。

## 結果

| 確認 | 結果 |
| --- | --- |
| build: inset の image `build/ws102-p015`（`build/ws099/p015i.img`）、criteria の image（`build/ws099/p015i-criteria.img`） | どちらも exit 0、desktop の warning 0（noct の既存の 1 件は範囲外）、`git diff --check` 0 |
| `host-inset.sh` | **PASS** 10/10 |
| host の libkeiui の回帰: ws090 の `host-input.sh`・`host-widgets.sh`・`host-draw.sh`、`plan/tools/keiui/host-chooser.sh` | **PASS**（63/63・94/94・13/13・85/85） |
| `inset-guest.sh`（1 回目） | 仕組みは動いた（`ZWL INSET client=1 surface=16 right=0 bottom=319 reason=2`、caret の行が中央）が、試験の側の誤り 2 つ（文書の末尾の改行で caret が 151 行目になり M の行とずれた、title bar の文字を text の行と数えた）で判定が FAIL、しかも `tee` で終了の値が消えて全体は PASS と出た。3 つとも直した |
| `inset-guest.sh`（2 回目、`build/ws102-p015-shots/run2/`） | **PASS**: QWERTY で `bottom=319 reason=2`。M の行（caret の行）の中心 y=292、keyboard の上に見える text（144〜156 行）の中点 y=278、差 0.54 行（±1 行以内）。閉じて `bottom=0 reason=0`。wlshm は inset を作らず動き続けた（log 47 → 61 行）。error 0 |
| C9（p052・p053・p072・p076・p126・p128・p134・p137・p138・cursor-owner） | 全て **PASS**（`build/ws099/p015i-regress/criteria/results.txt`） |
| WS079 `zdesktop-p010.sh`（inset の image） | **PASS** |
| boot test | **PASS**（`build/ws099/p015i-regress/boot/login.png`） |

画面: `build/ws102-p015-shots/run2/before.png`（keyboard の前、caret の 150 行目は view の上端の近く）、`after.png`（QWERTY の上の 144〜156 行の中央に 150 行目）、`wlshm.png`。

## 制限・残り

- 既定の動きの状態（最後の inset）は libkeiui の中に process で 1 つ。1 つの process に text の view を持つ窓が 2 つある時、inset を受けた窓でない方の `kui_ui_end` が先に動くと、その窓の view を寄せる（Text Editor・Notes のように窓が 1 つの app では起きない）。
- 浮いた窓の覆う幅は今の位置から計算する。p007 で窓が動く・縮むときは、p007 が configure の前に新しい位置・大きさで呼ぶ必要がある（今の関数は窓の今の位置を読むので、p007 は位置を変えてから呼ぶか、関数に矩形を渡す形に広げる）。
- 全画面の窓にも送る（覆う幅は窓の本体の下の辺から）。
- 実機（5330）は未実施。
