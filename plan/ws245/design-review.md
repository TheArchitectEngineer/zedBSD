<!-- awesome-plan project=zedbsd record=ws245-p001-design-review -->

# WS245 p001 の設計（design.md）の敵対的レビュー

2026-10-15 design-reviewer（P2 の worktree、main 1d8cc4d19）。対象: [design.md](design.md)（522 行）。design.md は直していない。
根拠は `file:行`（path は `src/drivers/gpu/i915/` からの相対、無印の `design.md:行` は対象の文書）と PRM の text
（`plan/ws245/temp/*.txt` の行）。Linux の照合は `/home/awe/zedBSD-worktrees/p1/build/ws229-p001/linux/linux-6.8.12/`（読むだけ、値と分岐の確認だけに使った）。
呼び出し元の照合は、Gen12 の 38 file の公開関数を識別子で集め、それ以外の全 source から参照を数えた（script は scratchpad、tree には置いていない）。

分類: **blocker** = このまま p002 以降に進むと 5330 で今動いている物が壊れるか、選んだ方式が成り立たない。**major** = 直さないと実装か受け入れで詰まる、または規則に反する。**minor** = 記述の誤り・小さな欠落。

件数: blocker 4、major 18、minor 16。

## blocker

### B1. ACM（Alchemist = DG2、Xe-HPD）を「display 13 の差分」として ADL-P に当てている。ADL-P に無い機能を設計に入れている

- design.md:15-16（「ADL-P は TGL に … CDCLK の squash などを足した物」）、:25-26（ACM を display 13 の差分の出典に）、:146（CDCLK の squash、PLL を 1305.6 MHz に固定）、:148（pipe count の mailbox を xd-power の pipe well の上げ下げに付ける）、:231・:233（xd-power・xd-clock の責務）、:383（CDCLK_SQUASH_CTL）、:404（T5 の squash の window）。
- 事実: ADL-P（XE_LPD）は CDCLK の **crawl** を持ち、squash を持たない。squash は DG2（xe_hpd）と MTL 以降。Linux `display/intel_display_device.c:690-705`（`xe_lpd_display` は `.has_cdclk_crawl = 1`、`xe_hpd_display` が `.has_cdclk_squash = 1`）。今の code も同じ読み: `display/clock.c:68-69`「Alder Lake-P can crawl the PLL; neither platform squashes」、`clock.c:374-381`。
- pipe count の pcode の通知は DG2 だけ（Linux `intel_cdclk.c:2522`・`:2567` の `IS_DG2`）。ACM Vol 12 の「Sequence for Pipe Count Change」（`acm-vol12-display-engine.txt:6130-6148`）は DG2 の手順。設計の「今の code はこれを pcode の pmdemand として持つ」（design.md:148）も誤り: pmdemand は display 14 の register の仕組みで、今の code は display 14 でしか使わない（`display/watermark.c:847-850`）。
- ACM の PHY は Synopsys（`acm-vol12-display-engine.txt:4267-4275` の SNPS_PHY_MPLLB）で、DKL も combo も ADL-P と違う。south の register も DG2 の物で、設計自身が SDE の bit の違いに気づいている（design.md:410）。
- 起こる事: xd-power が mode set ごとに ADL-P で未定義の mailbox を送る（失敗を mode set の失敗と扱えば eDP も灯らない）。xd-clock が ADL-P に無い CDCLK_SQUASH_CTL を読んで CDCLK を計算すると、GOP の CDCLK を誤読して mode を拒むか、足りない CDCLK で灯す。
- 修正: §0.1 の出典の表で ACM を「DG2（Xe-HPD）。ADL-P と同じ display 13 だが CDCLK・PHY・PLL・ports は別物」と明記し、ACM を使うのは south display の register の **offset** の確認だけにする（bit は Linux の ADL-P/ICP 系の値と 5330 の dump で 1 つずつ照合）。squash・pipe count・T5 の squash の行を消し、CDCLK の読みは crawl の形（PLL ratio と CD2X）だけにする。CDCLK を変えない方針（design.md:147）なので crawl の手順も不要と書く。

### B2. Type-C の model が TGL の物で、ADL-P の今の動き（5330 で動いている USB-C DP）と合わない

- design.md:136（TC の AUX の後に TCCOLD）、:162（TCCOLD を GT driver mailbox 0x80000026 で止める）、:163-165（DFLEXDPPMS/DFLEXDPCSSS の handshake、「live state は DFLEXDPSP（割り込みの ISR は信用しない）」）、:236（xd-tc が xd-pcode で TCCOLD）、:394（command の表の TCCOLD 0x26）、:407（T8 の TCCOLD 200/600 µs）。
- 事実: ADL-P には TC_cold_off の well も pcode の TCCOLD も無い。DP-alt/legacy では「legacy の AUX の power domain を持つ」事が TC cold を止める（Linux `display/intel_tc.c:757-766` `adlp_tc_phy_cold_off_domain`。TC_cold_off の well は TGL だけ: `intel_display_power_map.c:952-994`）。今の code も pcode を使わない（`display/power.c:2248-2251` は TGL の TC cold-off を「not ported」と書く）。
- live state: ADL-P は DE_HPD の ISR と SDEISR で読む（Linux `intel_tc.c:768-794` `adlp_tc_phy_hpd_live_status`。今の `display/tc.c:90-96`・`:947-948` も DE_HPD_ISR 0x44470 と SDEISR）。設計の「DFLEXDPSP を読み ISR は信用しない」は TGL の `icl_tc_phy_hpd_live_status` の読み方で、ADL-P では今と違う結果を返し得る。
- 設計の中の矛盾: §2.8 の connect の bullet（:163）は TGL の DPPMS/DPCSSS の flow を書き、次の bullet（:166）と §7 #4（:500）は「display 13 の取り方（今と同じ）」を既定にする。どちらで実装するかが本文から決まらない。
- 起こる事: USB-C DP（受け入れの条件、ws.md:29）の connect が pcode の失敗で abort するか、live state の誤読で plug を見落とす。
- 修正: §2.8 を ADL-P の flow に書き直す: live = DE_HPD_ISR（TC: bit 16+n、TBT: bit n）と SDEISR（legacy: bit 24+n）、ready = TCSS_DDI_STATUS の bit 2、所有 = DDI_BUF_CTL の bit 6、TC cold の阻止 = AUX USBCn の well を所有の間保持。TCCOLD の mailbox を表 T8・§5.2・xd-tc・xd-pcode から消す。手順の出典は「Procedure referenced: Linux v6.8.12 … (adlp_tc_phy_*)」で書く必要があり、§5.4 の禁止と衝突する（M11）。

