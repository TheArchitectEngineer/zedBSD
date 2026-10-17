<!-- awesome-plan project=zedbsd record=ws245-p050d4a -->

# ws245-p050d4a: 画面の口（xd-picture）

Status: in-progress（2026-10-17 q1229-i11 で実装・host 試験・build 済み（86041583b）、5330 は全部の段の後に 1 回）
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
- link の失敗の hook: hobl が効いていたら何もしない（xd-output が hobl_failed を立てる）。それ以外は旧と同じく error を 1 つ数える（文言は「no lower link is tried」。旧は Linux の fallback の値を求めて「not ported」の error を数えていた）。結果は旧と同じく ERRORS で、呼ぶ側の fallback（LINK_NOT_TRAINED で動く）は今まで通り HOBL の時だけ働く（振る舞いを変えない。fallback を本当に効かせるかは後の段の判断）。

## 受け入れ

- 消した関数の本番の呼び手が 0（grep）、既定と I915_TESTS=y（vke2・execution）の build が warning 0、`-DI915_XD_STRICT` で xd-picture.c が通る、R11 の数（触った file）。
- 旧の host 試験（host-lcd-modeset-test を新しい口に付け替えた物）の前後の比べ（書き込みの列の trnorm の差）と、新しい `host-xd-picture-test`。5330 は全部の段の後に 1 回。

## 結果（2026-10-17 q1229-i11、P1。base 794c535ff（phase.md の commit）、段の commit 86041583b）

形:
- `xd-backend.h`（新、own）: `struct i915_xd_backend`（旧 `struct i915_lcd_emit` の hook を own の説明で。足した hook は `power_domain(ctx, use, index, thunderbolt)`: xd-output の use と番号を backend の domain の番号に）、`struct i915_xd_sink_hooks`（旧 `i915_lcd_aux_emit`）、commit の点 `I915_XD_OBSERVE_*`、Type-C の mode `I915_XD_BACKEND_TC_*`。internal.h は旧名を `#define` の別名で残す（`i915_lcd_emit`・`i915_lcd_aux_emit`・`i915_lcd_modeset_status`・`i915_lcd_flip_result`・`I915_LCD_MS_*`・`I915_LCD_FLIP_*`、`enum i915_lcd_observe` の値は xd の定数）。d4d で別名ごと消える。
- `xd-picture.c/.h`（新、約 3900 行、`I915_XD_STRICT` で通る）: `struct i915_xd_pictures`（`struct i915_xd` の field `pictures`。screen 0 = resident、1 = head、device の PLL の pool 7 つ、DBUF の layout、retained の印、短い update の割り込みの入れ子、今の操作の screen）。口は `drv_i915_xd_picture_prepare/light/dark/plane_off/released/abandoned/retained/discard_model/status/link_status/flip/flip_nowait/flip_poll/flip_wait/flip_settle/brightness/brightness_get/backlight`、`drv_i915_xd_pictures_init/platform/forget/fault`。screen ごとに backend の上の io の window と Dekel の route を持つ。prepare は config の検査 → PLL の予約（combo は同じ語の共有か空き、Type-C は自前の TC PLL と Thunderbolt PLL、rate は TC PLL の語の clock に）→ output の記述（旧の crtc state の代わりに config から直に）→ plane → xd-screen の plan → DBUF の layout。light/dark は xd-screen の light/dark に xd-output の step を直に（旧 modeset.c の step の hook と output-shell.c の env を移した）。
- 振る舞い: 旧と同じ値を保った（phase.md の「決めた事」）。PLL の pool は device ごとに一つ（共有の PLL の on/pipes は output の記録と pool の間で受け渡す）。
- 付け替え: head.c（select を外し screen 1 を名指し）、present.c（screen 0）、backlight.c（screen 0）、diagnostics.c（retained）、modeset.c の run の code（show run・resident run・kernel の binding: 旧入力から `drv_i915_lcd_picture_config` で config を作る。d4b で消える）。kernel の bind_ops と lcd-fake-hw に `power_domain`（`drv_i915_lcd_power_domain_of`、pipe.c: 旧の domain の写像）、diagnostics の trace の wrapper にも。world の error の hook は display の pictures に（`drv_i915_lcd_backend_fault`・`drv_i915_lcd_error`・`drv_i915_lcd_debug` は今の操作の screen へ）。
- 消した: modeset.c の select〜flip_settle の口と commit の step（約 2300 行）、output-shell.c・panel-backlight.c・color.c（中身を空にし vmunix.mk から外した）、plane.c・vblank.c・watermark.c・clock.c・ddi.c・dp.c・diagnostics.c・hotplug.c・power.c の呼び手 0 の関数（`killdead.sh` 3 回り＋表 2 つ）、world の ms_pool と PLL の pool の field、古い宣言 69 行。`struct i915_lcd_modeset` の定義は旧の Linux の macro（dp の AUX の container_of、ddi の level shift）が残るので d4d まで残る。
- 試験の組: `I915_TEST_SET=display/display_ktest/display_ktest2` は旧の口を呼ぶので vmunix.mk で `$(error)` にした（design §6.4・§7 #26 の既定）。その 20 file は中身を空にした（削除は Q1）。hpd-ktest の case は p039 の host 試験で書き直す事になる。`tests/execution/runner.c` の display の scenario の weak 参照と `tests/display/scenarios.h` は残した（害なし、p043）。
- 旧 host 試験: host-lcd-modeset-test を新しい口に付け替えた（prepare は変換と picture の prepare、J2 の evasion window は xd-pipe を直に、B2 と CDCLK の文言の check を新しい文言に）。out50c の harness は新しい口の版 `temp/harness/out50c-picture.c` を足し、`run-out50c.sh` と `build/tmp/stagecmp.sh` に harness の名前の引数を足した。
- 新しい xd の host 試験 `host-xd-picture-test`（54 check）: prepare の拒み 10、panel の prepare・light・flip・brightness・backlight・dark の status と電源・PLL・lock の返し、2 つ目の screen の DPLL 1・同じ語の共有・空き無し、確かめられない dark の retained と model の discard、TC1 の PLL の予約と rate・PHY。`run-xd-host-tests.sh` に足した。

