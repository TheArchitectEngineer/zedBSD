<!-- awesome-plan project=zedbsd record=ws039-p014 -->

# ws039-p014: 段 A10 mode set の 1（読むだけ＋AtomBIOS の interpreter）

Status: in-progress（2026-10-16 21:05 P3 着手）
Disposition: normal
Parent: [WS039](../ws.md)
Queue: q1234 / q1234-i01（p013 → p014 → p015 → p016、各段は実機で ok の後に main）
承認: 2026-10-16 ユーザー「p013,p014,p015,p016を現在のサブエージェントで継続してください。」
実行者: P3（worktree `/home/awe/zedBSD-worktrees/p3`、branch `agent/p3-ws039`）

## 範囲（[design.md](../design.md) §16 の p014 の行、§8.6 の 1・3・10、§13.3）

i915 の present の大きさの変更の扱いの確認、AtomBIOS の command の表の interpreter（dry run と oracle の host 試験 (j)）、AUX・DDC の engine、DPCD・EDID の読み、link の状態の readout、`display.mode` の ENUMERATE・VALIDATE（boot の clock の中の mode だけ）。**実機: A10 の ok（読むだけ）**、形は `A10 ok dpcd=1.4 lanes=4 rate=0x14 edid=<vendor> 1920x1080@60 modes=N atom=dry ok`。mode は設定しない（p015）。

## 手順（順に、各段で build・host 試験・commit）

1. **interpreter**（`bytecode.c`、header は `amdrdna-hw.h`）: VBIOS の command の表の bytecode を実行する。operand の空間（register・parameter・workspace・frame buffer の scratch・data の表・即値・PLL・MC）と幅（dword・word・byte の位置）、比較の flag、分岐、呼び出し（入れ子の深さの上限）、switch、遅延、IIO（間接の register の小さな bytecode）。register の読み書き・遅延は呼び手の callback（実機は MMIO、dry run は記録）。画像の外の読み、上限の手数、未知の opcode は中止して error。PLL・MC は Linux と同じく読み 0・書き無し。opcode の名前は振る舞いからの独自の語（design 13.3 の表、review M13）。出典 Linux v6.19 `amdgpu/atom.c`・`atom.h`・`include/atom-bits.h`（Procedure / Values referenced）。
2. **host 試験 (j)**: 実機の VBIOS（`plan/ws039/temp/dump/cold/rom.bin`、tree の外）の command の表を dry run（register の読みは固定の値の表、書きは列に記録）で走らせ、**Linux の `atom.c` を host で build した oracle**（tree に入れない: `plan/ws039/temp/linux` の v6.19 の file を test の script が `build/tmp` の下で shim の header と build）の書き込みの列と比べる。design の「tarball の取得」は、監査で既に使っている temp の tree（同じ v6.19、tree の外）に置き換える（記録の差分）。
3. **AUX と DDC**: DP_AUXn の engine（Linux `dce_aux.c` の順）で native AUX の DPCD の読み、I2C-over-AUX の EDID（address 0x50）。HDMI の DDC の I2C engine（`dce_i2c_hw.c`）は GOP の output が HDMI の時。register は `dcn_2_0_0_offset.h`・`sh_mask.h` から出典付き。
4. **EDID の decode**（amdrdna の中、i915 の `xd-edid` は i915 の内部なので使わない）: 版、vendor・product、物理の大きさ、detailed timing と standard・established の timing → mode の一覧。
5. **link の状態**: DPCD の能力（版・最大 rate・lane の数）と状態（lane の status・align）、GOP の DP の register（lane の数・rate）と照合。
6. **`display.mode`**: ENUMERATE は EDID の mode のうち boot の clock（A1 で読んだ DISPCLK・DP の link の帯域）で出せる物、VALIDATE は一致の検査（無ければ EINVAL、帯域の外は ERANGE）。present.c は P5（p019）も触るので、触る前に Q1 に知らせる。
7. **段 A10** の配線（`amdrdnactl advance A10`、auto には入れない）と ok の行、`amdrdnactl status` の行。
8. **実機**（時刻を Q1 に知らせてから）: A10 ok、DPCD・EDID の値、modes、atom の dry run の ok。

## 記録

### 2026-10-16 21:05〜21:45 P3（host まで）

- 手順 1・2: `bytecode.c`・`amdrdna-bytecode.h`（runner）。host 試験 `amdrdna-bytecode-test.sh`: 実機の VBIOS（rom.bin）の 35 routine を 3 通りの parameter で、Linux の atom.c（temp の tree を build/tmp に写して shim の header `tests/bytecode-oracle/` で host build、tree に入れない）と同じ register の model で走らせ、register の読み書き・待ちの列・parameter・scratch・状態を比べて **475 checks 全一致**（polling で 4096 event に切った run 16 も先頭は一致）。合成の image で深さ 16・手数の上限・image の外・branch table・無い routine。初版の誤り: routine の header の parameter の byte の位置を mask の値で読んでいた（呼ばれた routine の parameter がずれた）→ oracle の差で発見して直した。design の「tarball の取得」は temp の tree（監査と同じ v6.19、tree の外）に置き換えた。
- 手順 3〜5（一部）: `dcn-registers.c` に I2C engine・DDC pad・AUX の register（amdrdna-dcn-compare 492 registers 一致）。`vbios-table.c` に `amdrdna_vbios_connector`（path の表・I2C と HPD の record・pin の表）と boot の DISPCLK（dce_info）。`edid.c`（decode と mode の検索、established・standard は VESA DMT の表から clock）。`sink.c`（段 A10: GOP の DIG の番号 = transmitter → path → DDC の line、I2C engine で EDID、VBIOS の I2C routine（0x36）を runner で実機の register に走らせ同じ block を読み比べる、DP の時は AUX で DPCD 16 byte、出せる pixel clock の上限と mode の数）。段 A5c は印だけの段（A10 の前に通る）。
- この board: VBIOS の path は 4 本（DP×3: transmitter E/C/D、HDMI: A）。GOP は DIG 4 = transmitter E = **DP の connector**（I2C id 0x95 = DDC6、HPD 6）で、signal は DVI（DP++ の受動の変換で HDMI の monitor と見る）→ この机では AUX の DPCD は確かめられない（DP の monitor が要る）。
- host: `amdrdna-host-test.sh` PASS（sink 21 checks、dcn 492、audit）、build warning 0。実機は未。
