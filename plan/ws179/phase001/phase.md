<!-- awesome-plan project=zedbsd record=ws179-p001 -->

# ws179-p001: アクセントカラーの選択

Status: cleared（2026-10-07 Q1 の判定: T1-335・339（QEMU、light の 4 色と dark の 5 色の Settings・Files・検索・OSK・bar、主の button の文字、App Home の tile が変わらない、Files の desktop の pill が選び直さずに変わる）を Q1 が PNG で目視。greeter は未撮影（設計で accent に従わせない））（旧: in-progress（q833、P1。2026-10-07 実装済み、host 試験 PASS、T1 の撮影待ち））
Disposition: normal
Parent: [WS179](../ws.md)

## 範囲

ws.md の目標のとおり（8 色の固定、ライト・ダークで contrast を確かめた値、Settings の Appearance、libkeiland と compositor の UI が従う）。KL_VERSION は「次の番号」。

## 確認

host の描画（各色 × ライト・ダーク）、build、T1 で Settings の Appearance で色を変えた時の Files・Settings・bar の撮影。

## 記録

- 2026-10-07 範囲（Q1 の ACK）: p001 は libkeiland・compositor の UI・Settings・Files。6 app は p002。App Home の app の tile の色は従わせない。
- 設計: [design.md](../design.md)（§1〜§8 が第 1 版、§9・§10 が review の反映と人の判断。§9 が §2〜§7 を読み替える）。
- 2026-10-07 人の判断 6 点はユーザーが全部推奨どおりに回答（design.md §10.1）。

## 実装（2026-10-07、P1）

- 表: `userland/desktop/artwork/accent.h`（`ka_accent_values`、static inline、libkeiland と compositor が include、`kl_` の名でないので os-boundary の B2 に掛からない）。
- protocol: `libwayland/theme-protocol.c`・`zed-theme-v1-client-protocol.h`（version 2、event `accent` は `"2u"`）、compositor `wayland/protocol.c`（global 2）・`theme.c`（version 2 以上の object にだけ `accent`、log `KWL THEME appearance=N accent=M`、`kwl_accent_colour`・`kwl_accent_as_is`・`kwl_accent_done`）・`settings.c`（`appearance.accent`）・`kwl.h`（`server->accent`）、`settings-keys/settings-keys.c`（`appearance.accent` INT 0〜7 既定 0）。
- libkeiland: `keiland.h`（KL_VERSION 57、`KL_ACCENT_*`・`KL_ACCENTS`・`struct kl_accent`・`kl_accent_get`・`kl_accent_values`、`kl_theme` の末尾に `accent_ink`・`accent_text`）、`exports.map`（exports.py で作り直し）、`appearance.c`（version を広告と 2 の小さい方で bind、`accent` を受けて theme を作り直し watch を呼ぶ）、`ui/theme.c`（`keiui_theme_set(appearance, accent)`・`keiui_theme_with`・`kl_accent_values`）、`ui/widgets.c`（主の button の ink、陰は ink から遠ざかる向き、無効の主の button の ink、focus の輪）、`ui/list.c`・`views.c`・`chooser-view.c`（accent の上の ink、今の場所の文字は `accent_text`）。
- compositor の UI: `titlebar-shell.c`（suggestions の行、checked の control、検索の欄の縁、欄の選択、progress、tab の印、drop の縁と面）、`keyboard.c`（OSK の押した key 8 か所、`keyboard_draw_key` で as_is）、`network.c`（行の帯と ink、Wi-Fi の switch、key の欄の縁、Disconnect、bar の icon の背）、`volume.c`（bar の icon の背、slider、switch）、`menu-shell.c`（menu bar の開いた項目、popup の選んだ行）、`switcher-shell.c`、`shell.c`（desktop の点、dock の縁、Wiseview の glow、drag の badge の線）、`home.c`（page の点）。greeter・mark・App Home の tile の色は変えない。
- Settings: `palette.c`（`se_palette_set` が theme の accent・selection・`accent_text` を取る）、`settings.h`（`accent_text`・`SE_COLOR_ACCENT_TEXT`・`se_look.accent`）、`look.c`（`appearance.accent` を読む）、`page-look.c`（Appearance に「Accent color」の card、8 つの丸、選んだ丸に輪と ink の点、色の名、click で保存、log `ZSETTINGS ACCENT index=N`）、`ui.c`・`page-network.c`（選んだ行の文字は `accent_text`）、`locale/settings.keys`・`locale/ja/settings.tr`（9 語）。
- Files: `palette.c`・`files.h`（`fm_palette_set`・`fm_palette_take`、`accent_ink`・`accent_text`）、`main.c`（window は log に accent、desktop mode は `kl_settings` の `appearance.accent` を watch して light の値、log `ZFILES ACCENT index=N`）、`ui-desktop.c`・`ui-desktop-drag.c`・`ui.c`・`ui-home.c`・`ui-field.c`・`ui-overlay.c`・`ui-tabs.c`・`ui-grid.c`・`ui-drag.c`。