### B3. 「seam で一度に切り替え ＋ `i915.display=old` の逃げ道」と Phase の分割が両立しない

- design.md:427-429（own の file を p008 で xd の口に付け替え、`i915.display=old` で旧の段に戻る）、:332（`struct i915_display` から 40 近い field を消す）、:316（own の file は「呼び出し側の行を直す」だけ）、:444-445（p008 = eDP だけ、hotplug・TC・HDMI・head は p009）、:497（§7 #1 の既定は xd）。
- 事実と矛盾:
  1. own の file は旧 code の型と field を直に使う: `rctx`/`rlcd`/`.state.`/`i915_lcd_*`/`i915_hpd_output`/`i915_gop_output`/`world` などの参照が present.c 20、head.c 66、output.c 56、display.c 78 箇所（grep の数）。例 `display/output.c:391`・`:530`（`display->rctx.lcd`）、`output.c:401`（`display->output.state.mode` は `struct i915_lcd_mode`）。p008 で own の file を xd に付け替えると、`old` の時に旧 code を呼ぶ道が無くなる。`old` を残すなら、own の file の約 220 箇所に「old か xd か」の分岐を置くか、旧の型と xd の型の両方を `struct i915_display` に持つ事になる。これは設計が退けた案 B（adapter、design.md:431）そのもの。
  2. p008 は interrupts.c の hotplug の入口を `xd_hpd_irq_*` に向ける（design.md:296、:327）が、xd-hpd は p009（:445）。present.c の `drv_i915_hpd_topology_touch`（`display/present.c:809-811`）、output.c の hpd の 6 関数（`output.c` → `hotplug.c`）も同じ。p008 の xd は hotplug・外部の出力・head を持たない。
  3. §7 #1 の既定（xd）で p008 を main に入れると、5330（ユーザーの日常の機械、ベータ2 と OSC のデモの機械: master.md:121・:147）で HDMI・USB-C・2 画面・GOP が外部を灯していた起動（M6）が p009 まで退行する。
- 修正（どれかを選んで §6 を書き直す）: (a) p008 と p009 を 1 つの切り替えの単位にし、xd が全部の受け入れ（eDP・HDMI・USB-C・hotplug・head・move・suspend）を host 試験と passthrough（B4）で満たしてから seam を切る。`old` の逃げ道は「切り替えの commit の前の image」で代える（kernel の中に 2 つの tree を持たない）。(b) 逃げ道を kernel の中に持つなら、own の file の分岐の場所と数、`struct i915_display` の両持ち、どちらの tree も触らない register の所有を明記し、LW を足す。どちらでも「p008 の時点で xd が持たない機能」が `xd` 既定の image で退行しない事を受け入れの条件に書く。

### B4. 試験の道が、5330 の passthrough（agent が日常に使う実機の道）を「使わない」とし、実機の確かめを全部ユーザーに回している

- design.md:458（「VFIO の passthrough … 5330 を host にする必要があり、ユーザーの機械なので使わない」）、:459-463（実機は全部ユーザーの確かめ）、:473（実機の scenario を p011 で消す）。
- 事実: 5330 の passthrough は「日常の試験」（`plan/tools/hw5330/README.md` §1.2: 5330 自身が KVM の host、iGPU を vfio-pci で guest に渡し、i915 が本物の LCD を駆動。`/tmp/i915-hw.lock` の下で T1 が流す）。master.md:631（ユーザー「5330はつけっぱなしですので…再起動は自由にどうぞ」）、:833（WS101 の 5330 の測定は T1 が passthrough で）、Tools の表 :1018・:1039（test-hw.sh・capture-hw.sh）。`platform/amd64/vmunix.mk:218-229` の `I915_TEST_SET=display`（lcdb lcdc lcdd lcdg lcdo lcdr hdmib dual dual_share n1 aux hdmi_edid hdmi_hpd）は passthrough で流す実機の scenario の組。
- 起こる事: p002〜p010 の 30 LW 分を、実機の帰還無しに host の fake の model だけで書き、最初の実機の確かめが p008 の「ユーザーの確かめ」になる。memory の規則（ユーザーに今の試験を頼まない、一覧に載せる）の下では、帰還はさらに遅れる。xd の TC・DMC・takeover の誤り（B1・B2・M2）は host 試験では見つからない（fake の model は設計者が書いた PRM の読みを返すだけ）。
- passthrough の制約も設計に入っていない: guest には OpRegion が見えない（ASLS=0）ので VBT 無しの既定の道で走る（README §1.2、`I915_TEST_VBT` は 2026-10-14 に削除: `vmunix.mk:203-206`）。GOP の takeover は passthrough では確かめられない（README §1.2、`plan/ws084/ws.md:44`）。
- 修正: §6.3 に「T1 の 5330 passthrough」を加える: p005（power・DMC・DC）、p006（eDP の訓練・backlight）、p008（eDP の resident run）、p009（HDMI・USB-C・head）の各々の終わりに、xd の kernel で passthrough の scenario を T1 に依頼する（`i915.display=xd` の image、VBT 無しの既定の道）。takeover と suspend だけを単独起動（ユーザー）に残す。`I915_TEST_SET=display` の scenario は消さずに xd の口へ付け替えるか、同じ観測（LCD の resident buffer の読み戻し）を xd 用に作り、§6.4 の「p011 で消す」を直す。

