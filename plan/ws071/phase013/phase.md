<!-- awesome-plan project=zedbsd record=ws071p013 -->

# ws071-p013: タブ

Phase ID: `ws071-p013`
Parent: [WS071](../ws.md)
Status: cleared（2026-09-27、メインのセッションが実行）
Phase disposition: normal
Queue: なし（2026-09-27 15 時台、rate limit の reset までの間にユーザーの指示でメインのセッションが main の tree で実行）

## 範囲

2026-09-27 に元の p008 から分けた。[design.md](../design.md) §3（tab bar）、§10.1（File・Window のタブの項目）、spec §30:

- model: `fm_app` の `tabs[FM_TABS]`（8）に New Tab（今の場所を複製）、Close Tab（最後の 1 つなら窓を閉じる）、選択、前後。
  タブを替えるときは検索の走査を止め、Quick Look・Info・名前の編集を閉じ、folder と Home は読み直す（mtime の変化も拾う）。
- tab bar: タブが 2 つ以上のときだけ toolbar の下（高さ 34）に pill のタブ（場所の名前、今のタブは白、× で閉じる、click で選ぶ）。
  sidebar・content・preview はその分下がる。
- key と menu: Ctrl+T（New Tab）、Ctrl+W（Close Tab）、Ctrl+Tab・Ctrl+Shift+Tab（Next・Previous Tab）、Ctrl+PageDown・PageUp。
  File に New Tab・Close Tab、Window に Previous Tab・Next Tab（タブが 1 つなら無効）。folder の中 click（middle）で新しいタブに開く。

## 受け入れ

1. host の画面（タブ 2・3 つ、閉じた後）と action・key の試験。
2. Venus（QEMU）で Ctrl+T・click・Ctrl+Tab・Ctrl+W・menu の項目が動く。画面を撮る。
3. warning 0、`style-check.py` 0、回帰（p002〜p008、p012）PASS。

## 実装（2026-09-27）

- 新しい `userland/base/zdesktop-files/ui-tabs.c`: `fm_tabs_new`（今のタブの右に、場所を渡して開く。8 つまで）・`fm_tabs_duplicate`（New Tab、
  今の場所）・`fm_tabs_close`（最後の 1 つなら窓を閉じる request）・`fm_tabs_select`・`fm_tabs_step`（端で回る）・`fm_tabs_layout`・`fm_tabs_draw`。
  タブを替える・開くときは名前の編集を終え（入力は保つ）、Quick Look・Info を閉じ、focus を items へ。見せるタブは `fm_ui_reload` で読み直す
  （検索のタブは検索を始め直し、他のタブは走査を止める）。ログ `ZFILES TABS new|close|select`。
- tab bar: タブが 2 つ以上のとき窓の上端（toolbar は p014 で titlebar へ移ったので、元の設計の「toolbar の下」は窓の上端）に高さ 34 の
  pill（幅 90〜220 で等分、今のタブは白と影、hover は明るく、× の close button）。sidebar・content・preview は 44 px 下がる。
- key（`ui-input.c`）: Ctrl+T・Ctrl+W・Ctrl+Tab・Ctrl+Shift+Tab・Ctrl+PageDown・Ctrl+PageUp。middle click で folder の item か sidebar の
  場所を新しいタブに。click でタブを選ぶ、× で閉じる。
- menu（`menu.c`）: File に New Tab（Ctrl+T）・Close Tab（Ctrl+W）、Window に Previous Tab（Ctrl+PageUp）・Next Tab（Ctrl+PageDown）。
  New Tab は 8 つ未満、Previous・Next は 2 つ以上のとき有効（`fm_menu_state.tabs`）。Ctrl+Tab は window の key のまま（zdesktop に渡さない）。
- 変えた file: `files.h`（action 4 つ、`fm_menu_state.tabs`、prototype）、`ui.c`（layout と draw の呼び出し）、`ui-input.c`、`ui-menu.c`、
  `menu.c`、`Makefile`。試験: `plan/ws071/tests/host-p013.sh`（新）、`host-render.c`（`middle=`・`tabs`、state の `tabs=`）、
  `files-p013.sh`（新）、`files-regress.sh`（既定に p013）。

## 検証（amd64 だけ、2026-09-27 のユーザーの方針）

- host: `sh plan/ws071/tests/host-build.sh`（-Werror）通る。`host-p013.sh` 17/17 ok（new・step・close と request 4・middle・bar・state）、
  `host-p014.sh` PASS、`files-model` PASS。画面 `build/ws071-p013-host/{two,three,closed}.png`。
- guest（QEMU、Venus、lean image `sh plan/ws071/tests/build-files-image.sh build/main-files`、zdesktop-files の warning 0）:
  `files-p013.sh` PASS（Ctrl+T の menu の経路、sidebar、tab の click、Ctrl+Tab、Ctrl+PageDown の menu の経路、Ctrl+W、× の click、zdesktop の
  ERROR 無し）。画面 `build/ws071-shots/p013-20260927-venus-{two,three,closed}.png`。
- 規約: `ui-tabs.c` の style-check 0。変えた既存の file（ui.c・ui-input.c・ui-menu.c・menu.c・files.h）は 0 → 0。試験の `host-render.c` は
  53 → 55（既存の `else if (sscanf(...))`・`strcmp` の連鎖の書き方に合わせた 2 件）。
- 回帰（同じ image）: `files-regress.sh` の p002・p003・p004・p005・p006・p007・p012・p008・p014 PASS。boot test PASS（`build/ws071-p013-boot/login.png`）。
- 実機（i915）: 未実施。

## 残り

- Help の Shortcuts のカードにタブの key の行が無い（p011 の規約の照合のときに足す）。
- タブの drag での並べ替えは Future Work（ws070-p007 の設計のとおり）。
