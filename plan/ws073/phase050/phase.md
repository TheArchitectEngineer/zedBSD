<!-- awesome-plan project=zedbsd record=ws073-p050 -->
# ws073-p050: package の menu の Fonts の分類（BUG-129）

Status: in-progress（q652-i01、P2 generation7。host で確認済み・Q1 の判定待ち）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-129](../../bugs/BUG-129.md)
Queue: q652 / q652-i01（承認: 2026-10-04 user「P1、P2はBUG-143, 053, 052, 162, 103, 129, 033, 157, 139を修正します。直す順序は任せます。」）

## 範囲

`userland/packages/fonts/noto-color-emoji` を menuconfig の Packages から選べること。host の fixture（`plan/tools/menuconfig-target-host-test.py`）の全体が PASS すること。

## 結果

- 直し自体は 2026-10-03 の q643（P1）で main に入っていた: `tools/menuconfig.py` の `PACKAGE_CATEGORIES` に Desktop（packages/desktop）と Fonts（packages/fonts）。
  `userland/packages/` の下の他の directory（`tools`・`libseat`）は package の分類ではない（script と FreeBSD の Makefile だけ）。
- 残っていた「UI の操作は未実施」を host で埋めた（今回の変更、fixture だけ）: `check_fonts_menu()` が `menu.select_package_programs` を curses なしで動かし
  （`choose` を順の答えに置き換え、表示の label を確かめる）、Packages に Fonts が出る → Fonts に noto-color-emoji の label（`Colour emoji font`）が出る →
  選ぶと `[*]` になり `ZEDBSD_USER_PROGRAMS` に入る、を確かめる。`expected_group` に `noto-color-emoji: packages/fonts` を足した。
- `python3 plan/tools/menuconfig-target-host-test.py` → `MAC-T001 menuconfig round-trip: PASS`（exit 0）。
- 負の確認: `PACKAGE_CATEGORIES` から Fonts を外して `check_fonts_menu()` を呼ぶと `the Packages menu shows no Fonts` で落ちる（一時の実行、変更は残していない）。
- 未実施: 実際の端末での curses の画面の操作（host の fixture は `choose` の置き換え）。QEMU・実機は対象外（menu は host の tool）。