## major

### M1. raw clock は 19.2 MHz。backlight の周期 96000 は 19.2 MHz / 200 Hz

- design.md:65（「raw clock 38.4 MHz」）、:189（「PWM clock = raw clock 38.4 MHz」「max 96000（= 38.4 MHz / 400 Hz）」）。
- 根拠: `plan/ws031/display-ref/dbg-i915_display_capabilities.txt:21`（rawclk rate: 19200 kHz）、`dmesg-display.txt:35`、`vbt-decode.txt:470`（PWM freq: 200）、`regs-selected.txt:7`（BLC_PWM_PCH_CTL2 0x00017700）。38.4 MHz は DSSM の参照 clock（CDCLK・DPLL 用）で別の clock。
- 起こる事: xd-backlight が VBT の 200 Hz から周期を 192000 と計算し、backlight device の range（§4.4 の受け入れ: 96000）が変わる。RAWCLK_FREQ を 38.4 MHz で書けば（§2.2 の init）、PP の delay の単位・GMBUS・HPD の filter の時間が 2 倍ずれる。
- 修正: §1 と §2.11 を 19.2 MHz に直し、raw clock は「SFUSE_STRAP/RAWCLK_FREQ を読む（固定値にしない）」と書く。p006 の受け入れ（d）に「VBT 200 Hz → 96000」を加える。

### M2. DMC の載せ方の誤りと欠落

- design.md:195（「payload を program storage（main は 0x8F000 から …）へ」）、:194-195（手順に event handler の無効化・clock gating・PIPEDMC_CONTROL が無い）、:385（PIPEDMC_CONTROL を register の表にだけ載せる）。
- 事実: program storage の番地は firmware の header の `start_mmioaddr`（v1 は 0x80000）。0x8F000 は main の event handler の register の block（`display/dmc.c:77-78`・`:96-103`・`:916-956`・`:431`）。0x8F000 に payload を書くと DMC の制御 register を壊す。
- 欠落: 載せる前の全 event handler の無効化（`dmc.c:413`）、pipe DMC の載せ方の前後の CLKGATE_DIS_PSL_EXT の PIPEDMC_GATING_DIS（`dmc.c:406`・`:465`、Wa_16015201720）、pipe の enable/disable の時の PIPEDMC_CONTROL（`dmc.c:591-619`）。§2.3・§3.3 の enable/disable の列に PIPEDMC の step が無い。
- 起こる事: 設計自身が危険に挙げる「DC6 で display が止まる」（design.md:516）の直接の原因になる。
- 修正: §2.12 を「header の start_mmioaddr に dword で書く。0x8F000 からは event handler の register」に直し、上の 3 step を §2.12 と §3.3 の enable/disable に加える。p005 の host 試験（c）に「program の書き先が header の番地」を加える。

### M3. Gen9 との境界は関数 3 つではない。internal.h の型と output.c の口を共有している

- design.md:311（「Gen9 が Gen12 の file から呼ぶ物は 2 つだけ … 一切触らない」）、:332（`struct i915_display` の field を消す）、:337（`struct i915_xd_mode` を新しく作る）、:447（p011 で internal.h の Linux 部分を消す）。
- 事実: gen9-mode.h:22、gen9-backlight.c:31、gen9-flip.c:46、gen9-node.c:30 が `internal.h` を include する。Gen9 は `struct i915_lcd_mode`（`display/internal.h:2312`。gen9-mode.c/.h・gen9-modeset.c/.h・gen9-node.c・gen9-outputs.h で 12 箇所）、`struct i915_pch_state`（internal.h:1057）、`struct i915_scanout`（:2565）、`struct i915_resident_display`（:3284）、`struct i915_display`（:3722）を使い、`drv_i915_output_edid_timing()`（`display/output.h:85`、引数が `struct i915_lcd_mode *`）を呼ぶ。internal.h は Gen12 の `dkl-phy.h`・`dp-ext.h`・`tc.h` を include する（internal.h:83-85）。
- 矛盾: p002 で 3 関数を gen9-outputs.c・bootfb.c に移す（design.md:311・:438）のは「Gen9 は一切触らない」と矛盾し、WS244 の (1)「Gen9 の display を guardrail に沿って直す」（plan/ws244/ws.md:48）と同じ file を同時に変える。
- 起こる事: p011 で internal.h の Linux 部分と tc.h などを消すと Gen9 の build が壊れる。xd-edid が `drv_i915_output_edid_timing()` を呼ぶ（design.md:228）と、xd の型が `i915_lcd_mode` に縛られる。
- 修正: §4.1 に「共有の型」を列挙し、p002 の仕事として「Gen9 と own の file が使う型（mode・pch・scanout・resident・display の root）を Linux 由来でない own の header へ移す」を加える。`i915_xd_mode` を作らず、共有の mode の型を 1 つにする。gen9 の file を変える時は WS244 と順序を決める（M14）。

