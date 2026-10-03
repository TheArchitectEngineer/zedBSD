<!-- awesome-plan project=zedbsd record=ws099p005 -->

# ws099-p005: C7 の残り（client が描くガラスの上の文字の contrast）

Phase ID: `ws099-p005`
Parent: [WS099](../ws.md)
Status: cleared（2026-09-30、サブエージェント、worktree `wt/ws035`。QEMU の Venus、実機は未実施。1 回目の報告は uncleared（client の色が残り）、main の許可で色を変えて cleared）
Phase disposition: normal
Queue: なし（2026-09-30 main の割り当て「p005: C7 の残り: client が描くガラスの上の文字の contrast を測り、足りなければ直す」。すりガラスは残すと
ユーザーが決めた（2026-09-30））

## 測り方（`plan/ws099/tests/c7-contrast.sh` を広げた）

壁紙 6 枚（既定と生成の Aurora・Dawn・Lagoon・Meadow・Twilight）ごとに、p001 の system bar の時計と窓の題に加えて:
- Settings の Home の頁: 題（s-title）、副題「Change how Kei looks and works.」（s-subtitle）、群の見出し「Connectivity」（s-section）、横の行 2 つ
  （s-side-wifi・s-side-appearance）、title bar の「Settings」（s-bar-title）。
- Files の Home: 見出し「Folders」（f-heading）、横の群の見出し「Locations」（f-group）、横の行「Recents」（f-side-recents）、title bar の「Home」（f-bar-home）。
- 別に記録（数えない）: 使えない項目（Files の横の、無い folder の行: f-inactive）と、空の一覧の案内「Files you open appear here.」（f-hint）。
  WCAG は使えない UI の部品を対象から外す。案内はユーザーの判断に残す。
- 領域は 1280x800 での窓の既定の位置（壁紙によらず同じ）。変数 `C7_SETTINGS`・`C7_FILES`・`C7_INFO`、`C7_CLIENTS=0` で前の範囲だけ。

## 変更前の結果（`build/ws099-p005-before/c7-contrast.log`）

72 点のうち **19 点が 4.5 未満**（最小 1.91）。原因は 2 つ:
1. **compositor のガラスが暗い壁紙の上で暗い**: ガラスは「下の景色（ぼかした壁紙）と白の混ぜ合わせ」なので、Aurora・Twilight ではガラスの輝度が
   0.23〜0.54（明るい壁紙で 0.70〜0.99）。主な文字（輝度 0.019〜0.022）でも Aurora の Settings の横の「Wi-Fi」が 4.13。
2. **client の副次の文字の色が明るい**: Settings の `SE_COLOR_TEXT_SECONDARY` と Files の `FM_COLOR_TEXT_SECONDARY` は `0x6b7585`（輝度 0.175）。
   明るい壁紙の上でも 3.36〜4.31（Settings の副題・群の見出し、Files の群の見出し）。

## compositor の変更（1 の直し）

`userland/desktop/wayland/shaders/panel.frag` の glass（mode 0）に明るさの下限を足した: 混ぜた後の色の luma（符号化した値の Rec.709 の重み）が
`GLASS_LEAST_LUMA`（0.85）より低い画素だけ、下限に届くまで白へ寄せる。色合いが白の（全ての成分が 0.9 以上の）ガラスだけに当て、色の付いた
ガラスは変えない。明るい壁紙の上ではほぼ変わらない（既定の壁紙の点は全て同じ値）。`shaders/regenerate.py`（host の glslc・spirv-val）で
`shaders.h` を作り直した（変わったのは `zwl_panel_frag` だけ、他の 3 つの shader の配列は同じ）。系の分岐（OpSwitch）は足していない（i915 の
native の compiler の制約）。build: desktop の warning 0。

## 変更後の結果（`build/ws099-p005-after/c7-contrast.log`、`build/ws099-p005-after.img`）

- 主な文字（時計・窓の題・Settings の題と横の行と title bar・Files の見出しと横の行と title bar）: 6 枚の全てで 4.5 以上、最小 **10.77**
  （変更前 4.13）。暗い壁紙の上のガラスの輝度は 0.73〜0.76 になった。
- 副次の文字（s-subtitle・s-section・f-group）: 6 枚の全てで 3.43〜4.31 のまま（**18 点が 4.5 未満**）。ガラスを白にしても 4.5 に届かない
  （既定の壁紙のガラス 0.86〜0.90 でも 4.0〜4.2）ので、client の文字の色を変える必要がある。
- 別の記録: f-inactive 1.73〜2.11、f-hint 1.78〜2.01。
- 画面: `build/ws099-p005-after/c7/`（壁紙ごとに `NAME.png`・`NAME-settings.png`・`NAME-files.png`）。前は `build/ws099-p005-before/c7/`。
  Aurora の Settings は、ガラスが白くなり文字が読みやすい（`Aurora-settings.png`）。

## 残り（main への依頼）

- Settings（WS089）と Files（WS071）の副次の文字の色 `0x6b7585` を、輝度 0.123 以下にする。例 `0x56606f`（輝度 約 0.115）: ガラスの下限
  （暗い壁紙で輝度 約 0.73）でも 4.7。明るい壁紙では 6 以上。WS099 の修正範囲の外なので変えていない。
- 空の一覧の案内（f-hint、`0x`… 輝度 0.40）を基準に入れるかの判断（ユーザー）。

## 回帰

`criteria.sh build/ws099-p005-after.img … C8 C9`（`build/ws099-p005-regress/results.txt`）: C8（p134）と C9 の 9 本（p052・p053・p072・p076・p126・p128・p134・p137・p138）が全て PASS。

## 続き: client の副次の文字の色（2026-09-30、main の許可）

main の許可（「変更は色の定数だけ。libkeiui の theme の Files の値も揃える。host の描画の試験が期待の色を持てば合わせる」）で、
`0x6b7585` → `0x56606f`（輝度 0.175 → 約 0.115）:
- `userland/desktop/settings/settings.h` の `SE_COLOR_TEXT_SECONDARY`
- `userland/desktop/files/files.h` の `FM_COLOR_TEXT_SECONDARY`
- `userland/desktop/libkeiui/theme.c` の `theme_light` の `text_secondary`

同じ値の他の定数（Files の drag の線 `DRAG_COLOR_LINK`、libkeiland の file chooser の `DRAW_TEXT_SECONDARY`、Notes の `UI_TEXT_SECONDARY`）は
許可の範囲の外なので変えていない。

- host の描画の試験 `plan/ws090/tests/host-draw.sh build/ws099-p005/host-draw`: 13/13 PASS、違う画素 0（試験の `0x6b7585` は比べる両側に
  直の値で渡しているので変えなくてよい）。
- build（`build/ws099-p005-colors.img`）: desktop の warning 0。
- `criteria.sh build/ws099-p005-colors.img … C7`（`build/ws099-p005-colors/`）: **72/72 PASS、最小 4.68**（Twilight の Files の群の見出し）。
  副次の文字は 4.68〜5.89（前は 3.43〜4.31）。主な文字は前と同じ（最小 10.77）。別の記録の f-inactive・f-hint は変わらない
  （使えない項目と空の案内は基準の外とする案をユーザーの判断の表に載せた、main）。
- 画面: `build/ws099-p005-colors/c7/`（例 `Aurora-settings.png`・`Twilight-files.png`）。

## Resume point

2026-09-30: cleared。C7 は 6 枚の壁紙で 72/72 が 4.5 以上。
