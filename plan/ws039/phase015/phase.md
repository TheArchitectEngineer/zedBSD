<!-- awesome-plan project=zedbsd record=ws039-p015 -->

# ws039-p015: 段 A11〜A13 mode set（re-light、別の mode、hotplug）

Status: in-progress（2026-10-17 01:56 P3: A11・A12・A13 と present の大きさでの mode set・sink の上限の plan での判定は実機で ok。残りは DP の host 試験と本物の HPD（cable の抜き差し）。clearance の判定は Q1）
Disposition: normal
Parent: [WS039](../ws.md)
Queue: q1234 / q1234-i01（p013 → p014 → p015 → p016、各段は実機で ok の後に main）
承認: 2026-10-16 ユーザー「p013,p014,p015,p016を現在のサブエージェントで継続してください。」
実行者: P3（worktree `/home/awe/zedBSD-worktrees/p3`、branch `agent/p3-ws039`）

## 範囲（[design.md](../design.md) §16 の p015 の行、§8.6 の 2・5〜11）

VBIOS の command の呼び出し（power gating・pixel clock・encoder・transmitter・CRTC）、OTG・OPP・MPC・DPP・HUBP・DIG の組み立て、DP の link training、同じ mode の re-light（A11）、別の mode（A12、DLG・TTU・watermark の固定小数、host 試験 (m)）、hotplug（A13）、戻し、HDMI（後半）。**実機: A11・A12・A13 の ok、underflow 0**。

## 判断（2026-10-16 夜、Q1 経由でユーザーへ）

design は「DP を先に、DVI は範囲の外」。p014 の A10 で分かった実機の接続は、board の DP の connector（transmitter E、DDC6）に DP++ の受動の変換で HDMI の monitor（DELL SE2426H）を **DVI の signal（TMDS）** で駆動。DP の sink が無いので DP の link training・AUX は実機で確かめられない。選択肢: (A) DP の monitor を繋ぐ、(B) TMDS（DVI over DP++、HDMI の connector）を先に作り DP は host まで、(C) p016 を先に。P3 の推奨は (B)。
- **決定（2026-10-16 ユーザー、Q1 経由のクリック）: 「TMDS（DVI/HDMI）を先に」= (B)。DP は host 試験まで、DP の monitor がつながったら実機で。実機を使う時間は「いつでもよい（時刻は知らせる）」。毎回、触る前に時刻と内容（画面を消すか）を Q1 に。** design.md 8.6 の範囲の段落に記録した。

## 手順の案（(B) の場合。(A) なら 5 を先に）

1. **VBIOS の command の parameter**（`atombios.h`・`atomfirmware.h` の struct の値、`command_table2.c` の組み立て）: この board の版は digxencodercontrol 1.5、setpixelclock 1.7、enabledisppowergating 2.1、enablecrtc 1.1、blankcrtc 1.1、dig1transmittercontrol 1.6、selectcrtc_source 2.3、setcrtc_usingdtdtiming 1.1（DCN では使わない見込み）。host の runner の dry run で、GOP の今の mode の parameter で各 routine が書く register の列を記録して読む（PHY・PLL の何を触るか）。
2. **A11 re-light（同じ mode）**: GOP の DIG・OTG・pixel clock を読み（A1）、DPG で blank → DIG の BE の disable（transmitter の DISABLE、encoder の DISABLE）→ OTG の disable（EnableCRTC 0）→ SetPixelClock（同じ clock）→ OTG の timing を GOP の値で書き直し → EnableCRTC → encoder の SETUP・transmitter の ENABLE（TMDS、symclk = pixel clock）→ unblank。DLG・TTU・watermark・HUBP は GOP の値を保つ。確かめ: OTG の frame counter が進む、underflow 0、OTG の CRC の前後一致（register は出典を写す）、画面の目視（ユーザーの都合の良い時、一覧に載せる）。
3. **A12 別の mode**: EDID の 1280x720@60 など（boot の DISPCLK・DVI の 165 MHz の中）。OTG の timing、OPP・DPP・HUBP の viewport と大きさ、HUBP の DLG・TTU・RQ、HUBBUB の watermark を 64 bit の固定小数で（`display_rq_dlg_calc_20.c`・`dcn20_fpu.c` の式、host 試験 (m) は同じ mode で GOP の値と比べる）。戻しは A11 の手順で GOP の mode へ。
4. **A13 hotplug**: HPD の割り込み → debounce → DC_HPD_SENSE → 外れ: blank と link の停止、events を進める → 繋がり: EDID を読み、今の mode が出せれば同じ mode で re-light。
5. **DP**（(A) なら先に、(B) なら host まで）: DPCD の能力、DIG1TransmitterControl の rate、LINK_BW_SET・LANE_COUNT_SET、pattern 1（CR、swing・pre-emphasis の調整）→ pattern 2/3（EQ・lock・align）→ off、失敗は rate・lane を落として再試行。MSA・VID stream。
6. **display の ops**: present・commit の大きさが今の mode と違い、VALIDATE で出せる mode なら mode set（A12 の道）、その間の present は EAGAIN（design 8.6 の 10。i915 は mode を変えず EINVAL なので amdrdna 独自の形、p014 の確認）。