### 確認（host）

| コマンド | 結果 |
| --- | --- |
| target の clang（amd64、`-Werror -fsyntax-only`）: 変えた desktop の C 全部 | warning 0 |
| host の gcc（Linux の build の flag）: libkeiland・Settings・Files・compositor の主な 12 file | warning 0 |
| `sh plan/ws179/tests/run-host-accent.sh build/p1-accent/host-accent`（新規） | 74 checks, 0 failures、HOST-ACCENT PASS（基準 (1)〜(3)、blue が前の値、theme・Settings・Files の palette が accent を取る、範囲外は blue） |
| `sh plan/ws179/tests/run-host-accent-widgets.sh build/p1-accent/aw`（新規） | HOST-ACCENT-WIDGETS PASS、`build/p1-accent/aw-light.png`・`aw-dark.png`（8 色 × 主の button・switch・slider・欄） |
| `sh plan/ws089/tests/run-host-dark.sh` | HOST-DARK PASS（72 pairs） |
| `plan/ws090/tests/host-widgets.sh`・`host-input.sh` | 94/94、78/78 |
| `plan/tools/files/host-build.sh` | build 成功、warning 0 |
| `python3 userland/desktop/libkeiland/exports.py --check`、`tools/i18n/tr.py check …/ja/settings.tr` | 一致、107 件 0 problems |

未実施: QEMU（T1: Settings の Appearance で色を変え、Settings・Files・bar・titlebar・OSK を撮る）、実機。

## T1-335 の FAIL の疑いの直し（2026-10-07 夕、P1）

- 所見（T1-335）: Files の desktop の icon の pill が accent を purple・graphite にしても青のまま、`ZFILES ACCENT index=` が log に無い。
- 原因（code の読み）: desktop mode の Files は `main_accent_settings` を `kl_settings_watch` していたが、main の loop が `kl_settings_dispatch` するのは `main_language`（window の時だけ開く）だけで、`main_accent_settings` の queue を一度も dispatch していなかった。watch は呼ばれず、起動の時の accent（blue）のまま。もう 1 つ、desktop は前の frame を残して変わった cell だけを描き直す（ws094-p009）ので、選ばれたままの icon の pill は accent が変わっても描き直されない。
- 直し（`userland/desktop/files/main.c`）: loop で `main_accent_settings` も dispatch する。`main_desktop_accent_changed` で `fm_desktop_repaint`（残した frame を捨て全体を描く）。
- 起動の時の `ZFILES ACCENT index=0` の 1 行も log に無かった点は、code の上は起動の時に必ず出るので、T1 の log の取り方（起動の前の行）か読んだ範囲と推定（未確認）。再撮影で起動の行と変更の行の両方を確かめる。
- 確認: target の clang（`-Werror -fsyntax-only`）で `files/main.c`・`ui-desktop.c` は warning 0。`git diff --check` は空。QEMU は T1（再撮影）。