### M4. 呼び出し元の一覧（§4.2）から抜けている own の file と口

grep で、Gen12 の file の外から Gen12 の公開関数を呼ぶ file を全部数えた。§4.2 に無い物:

- `display/tc-kern.c`（own、Zlib）: Gen12 の 17 関数（power の get/put、dkl-phy の read/write/rmw、tc の 10 関数、`drv_i915_vbt_encoder_for_port`）を呼び、UCSI の層へ `drv_typec_display_report`・`_forget` を送る（tc-kern.c:55-56、`src/drivers/typec/typec.c:753`・`:837`）。WS245 の対象の一覧（ws.md:21）にも §4.2 にも p011 の削除の一覧にも無い。tc.c を消すと build が壊れ、残すと Linux の型の `tc.h` に縛られる。design.md:236 は「drv_typec_display_report の口を保つ」と書くだけ。
- `display/capture.c`（own、I915_TEST_CAPTURE の試験の display、`hotplug.h` を include）: §4.2 に無く、`host-capture-test.c` は §6.4 で消す（design.md:470）。own の file の試験を消す事になる。
- `display/dc9.c`（own）: DC9 の可否の判断と SOUTH_CHICKEN1 の参照 clock の bit（`display/power.c:1582-1623`）。§2.5 の DC9 の手順（design.md:135）に SOUTH_CHICKEN1 の step が無い。
- `display.h` の口のうち §3.6・§4.1 に無い物: `drv_i915_display_output_moved/_back/_fail_back/_lost`（present.c:754・:780-807・:988）、`_gpu_retained`（device.c:2132）、`_abandoned`（device.c:2217）、`_session_close`（session.c:216）、`_irq_sync_failed`（device.c:2174）、`_destroy`。
- head.c の `drv_i915_lcd_modeset_flip_settle`（head.c:1054）が §4.2 の head.c の行に無い。
- 修正: §4.2 を grep の結果で作り直し（呼び出し元の file・行・新しい口）、tc-kern.c・capture.c・dc9.c の扱い（保つ・付け替える・吸収する）を決めて §6.2 の Phase に入れる。

### M5. 出力の移動（ws113-p011a）と 2 pipe の DBUF のための resident の再点灯の契約が設計に無い

- design.md:267（「resident = GOP の出力、head = 2 つ目」）、:320-321（`drv_i915_xd_screen_run(display, screen, serve, ctx)`、`_screen_plan(display, 1, output, &plan)`）、:349-354（変えない振る舞い）。
- 事実: lease が無い時の claim は resident の出力を別の connector へ**移す**（`display/display.c:2884-2960` の `i915_display_claim` → `i915_display_move`、`output.c:541-600`）。移した出力が失敗・unplug なら firmware の出力に戻す、sleep の後に HDMI が戻らなければ panel を試す（`present.c:766-807`）。head を灯す前に resident を「2 pipe 分の DBUF」で灯し直す（`head.c:20-25`・`:450-541`、`head.c:862` の `also_active_pipes`、2026-10-07 ユーザーの決定）。外部 DP の訓練の失敗は EAGAIN で link を下げて再試行（present.c:744-764）。
- 設計の口には `also_active_pipes`（run が DBUF を空けておく pipe）も、EAGAIN・spared・unplug の戻りの契約も、DBUF の状態の照会（`head.c:1411-1425` の log が読む pipes・slices・MBUS joined）も無い。
- 起こる事: own の present.c・head.c を「呼び出しの行だけ直す」と、2 画面の時に resident の DBUF が足りず underrun するか、head が灯らない。移動と戻りが壊れる。
- 修正: §4.2 に xd_screen_run・plan の入出力（`run_pipes`、戻り値の意味、retained の条件）を表で書き、§4.4 に「出力の移動・戻り・sleep の後の panel への戻り・2 pipe の再点灯」を受け入れとして加える。p009 の受け入れに入れる。

### M6. GOP が eDP 以外を灯して起動した時の p008 の振る舞いが無い

- design.md:75（GOP は eDP を pipe A で灯す）、:444（p008 は eDP だけ）。
- 事実: 今の code は GOP の出力が DP-TC なら sink を AUX で確かめてから GOP の出力を止め、駆動できなければ GOP の絵を残す（`display/output.c:347-357`）。HDMI なら sink の応答を待つ（:366-372）。対応しない出力なら何もしない（:360-364、GPU scanout rule）。
- 起こる事: p008 の xd（eDP だけ）で、外部の monitor をつないで起動した 5330（GOP が外部を灯す）は、GOP の出力を止めて eDP を灯すか、何も出ない。
- 修正: p008 の xd は「GOP の出力が eDP でなければ GOP の絵を残し、display を使わない」を明記し、その host 試験（GOP の分類 → 何もしない）を p008 の受け入れに加える（B3 の (a) を選ぶなら不要）。

### M7. power well と割り込みの結びつき（pipe の割り込みの復元と drain）が xd-power に無い