## 危険と退路

- A11 以降は画面を一度消す。失敗すると次の起動まで画面が無い（warm reboot で GOP が戻す見込み）。実機の操作の前に毎回 Q1 に時刻を知らせ、ユーザーが RDNA 機を使っていない時間に限る。
- PHY・PLL は VBIOS の command に頼る（runner は p014 で oracle と全一致、実機で I2C の routine を確かめ済み）。

## 結果（実施の記録）

### A11 re-light（2026-10-16 22:29〜22:38、実機 RX 5600 XT、zedBSD）

- 実装: `src/drivers/gpu/amdrdna/relight.c`（`amdrdna_a11_relight`）、`display-command.c`（VBIOS の command の parameter の組み立て、30a4ee948）、
  `sink.c` の `amdrdna_bios_run`（A10 の runner を他の段から使う）、`display.c` の `amdrdna_display_underflow`、OTG の BLANK・CRC の register（`dcn-registers.c`、DCN compare に追加）。
  commit f2cbb8585・7fac15fed（mark の行を 160 桁に収めた）。
- 手順（Linux の TMDS の off/on に合わせた。PLL・OTG の timing・HUBP は触らない）: OTG の CRC → OTG blank → transmitter DISABLE（routine 0x4c、connector id 付き）→ 300 ms 信号なし →
  encoder STREAM_SETUP（routine 0x04、DIG FE、DVI、4 lane、pclk、DVI は色深度 0 ＝ Linux と同じ）→ transmitter ENABLE（4 lane、symclk = pclk、HPD sel、FE sel 0、connector id 0）→ 2 frame → unblank → 2 frame → CRC・frame counter・underflow。
- 実機の結果（3 回、すべて ok）:
  - r1（f2cbb8585）: `A11 ok relit 1920x1080@60.000 DVI transmitter=4 dig=4 pixel=148500kHz dark=307ms steps=381 frames=26 crc=same (aa0f0e0ce003fe4f -> aa0f0e0ce003fe4f)`、status の underflow=0。
  - r2・r3（7fac15fed）: `A11 ok relit 1920x1080@60.000 DVI phy=4 dig=4 pclk=148500kHz dark=307ms steps=381 frames=26 crc=same aa0f0e0c/e003fe4f underflow=0/0`。
  - A11 の後も `amdrdnactl modes` は 13 mode、display の counts は進み（refreshes・frame_irq）、faults=2 は A5・A5s の意図した fault の確認の分。
- 証拠: `build/tmp/p15-a11-r1`〜`r3`（dmesg.txt・advance.txt・after.txt、worktree の中）。
- host: build warning 0、`plan/ws039/tests/amdrdna-host-test.sh` PASS（command 5、DCN compare 516 register、audit）。
- 未実施: monitor が信号を取り戻した画面の目視（CRC は OTG の出力の値で monitor 側ではない。ユーザーの試験の一覧に載せる）。HDMI の connector（board の HDMI に繋いだ時）。
- design.md の A11 の行（「自前の手順で OTG・OPP・MPC・DPP・HUBP・DIG を組み直す」）より狭い: 今の A11 は link（DIG の stream と PHY）だけを落として戻す。
  OTG の timing・pipe の組み立ては A12 で自前に書き、A12 の「戻し」（GOP の mode へ自前の組み立てで戻す）が design の A11 の残りを満たす形にする。

