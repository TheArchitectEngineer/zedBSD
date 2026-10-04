<!-- awesome-plan project=zedbsd record=ws142-p005 -->

# ws142-p005: 切り替えの UI（Alt+Tab・3 本指の tap・2 本指、中央の popup）

Status: in-progress（2026-10-05 P1 generation17。実装・build・host の試験まで。QEMU は Q1 経由で T1 に依頼。結果の判定と実機の UAT まで cleared にしない）
Disposition: normal
Parent: [WS142](../ws.md)
Queue: Q1 の指示（2026-10-05、D1・D2 決定済み、fullscreen の間は動かさない）

## 範囲と受け入れ

- Alt+Tab、またはタッチパッドの 3 本指の tap（D1）で、今のデスクトップのアプリを最近使った順（D2）に取り、今のアプリの 1 つ次（直前に使ったアプリ）を選ぶ。
- 進める: Tab・→（Shift+Tab・← で戻る）、3 本指の tap をもう一度、2 本指で左右（12 mm で 1 歩、指の向き）。端で一周。
- 決める: Alt を離す、Enter、pad から開いた時は 1 本指の tap か pad の click。プレビューの click はその窓、中央の icon の click はそのアプリ。
- やめる: Esc、鍵盤から開いた時に他の所の click。
- 置き場: docked の窓が無い時はバーのその icon の下（p004 のプレビューと同じ、via=switch）、docked の窓がある時（D6）とバーに icon の無いアプリ（「+N」）は中央。fullscreen・greeter・lock・App Home・Wiseview の間は開かない（出ていたら閉じる）。
- build warning 0、host の試験、境界の検査、QEMU（T1）、実機（UAT）。

## 実装（2026-10-05）

| 所 | 内容 |
| --- | --- |
| `userland/desktop/wayland/switcher.c`・`.h`（新、純粋） | `zwl_switcher_open`（アプリの MRU の key を写し、index 1、1 つなら 0、0 個なら開かない）、`zwl_switcher_step`（一周、大きな数も）、`zwl_switcher_travel`（12 mm で 1 歩、余りは次へ、両向き）、`zwl_switcher_selected`、`zwl_switcher_close` |
| `switcher-shell.c`（新） | 開く・進める・決める・やめる、key（Alt+Tab・Shift、ON の間の Tab・矢印・Enter・Esc・Alt の解放（Alt の解放は client にも渡す））、button（プレビュー・中央の icon・それ以外は pad なら決める・鍵盤ならやめる。取った press の release は窓に渡さない）、pad の 2 本指の scroll（natural を戻して指の向き、縦も含めて client に渡さない）、tick（出せなくなったら閉じる）、中央の描画（選んだアプリのプレビューの下に MRU の icon の列、選んだ物を光らせる、最小化は薄く）。log `ZWL SWITCH open via=keys|pad index= app= placement=bar|center count=`・`step index= app= via=`・`center app=`・`commit app= surface= via=`・`cancel via=` |
| `apps-bar.c`・`apps-bar.h`（新） | p004 の部品を切り替えと共有: アプリの集め方（`zwl_apps_view_collect`、バーの場所が無くても）、`zwl_apps_view_build`、プレビューの配置（`zwl_apps_tiles_layout`）、`zwl_apps_bar_panel`、`zwl_apps_bar_show`（via=switch）・`hide`。切り替えが ON の間はバーの hover が状態を変えない |
| `shell.c` | `zwl_glass_switch_place`（出してよいか、bar か center か）、key・button の入口（先頭）、tick、中央の描画の呼び出し、gesture: TAP3 で開く、ON の間は TAP3 で進め、他の gesture は始めない |
| `input.c` | touchpad の action に pad を渡し、SCROLL を ON の間は切り替えへ |
| `zwl.h`・`glass.h`・`Makefile*` | `struct zwl_switcher switcher`・`switch_swallow`、`ZWL_APPS_VIA_SWITCH`、宣言、source の追加 |

## 確認

| 確認 | 結果 |
| --- | --- |
| zedBSD の compositor（`make BUILD=build/q713 build/q713/bin/wayland`） | 成功、warning 0 |
| Linux の Keiland（`keiland-linux.mk all`） | 成功、warning 0 |
| `sh plan/ws142/tests/run-host-switcher.sh`（ASan・UBSan でも） | 20 checks ok: 0 個は開かない、1 個はそれ、3 個は直前のアプリ、MRU の順、一周（前後・大きな数）、pad の travel（6 mm は 0 歩、合わせて 12 mm で 1 歩、25 mm 左で 2 歩戻る、余り）、開いている間は順が変わらない、閉じた後は何もしない |
| `run-host-apps.sh`・`run-host-gesture.sh`、ws131 `host-system.sh`、ws102 `host-keyboard.sh` | ok・PASS（p004 の部品の切り出しの後も） |
| style-check（新しい file と変えた hunk） | 指摘 0 |
| `plan/tools/keiland-os-boundary/check.sh` | PASS |
| QEMU（`plan/ws142/tests/p005-guest.sh`） | **未実施**。T1 に依頼（Q1 経由） |
| 実機（5330） | **未実施**（UAT） |

## QEMU の試験（T1 への依頼）

- image: pen の image（`plan/ws079/tests/build-pen-image.sh BUILD`、main の最新）。`plan/ws079/tests/pen-guest.sh start IMAGE`。
- 試験: `plan/ws142/tests/p005-guest.sh BUILD [OUTDIR]`（wltest `--app-id` で apps.a・apps.b（2 窓）・apps.c、QMP の key、touchinject の pad）。
- 合格: 全行 ok（keys-open、keys-bar-preview、keys-tab、keys-shift-tab、keys-commit、keys-raise、escape-cancels、escape-brings-nothing、quick-back、pad-open、pad-step、pad-commit、docked、center-open、center-shown、center-commit、fullscreen-no-switcher、alive、no-error）。`switch-bar.png`（apps.b の icon の下に 2 枚）と `switch-center.png`（中央の popup）を目で見る。

## 残り

- QEMU の結果の判定、実機の UAT（Alt+Tab の慣れ、2 本指の 12 mm、3 本指の tap）。遅れの目標（操作から最初の frame まで 100 ms）は log の `at_ms` で T1・UAT で見る。
- p006: 全文の規約の確認と QEMU の回帰。
