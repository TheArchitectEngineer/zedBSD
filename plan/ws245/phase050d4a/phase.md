<!-- awesome-plan project=zedbsd record=ws245-p050d4a -->

# ws245-p050d4a: 画面の口（xd-picture）

Status: in-progress（2026-10-17 q1229-i11、P1）
Parent: [WS245](../ws.md)
Queue: q1229-i11（P1。Q1 の判断で p050d4 を 4 段に分けた 1 段目）
設計: [design.md](../design.md) §6.2 の p050 の行、§4.2・§4.3。分け方は [phase050d4](../phase050d4/phase.md) の「4 段に分ける案」

## 範囲

- 新しい xd の file `display/xd-picture.c`（口は `xd-picture.h`、`xd.h` が include）に、画面（screen 0 = resident、1 = head）の object と、prepare・light（enable の commit）・dark（disable の commit）・plane の off/released・flip（arm・poll・wait・settle）・status・retained・backlight を own の型で置く。旧の `struct i915_lcd_modeset`（Linux の crtc state・digital port・connector を持つ）とその select の口を消す。
- 環境の hook は backend（`struct i915_xd_backend`、旧 `struct i915_lcd_emit` を own の header `xd-backend.h` に移して改名、internal.h は旧名を別名で残す）を直に呼ぶ。電源の hook は「use と番号」を backend の `power_domain` で旧の domain の番号（opaque）に変えてから `power_get/put` に渡す（番号の意味は xd の外: kernel と試験の backend が同じ写像 `drv_i915_lcd_power_domain_of` を使う）。
- head.c・present.c・backlight.c と、modeset.c に残る run の code（resident run・show run・kernel の binding: d4b で xd-display へ）を新しい口に付け替える。旧の入力（`struct i915_lcd_modeset_cfg` と `struct i915_lcd_state`）は modeset.c の変換 `drv_i915_lcd_picture_config` で `struct i915_xd_picture_config` にする（d4b で fill_cfg が直に作り、変換は消える）。
- 消す: modeset.c の select・prepare・status・plane・commit・abandoned・retained・discard_model・flip の口と step の hook、output-shell.c、panel-backlight.c、plane.c・vblank.c・color.c・watermark.c・clock.c・ddi.c・dp.c の modeset object の関数、world の ms_pool。
- 試験: host-lcd-modeset-test と out50c の harness を新しい口に付け替え（lcd-fake-hw に backend の power_domain を足す）、段の前後の書き込みの列を比べる。新しい xd の host 試験 `host-xd-picture-test`（xd-fake-regs の上）で prepare の拒み・status・PLL の pool・DBUF の状態・flip の状態・backlight の拒みを見る。display の ktest の組（display・display_ktest・display_ktest2）は旧の口を呼ぶので、この段で vmunix.mk から外す（design §6.4・§7 #26 の既定「壊れた段で消す」、削除の path は Q1 へ）。

## 決めた事（実装の前、2026-10-17）

- 名前: 画面の object は「picture」（`drv_i915_xd_picture_*`、状態は `struct i915_xd_pictures`、`struct i915_xd` の field）。xd-screen.c の `drv_i915_xd_screen_*`（plan・light・dark の純関数）と分ける。
- 結果の値: `I915_XD_PICTURE_OK/NOT_READY/ERRORS/LINK_NOT_TRAINED/STILL_OWNED`、flip は `I915_XD_PICTURE_FLIP_DONE/NOT_LATCHED/TIMEOUT/REFUSED/ARMED`（旧の `I915_LCD_MS_*`・`I915_LCD_FLIP_*` は internal.h の別名で残す: 旧の run の code と ktest が消える d4b〜d4d まで）。
- 振る舞いを変えない所（旧と同じ値を保つ）: WM_LINETIME は 0（旧の crtc state は linetime を計算していなかった）、GAMMA_MODE は 8 bit（0）・CSC_MODE は 0（LUT・CTM 無し、RGB、limited range 無し）、eDP 1.4 の rate select は使わない（rate_index −1、旧の object は use_rate_select を立てていなかった）、LANES の電源は port の DDI lanes の domain、HDMI は DVI mode（lane 4、dither 無し）、6 bpc の時だけ dither。
- 変える所: combo PLL を 2 つの画面が同じ語で共有した時、2 つ目の画面は PLL に加わるだけ（旧は xd-output の記録が画面ごとで、2 つ目が PLL を書き直していた）。PLL の pool は picture の中に自前で持つ（DPLL 0・1、Thunderbolt、TC 1〜4）。
- Thunderbolt の PLL の on/off は xd-clock（`drv_i915_xd_dpll_tbt_words`・`_enable`・`_disable`）を直に（旧は Linux の pool の enable）。
- link の失敗の hook: hobl が効いていたら hobl_failed を立てる。それ以外は「fallback は呼ぶ側」（旧は Linux の fallback の値を求めて「not ported」の error を数えていた。この error は数えない: 呼ぶ側の fallback（head・resident の DP-ext）は LINK_NOT_TRAINED で動く）。

## 受け入れ

- 消した関数の本番の呼び手が 0（grep）、既定と I915_TESTS=y（vke2・execution）の build が warning 0、`-DI915_XD_STRICT` で xd-picture.c が通る、R11 の数（触った file）。
- 旧の host 試験（host-lcd-modeset-test を新しい口に付け替えた物）の前後の比べ（書き込みの列の trnorm の差）と、新しい `host-xd-picture-test`。5330 は全部の段の後に 1 回。
