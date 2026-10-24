<!-- awesome-plan project=zedbsd record=ws288-p009 -->

# ws288-p009 VF・SU の vertex/fragment・TU（graphics の pipeline の RTL）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §2.3（VF の STATE 0x020〜0x04F、ATTR_DST 0x1C0〜）、§3.4.3、§3.7、§3.8、§8 の p009 の行、§7.6 の p009 の module。
- 状態: in-progress（2026-10-24 Q1）
- 方針（2026-10-24 ユーザー）:「今回は合成できればいいことにして、KC705での動作とパイプライン分割はあとにしませんか？全体のRTLをまずは一通り揃えることを優先したいです。」→ この Phase は RTL と simulation（iverilog の単体 TB、必要なら Verilator）まで。板と timing は対象外。

## 部分（part）

| part | 範囲 | 受け入れ | 状態 |
| --- | --- | --- | --- |
| A | VF: `atg_vf_index`・`atg_vf_fetch`・`atg_vf_conv`・`atg_vf_asm` を spec（amaterrace-gpu `spec/gpu/vf/`）から実装、`atg_vf` の結線の確認 | spec §8 の TB 4 本（`atg_vf_index_tb`・`atg_vf_fetch_tb`・`atg_vf_conv_tb`・`atg_vf_asm_tb`）の PASS 行が全部出る、`tools/lint.sh` clean、既存の `sim/run.sh` の PASS が減らない | in-progress（2026-10-24 P1 に依頼） |
| B | SU の `atg_su_exp`（PT・export）・`atg_su_lda`（primitive table）、VF → SU → RX → SU → ROP の結線、shading された三角形 | Verilator の PNG が model と一致。入力: part A の記録の A・B・1 | planned |
| C | TU（`atg_tu_addr`・`cache`・`filter`）と `atg_su_tex`、最近傍 → bilinear | textured な三角形が model と一致 | planned |

## 記録
- 2026-10-24 Q1: part A を P1（phase-runner、Opus 5.5 high）に依頼。amaterrace-gpu の worktree `~/amaterrace-worktrees/p1-vf`（branch `p1-vf`、基点 main 3839cb5）。範囲は VF の子 4 つと単体 TB 4 本、spec §10 の更新。結果は P1 の返答を Q1 がここに書く。
- 2026-10-24 P1 → Q1（part A の結果）: amaterrace-gpu 30dc42a（15 file: `rtl/gpu/vf/atg_vf_{index,fetch,conv,asm}.v`、spec 5 本、TB 4 本、`tools/vf-vectors.py` と `sim/vec/vf_conv.txt`）。P1 の申告: `tools/lint.sh` 102 項目 clean、`sim/run.sh` 全体 64/64 PASS、TB に故意の誤りを入れて FAIL になることを確認（conv の vector 1 語、asm の RTL 3 通り）。Q1 の確認: `git cat-file -t 30dc42a` = commit（親 3839cb5、メッセージ WIP）、TB 4 本を再実行して PASS 行は index 5・fetch 3・conv 3・asm 5（spec §8 の数と一致）。main に fast-forward（`30dc42a 3839cb5`）、RX の spec 2 本の prim_last の行を asm §7 に合わせた 84c1753。push はしていない。
  - spec の読み（P1 が実装し各 spec §10 に記録）: (1) ATTR_DST[k] の word 全体が 0 なら属性 k は無効（[7:6] は数 − 1 なので 0 個を表せない。代償: 1 成分の属性を r0 に置けない。代案は design §2.3 に enable の bit）。(2) per-instance は Vulkan の規則 FIRST_INSTANCE + (inst − FIRST_INSTANCE) / divisor、divisor 0 は FIRST_INSTANCE。(3) 属性の無い warp は dummy の af 1 つで conv_done。(4) fp16 の denormal は正確に fp32 へ、NaN は 0x7FC00000、予約の format は (0,0,0,1)。(5) primitive の無い draw は prim を出さず busy が落ちるだけ。(6) point は (p,p,p)・line は (a,b,b)、VERTEX_OFFSET は indexed だけ、instance の切り替えで strip・fan を切る、予約の TOPOLOGY は points。
  - part B への入力: **A. PT の allocator**（atg_su_exp）は 1 本の counter では足りない。vl_mask の外の lane の PT entry は SU が vl_go の時に返し、32 entry の block ごとに使用数を数える（asm は最後の primitive を 1 つ保留するので、古い warp の entry が最大 3 つ残る）。vl_mask = 0 の vl_go も受ける。exp の port の変更になるので part B の spec の変更として決める。**B. CP の SET_REG**: `atg_cp_seq` の pay_wait が VF の範囲（STATE 0x020〜0x04F、0x1C0〜0x1CF）への書き込みを VF の busy の間も止めない。part B で直す。
  - 残件: asm は warp を 1 つずつ（PRIMS と次の SEND が重ならない）、fetch は属性 1 つ約 6 cycle・同時 2 本（spec §6 の目標は 4 本）。p015 の性能の作業で扱う。合成・timing・板は未実施（範囲外）。
