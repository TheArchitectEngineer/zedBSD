<!-- awesome-plan project=zedbsd record=ws259-p001 -->

# ws259-p001 rtl8822b-tables.inc を値の引用に書き換え、表の値を比べる

Status: in-progress（2026-10-19 q1258-i01、P2。書き換え・比べ・build は済み。license の package の扱いは判断待ち、実機の WiFi は実機待ち）

- 親: [WS259](../ws.md)。Queue: q1258-i01。
- 規則: [Guardrail](../../guardrail.md)「MIT・GPL の出典のデータの引用」。AGENTS.md の「RTL8822B の `.inc` は別の file のまま保つ」は守った。

## したこと

- `src/drivers/wifi/rtl8822b/rtl8822b-tables.inc`: 先頭の Realtek の BSD-3-Clause の表示・抽出の宣言・出典の一覧（commit・path・SHA-256・配列の語数）を外し、zedBSD の著作権の header と、何を持つ file か（出典を書かない説明）に。表ごとの直前に複数行の comment で役割と `Values referenced: Linux git commit 0b8db87da54178717d302ca5dc09285ad4922abc drivers/net/wireless/realtek/rtw88/rtw8822b_table.c（または rtw8822b.c）(<配列の名前と取った部分>)`。
- 名前を zedBSD の物に: `rtw8822b_mac`→`rtl8822b_mac_table`、`_agc`→`rtl8822b_agc_table`、`_bb`→`rtl8822b_bb_table`、`_rf_a`・`_rf_b`→`rtl8822b_rf_a_table`・`rtl8822b_rf_b_table`、`rtw8822b_phy_sections`→`rtl8822b_table_sections`、`rtw8822b_2g_legacy_world_limit`→`rtl8822b_2g_legacy_limit`、`rtw8822b_w52_legacy_world_limit`→`rtl8822b_w52_legacy_limit`、`rtw8822b_w52_rf_be`→`rtl8822b_w52_rf_band_select`、`rtw8822b_w52_cca_type2`・`_type35`→`rtl8822b_w52_cca_rfe2`・`rtl8822b_w52_cca_rfe35`、rate offset の 2 つは `rtw`→`rtl`。`rtl8822b.c` の参照を同じく付け替えた（名前だけ）。

## 表の値の比べ（2026-10-19）

- 文字の比べ（comment を除き、配列ごとの型・大きさ・初期値の列を順に、名前は対応表で読み替え）: 13 配列とも SAME、「RESULT: identical arrays=13/13」（mac 250 語、agc 20302、bb 2984、rf_a 10178、rf_b 8904、表の一覧 5 件、ほか 7 つの小さい表）。
- 値の byte の比べ（host の cc で旧と新の .inc をそれぞれ include し、全部の配列の byte を宣言の順に出す）: 両方とも 341,062 byte、SHA-256 `a497984dfc63f695db0ca489270a39489dbfd7b6a3c90cc886a25362b5c7d509` で一致。
- 比べの script は使い捨て（scratchpad）。

## build

- amd64（`config/ci/config-amd64.mk`、CONFIG_DRIVER_USB_RTL8822BU=y）の vmunix: rc=0、warning 0、amd64 vmunix check: PASS。

## 判断待ち・残件

- license の package（完了の条件 4）: `userland/base/licenses/rtl8822b-tables/`（Realtek の BSD-3-Clause の LICENSE、CONFIG_DRIVER_USB_RTL8822BU で Makefile が自動で選ぶ）と `tools/release/license-components.json` の rtl8822b-tables の項目。Guardrail の規則（値の引用は出典の著作権表示と許諾の文を付けない）に合わせるなら、WS244 の i915-driver（ユーザーの決定「外す」）と同じく外すのが筋。ただし RTL8822B についてユーザーの明示の決定は無いので、変えずに Q1 に判断を求める。
- 使われていない検査の道具 `tools/build/check-rtl8822b-tables.noct`（どこからも呼ばれず、旧 file の先頭の出典の一覧と LICENSE を検める）: 削除の候補（Q1 に path を送る）。
- 実機の WiFi の確かめ: 実機待ち（ユーザーの決定「実機は残して待つ」）。ユーザーの一覧に載せる。
