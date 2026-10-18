# WS260 の設計: i915 の Gen9 の display（gen9-*.c）の自前の書き直し

- 作成: 2026-10-18 P1（q1252、ws260-p001）。code は書かない。design-reviewer は使わない（ユーザーの決定）。
- 出典: Linux v6.8.12（build/ws229-p001/linux/linux-6.8.12.tar.xz）、今の tree（main 55aa80ef7 を取り込んだ 484178d46）、[WS245 の設計](../ws245/design.md)。
- 手本: WS245 の xd。PRM と hardware の振る舞いから自前で書き、Linux の行ごとの言い換えをしない。値は block ごとに `Values referenced:`、手順は `Procedure referenced:` で示す。PRM に載る名前は使ってよい（2026-10-18 WS244 の決定 1）。

## 1. 棚卸し（今の gen9-*）

### 1.1 file と役割（22 file、9,590 行）

| file | 行 | 役割 | 主な外の呼び手 |
| --- | --- | --- | --- |
| gen9-flip.c・.h | 1837+185 | firmware が点けた 1 つの pipe の plane を、driver の 2 つの buffer の間で flip する（bootfb の desktop） | bootfb.c、device.c（irq）、i915.c（resume） |
| gen9-node.c・.h | 1525+77 | 外の display（port B〜D）を node の display として出す: claim、present、wait、lease、session、suspend/resume | bootfb.c、i915.c |
| gen9-mode.c・.h | 1402+206 | 数の計算だけ: CDCLK、EDID からの mode、DP の link（rate・lane・M/N）、DPLL の語（DP・HDMI の WRPLL）、DDI buffer の表、訓練の調整、WM・DDB | gen9 の中、host 試験 |
| gen9-modeset.c・.h | 711+94 | register の列: well 2・DDI IO の well、DPLL、外の DP/HDMI の点灯と消灯、pipe の WM・DDB、plane、hotplug sense | gen9-outputs.c |
| gen9-outputs.c・.h | 711+138 | port B〜D の sink の検出（DP か HDMI か）、mode の選択、pipe の割り当て、全 pipe の WM・DDB を重ならない順に動かす | gen9-node.c |
| gen9-link.c・.h | 530+62 | DDI buffer の translation と signal level、DP_TP、link training（CR → EQ） | gen9-outputs.c |
| gen9-ddc.c・.h | 589+49 | AUX（native・I2C-over-AUX の EDID）と GMBUS | gen9-outputs.c |
| gen9-backlight.c・.h | 515+86 | 南の PWM（SPT/KBP の LPT の形、CNP/CMP の BXT の形）を /dev/backlight に出す | bootfb.c、i915.c |
| gen9-firmware.c・.h | 368+26 | GT の起動の前に firmware が残した display を読むだけで確かめる（VT-d の block など） | display.c |
| gen9-display.c・.h | 244+80 | `i915.gen9.display=` の語（段の上限）と報告 | bootfb.c |
| gen9-io.c・.h | 105+50 | register の窓口の表（kernel は MMIO、host 試験は model） | gen9 の中 |

Gen9 だけが使う intel/ の header: `display-regs-gen9.h`・`display-regs-gen9-modeset.h`（WS244 p002 で先頭の MIT の表示を外し、block ごとの referenced に直し済み）。`internal.h`（Linux 由来の型と own の型が同居）を include するのは gen9-flip・gen9-firmware・gen9-backlight・gen9-node の 4 つ。

host 試験: `tests/display/gen9-fake-hw.c`（592 行。register の file、AUX の DP sink、GMBUS の HDMI sink、well・DPLL・transcoder・DDI buffer の model と **書き込みの記録 8192 個**）と `host-gen9-{flip,backlight,mode,modeset,outputs}-test.c`。runner は master の Tools の `plan/ws237/tests/run-gen9-*-host-test.sh`。

### 1.2 写しの点検（Linux 6.8.12 と関数ごとに）

方法（補助の script は scratchpad、結果は build/tmp/ws260-p001-*/）:

1. **comment**: gen9-*.c・*.h の comment の 7 語の並びを、Linux の i915/display・drm/display・i915_reg.h の comment の 7 語の並びと照らした。**一致は 0**（全 22 file）。comment の写しは無い。
2. **code の字面**: gen9 の 178 関数のそれぞれを、Linux の display の全関数と 5 token の並び（識別子はそのまま）で比べた。最も高い物で 0.19（`i915_skl_safe_mode` 対 `intel_mode_from_crtc_timings`）、ほぼ全部が 0.1 未満。行の写しは無い。
3. **構造**: `Procedure referenced:` が名指す Linux の関数と、識別子を伏せた token の列の類似度（difflib）を 56 組で取った。

