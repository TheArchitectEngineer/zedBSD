<!-- awesome-plan project=zedbsd record=ws245 -->

# WS245: i915 の Gen12 系（display 12・13）の display の書き直し

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27。Phase は全部 cleared か canceled（2026-10-26 23:40 Q1）。受け入れは 2026-10-19 ユーザー「WS245は5330で自動テスト合格ならそれでいいです。」と「SV7は試しません。clearedの条件は5330のみにします。」に従い、5330 の素の起動の自動の回帰の合格で判定）
Primary Milestone: MG006
Related Milestones: MG002
Parent: [Master](../master.md)
Related: [WS244](../ws244/ws.md)（i915 の Linux 由来の code の整理）、[WS252](../ws252/ws.md)（ICL・RKL・ADL-S の GT。display 11 の ICL は p045 の xd の表で入った）、[Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-15）

「Gen12（5330）のディスプレイは書き直します。WSを立ててください。」
（前提の決定: 「code・コメント・構造:については、MITであっても写してはいけません。」「範囲を全部の WS に広げます。」「ガードレイルに、ファイル上部には参考にしたことや引用したことを一切書かず、該当箇所にブロック単位で記載することを明記してください。」）
進め方（2026-10-15 夜）:「ただし、各ステップで5330で動くかどうかをきちんとリグレッションテストしながら、ステップで書き換えていきます。そうでないと破綻します。」、2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」

## 結果

- display 11〜13（ICL・TGL・RKL・ADL-S・RPL-S・ADL-P の系列）の display を、`src/drivers/gpu/i915/display/` の xd の部品（`xd-*.c`: core・power・DMC・clock・DPLL・DDI・DP・AUX・TC・pipe・plane・DBUF・帯域・HPD・takeover・screen・picture・output・diag）と自前の殻（`display.c`・`head.c`・`resident.c`・`present.c`・`edp-device.c`・`hpd.c`・`tc-port.c`・`display-power.c`・`run-log.c` ほか）に書き直した。設計は [design.md](design.md)（改訂 6）。
- Linux の code・comment・構造を写した旧 file（modeset・state・ddi・dp・pipe・plane・panel・edid・takeover・hotplug・tc・watermark・power・dc9・diagnostics・intel/ の Linux 型の header ほか）は全部消した（段ごとに中身を空にし、merge の時に Q1 が削除）。register・表はデータとして block ごとの「Value(s) referenced:」で残す。
- 段ごとの 5330 の回帰（T1 の i18〜i21、P4 の q1597）: 起動・GOP の引き継ぎ・内蔵 panel（eDP）・backlight・compositor の画面が書き直しの前と同じ（survey の steady の register の差 0）。最後は main 684a5243f（2026-10-26、q1597）で PASS、以後も WS252・WS293 の kernel で PASS（`plan/ws245/tests/evidence/`）。
- host 試験: `plan/ws245/tests/run-xd-host-tests.sh`（xd の部品の model の試験の全部、check-sources・check-display-steps の監査を含む）all passed（2026-10-27 の WS252 p008 でも）。

## 制限と移管

- 5330 の実機で確かめていない物（ユーザーの試験の一覧のまま）: suspend の戻り、外部の HDMI・USB-C の DP、hotplug、mode set の切り替え、LCD の目視の詳細、reboot の止める道。
- SV7（Gen9.5）・5320 では試していない（2026-10-19 ユーザー「SV7は試しません。」）。display 11（ICL）・RKL・ADL-S の実機は無い（WS252 p007 に移管）。
- DC6 に入らない: [BUG-334](../bugs/BUG-334.md)（書き直しの前からの性質、tracking）。
- ICL・RKL・ADL-S の DMC の file は 2026-10-27 に i915-firmware の package に入った（WS252 p006・q1601）。

## 試験と道具

- 回帰に使う物（plan/tools への移動を Q1 に提案中、2026-10-27）: `tests/run-xd-host-tests.sh`（`check-sources.py`・`check-display-steps.py`・`check-xd-regs-linux.sh`/`.c`・`linux-stub/` を使う）、5330 の実機の `tests/5330-run.sh`（`5330-survey.sh`・`5330-survey-diff.py`・`config-5330.mk`・基準 `evidence/t1229i21-main-merged-20261017.md`）と `tests/5330-bootcycle.sh`。
- Phase の dir、`temp/`、WS だけの試験・証拠は削除を Q1 に依頼した（2026-10-27）。

## Phase

| ID | 内容 | 状態 |
| --- | --- | --- |
| p001 | 設計: display 11〜13 の model、外向きの口と契約、データの表の引用の方法、段の分割と順序、試験の道、LW。design-reviewer を通す（5 回） | cleared（2026-10-16 Q1、design.md 改訂 6） |
| p002 | 土台: register と表の header、xd-io、xd-platform、host の試験の枠と own の register の model、own の型の分離、Gen9 の own への移設 | cleared（2026-10-15、T1-699 PASS、U10 で 5330 の素の起動に合格） |
| p003 | （改訂 3a）口 `xd.h` と adapter、`i915.display=` | **canceled**（2026-10-15 夜、段ごとの置き換えに置き換え。adapter と boot の語は作らない: design.md §6.1 R5） |
| p004 | VBT（合成の fixture）・EDID の解析（呼ばれない部品） | cleared（2026-10-15、host 試験、main 2d5b167aa、U10 で 5330 に合格） |
| p005 | （改訂 3a）AUX・GMBUS | **canceled**（→ p019・p020） |
| p006 | （改訂 3a）power・DMC・clock・TC の register | **canceled**（→ p023〜p028・p040。q1207 の attempt は uncleared: 途中の差分は `temp/p006-partial-20261015.patch` と `temp/p006-partial-20261015-untracked/`、design.md §6.7） |
| p007 | （改訂 3a）DDI・DP・panel | **canceled**（→ p021・p022・p029〜p031） |
| p008 | （改訂 3a）pipe・DBUF | **canceled**（→ p032〜p035） |
| p009 | （改訂 3a）screen・takeover・xd の opt-in | **canceled**（→ p036〜p038） |
| p010 | （改訂 3a）hotplug・Type-C・外部 DP・HDMI・head | **canceled**（→ p039・p040） |
| p011 | （改訂 3a）suspend・既定の切り替え | **canceled**（→ p041。既定の切り替えは無い） |
| p012 | （改訂 3a）platform の網羅 | **canceled**（→ p044） |
| p013 | （改訂 3a）旧 code の削除と出典の監査 | **canceled**（→ 各段で消す + p043） |
| p014 | （改訂 3a）display 11 | **canceled**（→ p045） |
| p015 | （改訂 3a）規約の全文 | **canceled**（→ p046） |
| p047 | 回帰の証拠の道具: own の survey（PP_STATUS・PWM・frame counter・SURFLIVE・pipe CRC・underrun・TRANS_CONF などを picture up の後に dmesg へ）と `mmio.c` の書き込みの log（`i915.debug=display`）、`5330-survey.sh`（改訂 5、レビュー 4 の B3・M6） | cleared（4 LW。2026-10-16 main 3e1e0c687。LCD の目視は beta2.md の U11） |
| p016 | OpRegion の場所と map（xd-vbt の find、`struct i915_xd` と io・platform の用意）。opregion.c を消す | cleared（2026-10-16 5330 の回帰に合格、main 9244ac507、旧 file の削除 54fc334c5。SV7 は beta2 の U12） |
| p017 | VBT の解析（xd-vbt を真に、旧 parser と影の比較を 1 段） | cleared（2026-10-16 5330 の回帰に合格、main 45c83da79） |
| p018 | 旧 VBT parser の本体の削除（本番の呼び手 10 関数を殻 `vbt-shell.c` に、ktest-display の 2 file を削除）。2026-10-16 に p018 と p053 の 2 段に分けた | cleared（2026-10-16 5330 の回帰に合格、merge 8fea53a1d、旧 4 file は Q1 が削除） |
| p053 | EDID の decode を xd-edid に（`edid.c` の decode を消し、`state.c` の mode を xd-edid の first timing に。drm_modes の helper は edid.c に殻として残る）。p018 から分けた | cleared（2026-10-16 T1-702 で 5330 の回帰 PASS、全部の列の差 0、merge 7e3bf81cf） |
| p019 | AUX の transaction（xd-aux。well・DC off・VDD は呼ぶ側） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p020 | GMBUS と DDC の EDID の読み（xd-gmbus） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p021 | backlight の PWM（xd-panel） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p022 | panel power（PPS、xd-panel） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p023 | power well の操作（W1、xd-power。p006 の差分を当て直す） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p024 | well の所属（W3）と refcount（xd-power） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p025 | DC state と DC9（xd-power、dc9.c を消す） | cleared（2026-10-19 Q1、DC6 は BUG-334） |
| p026 | DMC（xd-dmc） | cleared（2026-10-19 Q1、DC6 は BUG-334） |
| p027 | CDCLK（xd-clock） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p028 | combo DPLL（xd-clock） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p029 | combo PHY（xd-ddi: init・lane・swing） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p030 | DDI の buffer（xd-ddi: BUF_CTL・DP_TP・idle） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p031 | DP の訓練（xd-dp） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p032 | transcoder と pipe（xd-pipe） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p033 | plane と flip（xd-pipe） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p034 | DBUF と watermark（xd-dbuf） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p035 | display の init/uninit の列（xd-core、司令） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p036 | GOP の読み出しと判断（N0、xd-takeover。旧 N0 と影の比較） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p037 | 引き継ぎ（N1）の判断と GOP の出力の止め方（xd-takeover。readout・setup_outputs・n1 の helper は p050 まで旧） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p038 | screen の plan（純関数。eDP・HDMI・DP-ext の 3 種を同じ段で） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p048 | screen の enable/disable の列（3 種を同じ段で。retained） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p049 | flip と resident run の契約（EAGAIN・UNPLUGGED・RELIGHT・acquired） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p050a | takeover の readout と sanitize を xd-takeover の record と stop に（takeover.c の registry を消す）。2026-10-16 Q1 の判断で p050 を 4 段に分けた（design の 4 LW を大きく超えるため） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p050b | eDP の step の本体を xd の部品（PPS・backlight・AUX・DDI の IO power・訓練の DPCD・WM/DBUF の書き込み・vblank の event）を直に呼ぶ形に | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p050c | HDMI と DP-TC の step の本体 | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p050d | 口 `xd.h` と own の file の付け替え（backlight.c を含む）、旧 file（modeset・state・ddi・dp-sink・pipe・plane・aux・panel・panel-backlight・vblank・takeover の残り・internal.h の Linux の型）と旧 host 試験（host-dp-test・host-lcd-modeset-test）の削除、oracle の check を xd の試験へ | 4 段に分けた（2026-10-17 Q1、q1229-i08）: 下の p050d1〜d4 |
| p050d1 | 内蔵 panel の sink の取得（PPS の用意・VDD・DPCD・EDID）と kernel の panel・DPCD の hook を xd の部品に（旧の Linux の eDP の world は旧 host 試験の間残す） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p050d2 | prepare の check（state.c の compute・CDCLK）と WM/DBUF を xd-screen の plan・xd-dbuf に | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p050d3 | plane・flip・vblank を xd-pipe・xd-screen に | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p050d4 | 口 xd.h と own の file の付け替え、旧 file と旧 host 試験・ktest の削除（oracle を xd の試験へ） | 4 段に分けた（2026-10-17 Q1、q1229-i11）: 下の p050d4a〜d4d |
| p050d4a | 画面の口: 新しい xd-picture.c に画面の object と prepare・light・dark・plane・flip・status・retained・backlight を own の型で、head.c・present.c・backlight.c を付け替え | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p050d4b | 走らせ方の口: resident run・show run・kernel の binding を own の resident.c へ（fill が picture の config を直に）、output.c の link を xd-screen の plan_link に | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p050d4c | 始まりと終わり: port の表（xd）、eDP の device を own の edp-device.c へ、始まりの前半を xd の部品で、VGA を own の vga.c へ、takeover.c を空に | cleared（2026-10-26 23:40、5330 の自動の回帰 PASS、q1597。前: in-progress（2026-10-17 q1229-i12 で実装・hos） |
| p050d4d | 旧 file の削除と旧 host 試験・fake の終わり（size、`I915_XD_STRICT`、5320・SV7・QEMU の節目） | cleared（2026-10-19 Q1、T1-719） |
| p039 | hotplug（xd-hpd。hotplug.c を消す） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p051 | own の output.c の port の規則と head-rules の pipe の規則を platform の記述に（hdmi.c・hdmi-mode.c・head-rules.c を消す） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p040 | Dekel の窓と TC PHY、TC PLL（xd-tc の窓を dkl-phy.c と一緒に入れ替える、clock.c の残りを消す） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p052 | Type-C の状態機械（流れ A）と USB-C の DP（UCSI、DP-ext の probe。tc.c・tc-kern.c・dp-ext*.c を消す） | cleared（2026-10-18 Q1、5330 の i20・i21） |
| p041 | suspend の戻り（xd-display） | cleared（2026-10-26 23:40、5330 の自動の回帰 PASS、q1597。前: in-progress（4 LW。2026-10-17 q1229-i14 で実） |
| p042 | display.c の段の縮約（§7 #12）・log の文言・xd-diag | cleared（2026-10-26 23:40、5330 の自動の回帰 PASS、q1597。前: in-progress（4 LW。2026-10-17 q1229-i15 で実） |
| p043 | 旧 code の残りの削除・試験の整理・出典の監査 | cleared（2026-10-26 23:40、5330 の自動の回帰 PASS、q1597。前: 2 段に分けた（2026-10-17 Q1、q1229-i15）: 下の p04） |
| p043a | 試験の整理・intel/ の Gen12 だけの header・PCH の判定・clock.c・§5.4 の監査 | cleared（2026-10-26 23:40、5330 の自動の回帰 PASS、q1597） |
| p043b | watermark.c の帯域を xd に（段の境 1）、power.c の domain 層を xd-power に（段の境 2）、Derived の表示の削除 | cleared（2026-10-26 23:40、5330 の自動の回帰 PASS、q1597） |
| p044 | platform の網羅（display 12 の行）と TGL の TC（流れ B、host だけ）。#21 で全部 xd | cleared（2026-10-26 23:40、5330 の自動の回帰 PASS、q1597。前: in-progress（4 LW。2026-10-17 P6 が q1236-i） |
| p045 | display 11（Ice Lake） | cleared（2026-10-26 23:40、5330 の自動の回帰 PASS、q1597。前: in-progress（8 LW。実機は無い。2026-10-17 P6 が q） |
| p046 | 規約の全文の見直しと p045 の出典の監査 | cleared（2026-10-19 Q1） |
