<!-- awesome-plan project=zedbsd record=ws245-p053 -->

# ws245-p053: EDID の decode を xd-edid に

Status: cleared（2026-10-16、ws.md の表と同じ: T1-702 で 5330 の回帰 PASS、全部の列の差 0、merge 7e3bf81cf。下の記録は q1224-i01 の時点の物）
Parent: [WS245](../ws.md)
Queue: q1224 / q1224-i01
設計: [design.md](../design.md) §6.2 の p018 の行の EDID の部分（2026-10-16 に p018 から分けた: phase018）

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-edid.c/.h` | `drv_i915_xd_edid_first_timing`: base block の 4 つの 18 byte の descriptor を順に見て、pixel clock が 0 でない最初の物を own の `drv_i915_output_edid_timing` で decode し、何番目かを返す（旧 `drv_i915_edid_preferred_mode` と同じ選び方）。descriptor の数と大きさの define（E-EDID の Values referenced の block に） |
| `display/state.c` | `drv_i915_lcd_compute` の mode を `drv_i915_xd_edid_first_timing` で `out->mode`（own の `struct i915_display_mode`）に直に。drm_display_mode を経ない。link の必要量と M/N は `out->mode.clock_khz` から。R11: 2 箇所 |
| `display/edid.c`・`edid.h` | decode（`drv_i915_edid_preferred_mode`・`i915_drm_mode_detailed`・`i915_edid_mode_create`・interlace の quirk、EDID の quirk の define）と drm_edid.c の notice を削除。残りは drm_modes.c 由来の helper（`drv_i915_drm_mode_copy/_init/_get_hv_timing/_set_crtcinfo`、`drv_i915_lcd_drm_mode_set_name`）だけの殻（p048・p050 まで: design の M1） |
| `tests/display/host-xd-vbt-test.c` | first timing の試験 3 つ（panel の descriptor 0、display descriptor の後の 1、timing の無い block の EINVAL） |
| `plan/ws245/tests/5330-bootcycle.sh`（新） | 1 回の素の起動（kernel の差し替え → BootNext → ssh → 60 s → `5330-survey.sh` → Linux へ）。`DEBUG=1` で `i915.debug=display` を付けて後で戻す。T1 に渡す手順 |
| `plan/ws245/tests/5330-ws253-early.sh`・`5330-ws253-collect.sh`（新） | WS253（起動の時間）の読むだけの 6 項目（Q1 の依頼） |

違いの注意: 旧の decode は interlaced の timing を quirk 付きで受けたが、own の decode は受けない（EINVAL）。panel の EDID は progressive なので 5330・5320 には当たらない。mode の物理の大きさは timing の descriptor の値（5330 は 293×165 mm。base block の 29×17 cm ではない）で、旧と同じ。

段の phase.md の必須の項目: 消す関数（上の 4 つ。本番の呼び手は state.c の 1 箇所だけ → 0）。殻が更新する旧の写し: 無し（`i915_lcd_state.mode` は own の型）。表 R・hook: 無し。R11: state.c 2 → 0（`P50-C-18C` は製品名で残す）、edid.c・edid.h・xd-edid 0。

## 確かめた事

- host: regs 495・io 48・platform 89・vbt 137・survey 57、失敗 0。native-decide 14、失敗 0。xd の .c の STRICT の検査は通る。
- build: 既定の vmunix warning 0、`I915_TESTS=y` の 5 組 warning 0、image の build の kernel の source の warning 0。

## 5330 の素の起動（2026-10-16、P1 が流した分）

| 起動 | kernel | 結果 |
| --- | --- | --- |
| 16:18（image e4c1b4295、Boot0007） | p053 cecf540f（6ebb2078…）。基準は同じ binary の p018 の起動（e6237eb9…） | graphical・msgbuf 0・session ready=1・lease 1・picture up 2・device lost 0・backlight 100、survey lit（frames +62、CRC 2 回が違う値 = 絵が変わった）。digest set 0xa19e1ed1（初めて見る値） |
| 16:27（image 33cde7ac9 = main d454d4bd3 以降、Boot0008） | 基準 main 8666f25a6（6ae36ef7…） | lit、digest set 0x6e910c28 |
| 16:30（同） | p053 33cde7ac9（ea5b3427…） | lit（frames +109、CRC 2 回同じ）、digest set 0xdcd7004e |

digest の set の値は、同じ kernel でも起動ごとに 0x162100 の rmw の読み戻しで変わる（phase017）。p053 は mode の計算を替える段なので、`i915.debug=display` で基準と p053 の全部の書き込みの列を取り、steady の差が 0 である事を確かめるまで合格にしない → T1 に依頼（Q1 経由）。