- design.md:184（割り込みの bullet）、:231（xd-power）、:327（interrupts.c の行）。
- 事実: pipe の well が上がった後に pipe の IMR/IER を書き戻し、落とす前に pipe の割り込みを drain し、drain に失敗したら well を落とさない（`display/interrupts.c:151-187`・`:503-589`・`:952-969`、`internal.h:421-424` の「0, or the drain failed: the well must NOT go off」）。vblank の参照数（interrupts.c:681-769）も同じ層。
- 起こる事: DC・suspend・head の再点灯で pipe の well が回ると、vblank と FLIP_DONE の割り込みが来ず、flip の待ち（100 ms）が全部 timeout する。
- 修正: xd-power の責務に「pipe の well の post-enable と pre-disable の hook（interrupts.c の口）」を加え、§4.2 の interrupts.c の行に `post_enable`・`pre_disable` を載せる。p005 の host 試験に「drain の失敗で well を落とさない」を加える。

### M8. VGA の無効化と VGA の decode の調停が黙って落ちている

- design.md:291-302（§3.6 の新しい中身に VGA が無い。「今の中身」には VGA がある: :295）。
- 事実: `display/takeover.c:145-146`・`:2649-2681`（CPU_VGACNTRL に VGA_DISP_DISABLE）、`takeover.c:2433-2580`（VGA の decode の調停）、`display.c:1048`・`:1786`（vga_register/unregister）。PRM TGL Vol 12 の DP の enable にも VGA の step がある（`tgl-vol12-display-engine.txt` の Enable Sequence 8.d）。
- 修正: 「GOP の後に VGA plane が有効なら止める（disable の順は takeover.c:2643 の注記の順）」と VGA の decode の扱いを xd-takeover に入れるか、要らない理由（ADL-P の GOP は VGA plane を使わない、の実測）を書く。

### M9. 並行性の model の穴

- design.md:272-273（register を書くのは request worker だけ、lock の順）、:280-282（TC の状態機械）、:295（DMC の load は非同期のまま）。
- (a) 非同期の DMC の load は request worker の外で display の register（program storage、MMIO pair、DC_STATE_DEBUG、CLKGATE）を書く。§3.4 の例外の一覧に無く、DC off の refcount との順序（load の間は DC off を保つ、load の後に DC6 を許す）も無い。
- (b) TC の状態機械（:280-282）は `IN_USE --disconnect / partner gone--> FREE` を直に許す。hotplug worker が所有を返す間に request worker の screen がまだその port を駆動している（DDI_BUF_CTL が on）と、PHY の所有を外した port に書く事になる。今は「unplugged として window を抜ける」（present.c:757-807）で screen を止めてから返す。
- (c) syscall の文脈の読み手（query・which・claim・events・resident_identity が読む connector の表と generation、head の claim の irq_lock の下の状態: head.c:461-477）が lock の表に無い。
- (d) 全部の mutex が LOCK_RANK_DEVICE（`include/kern/lock.h:44`、`device.c:1138` の sb_lock も同じ）なので、xd.lock → sb_lock の順は rank では守られない（`src/kern/lock.c:53-58` は rank を記録するだけ）。
- 修正: §3.4 に (a) DMC の worker の順序、(b) TC の `IN_USE → (screen を止める要求) → OWNED → FREE` の 2 段、(c) 読み手と lock、(d) 順序の文書化を書く。

### M10. watermark の oracle が設計の選択と PRM の算法の両方と食い違う

- design.md:181（PRM の算法、cursor の DDB 0 は §7 #5）、:443（p007 (b) の期待値 PLANE_WM0 0x80004010、BUF_CFG 0x0fdb0000）、:501（§7 #5）。
- 事実: BUF_CFG 0x0fdb0000 は plane 1 の 0–4059（Linux が cursor に 36 block を残した時: `display-ref/dbg-i915_ddb_info.txt`）。cursor を 0 にすれば plane 1 は 0–4095 で BUF_CFG は変わり、WM の level の有効も DDB の大きさで変わり得る。PRM の選択規則は「allocation が既知で allocation/blocks per line ≥ 1 なら method 2」「latency ≥ line time なら method 2」（`tgl-vol12-display-engine.txt:16385-16397`）で、Linux の `min(method1, method2)` と結果が違い得る。
- 「PRM に 4 slice の表あり: 行 12031 付近」（:181）は誤り: TGL の 12031 行は 2 slice の表。4 slice の表は ACM（DG2）の 9576・9596 行。
- 起こる事: p007 の host 試験が通らないか、通すために Linux の式に寄せる（＝ Linux の手順を写す）。
- 修正: oracle の比較を「plane 1 の WM の lines/blocks の語と LINETIME」に限り、BUF_CFG は設計の割り当て（§7 #5 の決定後）から期待値を作る。PRM と Linux が違う所は PRM を採ると書き、Linux の dump との差を許す項目として列挙する。4 slice の表の出典は ADL-P の Linux の値（`adlp_allowed_dbufs`）と 5330 の dump にする。

### M11. 出典の規則の使い方が guardrail と食い違い、Linux の構造を持ち込む所がある

