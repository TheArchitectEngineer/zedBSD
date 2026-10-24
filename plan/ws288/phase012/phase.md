<!-- awesome-plan project=zedbsd record=ws288-p012 -->

# ws288-p012 KC705 の RTL（board の top、JTAG の bridge、DC の HDMI）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §8 の p012 の行、§7.6 の p012 の module（`am_jtag_regs`、MIG/MMCM の wrapper、`kc705_raster` の board）、DC の HDMI（U7: ADV7511 を I2C で設定）。
- 状態: cleared（2026-10-24 Q1: part A と残件の EXTCAP の DISP。板の確認は p024、timing は p025）
- 方針（2026-10-24 ユーザー）:「タイミングの調整はまだ行わないので、KC705 での動作確認は後にします。実装を優先し、次のフェーズへ進んでも OK です。」

## 部分（part）

| part | 範囲 | 受け入れ | 状態 |
| --- | --- | --- | --- |
| A | RTL と simulation: `am_jtag_regs`（BSCANE2 USER2 の 72 bit の register から register bus へ）、`atg_dc_i2c`（ADV7511 の I2C の master）、DC の KC705 の HDMI の出力（ADV7511 への並列の RGB と sync）、`atg_kc705_raster` の board の top（MIG・MMCM は wrapper。sim は振る舞いの model） | 各 spec §8 の TB の PASS、board の top の sim で JTAG の bridge から fill → DBG の読み戻し、lint clean | cleared（2026-10-24 Q1: amaterrace-gpu 3e02106、main の merge 9e00981） |

## 記録

- 2026-10-24 Q1: part A を P2（phase-runner、Opus 5.5 high）に依頼。worktree `~/amaterrace-worktrees/p2-kc705`（branch `p2-kc705`、基点 main b181b44）。
- 2026-10-24 ユーザー「タイミングの調整と KC705 での確認は別なフェーズに分けた方がいいと思います。」→ part B（板）を外し、板での確認は [p024](../phase024/phase.md)、timing は [p025](../phase025/phase.md) に。p012 は RTL と sim だけの Phase。
- 2026-10-24 P2 → Q1（part A の結果）: 7392625（`am_jtag_regs`、`atg_dc_i2c`、BSCANE2 と I2C の slave の sim の model）、0b729de（新しい `atg_dc_ycc`、`atg_dc` の parameter `PX_FMT`: 1 = YCbCr 4:2:2 の 16 bit）、3e02106（KC705 の board の top と lib の clk・jtag・mig、XDC、`sim/models/kc705/`・`xilinx/`、board の TB、`sim/run.sh` の `sim-sources:` の行）。P2 の申告: lint 106 項目 clean、`sim/run.sh` 全体 78/78 PASS、Verilator 4 本 PASS。
  - Q1 の確認: 3 つの SHA は commit（WIP）。worktree で TB を再実行し PASS 行は am_jtag_regs 4・atg_dc_i2c 3・atg_dc_ycc 3・atg_kc705_raster 5（FAIL 0、報告と一致。board の TB は JTAG で MAGIC・SCRATCH・DC_ID・MEM_READY、JTAG から BLT の FILL → DBG で読み戻し、I2C で mux 0x74 に 1 byte と ACK、TEST_PATTERN の HDMI の pin の最初の 4 line × 32 pixel が YCbCr 4:2:2 で期待どおり）。main（2a13d43）へ `--no-ff` で merge、衝突なし、merge の後に lint clean、`atg_kc705_raster`・`atg_dc`・`atg_top_tex`・`atg_mc_atom` PASS、Verilator 4 本 PASS で 9e00981（親 2a13d43 3e02106）。P2 の案の I2C の bit と DC_CAPS[2] の注記を `docs/ghci.md` §13 に書いた（a5cc25b）。push はしていない。
  - spec の読み（Q1 が承認、技術の判断）: KC705 の ADV7511 への配線は 16 bit（ADI の hdl `projects/adv7511/kc705/system_constr.xdc` と KC705 の master XDC の写しで一致）なので YCbCr 4:2:2（{C, Y}、ADI の `ad_ss_444to422` と同じ並び）、board の top の `hdmi_d` 16 bit・`iic_mux_reset_n`・`CPU_IMEM_INIT`/`CPU_DMEM_INIT`、`am_jtag_regs` の status [1] dropped、clock は MMCME2_BASE × 2 と PLLE2_BASE（TMDS が無いので pclk5 無し）、MIG の clock の乗せ換えは AXI Clock Converter の IP（`atg_kc705_axi_cc`・`atg_kc705_ddr3`）、`atg_dc_i2c` の repeated START・WRITE 優先・busy の間の書き込みは無視・clock stretching・timeout 無し、`sim/run.sh` の `sim-sources:`。board の top は NSU 1 のまま（全 pipeline が目標）。
  - 未確認（p024 で確かめる）: HDMI の pin（schematic・UG810 では未確認、外部の 2 つの資料が一致しているだけ）、MIG と clock converter の IP の port の名前と clock manager の設定（記憶に基づく）、MIG の system clock を No Buffer で BUFG から、BUFGMUX の配置、ADV7511 の I2C の設定（mux 0x74 の channel、0x39 の power・input ID 1・style・CSC）、XDC の CFGBVS/CONFIG_VOLTAGE と HDMI の出力の timing の制約、720p・640×480 の color bar の目視。
  - 残件（p012 の RTL）: KC705 の構成で CP の EXTCAP の list に DISP の entry（+0x10000 を指す、design §5.1）が無い。driver が DC を見つけられないので、CP の regs に足す。
- 2026-10-24 Q1（ユーザー「Pushをお願いします。p012の残りをお願いします。」）: 残件を Q1 が直した（amaterrace-gpu dbdf6e7、親 a5cc25b）。`atg_cp` に parameter `HAS_DISP`（CAPS[3] と DISP の capability、+0x10000 を指す）・`HAS_DBG`（DBG の capability）・`PACKED`、`atg_top` の `HAS_DISP` を KC705 の board の top で 1、`HAS_JTAG` を `HAS_DBG` に。合わせて GHCI の CAPS[27] graphics（VF・RX・ROP）と CAPS[28] packed を出す（CAPS[24] subgroup は命令が結果を返さないので 0 のまま）。board の TB に CAPS[3] と capability の list の walk（DISP が DC を指す、DBG が有る）を足した。確認: `atg_kc705_raster` の PASS 5 行、`HAS_DISP(0)` にすると FAIL 2 件（CAPS[3]、DISP 無し）で戻した、lint clean、`atg_cp_regs`・`atg_top`・`atg_top_fw`・`atg_top_raster`・`atg_top_shade`・`atg_top_tex` PASS、Verilator 4 本 PASS。`sim/run.sh` の全部は流していない。push はしていない（直前の push は 2d0633814・a5cc25b まで）。