| 類似度 | 組（gen9 の関数 ↔ Linux） | 見立て |
| --- | --- | --- |
| 0.71 | i915_skl_aux_header ↔ intel_dp_aux_header | 4 byte の AUX の header。DP の規格の形そのもので、誰が書いても同じ形 |
| 0.51〜0.55 | dpll_off ↔ skl_ddi_pll_disable、dpll_dp ↔ skl_ddi_dp_set_dpll_hw_state、ddi_buffers ↔ hsw_prepare_dp_ddi_buffers、dp_link ↔ intel_dp_compute_link_config_wide、dp_m_n ↔ intel_link_compute_m_n、dpll_on ↔ skl_ddi_pll_enable、ddi_levels ↔ hsw_set_signal_levels | 小さい関数（45〜190 token）。ループの形（rate を昇順、lane を 1・2・4）と register の rmw の順が同じ。手順は Linux に従った |
| 0.3〜0.45 | aux_xfer、write_pipe_wm、outputs_init（WM の latency の調整）、output_off、clock_recovery、backlight の get/set ほか | 手順の順序が同じ。分け方・名前は別 |
| 0.04〜0.3 | output_on（hsw_crtc_enable ほか 10 関数を 1 つにまとめた）、wm_compute、buf_trans、channel_eq ほか | 分け方が違う |

結論: **字面の写し（code・comment）は無い。関数の手順と分け方は Linux の関数に 1 対 1 で沿って書かれている**（WS237 で Linux を手本に書いた。関数の head の comment に Linux の関数名が 63 個残っている: gen9-mode.c 27、gen9-modeset.c 15、gen9-link.c 13、ほか）。WS245 の基準（手順は PRM と hardware の単位で自前に組む。Linux の関数の分け方に沿わない）には合わないので、書き直す。

## 2. xd とどこまで共有するか

### 2.1 案

- **案 A（既定）: xd に display 9 の platform を足す。** xd は platform の記述（`struct i915_xd_platform`、表 P1）で分かれており、platform の名前で分岐しない。display 9 の違いは記述と、部品の中の「display 9 の形」の分岐に収まる:
  - そのまま使える（display 9 も同じ hardware の形）: `xd-aux`（DDI_AUX_CTL/DATA は同じ番地と形）、`xd-gmbus`（PCH の GMBUS）、`xd-edid`、`xd-dp`（DP の規格の側）、`xd-vbt`（VBT の block）、`xd-io`（書き込みの記録つきの窓口）、`xd-diag`。
  - display 9 の形を足す: `xd-platform`（SKL・KBL・CFL・CML の記述。port A〜E、PCH の SPT・KBP・CNP・CMP）、`xd-power`（well の表 W1 に SKL の PW1・PW2・DDI IO・AUX の well。DC5・DC6）、`xd-clock`（PLL の種類に「DPLL_CTRL1/2 の DPLL0〜3」を足す。CDCLK は LCPLL1 の読みだけ。HDMI の WRPLL の算法）、`xd-ddi`（combo PHY が無い。DDI_BUF_TRANS の表と iboost）、`xd-panel`（PWM の LPT の形を足す。CNP/CMP の形は今の形に近い）、`xd-pipe`（transcoder EDP は display 11 の扱いと同じ）、`xd-dbuf`（1 slice、892 block、8 level）、`xd-hpd`（SPT の SDE と PCH_PORT_HOTPLUG）。
  - display 9 では使わない: `xd-tc`・`xd-tc-port`・`xd-dmc` の pipe DMC・`xd-bandwidth` の QGV。
  - display 9 だけの振る舞い（今の Gen9 の約束。§4）: panel は firmware の mode のまま（mode set しない）、firmware の pipe の plane を flip、外の display は `i915.gen9.display=` の語で段を上げる。これは `xd-screen`・`xd-takeover` の「display 9 の方針」として持つ（firmware の panel を引き継いで flip する道は、xd の resident の run の display 9 版）。
- **案 B: 別の層（gen9 の新しい自前の file の組）。** gen9-*.c を同じ分け方の自前の file に書き直す。xd の部品は呼ばない。

### 2.2 比べ

| 観点 | 案 A | 案 B |
| --- | --- | --- |
| 書く量 | 新しい code 約 4,000〜5,500 行（display 9 の記述・表・分岐、display 9 の方針）。gen9-*.c の 9,600 行は消える | 約 7,000〜8,000 行（AUX・GMBUS・EDID・DP の訓練・WM をもう 1 組書く） |
| 写しの点検 | xd は WS245 で点検済み。足す分だけを見る | 全部を新しく点検 |
| 危険 | xd の部品に display 9 の分岐が入り、display 12・13（5330 ほか）を壊す恐れ。各段で display 12・13 の host 試験と、最後の 5330 の回帰が要る | display 12・13 に触れない |
| 保守 | 部品が 1 組。直しが両方に効く | 2 組の AUX・訓練・WM を別々に直す |
| 今の Gen9 の約束（firmware の panel を残す） | xd の screen に「display 9 の方針」を足す（xd の start の段の分岐） | 今の分け方のまま |