- design.md:419（「Linux の関数は Procedure referenced に書かない」）。guardrail（plan/guardrail.md:105）は「参考にした手順は Procedure referenced: <版> <path> (<関数の名前>)」と、Linux の関数を手順の出典に書く事を認めている。
- 設計は Linux の手順を実際に使う: ADL-P の TC の所有（:166、§7 #4）、transition WM と SAGV の式（T6、:405）、display 13 の WA（:104、:179 PIPE_ARB_CTL、:183 PIPE_MBUS_DBOX_CTL）、DMC の event handler の無効化（M2）。§5.4 の規則のままでは、これらは出典無しか「Values referenced」の誤用になり、WS244 の後の監査（guardrail:105「あとでドライバ全体を…監査」）で引っかかる。
- 構造の持ち込み（規則 3）: (a) W1 の「domain の集合」（:408、`xelpd_pwdoms_*`）は Linux の POWER_DOMAIN という software の抽象で、hardware の data ではない。well の index と fuse の bit は data だが、domain の集合を写すと Linux の power domain の構造を持ち込む。PRM の「Functions Within Each Well」と 5330 の dump から well ごとの機能を自分で書く。(b) `display.h` の段（`init_opregion`・`init_noirq`・`irq_prepare`・`init_nogem`・`register`、:291-302）は Linux の `intel_display_driver_probe_noirq/nogem` と `i915_driver_register` の段で、呼ぶ側の `device.c` は棚卸しで derived（plan/ws244/inventory.md の device.c の行）。xd-display をこの段に合わせると Linux の probe の流れを持ち込む。
- 修正: §5.4 を guardrail に合わせて「PRM に無い ADL-P の手順は Procedure referenced: Linux v6.8.12 <path> (<関数>) を付け、code の形は写さない」に直す。W1 から domain の集合を外す。段の名前と分け方を「device.c が決める外の制約」と明記し、WS244 の device.c の整理と一緒に own の段（start・serve・suspend・resume・stop）へ縮める案を §7 に加える。

### M12. PRM の license の範囲を検討していない

- design.md:35-40（PRM から手順と値を読んで実装する事は「許される」と結論）。
- 事実: Notices は「You may create software implementations based on this document … that are intended to execute on the Intel product(s) referenced in this document」（`plan/ws245/temp/tgl-vol1-preface.txt:23-25`）。TGL の PRM が指すのは Tiger Lake、ACM の PRM は DG2 で、ADL-P は「referenced」ではない。同じ文書は「You agree to grant Intel a non-exclusive, royalty-free license to any patent claim thereafter drafted …」（:29-30）と「legal analysis」での使用の禁止（:28）も持つ。
- 修正: §0.2 にこの 3 点を書き、§7 に「TGL・ACM の PRM を ADL-P の実装の出典にしてよいか（grant の範囲）、patent の grant-back の条項を受けるか」をユーザーの判断として加える。判断までは PRM の値も「照合」扱いに留める案も書く。

### M13. 5330 の VBT の生の bytes は、2026-10-14 にユーザーの判断で消した物

- design.md:439（p003: 無ければ i915_vbt の dump を頼む）、:508（§7 #12: `/sys/kernel/debug/dri/0/i915_vbt` の取得を頼む）。
- 事実: `plan/ws031/display-ref/i915_vbt.bin`（8704 byte）・`i915_opregion.bin` と `vendor/intel-vbt/` は 531dfe8d4（ws233-p001）で削除した。ユーザー「VBTはテスト用だったので、もう不要」（plan/ws233/ws.md:34）。削除前の README は「Dell/Intel platform data. No redistribution licence has been audited」（531dfe8d4^ の `vendor/intel-vbt/README.md`）。
- 起こる事: §7 #12 の既定は、ユーザーが消すと決めた未監査の data を tree に戻す提案になる。
- 修正: fixture は `vbt-decode.txt` の値から自分で組み立てた合成の VBT（自前の bytes、使う 8 block）にし、5330 の実物は passthrough で使わない（guest には VBT が無い: B4）事と、単独起動の確かめで読む事だけを書く。§7 #12 を書き直す。

### M14. Phase の依存の誤りと、他の WS との依存の欠落

- design.md:436-452。
- p011（旧 code の削除）の依存が p010 だけ。p010 は p008 にだけ依存し、p009（hotplug・Type-C・外部）を経ない。p009 が終わる前に旧 code を消せる表になっている。
- 「p005〜p007 は互いに依存しない」（:450）は表と矛盾: p006 は p005 に、p007 も p005 に依存（:442-443）。
- p008 の中身（interrupts.c を xd-hpd へ）が p009 の xd-hpd に依存する（B3）。
- 他の WS: WS244（Gen9 の file・intel/ の header・license の package、design.md:311・:447）、WS051（incomplete、p004a〜p005b は今の tc.c・dp-ext の上に実装済み: plan/ws051/ws.md:6）、WS113・WS084・WS075（incomplete、同じ code を対象に残りの Phase を持つ）。この WS が同じ file を消すと、それらの残りの Phase の対象が消える。§6.2 の依存の列に無い。
- 修正: p011 の依存を「p009 と p010 と、ユーザーの確かめ」にする。p005〜p007 の並行の文を直す。WS244・WS051・WS113・WS084・WS075 の残りの Phase を「WS245 の切り替えの後に xd の上でやり直すか、cancel するか」を §7 に加え、Q1 が Queue で凍結を管理できるよう依存を表に書く。

### M15. §6.4 の試験の資産の一覧が足りず、fake の model の付け替えを軽く見ている

- design.md:466-476。
- 抜け: `tests/execution/ktest-display.c`（Gen12 の 56 関数）、`ktest-display-probe.c`（28）、`ktest-gt.c`（5: power well と HPD）、`ktest-sync.c`（6: DRAM・BW・power well）。どれも `I915_TEST_SET=execution` に link される（`platform/amd64/vmunix.mk:239`）。`tests/display/display-ktest.c`・`scanout-ktest.c`（display_ktest の組: vmunix.mk:243）、`host-kernel.c`・`host-test.c`、`lcd-gpu.h`・`lcd-run.h`・`scenarios.h`、`tests/display/README.md`（消えた script を名指す）。p011 の後にこれらの組の build が壊れる。
- fake の model: `lcd-fake-hw.c` は Linux 由来の環境の型を 204 箇所使い、`lcd-fake-hw.h`（棚卸しで derived）と `dp-fake-hw.h` は `display/internal.h` を include する（lcd-fake-hw.h:34、dp-fake-hw.h:26）。`dp-fake-hw.c` は `dp-sink.h` に依存（:22）。「bind する関数だけ xd の hook に変える」（:457、:471）では済まない。
- 修正: §6.4 を grep の結果で作り直し、execution の組の display の部分と ktest-gt/sync の power well の部分の扱い（xd の口へ付け替える・消す）を Phase に割り当てる。fake の model は「register の挙動」だけを own の新しい model（Linux の型を使わない）に書き直す作業として p002/p004/p006 に LW を付ける。

