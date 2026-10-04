<!-- awesome-plan project=zedbsd record=ws142-p004 -->

# ws142-p004: 上部のバーのアプリの一覧とプレビュー

Status: cleared（2026-10-05 Q1: T1-131 PASS（17 行すべて ok: 開いた順・hover のプレビュー・click・drag の並べ替え・デスクトップごとの順・docked で出さない）、preview-hover.png を Q1 が目視（B の 2 窓のプレビュー、1 枚は画面の約 25%））。以前: in-progress（2026-10-05 P1 generation17。実装・build・host の試験まで。QEMU は Q1 経由で T1 に依頼。結果の判定と実機の UAT まで cleared にしない）
Disposition: normal
Parent: [WS142](../ws.md)
Queue: Q1 の指示（2026-10-05、D2・D4〜D8・D11 と並べ替えの要望は決定済み）

## 範囲と受け入れ

- 窓を最大化（dock）していない時、バーに今のデスクトップのアプリの icon（D5）。app_id ごと、無ければ client ごと（D7）。並びは開いた順で、icon の drag で並べ替えられ、デスクトップごとに持つ（2026-10-05 の追加の要望、D2 の「開いた順」は初期の順）。
- hover 400 ms でその窓のプレビュー（1 枚は出力の幅・高さの 25% まで、D4）、複数の窓は並べる。離れて 300 ms で閉じる（D8）。
- icon の click: 窓 1 つなら最前面、複数なら待たずにプレビュー。プレビューの click でその窓を最前面。
- 最小化の窓も出し（薄く）、選べば戻す（D11）。docked の窓の題がバーにある時は出さない（D6）。fullscreen の時はバー自体が無い。
- build warning 0、host の試験、境界の検査、QEMU（T1）、実機（UAT）。

## 実装（2026-10-05）

| 所 | 内容 |
| --- | --- |
| `userland/desktop/wayland/apps.c`・`.h`（新、純粋） | `zwl_apps_build`: 窓をアプリにまとめ（key は app_id か `client:N`）、デスクトップの並び（`struct zwl_apps_order`）で並べ、知らないアプリは右に開いた順で足し、並びを書き戻す（閉じたアプリは並びから消える）。各アプリの窓は最後に前に出た順（16 まで、溢れたら古い物を落とす）、全部最小化なら minimized、MRU の順（`recent`、p005 の切り替え用）。`zwl_apps_move`（drag）、`zwl_apps_find`、`zwl_apps_key` |
| `apps-bar.c`（新） | bar の icon（36 px の枠に 28 px の mark、今の窓のアプリに点、全部最小化は薄く、入りきらなければ最後が「+N」で click で Wiseview）、hover の状態機械（IDLE・ARMED・SHOWN、via hover・click）、プレビューの panel（icon の下 8 px、ガラス、Wiseview の tile、1 行に収まらなければ半分まで縮めて折り返す）、icon の press・click・drag（8 px で drag、指の下の位置へ live に移る）、プレビューの click で最前面、× で閉じる、Esc と他の所の press で閉じる（press はそのまま先へ）。窓の move・pull・デスクトップの swipe の間は hover を始めない。log `ZWL APPS bar …`・`icon …`・`preview app=… windows=… via=…`・`preview window surface=…`・`preview close via=…`・`raise surface=… via=bar|preview`・`reorder app=… place=… desktop=…` |
| `shell.c` | `zwl_glass_apps_room`（glass の窓 mode、greeter・lock・fullscreen・docked・Home・Wiseview が無い時、launcher の線の後から desktops の線の前まで）、`zwl_glass_bring`（最小化から戻して最前面、focus）、`zwl_glass_open_wiseview`、`zwl_glass_draw_app_mark`（大きさ付きの mark）、`zwl_glass_draw_preview`（Wiseview の tile）。`draw_system_bar` で docked の題の代わりに icon と線、`zwl_glass_draw` で panel（窓の上・menu の下）、motion・button・key（Esc）・tick の入口 |
| `zwl.h`・`glass.h`・`display.c`・`Makefile*` | `struct zwl_apps_bar`（デスクトップ 4 つの並び、状態）、窓の `open_order`（最初の map の map order）、宣言、source の追加（zedBSD・Linux・FreeBSD） |

## 確認

| 確認 | 結果 |
| --- | --- |
| zedBSD の compositor（`make BUILD=build/q713 build/q713/bin/wayland`） | 成功、warning 0 |
| Linux の Keiland（`keiland-linux.mk all`） | 成功、warning 0 |
| `sh plan/ws142/tests/run-host-apps.sh`（ASan・UBSan でも） | 32 checks ok: まとめ方（同じ app_id の 2 client は 1 つ、app_id 無しは client ごと）、開いた順、窓の順、MRU、全部最小化だけ minimized、drag の並びが次の build で保たれる（左端・右端）、新しいアプリは右、閉じたアプリは並びから消え、また開くと右、デスクトップごとに別の並び、範囲外の move を拒む、key、find、上限（32 アプリ、16 窓で最新を残す）、窓なし |
| `sh plan/ws142/tests/run-host-gesture.sh`・`run-host-apps.sh` | ok（host-gesture.c の規約の指摘も直した） |
| ws131 `host-system.sh`・ws102 `host-keyboard.sh`（zwl.h を使う host の試験） | PASS |
| style-check（新しい file と変えた hunk） | 指摘 0 |
| `plan/tools/keiland-os-boundary/check.sh` | PASS |
| QEMU（`plan/ws142/tests/p004-guest.sh`） | **未実施**。T1 に依頼（Q1 経由） |
| 実機（5330） | **未実施**（UAT） |

## QEMU の試験（T1 への依頼）

- image: pen の image（`plan/ws079/tests/build-pen-image.sh BUILD`、main の最新）。`plan/ws079/tests/pen-guest.sh start IMAGE`。
- 試験: `plan/ws142/tests/p004-guest.sh BUILD [OUTDIR]`（BUILD の bin/wayland を写す。wltest `--app-id` で apps.a・apps.b（2 窓）・apps.c、デスクトップ 2 に apps.d）。
- 合格: 全行 ok（bar-opening-order、icons-left-to-right、hover-preview、hover-leaves、click-single-raises、click-preview、preview-raises、second-click-hides、escape-hides、drag-reorders、drag-bar、desktop2-bar、desktop1-order-kept、docked、docked-no-preview、alive、no-error）。`preview-hover.png`（apps.b の 2 枚のプレビュー）と `docked.png`（題がバーにあり icon が無い）を目で見る。

- 2026-10-05 T1-134（p005）で分かった試験の誤り: surface の番号は client ごとで、p004-guest.sh の click-single-raises・preview-raises・dock の段も surface の番号だけで窓を探していた（偶然一致しうる）。log の行に `client=N` を足し、client で探す形に直した。再試験は T1。

## 残り

- QEMU の結果の判定、実機の UAT（hover の時間、プレビューの大きさ、drag の感じ）。
- p005: 切り替え（Alt+Tab・TAP3・2 本指）。`zwl_apps` の `recent` と panel の描画を使う。

## Q1 の判定（2026-10-05）

T1-131 PASS（17 行すべて ok: 開いた順・hover のプレビュー・click・drag の並べ替え・デスクトップごとの順・docked で出さない）、preview-hover.png を Q1 が目視（B の 2 窓のプレビュー、1 枚は画面の約 25%）。**cleared**。触った感じは 5330 の UAT。
