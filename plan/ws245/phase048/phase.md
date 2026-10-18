<!-- awesome-plan project=zedbsd record=ws245-p048 -->

# ws245-p048: screen の enable/disable の列（3 種を同じ段で。retained）

Status: cleared（2026-10-18 Q1 判定: q1229-i20・i21（5330 の実機）でこの段の「実機で確かめる事」の 5330 の項目を満たした（「判定の材料」の節、P1 q1241）。外部出力・suspend などの残りは beta2.md の U19〜U23 で追う）（旧: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない。design は「ユーザーが居る時間に流す」段））
Parent: [WS245](../ws.md)
Queue: q1229-i05（P1）
設計: [design.md](../design.md) §6.2 の p048 の行、§2.3、§2.4、§3.3

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-screen.c/.h` | light と dark の列（step の表）: light = power → dbuf → setup → pipe dmc → link power → pll → port → pipe → output → check → plane → settle（settle は失敗の後も走る）、dark = power → plane off → output off → port off → pll off → link power off → pipe dmc off → confirm → settle。step ごとに xd-io の trace に名前（`xd-screen <step>`）、失敗の扱い（light: 残りを飛ばし settle だけ、`undo` の時は最後に済んだ step の戻し点から dark の step を confirm の前まで走らせる。dark: 失敗で止め retained、settle を走らせない＝何も返さない）、screen の phase（OFF・POWERED・LINKED・LIT・RETAINED）。step の中身は呼ぶ側の hook |
| `display/modeset.c`（殻） | `drv_i915_lcd_modeset_commit_enable`・`_commit_disable` の中身を xd-screen の light・dark に（hook は今の commit の中身を step に分けた物: DC_OFF と crtc の domain、DBUF の pre-plane と MBUS、backlight の setup と active timings、crtc の part、link の確かめ、plane、settle / confirm）。`ms->screen` に phase。写しの更新は今のまま（`ms->crtc.active` は pipe の part と dmc off の part、`stop_unconfirmed` は confirm と settle、`dc_off_held` は power と settle、`plane_armed` は plane の段）。消した: `drv_i915_lcd_modeset_enable`・`_disable`（中身は step に） |
| `display/pipe.c`（殻） | `i915_hsw_crtc_enable/_disable` を part の関数（`I915_LCD_CRTC_PART_DMC/LINK/PLL/PORT/PIPE/OUTPUT`）に分けた。display 9 以前・GLK・HSW・bigjoiner の分岐は display 12・13 の道に無いので消した。`drv_i915_lcd_ms_crtc_enable/_disable` → `_enable_part/_disable_part`。display funcs の表の crtc_enable/disable は NULL（呼び手 0: p037 から takeover は xd の stop） |
| `display/modeset-internal.h`・`modeset.h`・`pipe.h` | part の定数と宣言、`ms->screen`、消した関数の宣言 |
| `tests/display/host-xd-screen-test.c` | +6 check（light の 12 step の順、check の失敗で plane を飛ばし settle は走る、undo、dark の 9 step、confirm の失敗で retained、hook 無し） |

段の phase.md の必須の項目:
- 消す file: 無し（`ddi.c`・`pipe.c` の encoder の段は step の中身として呼ばれ続ける）。
- 殻が更新する旧の写し: 上の表の 4 つ（M5 (3)）。
- 表 R: 書き手は今と同じ関数（列の順だけが xd-screen）。
- R11: `modeset.c`・`pipe.c`・`pipe.h`・`modeset.h`・`modeset-internal.h` 0。

## 旧と同じに保った物と違い

- 確かめ方: `sh build/tmp/refcmp.sh 44eecb132 p048` で旧の host 試験（dp 72・lcd 120）の出力が同じ。lcd の試験は commit_enable/disable を何十回も通り（family A の順序、HDMI、retained、DBUF、observer の点）、書き込みの列も observer の標本も変わらない。
- 設計との差: (1) **step の中身は殻の関数**（ddi.c・pipe.c の encoder の段で、hardware は p028〜p035 から xd の部品）。xd-screen が持つのは順・trace・失敗の扱い・undo・phase。step の中身を xd の部品の直の呼びにするのは、Linux の型の state（PPS・backlight・wakeref・TC link）が消える p050 で。(2) **plane の位置は Linux の順のまま**（§2.3 の 8 は transcoder の timing の前だが、決定 4「Linux を優先」と 5330 での実績で、plane は enable の後）。書き込みの列の差は 0 の見込み（xd-io の step の印が増えるだけ）。(3) light の undo は殻では使わない（`undo` 0: 失敗の後は今のとおり呼ぶ側の disable が止める。undo は host 試験だけ）。
- 3 種（eDP・HDMI・DP-TC）は同じ列（違いは step の中の encoder の段）。

## 実機で確かめる事（5330、全部の段の後の 1 回で。ユーザーが居る時間に）

- 起動・session・compositor・LCD（survey）・backlight・GPU の投入・device lost 0（U10 と同じ）。digest の差が無い（`xd-screen` の step の印は書き込みでない）。
- 節目（ユーザーの一覧）: HDMI の plug で 2 画面、USB-C DP（TC2）の plug で 2 画面、unplug で戻る、HDMI を挿したまま蓋を閉じての起動（GOP が外部を灯す）、LCD の目視。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p048/xd-host` → 全部 pass（screen 39）。
- 旧の host 試験: `sh build/tmp/refcmp.sh 44eecb132 p048` → dp・lcd とも SAME。
- build（2026-10-16）: `sh build/tmp/kbuild.sh p048` で 6 組が rc 0・warning 0（undo の直しの後に既定の組をもう一度、warning 0）。`git diff --check` は空。
- 5330: 未実施（全部の段の後に 1 回）。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- session・compositor（HANDOFF）・LCD（survey lit）・backlight・GPU の投入（perf）・device lost 0

満たしていない・未確認の基準と残る物:

- HDMI・USB-C DP の 2 画面と unplug、蓋を閉じた起動、LCD の目視: ユーザーの一覧（beta2.md の U19〜U23）の項目で、未実施

P1 の見立て（Status は変えない。判定は Q1）: cleared にしてよい。