### M16. 36 LW の見積もりが自分の換算と合わず、実機の不具合の往復を含まない

- design.md:450（「1 LW ≈ 担当 1 人の 1〜2 時間」「36 LW ≈ 10〜14 h」）。1〜2 時間 × 36 = 36〜72 時間で、10〜14 時間と合わない（master.md:175 は 1 LW = 実時間 20 分、:178・:318 は 1〜2 時間と、master 自体も 2 つの換算を持つ）。
- p008 の 5 LW は、xd-screen・xd-takeover（今の takeover.c は 6672 行）・xd-display・xd-diag と、own の file の約 220 の参照（B3）の付け替えと `old` の切り替えを含む。p009 の 5 LW は hotplug（今 3823 行）・TC・HDMI・head の付け替え。
- 今の Gen12 の display は WS031・WS051（BUG-256・266・268）・WS084・WS113 で、実機の往復が多かった。見積もりは「判断待ち・実機を含まない」（master.md:179）規則なので、実機の往復の分を別に書くべき。
- 修正: 換算を 1 つに決めて書き直し、p008・p009 を分けて（例: p008a takeover と eDP、p008b own の file の付け替え）LW を付け直し、実機の往復（passthrough・単独起動）の LW を別の行で見積もる。

### M17. `i915.display=old` は今の boot の parser では黙って無視される。他の boot の語も §4.1 に無い

- design.md:312（保つ boot の語は 2 つ）、:429（`i915.display=old`）。
- 事実: `src/kern/boot.c:250-263` は表に無い名前を「counted and passed over」とする。名前の表は `boot.c:38-58`、値の検査は `:299-305`・`:353-450`。`i915.display=` を足すには kernel の boot の parser（WS245 の file の外）を変える必要がある。綴りを誤ると黙って xd で起動する。
- 今ある語: `display=`（auto・hdmi・edp・panel。今は「ignored」と log するだけ: `display/output.c:326-330`）、`i915.start=`（auto・manual: `device.c:276`）、`i915.debug=`（`device.c:277`）、`i915.gen9=`・`i915.gen9.display=`（`i915.c:517-522`、`gen9-display.c:232`）。§4.1 は `i915.debug=display` と `display.mode=` だけ。
- 単独起動の 5330 で xd が灯らない時、`old` に戻すには ESP の `zedbsd.cfg` を別の機械で書き換える必要がある（image の既定は build の時に焼く: master.md の「表示の build の既定」）。
- 修正: §4.1 に今の boot の語を全部載せ、`i915.display=` を足す変更（kern/boot.c と、その表の試験）を Phase に入れ Q1 の承認を取る。xd の start が失敗した時に GOP の絵を残す自動の逃げ道（takeover の前に失敗を検出、M6）を §6.1 に書く。

### M18. §7 の未決の判断の抜け

§7 の 14 件に無く、ユーザーの判断が要る物:

1. ACM（DG2）の PRM を ADL-P の出典にする事の是非と範囲（B1）。
2. PRM の license の grant の範囲と patent の grant-back（M12）。
3. 5330 の passthrough を agent の実機の試験の道として使う事、`I915_TEST_SET=display` の scenario を消すか付け替えるか（B4）。2026-09-28 の「古いグラフィックの試験の driver は捨ててよい」（master.md の有効な判断）との関係。
4. 切り替えの時期: ベータ2 の公開と OSC のデモ（2026-10-17、5330: master.md:121・:147）と、デモの 1 週間前から bug 修正だけにする方針（同「デモまでの進め方」）。p008 を xd 既定で main に入れてよい時期。
5. xd が扱う PCI ID の範囲: `device.c:776-783` は ADL-P・ADL-N・RPL-P・RPL-U を display 13 として同じ道に入れる。xd も全部を引き受けるか、5330 の ID だけか。
6. tc-kern.c と UCSI（typec）の口の扱い（M4）。
7. WS244 との分担（Gen9 の file の変更、intel/ の header、license の package の順序）と、WS051・WS113・WS084・WS075 の残りの Phase の扱い（M14）。
8. p008〜p009 の間の退行（hotplug・外部・head が xd に無い期間）を許すか（B3）。
9. VBT の fixture を合成の bytes にする事（M13、§7 #12 の置き換え）。

## minor