既定の案は **A**。理由: 部品が 1 組になる、点検が小さい、xd の platform の記述がもともと「違いは表に置く」形で、display 11 の transcoder EDP など display 9 と同じ形の物を既に持つ。危険（display 12・13 の回帰）は段ごとの host 試験（xd の host 試験を全部流す）と、最後の 5330 と SV7 の回帰で受ける。

## 3. 段の分け方と試験の道

### 3.1 置き換えの方法（WS245 §6.1 と同じ、回帰は最後に 1 回）

- R1 **段 = 1 つの hardware の機能**。段ごとに gen9 の 1 つの機能を xd の部品に置き換え、gen9 の旧 file の公開の関数は名前と型を残して中身だけ xd を呼ぶ殻にする（R2）。置き換えた中身は同じ段で消す（R3。file の削除は path を Q1 に送る）。
- R4 **受け入れ = build（amd64 既定・I915_TESTS=y、warning 0）+ host 試験**。実機の回帰は段ごとにはしない（ユーザーの指示「実機の回帰は最後に T1 に 1 回」）。最後の段の後に T1 が SV7（10.0.10.27、Gen9）と 5330（display 13、xd の回帰）で 1 回ずつ流す。
- 実機を段ごとに見ないので、**host 試験を回帰の柱にする**:
  1. **書き込みの列の golden**: 置き換えの前に、今の gen9 の code で `gen9-fake-hw` の場面（panel の flip、backlight の get/set、外の DP の点灯と消灯、外の HDMI、WM・DDB の移動、hotplug の enable）を走らせ、register の書き込みの列（番地・値・順）を `plan/ws260/tests/golden/` に保存する（p002）。各段の後、同じ場面を xd の道で走らせて列が一致することを確かめる。一致しない所は、段の phase.md に理由（意図した違いか誤りか）を書く。意図した違いは golden を直す前に Q1 に報告する。
  2. **数の計算の比較**: gen9-mode.c の純粋な関数（CDCLK、M/N、DPLL の語、WRPLL、buf trans の選択、WM）は、置き換えの前に入力の網（rate × lane × pixel clock、HDMI の clock 25〜594 MHz、WM の入力）で出力を表に取り、xd の版と全部一致を確かめる。
  3. **display 12・13 を壊さない**: xd の部品を変える段では、xd の host 試験（WS245 の tests）を全部流す。
- xd の model（WS245 の host 試験の fake）に display 9 の model を足すか、`gen9-fake-hw` を xd の窓口（`xd-io`）の下に繋ぎ直すかは p002 で決める（既定: gen9-fake-hw を xd-io の backend として繋ぐ。場面と golden をそのまま使える）。

### 3.2 段の表（案）

LW は 8 LW ≈ 実時間 2〜3 時間の換算（Q1 の memory）。

| Phase | 内容 | 受け入れ | LW |
| --- | --- | --- | --- |
| p001 | この設計 | Q1・ユーザーの判断 | 済み |
| p002 | 試験の土台: golden の書き込みの列と数の計算の表を今の code で取る。gen9-fake-hw を xd-io の backend に繋ぐ口 | golden が再現する（2 回流して同じ） | 4 |
| p003 | xd-platform に display 9 の記述（SKL・KBL・CFL・CML、PCH の SPT・KBP・CNP・CMP、port A〜E）。選ぶだけで呼ばない | xd の host 試験、build | 3 |
| p004 | 数の計算: CDCLK の読み、DP の link と M/N、DPLL の語（DP と HDMI の WRPLL）、buf trans の表を xd-clock・xd-dp・xd-ddi へ。gen9-mode.c の該当は殻 | 数の表の全部一致 | 6 |
| p005 | WM・DDB の計算を xd-dbuf へ（display 9 の 1 slice・8 level・IPC） | 数の表の全部一致 | 5 |
| p006 | AUX・GMBUS・EDID を xd-aux・xd-gmbus・xd-edid へ（gen9-ddc.c を殻に） | golden（EDID の読み、DPCD の読み） | 3 |
| p007 | backlight: SPT/KBP の LPT の形と CNP/CMP の形を xd-panel へ（gen9-backlight.c を殻に） | golden（get/set、suspend/resume） | 3 |
| p008 | power well（PW2・DDI IO・AUX）を xd-power へ | golden | 4 |
| p009 | DPLL の enable/disable と DDI clock の選択を xd-clock へ | golden | 3 |
| p010 | DDI buffer・signal level・iboost・DP_TP と link training を xd-ddi・xd-dp へ（gen9-link.c を殻に） | golden（訓練の各場面） | 5 |
| p011 | transcoder・pipe・plane の点灯と消灯を xd-pipe へ（gen9-modeset.c の残り） | golden（DP・HDMI の点灯と消灯） | 5 |
| p012 | WM・DDB の書き込みと順序、hotplug の sense を xd-dbuf・xd-hpd へ | golden（pipe の追加・削除の順） | 4 |
| p013 | firmware の点検と flip（firmware の panel の引き継ぎ、plane の flip、irq）を xd-takeover・xd-screen の display 9 の方針へ（gen9-firmware.c・gen9-flip.c） | golden（flip の場面）、host-gen9-flip の全部 | 6 |
| p014 | 外の display（gen9-node・gen9-outputs・gen9-display）を xd-screen・xd-display の口へ。bootfb.c・i915.c・device.c・display.c の呼び先を xd に付け替え、殻を全部消す | 全 golden、build。消す file の一覧を Q1 へ | 6 |
| p015 | 規約の全文の見直し、写しの点検（§1.2 の方法を新しい code に）、T1 に SV7 と 5330 の実機の回帰を 1 回 | T1 の結果（Q1 が判定） | 3 |

