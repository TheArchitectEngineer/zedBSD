<!-- awesome-plan project=zedbsd record=ws288-p009 -->

# ws288-p009 VF・SU の vertex/fragment・TU（graphics の pipeline の RTL）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §2.3（VF の STATE 0x020〜0x04F、ATTR_DST 0x1C0〜）、§3.4.3、§3.7、§3.8、§8 の p009 の行、§7.6 の p009 の module。
- 状態: in-progress（2026-10-24 Q1）
- 方針（2026-10-24 ユーザー）:「今回は合成できればいいことにして、KC705での動作とパイプライン分割はあとにしませんか？全体のRTLをまずは一通り揃えることを優先したいです。」→ この Phase は RTL と simulation（iverilog の単体 TB、必要なら Verilator）まで。板と timing は対象外。

## 部分（part）

| part | 範囲 | 受け入れ | 状態 |
| --- | --- | --- | --- |
| A | VF: `atg_vf_index`・`atg_vf_fetch`・`atg_vf_conv`・`atg_vf_asm` を spec（amaterrace-gpu `spec/gpu/vf/`）から実装、`atg_vf` の結線の確認 | spec §8 の TB 4 本（`atg_vf_index_tb`・`atg_vf_fetch_tb`・`atg_vf_conv_tb`・`atg_vf_asm_tb`）の PASS 行が全部出る、`tools/lint.sh` clean、既存の `sim/run.sh` の PASS が減らない | in-progress（2026-10-24 P1 に依頼） |
| B | SU の `atg_su_exp`（PT・export）・`atg_su_lda`（primitive table）、VF → SU → RX → SU → ROP の結線、shading された三角形 | Verilator の PNG が model と一致 | planned |
| C | TU（`atg_tu_addr`・`cache`・`filter`）と `atg_su_tex`、最近傍 → bilinear | textured な三角形が model と一致 | planned |

## 記録