確かめた事:
- 前後の比べ（`sh build/tmp/stagecmp.sh bfd44d75f d4a/cmp2 out50c-picture.c`）: dp SAME。lcd の trnorm の差 4 行（B2 の error の文言と check の文言だけ）、out50c の差 26 行（Type-C の model の STEP の名前と回数、Thunderbolt PLL の error の文言だけ。書き込みの列と status の数は同じ）。旧 host 試験 dp 72・lcd 120 check とも 0 failures。
- `run-xd-host-tests.sh` 25 本 all passed（`I915_XD_STRICT` で xd-picture.c を含む xd の .c が通る）。
- build（`sh build/tmp/kbuild.sh d4a`、組は既定・vke2・execution）: 3 組 rc=0 warning 0。`I915_TEST_SET=display` は案内の `$(error)` で止まる。`git diff --check` 0。R11: 触った source の file は全部 0（head.c 10・hotplug.c 14・hotplug.h 1・diagnostics.c 2・backlight.c 1 を消した）。
- master の Tools の `plan/ws051/tests/host-tc.sh`・`host-vbt-pll.sh` は p040・p028 からの既知の失敗（DKL の窓・PLL の表、ws.md の「merge の時に Q1 が消す物」）のまま。この段で増えた失敗は無い（host-tc 112 check 0 failures）。

実機で確かめる事（5330 は全部の段の後に 1 回）: 起動・picture up・flip の性能・backlight（backlight-probe）・survey、HDMI・USB-C DP の 2 画面（ユーザーの一覧）。Type-C の Thunderbolt mode の PLL は実機で通っていない道（model だけ）。

Q1 に頼む削除（中身を空にした、または参照されない物）:
- `src/drivers/gpu/i915/display/output-shell.c`、`panel-backlight.c`、`panel-backlight.h`、`color.c`、`color.h`
- `src/drivers/gpu/i915/tests/display/` の `lcd-run.c`・`lcd-run.h`・`hdmi-output.c`・`lcd-flip.c`・`lcd-gpu.c`・`lcd-gpu.h`・`hdmi-hotplug.c`・`aux.c`・`hpd-model.c`・`hpd-model.h`・`display-ktest.c`・`display-ktest.h`・`edp-ktest.c`・`edp-ktest.h`・`edp-sync-ktest.c`・`lcd-modeset-ktest.c`・`scanout-ktest.c`・`lcd-show-ktest.c`・`lcdg-ktest.c`・`hpd-ktest.c`
- 他の WS の物（判断は Q1）: `plan/ws051/tests/host-dkl.sh`・`host-dkl-test.c`・`host-dkl-linux.c`（master の Tools・未完の Phase から参照なし、消した pool の関数を呼ぶ）。`plan/ws031/tests/vkloop-hw.sh` の display の scene の行と `plan/tools/hw5330/README.md:302-303` の display の組の記述は、組が無くなったので直すか消す（Q1）。
