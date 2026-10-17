<!-- awesome-plan project=zedbsd record=ws245-p033 -->

# ws245-p033: plane と flip（xd-pipe の plane）

Status: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない）
Parent: [WS245](../ws.md)
Queue: q1229-i04（P1）
設計: [design.md](../design.md) §6.2 の p033 の行、§2.10

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-pipe.c/.h` | 第 1 plane: buffer の検査（linear XRGB8888、1〜8192、pitch が行を持ち 64 byte の倍数、surface が page の上）、制御の語（enable・format、display 13 は ARB slot 1、COLOR_CTL は plane gamma off）、arm しない語（STRIDE（64 byte 単位）・POS・SIZE・KEYVAL・KEYMSK（半透明なら定数 alpha）・KEYMAX（alpha）・OFFSET・AUX_DIST・CUS_CTL・COLOR_CTL）、arm（CTL → SURF）、off（CUS_CTL、呼ぶ側の WM の後に CTL=0・SURF=0）、SURFLIVE。step `xd-pipe plane`・`xd-pipe plane off` |
| `intel/display-regs-xd.h` | PLANE_KEYVAL・KEYMSK と alpha・KEYMAX と alpha・AUX_DIST・CUS_CTL・COLOR_CTL の gamma off（`Values referenced:`）。map に 8 対 |
| `display/plane.c`（殻、1632 → 751 行） | `drv_i915_lcd_ms_plane_prepare` と記録器の `drv_i915_plane_emit` の拒みは `drv_i915_xd_plane_check`、制御の語は `drv_i915_xd_plane_words`（`i915_plane_xd_words`）。`i915_icl_plane_update_noarm`（xd の語 → `skl_write_plane_wm`）・`_update_arm`・`_disable_arm`（xd の CUS → WM → CTL・SURF）は xd を呼ぶ。plane state → `struct i915_xd_plane` は `i915_plane_xd_of`。消した: stride・format・alpha・tiling・rotate・flip・ARB・ctl_crtc・color_ctl・surf・aux_dist・key・color_plane・sel fetch の 3・`i915_skl_plane_ctl`・`i915_glk_plane_color_ctl`・`i915_icl_is_hdr_plane`（計 24） |
| `tests/display/host-xd-pipe-test.c` | plane の 12 check を足した（計 35） |

段の phase.md の必須の項目:
- 消す file: 無し（plane.c は readout・watermark の data rate・min cdclk・`drv_i915_adjusted_rate`・`drv_i915_icl_hdr_plane_mask` が残る。`plane-shell.c` への移しは、呼び手の段（p034・p038・p048・p049）で plane.c を畳む時に）。
- 消した関数（本番の呼び手 0）: 上の 24。
- flip（`modeset.c` の `i915_modeset_flip_arm/retire`）と event（`vblank.c`）は own の状態機械で、plane の書き込みは上の殻を通る。SURFLIVE の読みは backend の read のまま（`drv_i915_lcd_modeset_flip_wait*` は worker でない thread から呼ばれ、共有の `drv_i915_lcd_xd_io` の window の hook を差し替えると競う）。flip の契約の書き直しは p049。
- 記録器（`drv_i915_lcd_emit_plane`）: xd の語を記録する形で残す（p050）。
- 殻が更新する旧の写し: `plane_state->ctl`・`color_ctl`（xd の語）。
- 表 R: PLANE_CTL・SURF・BUF_CFG のうち BUF_CFG と WM は p034 まで旧の `skl_write_plane_wm`（呼ぶ側で順を保つ）。
- hook と lock: modeset の lock の下で `drv_i915_lcd_xd_io` の window。
- R11: plane.c 0。

## 旧と同じに保った物と違い

- 確かめ方: `host-dp-test`（72）は段の前（a1a993d8f）と出力が同じ、`host-lcd-modeset-test`（120）は書き込みの列（`build/tmp/wr.py`）が全部一致（plane の STRIDE〜COLOR_CTL・WM・CTL・SURF、off の CUS・WM・CTL・SURF）。
- 違い: 受ける buffer は旧と同じ（linear XRGB8888）なので、旧の tiling・回転・YUV・PSR2 の selective fetch・scaler・CCS の分岐は持たない（旧も通らなかった道）。

## 実機で確かめる事（5330、全部の段の後の 1 回で）

- w の log に `xd-pipe plane` の step、flip の性能（`perf: … per submit` が基準と同じ桁）、FLIP_DONE の timeout の log が無い、device lost 0、survey の SURFLIVE と CRC の進み、PLANE_CTL 0x94000000。
- 外部出力: 未確認（2 画面の flip は p048 の節目で）。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p033/xd-host` → 全部 pass（pipe 35）、`I915_XD_STRICT` の検査は通る。
- plane の check: 拒む buffer 6 種と受ける 1、display 12 の CTL 0x84000000、5330 の panel の plane（CTL 0x94000000・STRIDE 0x78・SIZE 0x0437077f・SURF・COLOR_CTL 0x2000・KEYMAX 0xff000000 = regs-selected.txt）、書き込みの順、pipe B の位置と定数 alpha、off と SURFLIVE。
- `sh plan/ws245/tests/check-xd-regs-linux.sh …` → 1133 対が一致、差 0。
- build（2026-10-16）: `sh build/tmp/kbuild.sh p033` で 6 組が rc 0・warning 0。`git diff --check` は空。
- 5330: 未実施（全部の段の後に 1 回）。
