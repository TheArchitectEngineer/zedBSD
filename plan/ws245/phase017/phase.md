<!-- awesome-plan project=zedbsd record=ws245-p017 -->

# ws245-p017: VBT の解析（xd-vbt の値を本番に、旧 parser と影の比較）

Status: in-progress（q1218-i01 P1: 実装・host 試験・build・5330 の素の起動の回帰に合格（2026-10-16 15:48）。merge の依頼を Q1 に送った）
Parent: [WS245](../ws.md)
Queue: q1218 / q1218-i01（承認は phase047 と同じ）
設計: [design.md](../design.md) §6.2 の p017 の行、§6.1 R3（影の比較）（改訂 6）

## 変えた物（2026-10-16）

| file | 中身 |
| --- | --- |
| `display/vbt.c`（殻） | `drv_i915_bios_init_ex` が旧 parser の後に同じ bytes を `drv_i915_xd_vbt_parse`（device の description の platform で）にかけ、旧の結果（`struct i915_vbt`）と比べて 1 行の log を出し、**xd の値を旧の結果に入れる**（出力ごとの 18 field と、version・内蔵 panel・display clock mode・SSC・SSC の周波数）。2 つの parse の出力の数と DDI が揃わない時は旧の値のまま（log）。`drv_i915_vbt_init_panel` は旧の pass の後に `drv_i915_xd_vbt_panel` の panel と 33 field を比べ（port ごとに最初の 1 回を log）、xd の値を入れる。xd の description が無い device（Gen9）は旧の parse のまま。world に `xd_vbt` と log 済みの port の bit を足した |
| `display/xd.h` | `struct i915_xd` に `struct i915_xd_vbt vbt` と `vbt_parsed` |
| `tests/display/host-xd-vbt-test.c` | 段の殻の試験: 旧 parser だけの値（先に取る）と、`drv_i915_bios_init_ex` → `drv_i915_vbt_init_panel` の結果（xd の値が入った物）が、ADL-P の 2 つの VBT で byte 単位で同じ |
| `plan/ws245/tests/5330-survey-diff.py` | 書き込みの全部の列（`i915.debug=display`）があれば差を数える。起動ごとに変わる register（`VARYING`: 0x162100）は別に数える |

段の phase.md の必須の項目: 消す file 無し（旧 parser の本体は p018）。消す関数無し。殻が更新する旧の写し: `display->vbt_state.parsed` の enc の 18 field と一般の値、`drv_i915_vbt_init_panel` の出力の 33 field、`vbt_state.display_devices`（enc から作る）。表 R・hook: 無し。R11: `vbt.c` 0、`xd.h` 0。

host の試験で分かった事: 旧 parser は `drv_i915_bios_init_ex` と `drv_i915_bios_driver_remove` を通った後の次の parse で、前の VBT の値（AFC の override と eDP の最大 rate）を引き継ぐ（world の状態が parse ごとに戻らない）。本番は 1 起動に 1 回の parse なので当たらない。試験は旧 parser だけの値を先に取って比べる形にした。

## 確かめた事

- `run-xd-host-tests.sh`: regs 495・io 48・platform 89・vbt 149（+14: 段の殻）・survey 57、失敗 0。STRICT の検査も通る。
- build: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws245-dev vmunix` warning 0。image の build（`disk-image`）の kernel の source の warning 0。

## 5330 の素の起動の回帰（2026-10-16、素の起動。QEMU ではない）

image: main ed9621e9b（dhcpc の BUG-326 の直し ca4470702 と WS253 の 4e3ded77f を含む）を merge した e4c1b4295 の `disk-image`（sha256 619f6de5…）。`yes | make firmware-licenses` は試験の image の build の取り決めどおり。USB に dd、PARTUUID が変わったので UEFI の entry `Boot0007 zedBSD USB ws245 e4c1` を足した（BootOrder は変えない。古い Boot0006 は残っている）。基準の kernel は main ed9621e9b の source から（sha256 1d15ec1d…）、段の kernel は image の物（8d58d84b…）。AC 接続。

| 見る物 | 基準（ed9621e9b） | 段（p017） |
| --- | --- | --- |
| zedBSD の ssh まで | 51 s（語ありの起動は 27 s） | 57 s（語ありは 51 s） |
| kern.boot.login / msgbuf_dropped | graphical / 0 | graphical / 0 |
| sessiond の SESSION start と HANDOFF | あり（ready=1、go at_ms=28350） | あり（ready=1、go at_ms=16964） |
| lease 1 claimed / picture up / device lost / error・warn の行 | 1 / 2 / 0 / 3 | 1 / 2 / 0 / 3 |
| perf の per submit | 4.51・6.65 ms | 4.37・8.37 ms |
| backlight-probe | 100 / 100 / 100 | 100 / 100 / 100 |
| survey | lit | lit（frames +113、CRC 0xa92922d1 の 2 回、underrun 0） |
| 影の比較 | — | `i915: xd-vbt: agrees with the old parser (4 outputs, version 249, the VBT)`、`i915: xd-vbt: panel on DDI A agrees with the old parser (panel type 2)` |
| 書き込みの列（`i915.debug=display` の起動で全部） | 599 個 | 599 個。違いは 0x162100 の 1 個だけ（`80005f28` / `80005f2a`、combo PHY A の COMP_DW0 の rmw で読み戻した bit 1）。語無しの起動でも同じ kernel の digest が 2 つの値を行き来する（set 0x6e910c28 と 0x62c7059b を両方の kernel で見た）ので、段の差ではない |
| `5330-survey-diff.py`（語ありの 2 起動） | — | steady の差 0、whole lists の差 0（varying 1）、PASS |

判定: **合格**（Q1 の判定待ち）。LCD の目視: 未確認（一覧）。証拠: `plan/ws245/tests/evidence/5330-base-ed9621e9b-20261016.md`・`5330-p017-e4c1b4295-20261016.md`・`5330-base-ed9621e9b-debug-20261016.md`・`5330-p017-e4c1b4295-debug-20261016.md`。語ありの起動の後は USB の `zedbsd.cfg` を元に戻した。
