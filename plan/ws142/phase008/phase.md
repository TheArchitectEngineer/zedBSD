<!-- awesome-plan project=zedbsd record=ws142-p008 -->

# ws142-p008: 最大化を desktop の session の状態にする（layout_mode と切り替えの入口の集約）

Status: in-progress（2026-10-06 q781-i01 P2: 実装・build・host 試験まで。QEMU は p010 の AAT のシナリオとまとめて T1 に依頼する。結果の判定まで cleared にしない）
Disposition: normal
Parent: [WS142](../ws.md)
Queue: q781 / q781-i01（2026-10-06 user「このqueueで実行してください。」）
Design: [ws142-p007](../phase007/phase.md) §1 と 2026-10-06 の決定（DOCKED の間は他の app を描かない、中央の窓）
Related: [BUG-217](../../bugs/BUG-217.md)

## 範囲と受け入れ（p007 の表の p008）

- session に 1 つの `layout_mode`（WINDOWED / DOCKED）。窓を dock すると DOCKED、dock を外すと WINDOWED。
- app の切り替え（bar の icon・preview、Alt+Tab・3 本指の tap の switcher、WiseView の Enter・click、activation）は 1 つの関数 `zwl_glass_switch_to` に集め、切り替え先の窓を mode に合わせる（DOCKED なら dock、WINDOWED なら前の浮いた位置へ）。
- 新しい窓は DOCKED なら dock で開く（大きさの固定の窓も。親を持つ dialog・sheet と全画面は除く）。
- 全画面から出る時は mode に戻る（BUG-208 の `fullscreen_docked` の役は「前に dock だったので浮いた位置は restore_*」だけに）。
- DOCKED の間は今の app（前の窓の client）以外の窓を描かず、press も受けない。App Home・WiseView・switcher の間は全てを描く。
- 大きさの固定の dock した窓は dock の領域の中央に自分の大きさで、周りは暗い地。親を持つ dialog は DOCKED の間は画面の中央に、dock した親の sheet（File Chooser）も中央に。
- 中央の窓の下の blur は ws142-p008b（Q1 の分け方）。

## 判断（Q1 経由、2026-10-06）

- (a) WINDOWED の時、後ろに dock のまま残った別の app の窓を click で前に出すのも切り替え（窓に戻す）。その press は client に渡さない。Q1「案のとおりでよい」。
- (b) DOCKED の間に前の窓を閉じる・最小化した時に次に前に来る窓を dock するか: ユーザーの返事待ち。今は変えない（次の窓はそのまま）。
- Super+↑ で dock する key は無い（p007 の表の Super+↑ は無い操作として扱う、Q1）。

## 実装（2026-10-06）

| 所 | 内容 |
| --- | --- |
| `userland/desktop/wayland/layout.c`・`.h`（新、純粋） | mode の規則: `zwl_layout_switch_action`（keep・dock・float）、`zwl_layout_opens_docked`、`zwl_layout_unfullscreen_docked`、`zwl_layout_centred`、`zwl_layout_hidden`、`zwl_layout_centre`、`zwl_layout_name` |
| `shell.c` | `layout_set`（`ZWL LAYOUT mode=docked|windowed reason=VIA at_ms=`、`window_dock`・`window_undock` の最後で）、`layout_match`（`ZWL LAYOUT switch surface= action= mode= via= client=`、dialog・sheet は親で）、`zwl_glass_switch_to`（旧 `zwl_glass_bring`、`ZWL APPS raise` の行は保つ）、WiseView の選択も switch_to（via=wiseview）、`layout_hides`（`window_shown` と `window_at`）、`layout_takes_press`（DOCKED の letterbox の press は何にも届かない）、`layout_press_switches`（判断 (a)、via=press）、`docked_body`（固定の大きさの窓の中央、`body_rect`・dock の animation の行き先）、暗い地の描画、`zwl_glass_open_docked` を mode で言い直し（固定の大きさも dock、log の行は同じ形）、`zwl_glass_place` の dialog の中央、`sheet_anchor`・`sheet_centred`・`draw_sheet` の中央、`zwl_glass_unfullscreen_docks`、`dock_restore_default` |
| `protocol.c` | `zwl_window_leave_fullscreen` が `zwl_glass_unfullscreen_docks` で mode に戻る。前に dock で今 WINDOWED なら restore_* の浮いた位置へ |
| `zwl.h` | `layout_mode`、宣言、`fullscreen_docked` の注記 |
| `apps-bar.c`・`switcher-shell.c` | `zwl_glass_switch_to` へ |
| `Makefile`・`Makefile.linux`・`Makefile.freebsd` | `layout.c` |
| `plan/tools/aat/scenarios/aatlib.py`・`common.py` | `back_to_windowed`: 各シナリオの後、mode が docked なら bar の restore の button で windowed に戻す（次のシナリオが浮いた窓で始まるように） |

## 確認

| 確認 | 結果 |
| --- | --- |
| zedBSD の compositor（`make ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/p2-b194 build/p2-b194/bin/wayland`） | 成功、warning 0（-Werror） |
| Linux の Keiland（`make keiland-linux KEILAND_LINUX_BUILD=build/p2-keiland-linux`） | 成功、warning 0 |
| `sh plan/ws142/tests/run-host-layout.sh`（gnu11 と `-std=c89 -pedantic`） | 57 checks ok: 6 種の窓 × 2 mode の switch・open・全画面の後・中央、hidden の 5 通り、中央の計算、名前 |
| `run-host-switcher.sh` | 25 checks ok |
| `plan/tools/style-check.py`（layout.c・.h・host-layout.c・shell.c・protocol.c） | 指摘 0 |
| `plan/tools/keiland-os-boundary/check.sh` | FAIL は既存の物だけ（C4 account-zedbsd.c、M1 shot-none.c、B2 build/keiland-linux の kl_tr）。layout.c は 3 つの Makefile に揃っている |
| `sh plan/tools/aat/tests/run-host.sh`・`check-scenarios.py` | PASS |
| FreeBSD の build | 未実施（この host に FreeBSD の環境なし） |
| QEMU | 未実施。p010 で AAT のシナリオ（layout-mode-switch など）と既存の bug194-guest.sh 5.・p005-guest.sh とまとめて T1 に依頼 |

## 残り

- 判断 (b) の返事。
- p008b（中央の窓の下の blur）、p009（gesture）、p010（AAT のシナリオ・T1・規約の見直し）。