### A12 の調べ（2026-10-16 22:39〜23:15）

- VBIOS の SetPixelClock（routine 0x0c、v1.7）を host の runner で dry run（scratchpad の調べの道具、tree に入れない）: PLL0 を指すと UNIPHY0 の macro の register（0x5df1〜0x5e12）を書いて lock を待つ。
- GOP の状態（Debian、amdgpu は boot で vcn の early_init に失敗して device に付かない＝GOP の状態のまま）を hw-dump.py で読んだ（1524 個の DCN の register、読むだけ、`build/tmp/p15-gop/regs.txt`）:
  - `OTG0_PHYPLL_PIXEL_RATE_CNTL=4`: GOP の pixel clock は出力の PHY 自身の PLL（PHY E の PLL、ATOM_COMBOPHY_PLL4）。A12 の SetPixelClock は PLL E を使う（Linux は空きの最初の PLL を選ぶが、GOP に合わせる）。
  - GOP の DLG の値は DML の式と合わない（`REF_FREQ_TO_PIX_FREQ`・`REFCYC_PER_HTOTAL`・`REFCYC_H_BLANK_END` が別々の refclk を示し、`DLG_V_BLANK_END=19` は OTG の vblank_end 41 と違う）。GOP の値は「この値でも underflow しない」実例にとどまり、式の照合の基準には使えない。
- 事故（22:53〜23:05）: Debian で amdgpu を一時の firmware path で modprobe し直したら navi10 の firmware が無く（sos・smc が -2）modprobe が止まり、amdgpu が uncorrectable・`ACK should not assert` を出し続けた。reboot で戻らず、ユーザーが電源を入れ直した（23:05 に Debian、dmesg の該当の行 0）。**以後 Debian で amdgpu の load・rmmod・modprobe はしない（Q1 2026-10-16）。** Debian の永続の設定は変えていない。
- A12 の値の照合（Q1 了解 2026-10-16）: Linux の DML（`display_mode_vba_20v2.c`・`display_rq_dlg_calc_20v2.c`、Navi10 は DML_PROJECT_NAVI10v2）を host で走らせる oracle（atom.c と同じ形、tree に入れず temp の Linux から build、shim は `plan/ws039/tests/`）を作り、amdrdna の固定小数の計算（単一 pipe・linear ARGB8888・scaling 無し・ODM 無し）を複数の mode で照合する。最後は実機の underflow 0。design.md 15 節 #8（DML の oracle は使わない）の変更になるので design に記録する。

### A12 別の mode（2026-10-16 23:20〜2026-10-17 00:30、実機 RX 5600 XT、zedBSD）

- 実装:
  - `src/drivers/gpu/amdrdna/mode-plan.c`: 表示の plan。Navi10 の DML（`display_mode_vba_20v2.c`・`display_rq_dlg_calc_20v2.c`）の一つの plane・一つの pipe・linear 32 bpp・scaling 無しの場合を 64 bit 固定小数（32 bit の小数）で書き直したもの。prefetch mode の選び方は mode support（必要最小の clock、最後の start line）に合わせ、start line の loop は板の clock で回す。
  - `src/drivers/gpu/amdrdna/modeset.c`: A12。
    1. GOP の mode のまま DLG・TTU・RQ・watermark・global sync を plan の値に替える（OTG lock の下）。
    2. 別の mode（EDID の 1280x720@60）に全手順で替える: blank → transmitter off → OTG 停止 → SetPixelClock（**PLL0**）→ OTG の timing → DSCL の blank の写し・RECOUT・MPC の大きさ → viewport → plane の欄 → watermark → global sync・VTG → OTG 開始 → encoder の STREAM_SETUP → transmitter on → unblank。3 秒保つ。
    3. 全手順で GOP の mode に戻す。
  - clock:
    - hub の reference は crystal / hub timer の分周（50 MHz）。
    - DISPCLK・DPPCLK は CLK3 の VCO / DENTIST の DID（163.8 MHz）。
    - DCEFCLK・UCLK は VBIOS の getsmuclockinfo routine（506.7 MHz・101 MHz）。SMU の metrics の CurrClock は GFXCLK 以外 0 だった。
  - `edid.c`: established・standard の timing に DMT の porch・sync 幅・極性を足した（無いと 1280x720 が sync 幅 0 で出て OTG の frame が数えられなかった）。
  - register の表に HUBP の DLG・TTU・RQ、HUBBUB の watermark、OTG の global sync・CRC の window ほかを追加した（DCN compare 906 register）。
