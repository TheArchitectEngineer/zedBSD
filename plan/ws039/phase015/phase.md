<!-- awesome-plan project=zedbsd record=ws039-p015 -->

# ws039-p015: 段 A11〜A13 mode set（re-light、別の mode、hotplug）

Status: in-progress（2026-10-16 22:35 P3: 範囲は (B) TMDS を先に、と決まった）
Disposition: normal
Parent: [WS039](../ws.md)
Queue: q1234 / q1234-i01（p013 → p014 → p015 → p016、各段は実機で ok の後に main）
承認: 2026-10-16 ユーザー「p013,p014,p015,p016を現在のサブエージェントで継続してください。」
実行者: P3（worktree `/home/awe/zedBSD-worktrees/p3`、branch `agent/p3-ws039`）

## 範囲（[design.md](../design.md) §16 の p015 の行、§8.6 の 2・5〜11）

VBIOS の command の呼び出し（power gating・pixel clock・encoder・transmitter・CRTC）、OTG・OPP・MPC・DPP・HUBP・DIG の組み立て、DP の link training、同じ mode の re-light（A11）、別の mode（A12、DLG・TTU・watermark の固定小数、host 試験 (m)）、hotplug（A13）、戻し、HDMI（後半）。**実機: A11・A12・A13 の ok、underflow 0**。

## 判断（2026-10-16 22:20 Q1 経由でユーザーへ）

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
