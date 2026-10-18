<!-- awesome-plan project=zedbsd record=ws245-p038 -->

# ws245-p038: screen の plan（純関数。eDP・HDMI・DP-ext の 3 種を同じ段で）

Status: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない）
Parent: [WS245](../ws.md)
Queue: q1229-i05（P1）
設計: [design.md](../design.md) §6.2 の p038 の行、§3.3

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-screen.c/.h`（Zlib、新） | plan の純関数。`drv_i915_xd_screen_plan_link`: panel（DPCD の 4 つの 8b/10b の code、1・2・4 lane、eDP 1.4 は拒む、EDID の深さ（無ければ 24 bpp）を VBT で cap、必要と運べる帯域）、HDMI（TMDS = pixel clock、4 lane、24 bpp）、DP（branch の clock の上限、深さ 8〜6、rate は低い方から、lane の上限は fallback の上限と sink の小さい方: xd-dp の choose）、M/N（xd-dp）、combo の port なら DPLL の語（xd-clock の DP の表か HDMI の divider）。`drv_i915_xd_screen_plan_check`: DBUF の slice と join（xd-dbuf、並ぶ pipe の集合）、pixel/data rate、要る CDCLK（pixel/2、plane、帯域 ×10/512、TGL は pixel rate そのもの）、table の step・VCO・voltage（port clock > 594 MHz は level 2）、拒む: 別の CDCLK（か最高を超える）、別の voltage、QGV の点を超える帯域 |
| `display/state.c`（殻） | `drv_i915_lcd_compute`・`_hdmi`・`_dp_ext` の本体は plan を呼んで `i915_lcd_state` に写す（mode と EDID の深さの読みは殻、DP-TC の DKL PLL は旧の計算のまま: p040）。消した: `i915_state_dp_ext_max_bpc`・`_link`、4 つの macro |
| `display/modeset.c`（殻） | `i915_modeset_prepare_state` の check の前に plan の check を同じ state で流し、旧の check の結論（CDCLK・voltage・QGV、受けた時は CDCLK・level・min・slice・join）と違えば `i915: xd-screen: the plan's check differs: …` を 1 行。判断は旧の check（影） |
| `tests/display/host-xd-screen-test.c`（新） | 33 check（下） |

段の phase.md の必須の項目:
- 消す file: 無し。
- 殻が更新する旧の写し: `i915_lcd_state`（link・M/N・combo PLL の語）。`ms->crtc_state`・`ms->wm`・dpll の選択は今の prepare が `i915_lcd_state` から作る（変えていない）。
- 表 R: 書き込みは無い（純関数）。
- R11: `state.c`・`modeset.c` 0。

## 旧と同じに保った物と違い

- 確かめ方: `sh build/tmp/refcmp.sh 2153d2c3a p038b` で旧の host 試験（dp 72・lcd 120）の出力が同じ（lcd の試験の mode・link・M/N・PLL の検査は plan を通る）。plan の check の数は lcd 試験の D の行（crtc min 70400、bw min 11000 → 179200 kHz vco 537600 level 0、564 MB/s、slice 0xf joined）と host-xd-screen-test で一致。
- 設計との差: (1) 拒む 3 条件は plan と旧の check の両方で判定し、**判断は旧**（違いは log の 1 行。設計は「両者の結論が同じ事を殻の試験で見る」: 5330 の fixture で同じ。xd が判断するのは p048 の screen の列から）。(2) WM の level と DDB の割り当ては plan に入れていない（p034 で watermark.c が xd-dbuf を呼ぶ形のまま。plan は slice と join だけ）。(3) DP-TC の PLL（DKL）は p040 まで旧。(4) `host-lcd-modeset-test.c` の期待値の移動はしていない（旧の試験が plan を通って同じ出力なので、旧の試験を消す p050 で xd の試験に移す）。

## 実機で確かめる事（5330、全部の段の後の 1 回で）

- `i915: xd-screen: the plan's check differs` が出ない（出たらその値）。survey と digest が基準と同じ。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p038/xd-host` → 全部 pass（screen 33）。
- 旧の host 試験: `sh build/tmp/refcmp.sh 2153d2c3a p038b` → dp・lcd とも SAME。
- build（2026-10-16）: `sh build/tmp/kbuild.sh p038` で 6 組が rc 0・warning 0。`git diff --check` は空。
- 5330: 未実施（全部の段の後に 1 回）。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- `xd-screen: the plan's check differs` 0、survey・digest が基準と同じ（差は I・H の行）

満たしていない・未確認の基準と残る物:

- なし（5330 の範囲で）

P1 の見立て（Status は変えない。判定は Q1）: cleared にしてよい。