- m1. ADL-P に無い資源の記述: DPLL4（design.md:142・:383）、DDI C（:94「DDI A..C」、:152「B/C は sink」）、DDI D/E（:200）。ADL-P の port は A・B（combo）と TC1〜4（Linux `intel_display_device.c:699-700`）。誤解を招くので ADL-P の資源だけにする。
- m2. eDP の版: `display-ref/dpcd-drm_dp_aux0-700.bin` の 0x700 は 0x01（eDP 1.2）。design.md:67「eDP 1.4 の表示制御（0x700）あり」と :204「eDP 1.1 相当」はどちらも不正確。≥ 1.4 の判断に影響は無い。
- m3. VBT の DDC pin の値: ADL-P の VBT の値は 1 = DDI A、2 = DDI B、3〜6 = TC1〜4 で、それを GMBUS の pin 1・2・9〜12 に写す（Linux `intel_bios.c:2210-2217`、5330 の `vbt-decode.txt:218`・`:282` は DP-F・DP-G に DDC pin 0x03・0x04）。design.md:200 の「9..12 = TC1..4」は VBT の値と GMBUS の pin を混同している。V2 の表（:412）に「VBT の値 → GMBUS の pin」の写像を加える。
- m4. MBUS の join は pipe が 1 つなら pipe に関わらず行う（Linux `skl_watermark.c:1271` `adlp_check_mbus_joined` は active_pipes が 2 の冪か）。design.md:183 は pipe A だけ。GOP が HDMI を pipe B で灯す・移動の後の resident も同じ規則にする。
- m5. 「入らない mode は拒む」（design.md:147・:505）の条件に、port clock による最低の voltage level（今の `clock.c:819-840` は voltage level が今と違えば変更が要ると判定し、`modeset.c:3131-3134` で拒む）と QGV の帯域（`modeset.c:3137-3141`）を加える。pixel rate だけだと HBR3 などで voltage の足りない設定を灯し得る。
- m6. DDI A の lane capability（DDI_A_4_LANES）: PRM の DP の enable の前提（`tgl-vol12-display-engine.txt:6447`「DDIA Lane Capability Control must be configured prior to enabling any ports」）。今は init で読み保つ（`display/modeset.c:93`）。§2.3 に加える。
- m7. design.md:428 の「kernel には link するが呼ばれない」と「AMD64_I915_SOURCES に足すのは p008」が矛盾する。どちらかに決める。
- m8. kernel の大きさの上限 16 MiB（`platform/amd64/vmunix.ld:73-75`、`bootloader/include/amd64-kernel-image.h:33`）。main の amd64 の vmunix の load の範囲は約 13.4 MiB（`build/amd64/vmunix` の LOAD の末尾 0xF70000 − 0x200000）、試験の組は 1.4 MiB を足す（vmunix.mk:218-219）。p008〜p011 に旧と xd が並ぶと試験の kernel が上限を越える恐れがある（推測: xd の object の大きさは未測定）。§6.1 の cost に上限の確認を加える。
- m9. ADL-P の PCI revision → display stepping の表（今の `drv_i915_adlp_display_step`、`display.c:2003-2006`）が §5.3 の表の一覧に無い。DMC の選択に要る。
- m10. design.md:322 は output.c が「今呼ぶ物」に `drv_i915_gop_output_read/_name` を挙げるが、これは output.c 自身の関数（`output.c:212`・`:268`）。design.md:311 は bootfb.c を「Gen9 側」と呼ぶが、bootfb.c は own の file（棚卸しの「display の自前」）。
- m11. storm の検出を持たない（design.md:287、§7 #6）は「今と同じ」ではない: 今の `hotplug.c:124-129` は 1 秒に 50 回で storm とし、2 分で戻す。§4.4 と矛盾しないよう、少なくとも割り込みの rate の上限（mask して worker で再有効化）を持つと書く。
- m12. suspend の戻り（design.md:211「保存する register は無い」）: xd-pps が PP_ON/OFF_DELAYS と PP_CONTROL の power cycle delay、xd-backlight が FREQ を毎回の enable で書く事を明記する（firmware が書いた値に頼らない）。sleep の後に外部が戻らない時の panel への戻り（present.c:766-777、ws052-p009）も §2.14 に加える。
- m13. DC5/DC6 の「PG3 以上を落とし」（design.md:134）は TGL の言い方。ADL-P は PW_2 と PW_B〜D（`display-ref/dbg-i915_power_domain_info.txt`）で書く。
- m14. §4.4 の「試験の一覧が読む文言は §6.4 で列挙」（design.md:354）に対し §6.4 に文言の一覧が無い。今の log には Linux の関数名を含む物がある（例 `display.c:678`「P7 intel_opregion_register: …」）。残す文言と捨てる文言を列挙する。
- m15. p002 の (a)「旧 intel/*.h の define と比べる」試験（design.md:438）は p011 で旧 header を消すと build できなくなる。p011 で試験を退役させるか、期待値を試験の中の自前の表に移すと書く。
- m16. §7 #2（TGL を支えない）の「今の code は display 12 の分岐を持つが実機で確かめた記録が無い」（design.md:498）: 今の code は TGL を Gen12 として受け、`tgl_dmc_ver2_12.bin` を選ぶ（`device.c:766-773`、`display.c:2008-2010`）。xd 既定で 5320 の display が「display なし」に変わる事を退行として §7 に明記する（5320 は対象の platform: master.md:134）。

## 確かめられなかった事（推測を含む）

- design.md:13-15 の「Intel の公開の PRM に ADL-P の版は無い」は、この review では network で確かめていない。
- passthrough の guest で xd の VBT 無しの既定の道が 5330 の port（eDP on A、HDMI on B が VBT では DVI-D: `vbt-decode.txt:122`）を正しく組めるかは、code が無いので確かめていない。
- m8 の kernel の大きさは推測（xd の object の大きさは未測定）。
- B2 の「DFLEXDPSP の live state が ADL-P で今と違う結果を返し得る」は、Linux が ADL-P で ISR を使う事からの推測で、5330 で DFLEXDPSP を読んだ記録は見ていない（m3 の dump の `range-0x*.txt` は未照合）。
- 実行したのは grep・読み・`od`・`size`/`readelf`（build/amd64/vmunix）だけ。build・host 試験・QEMU は走らせていない。