合計 約 60 LW（実時間 15〜23 時間）。危険の高い段: p008（well）、p010（訓練）、p011（pipe）、p013（flip、起動の desktop に直に効く）。

### 3.3 実機の回帰（p015、T1）

- SV7（10.0.10.27、Gen9）: 起動、graphical の login、内蔵の画面（firmware の mode のまま flip で desktop）、`backlight-probe` の get/set、外の display（`i915.gen9.display=` の語ごと。SV7 に外の display が繋がるかは T1 が確かめる。繋がらないなら外の display の確かめは「未実施」）。
- 5330（display 13）: WS245 の回帰の手順（ws245/ws.md「5330 の素の起動での段ごとの回帰の手順」）を 1 回。xd の部品の変更が display 12・13 を壊していない事を見る。
- 止まった時の戻し: 段を全部 main に入れてから 1 回で流すので、壊れた段の特定は二分（`git revert` の範囲を半分ずつ。memory「i915 display: implement all, then bisect」と同じ）。

## 4. 変えない振る舞い（受け入れの基準）

1. Gen9 の node は、`i915.gen9.display=` が無ければ firmware の mode を残す（mode set しない）。
2. firmware が線形の XRGB8888 の 1 pipe を点けていれば、driver の 2 つの buffer の間で flip する（bootfb の desktop）。条件が合わなければ今と同じ道（firmware の framebuffer への copy）に落ちる。
3. backlight は firmware の period を保ち、duty だけを変える。/dev/backlight の range と既定は今と同じ。
4. 外の display（port B〜D、display id 1 + port）は語が `modeset` 以上の時だけ。claim は空いた pipe に黒を出す。
5. dmesg の `i915:` の log の文言は、照合する試験・道具があれば追従させる（§5 の判断 3）。

## 5. ユーザーの判断が要る点と既定の案

1. **案 A か案 B か**（§2）。既定: **A**（xd に display 9 を足す）。A だと xd の部品が変わるので、display 12・13 の回帰（5330）も最後に 1 回要る。
2. **段ごとの実機の回帰をしない事の確認**。指示どおり最後に 1 回（SV7 と 5330）にすると、壊れた段は二分で探す。p008・p010・p011・p013 だけ段の直後に SV7 を流す案もある（T1 の時間が 4 回分増える）。既定: 指示どおり最後に 1 回。
3. **log の文言**: gen9 の log（`i915: gen9 …`）を xd の形（`i915: xd-…`）に揃えるか、今の文言を保つか。既定: xd の形に揃え、照合する試験・道具（master の Tools の run-host-boot-gen9-display.sh ほか）を同じ段で追従させる。
4. **`i915.gen9.display=` の語**: 段の上限の語（firmware・flip・backlight・modeset・full）を残すか。既定: 残す（SV7 で外の display の不具合を切り分ける口として、今と同じ意味で）。
5. **外の display の確かめ**: SV7 に外の DP・HDMI の display を繋げるか（ユーザーの手）。繋がらなければ外の display は host 試験だけで受ける（実機は「未実施」）。

## 6. WS244 との分け方

- WS244 p002（display-regs-gen9*.h の表示と referenced）は済み。WS260 で使う register の名前は既に `I915_SKL_*`。xd に移す時は xd の header（`display-regs-xd*.h`）に display 9 の block を足すか、`display-regs-gen9*.h` を xd から include するかを p003 で決める（既定: xd の header に移し、`display-regs-gen9*.h` は p014 で消す）。
- WS244 p008（display の残りの言及の掃除）は gen9-mode.h・gen9-outputs.h の言及を含むが、その file は WS260 で消えるので、WS244 p008 から gen9-* を外す（Q1 が WS244 の表を直す）。
