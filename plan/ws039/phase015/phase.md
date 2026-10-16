<!-- awesome-plan project=zedbsd record=ws039-p015 -->

# ws039-p015: 段 A11〜A13 mode set（re-light、別の mode、hotplug）

Status: in-progress（2026-10-16 22:16 P3: 範囲は (B) TMDS を先に、と決まった）
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