- host 試験 (m) `plan/ws039/tests/amdrdna-dml-test.sh`:
  - Linux の DML を host で build する oracle（`plan/ws039/tests/dml-oracle/`、tree に Linux を入れない）と照合する。
  - 17 mode × 4 clock × 2 pitch で **8452 checks, 0 failures, 81 が最後の桁で ±1**（DML の float の丸め）。
  - DML が 2 pipe（MPC combine）を要する mode は refuse を期待する。
- 実機:
  - r1・r2（PHY E の PLL を使った版）: 1280x720 で OTG の frame 0。PHY E の PLL は transmitter の off で落ちるため。PLL0 に替えた。
  - r3・r4: sync 幅 0（EDID の DMT の欄が無かった）→ 直した。
  - **r5〜r8 は 4 回とも ok**:
    - `A12 same 1920x1080 start=31 prefetch=11/4 ... frames=60 underflow=0/0`
    - `A12 mode 1280x720@60.000 pclk=74250kHz start=21 frames=60 underflow=0/0`
    - `A12 ok 1280x720 and back to 1920x1080@60.000 frames=60 underflow=0`
  - 証拠: `build/tmp/p15-a12-r5`〜`r8`（worktree の中）。
- **CRC**:
  - r7 までの CRC は window を設定しておらず空の window の CRC で、値が変わらなかった。**A11 の「crc=same」は証拠にならない。**
  - dcd760d6c で window を active の全体にした（Linux の dc_stream_configure_crc と同じ）。r8 で 720p は `d40f9b3d/6db61baf`、1080p に戻した後は `8b3123aa/b196bac4` と変わることを確かめた。
  - A11 の前後一致は、この版で取り直していない（未実施）。
- 気づき（範囲の外、記録）: UCLK は idle で 101 MHz（UCLK の DPM が動いている）。SMU に display の watermark 表を渡していないため、UCLK の切り替えが表示中に起きると underflow の危険がある（WS039 の DPM の側の課題）。sink の mode の上限（A10 の limit）は boot の DISPCLK 625 MHz を使っており、実際の DISPCLK 163.8 MHz と違う。plan で判定する形に替えるのが残り。
- 未実施: 1280x720 の画面の目視（Q1 が beta2 の U16 に載せた）。HDMI の connector。

### 残り（2026-10-17 00:31 の時点、下で一部済み）

- A13 hotplug（HPD の割り込み、debounce、外れ・繋がりで blank と re-light）。→ 済（下）。
- display の mode の op: present の大きさの変更を引き金に mode set。sink の上限を plan の判定に替える。→ 済（下）。
- DP は host 試験まで（link training・AUX）。→ 未着手（残件）。
- p016 の取り込みと R1 → 済（[phase016](../phase016/phase.md)）。

### mode の op・sink の上限・A13（2026-10-17 00:45〜01:46、P3）

