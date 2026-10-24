<!-- awesome-plan project=zedbsd record=ws288-p009 -->

# ws288-p009 VF・SU の vertex/fragment・TU（graphics の pipeline の RTL）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §2.3（VF の STATE 0x020〜0x04F、ATTR_DST 0x1C0〜）、§3.4.3、§3.7、§3.8、§8 の p009 の行、§7.6 の p009 の module。
- 状態: in-progress（2026-10-24 Q1）
- 方針（2026-10-24 ユーザー）:「今回は合成できればいいことにして、KC705での動作とパイプライン分割はあとにしませんか？全体のRTLをまずは一通り揃えることを優先したいです。」→ この Phase は RTL と simulation（iverilog の単体 TB、必要なら Verilator）まで。板と timing は対象外。

## 部分（part）

| part | 範囲 | 受け入れ | 状態 |
| --- | --- | --- | --- |
| A | VF: `atg_vf_index`・`atg_vf_fetch`・`atg_vf_conv`・`atg_vf_asm` を spec（amaterrace-gpu `spec/gpu/vf/`）から実装、`atg_vf` の結線の確認 | spec §8 の TB 4 本（`atg_vf_index_tb`・`atg_vf_fetch_tb`・`atg_vf_conv_tb`・`atg_vf_asm_tb`）の PASS 行が全部出る、`tools/lint.sh` clean、既存の `sim/run.sh` の PASS が減らない | in-progress（2026-10-24 P1 に依頼） |
| B1 | `atg_su_exp`（PT・export、PT の block ごとの割り当て）・`atg_su_lda`（primitive table）を spec から実装し単体 TB、exp の port の変更（vl_mask）、`atg_cp_seq` の VF の範囲の SET_REG の待ち | exp・lda の TB の PASS、cp_seq の TB に VF の待ちの case、lint clean | cleared（2026-10-24 Q1: amaterrace-gpu c29f858） |
| B2 | VF → SU（vertex）→ RX → SU（fragment）→ ROP の `atg_top` の結線（HAS_VF、N=1）、shading された三角形（頂点の色の補間）の scene と model | Verilator（または iverilog）の TB で色と depth が model と一致、PNG | cleared（2026-10-24 Q1: amaterrace-gpu 1d3253e、main の merge ce607b4） |
| C | TU（`atg_tu_addr`・`cache`・`filter`）と `atg_su_tex`、最近傍 → bilinear | textured な三角形が model と一致 | in-progress（2026-10-24 P1） |

## 記録
- 2026-10-24 Q1: part A を P1（phase-runner、Opus 5.5 high）に依頼。amaterrace-gpu の worktree `~/amaterrace-worktrees/p1-vf`（branch `p1-vf`、基点 main 3839cb5）。範囲は VF の子 4 つと単体 TB 4 本、spec §10 の更新。結果は P1 の返答を Q1 がここに書く。
- 2026-10-24 P1 → Q1（part A の結果）: amaterrace-gpu 30dc42a（15 file: `rtl/gpu/vf/atg_vf_{index,fetch,conv,asm}.v`、spec 5 本、TB 4 本、`tools/vf-vectors.py` と `sim/vec/vf_conv.txt`）。P1 の申告: `tools/lint.sh` 102 項目 clean、`sim/run.sh` 全体 64/64 PASS、TB に故意の誤りを入れて FAIL になることを確認（conv の vector 1 語、asm の RTL 3 通り）。Q1 の確認: `git cat-file -t 30dc42a` = commit（親 3839cb5、メッセージ WIP）、TB 4 本を再実行して PASS 行は index 5・fetch 3・conv 3・asm 5（spec §8 の数と一致）。main に fast-forward（`30dc42a 3839cb5`）、RX の spec 2 本の prim_last の行を asm §7 に合わせた 84c1753。push はしていない。
  - spec の読み（P1 が実装し各 spec §10 に記録）: (1) ATTR_DST[k] の word 全体が 0 なら属性 k は無効（[7:6] は数 − 1 なので 0 個を表せない。代償: 1 成分の属性を r0 に置けない。代案は design §2.3 に enable の bit）。(2) per-instance は Vulkan の規則 FIRST_INSTANCE + (inst − FIRST_INSTANCE) / divisor、divisor 0 は FIRST_INSTANCE。(3) 属性の無い warp は dummy の af 1 つで conv_done。(4) fp16 の denormal は正確に fp32 へ、NaN は 0x7FC00000、予約の format は (0,0,0,1)。(5) primitive の無い draw は prim を出さず busy が落ちるだけ。(6) point は (p,p,p)・line は (a,b,b)、VERTEX_OFFSET は indexed だけ、instance の切り替えで strip・fan を切る、予約の TOPOLOGY は points。
  - part B への入力: **A. PT の allocator**（atg_su_exp）は 1 本の counter では足りない。vl_mask の外の lane の PT entry は SU が vl_go の時に返し、32 entry の block ごとに使用数を数える（asm は最後の primitive を 1 つ保留するので、古い warp の entry が最大 3 つ残る）。vl_mask = 0 の vl_go も受ける。exp の port の変更になるので part B の spec の変更として決める。**B. CP の SET_REG**: `atg_cp_seq` の pay_wait が VF の範囲（STATE 0x020〜0x04F、0x1C0〜0x1CF）への書き込みを VF の busy の間も止めない。part B で直す。
  - 残件: asm は warp を 1 つずつ（PRIMS と次の SEND が重ならない）、fetch は属性 1 つ約 6 cycle・同時 2 本（spec §6 の目標は 4 本）。p015 の性能の作業で扱う。合成・timing・板は未実施（範囲外）。
