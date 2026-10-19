<!-- awesome-plan project=zedbsd record=ws259-p001 -->

# ws259-p001 rtl8822b-tables.inc を値の引用に書き換え、表の値を比べる

Status: in-progress（2026-10-19 q1267-i01、P2。書き換え・比べ・build・license の package の取り外しは済み。package の directory の削除は Q1、実機の WiFi は実機待ち）

- 親: [WS259](../ws.md)。Queue: q1258-i01、q1267-i01。
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

## license の package を外す（2026-10-19 q1267-i01）

- ユーザーの決定（2026-10-19、クリック）: RTL8822B の license の package（`userland/base/licenses/rtl8822b-tables/` と license-components.json の `rtl8822b-tables`）を「外す」（WS244 の i915-driver と同じ。firmware の license は残す）。
- `Makefile`: `CONFIG_DRIVER_USB_RTL8822BU=y` で `rtl8822b-tables-license` を選ぶ block（comment と ifeq の 7 行）を外した。ほかに選ぶ所（*.mk・config・kernel の規則）は grep で 0 件。
- `tools/release/license-components.json`: `rtl8822b-tables` の項目を外した（JSON として読める）。
- `plan/ws129/tests/license-inventory-test.sh`（WS218 の受け入れ・WS129 の release の手順で使う回帰の試験）: case 2 は「rtl8822b-tables と musl-regex を外した表で src/drivers/wifi/rtl8822b/ の BSD-3-Clause が uncovered」を確かめていたが、p001 で tables.inc が Zlib になり成り立たないので、musl-regex だけを外す形にし、case 1 に「RTL8822B の source が第三者として報告されない」を足した。
- 確かめ（CI の config `config/ci/config-amd64.mk`、CONFIG_DRIVER_USB_RTL8822BU=y）:
  - 選ばれる package（`ZEDBSD_USER_PROGRAMS`）: 変更前 255（`rtl8822b-tables-license` を含む）→ 変更後 254（含まない）。`rtl8822b-firmware` は残る。
  - rootfs の recipe が複写する data の file の一覧（`ZEDBSD_USERLAND_DATA_FILES`）: 変更前は `/usr/share/licenses/rtl8822b-tables/LICENSE` と `/usr/share/licenses/rtl8822b-firmware/LICENCE.rtlwifi_firmware.txt`、変更後は firmware の 1 つだけ。rootfs の全体の build（全 package）は時間が掛かるので未実施で、staging の実物は見ていない（一覧で確かめた）。
  - `python3 -I tools/release/license-inventory.py --config config/ci/config-amd64.mk`: open items は変更の前後とも同じ 2 件（amd-navi-firmware・nvidia-gsp-firmware の未登録、この Phase の外、前から）。RTL8822B の source の報告は 0 件。
  - `sh plan/ws129/tests/license-inventory-test.sh`: case 1 の「open item 0」だけ FAIL（上の前からの 2 件）、ほかの確かめ（RTL8822B・AX211 の source が第三者でない、case 2〜5）は ok。
  - build: `make -j8 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws259-p001 vmunix` rc=0、warning 0、amd64 vmunix check: PASS。
- 使われていない検査の道具 `tools/build/check-rtl8822b-tables.noct` は既に tree から消えている（fec7f1d17）。

## 残件

- **削除は Q1 へ**: `userland/base/licenses/rtl8822b-tables/`（LICENSE・Makefile）。どこからも選ばれなくなった。
- 写しの記録 `plan/ws129/`（licenses-index.txt・licenses-generated.md）に rtl8822b-tables の行が残る（WS244 の i915-intel と同じく、WS129 の記録なので触っていない。Q1 の判断）。`plan/ws004/tests/README.md` の 216 行目・`plan/ws004/phase028/phase.md` は今は無い runner（run-rtl8822b-core-test.sh）の説明で、表の license の install に触れる。WS004 の記録なので触っていない（試験の整理の基準では参照を外す候補、Q1 の判断）。
- 実機の WiFi の確かめ: 実機待ち（ユーザーの決定「実機は残して待つ」）。ユーザーの一覧に載せる。