- 実装（commit 6df3c4b92・2d97bd05f・bf4fb396f）:
  - `modeset.c`: 公開の `amdrdna_mode_clocks`（板の clock を plan の input に）、`amdrdna_mode_timing_input`、`amdrdna_mode_current`（firmware の mode を最初の呼び出しで OTG から読み、EDID の同じ mode の pixel clock を使う）、`amdrdna_mode_set`（全手順の mode set、frame に収まらない・plan が断る mode は ERANGE、30 frame と underflow で確かめ、display の width・height・refresh を更新）、`amdrdna_mode_output`（blank と transmitter の off、on と unblank。OTG は回し続ける）。`modeset_switch` は OTG を止めるので refresh の数の base を取り直す。
  - `sink.c`: A10 の読みを `amdrdna_sink_read`（record に読む、hotplug でも使う）に分け、mode ごとに `carried`（信号の上限・sync の幅が分かる・frame に収まる・板の clock で plan が通る）。上限は boot の DISPCLK ではなく plan で判定（実際の DISPCLK 163.8 MHz）。`amdrdna_sink_choose`・`amdrdna_sink_carries`・`amdrdna_sink_preferred`。refresh 0 は出せる mode の中から選ぶ。
  - `present.c`: present の大きさが今の mode と違えば、display が出せる mode なら mode set してから flip（`present_resize`）、出せなければ EINVAL。lease の終わりで console を見せた後に firmware の mode に戻す（`present_firmware_mode`）。query の max は GOP の大きさ。mode・sink は node の display lock の下で読む。
  - `hotplug.c`（新）: 段 A13 と、HPD の割り込みの後の仕事（bring-up の thread、100 ms 待って HPD の level を読む）。外れ: blank と transmitter の off。繋がり: EDID を読み直し、同じ monitor か今の mode を出せる monitor なら同じ mode で点け直し、出せなければ新しい monitor の推奨の mode に mode set して topology を進める。root の `AMDRDNA_IOC_HOTPLUG`（7、`struct amdrdna_hotplug_ask` 32 byte）で外れ・繋がりを模して試せる。lock の順は node の present → display → display の mode lock。
  - `relight.c`: pixel clock 0 の時は今の mode（EDID の値）を使う。`amdrdnactl present WxH [s]`・`amdrdnactl hotplug off|on`（`userland/tests/amdrdnactl/present.c`）。
- build warning 0・include check/vmunix check PASS、host 試験（host・vk・bytecode・reset・dml）PASS。新しい関数の host 試験は無い（kernel の I2C・VBIOS を使う）。
- 実機（10.0.10.3、text login・`amdrdna.auto=A9` → `amdrdnactl advance A13`、kernel bf4fb396f、01:45〜01:46。前の 2 版は 01:35・01:39）:
  - `A10 ok … modes=13/15 limit=165000kHz`、`A11 ok relit 1920x1080@60.000 … pclk=148500kHz … underflow=0/0`、`A12 ok 1280x720 and back …`、**`A13 ok output off and on 1920x1080@60.000 dark=1924ms frames=30 underflow=0/0 edid=DEL modes=13`**。
  - `amdrdnactl present 1280x720`・`800x600`・`1024x768`（01:41・01:45）: `mode set ok 1280x720@60.000 pclk=74250kHz (was 1920x1080) … underflow=0`、`mode set ok 800x600@75.000 pclk=49500kHz …`、`mode set ok 1024x768@75.029 pclk=78750kHz …`、lease の終わりで `mode set ok 1920x1080@60.000 pclk=148500kHz … underflow=0`。frame の sequence 1・2 が出て query の current がその大きさ。`1234x567` は EINVAL（validate も refuse）。
  - `amdrdnactl hotplug off` → `dark=1`、`on` → `dark=0`、frame は進み続け underflow 0。status `failed=none`、`underflow=0`。
  - 証拠 `build/tmp/p15-m1/`・`p15-m2/`・`p15-m3/`（worktree の中、commit しない）。
- 未実施・残件:
  - **本物の HPD**（cable の抜き差し・別の monitor）: 割り込みの道と debounce は実機で通していない（この monitor は transmitter を切っても HPD を落とさない）。ユーザーの一覧へ。
  - **DP**（link training・AUX の DPCD の書き・MSA）: host 試験も未着手。DP の monitor がつながった時に実機。
  - HDMI の connector、mode set の画面の目視（1280x720・800x600 の絵）、Keiland が present の大きさを変える道（今の compositor は変えない）。
  - blob の present は stride が display の stride（GOP の pitch）と同じ時だけ（mode set しても plane の pitch は替えない）。