- 2026-10-24 Q1: part B を B1・B2 に分けて P1 に依頼。PT の割り当ては P1 の案で決定（技術の判断、Q1）: atg_su_exp に vl_mask を渡し、32 entry の block ごとに使用数を数える、vl_go の時に vl_mask の外の entry を返す、vl_mask = 0 の vl_go を受ける。属性の無効の判定（ATTR_DST の word 0）は part A のまま。
- 2026-10-24 P1 → Q1（part B の結果）: B1 c29f858（親 84c1753）、B2 1d3253e（親 c29f858）、38 file。P1 の申告: lint 104 項目 clean、`sim/run.sh` 全体 69/69 PASS、Verilator `PASS verilator`・`raster`・`shade`（117,564 cycle、6,145 語が model と一致）、故意の誤りで各 TB が FAIL になることを確認。
  - Q1 の確認: 両 SHA は commit（メッセージ WIP）。worktree で TB を再実行し PASS 行は atg_su_pt 3・atg_su_exp 3・atg_su_lda 2・atg_rx_frag 3・atg_cp_seq 7・atg_top_shade 3、atg_top_raster PASS（報告と一致）。main（d901e4c、README と GHCI の更新が先に入っていた）へ `--no-ff` で merge、衝突なし、merge の後に lint clean・`atg_top_shade`・`atg_cp_seq`・`atg_vf_asm` PASS、Verilator 3 本 PASS を確かめて ce607b4（親 d901e4c 1d3253e）。push はしていない。PNG（6 倍）: `build/review/ws288-p009-shade.png`（頂点の色の補間された三角形 24 個、depth で重なる）。
  - 実装: `atg_su_pt`（新、PT と block ごとの割り当て）、`atg_su_exp`（fragment の export buffer）、`atg_su_lda`、`atg_rx_frag`（新、covered な quad を最大 8 個で fragment warp、r0〜r5 = b0・b1・b2・z・x+0.5・y+0.5、perspective の b）、`atg_cp_seq` の VF の範囲の SET_REG の待ち、atg_top は VF の primitive を `pt_done` まで RX に通さない。
  - spec の読み（P1、Q1 が承認: 技術の判断で委任の範囲）: (1) exp を `atg_su_exp` と `atg_su_pt` に分割（module の規則）、(2) port の追加（pt の pt_warp・vl_go・vl_mask・pt_done、atg_su の pt_done、exp の fl_*）、(3) exp の読み（quad の位置は r4/r5 の floor、ex_mask は covered かつ exp を受けた lane、kill は exp より前に compiler が置く、書かれない成分は不定、point size と SECOND_COLOR は v1 で保存しない）、(4) r4・r5 は画素の中心（Vulkan の FragCoord）、(5) lda の pw_addr の配置（[10:8] primitive、[7:6] vertex、[5:0] word、0xFFF は map）、(6) scan を `atg_rx_scan` と `atg_rx_frag` に分割、(7) RX の PT の読みは frag を優先。
  - 残件: `atg_cp_seq.v` は 406 行（今回の前から 400 行の上限を 6 行超え、増えていない）→ p016（規約の全文の見直し）で分ける。`atg_top_shade` は iverilog で約 190 秒（`sim/run.sh` の上限 300 秒）。atg_rx_frag は quad 1 つ約 200 cycle で 1 つずつ（p015 の性能）。export buffer と lda の表の大きさの削減は未。合成・timing・板は未実施（範囲外）。part C（TU）は未着手。
- 2026-10-24 Q1: part C を P1 に依頼（ユーザー「進めてください。」）。worktree `~/amaterrace-worktrees/p1-tu`（branch `p1-tu`、基点 main ce607b4）。
