<!-- awesome-plan project=zedbsd record=ws291-p004 -->

# ws291-p004 RDNA の GPU 側の最初の改善（PA・VGT の設定を RADV に、scene-6500 の GPU の ms）

- 親: [WS291](../ws.md)。
- 状態: cleared（2026-10-27 04:43 Q1、driver の変更なし、差は NGG の culling と判明）。前: in-progress（2026-10-27 04:33 P2、q1609-i01）。結果は下（2026-10-27 04:50）。clearance は Q1 の判定。
- 依存: p003（cleared、merge 5914a9074）。

## 範囲

1. PA の small primitive filter、back-face の cull（pipeline の cull mode）、VGT・GE の vertex reuse などを RADV（Mesa 25.0.7）の値に合わせる。
2. `measure.sh` で前後の scene-1・1500・6500 と bench を測り、GPU の ms の変化を記録する。正しさは keiland-shot と Linux の基準を比べる。
3. 足りなければ NGG（primitive shader）の経路の作業の見積もり（compiler・executor・register）を書いて Q1 に送る（実装はしない）。
4. 他に安く効く GPU 側の設定（RADV と違う物）が profile で見つかれば、同じ形で試す。

## 受け入れ

- 前後の数値（GPU の ms）、正しさの比較、または「足りない」理由と NGG の見積もり。

## 出典

- Mesa 25.0.7: `build/ws291/ref/mesa-25.0.7.tar.xz`。SHA-256 `592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6`（ws039 の記録と同じ）。署名 `mesa-25.0.7.tar.xz.sig` を archive.mesa3d.org から取り、鍵 57551DE15B968F6341C248F68D8E31AFC32428A6（Eric Engestrom、keys.openpgp.org）で `gpg --verify` が Good signature（2026-10-27。鍵は web of trust で認証していない）。読むだけで、tree には入れていない。

## 結果

### 1. 設定の照合（amdrdna の vk-state.c と RADV）

| register | amdrdna（変更前） | RADV（Navi10） | 差 |
| --- | --- | --- | --- |
| PA_SU_SMALL_PRIM_FILTER_CNTL | SMALL_PRIM_FILTER_ENABLE 1 | 同じ（ac_cmdbuf.c gfx10_init_graphics_preamble_state、radv_queue.c radv_emit_graphics） | なし |
| PA_SU_SC_MODE_CNTL の CULL_FRONT・CULL_BACK・FACE | pipeline の cull mode と front face（amdrdna_vk_state_raster） | 同じ（radv_emit_raster_state）。mview は material ごとに BACK か NONE | なし |
| VGT_VERTEX_REUSE_BLOCK_CNTL | 14 | 14（legacy だけ） | なし |
| GE_CNTL | PRIM_GRP_SIZE 128、VERT_GRP_SIZE 256 | 同じ（gfx10_emit_ge_cntl、GS・tess 無し） | なし |
| VGT_SHADER_STAGES_EN | legacy VS、wave32、MAX_PRIMGRP_IN_WAVE 2 | Navi10 は NGG（下）。legacy なら同じ | NGG |
| PA_SC_BINNER_CNTL_0/1、PA_SC_NGG_MODE_CNTL、DB・CB の cache policy、PA_SC_MODE_CNTL_1 | p002a で RADV の preamble に合わせ済み | 同じ（binning は RADV が有効） | binning（効果なし、下） |
| PA_CL_GB_VERT_CLIP_ADJ 等（guard band） | 4 語とも 1.0 | viewport から (32767 − \|translate\|) / \|scale\|（radv_emit_guardband_state） | あり。画面の端を跨ぐ三角形の clip だけに効く。scene-6500 の格子は画面の内側なので効果なし（未実装、下の残り） |

結論: legacy の経路の設定は既に RADV と同じで、register を合わせる変更は無い。driver は変えていない。

### 2. Linux（RADV）の切り分け（実機 10.0.10.3、mview `--copies=6500 --spin=8 --content=game --present=mailbox`、GPU の待ちの ms/frame）

`measure.sh linux-mview`（LINUX_ENV で RADV_DEBUG・RADV_PERFTEST を渡す。この Phase で足した）。証拠 build/ws291/measure/p004-linux/。全 run で weston の repaint の大半が plane-only（direct scanout）。

| 構成 | fps | GPU（fence）ms |
| --- | --- | --- |
| 既定（NGG、shader の culling あり） | 49.2 | 19.9 |
| RADV_DEBUG=nonggc（NGG、culling なし） | 23.6 | 41.7 |
| RADV_DEBUG=nongg（legacy の VS、amdrdna と同じ形） | 23.2 | 42.5 |
| nongg と RADV_PERFTEST=gewave32（VS を wave32） | 23.3 | 42.3 |
| RADV_DEBUG=nobinning | 50.0 | 19.6 |
| RADV_DEBUG=nohiz | 47.5 | 20.6 |
| nonggc,nohiz | 23.4 | 42.2 |
| RADV_DEBUG=nodcc | 49.0 | 20.0 |
| scene-1500 既定 / nonggc | 200 / 100 | 4.67 / 9.62 |
| scene-1 既定 | 9128 | 0.06 |

zedBSD（p003 の後の kernel 9bbe2b60c、amdrdna はその後変わっていない。build/ws291/measure/bench-p003-async3/table.txt）: scene-6500 は GPU 42.18 ms（frame 45.38 ms、21.9 fps）、scene-1500 は 11.24 ms。

読み方:

- zedBSD の 42.2 ms は RADV の legacy の 42.5 ms・culling なしの NGG の 41.7 ms と同じ。差（42 対 20 ms）は全部 NGG の shader の culling（小さい・面積 0・裏・画面外の三角形を primitive shader が PA の前に捨て、生き残りの vertex だけ attribute を計算・export する）から来る。
- wave32 と wave64、binning、DCC は効かない。HiZ（HTILE）は約 0.7 ms（3%）。
- NGG の culling が無い限り、register の設定で scene-6500 は縮まない（PA・VS の legacy の経路がこの scene の限界）。

### 3. 前後の表

driver を変えていないので前と後は同じ（p003 の表）。正しさの比較（keiland-shot）も p003 のまま（1 体 差 0、6500 体 8 pixel）。この Phase では zedBSD を起動し直して測り直していない（未実施。同じ kernel の p003 の値を使った）。

### 4. NGG（primitive shader）の culling の見積もり（Q1 へ）

Linux 並み（GPU 約 20 ms）には NGG の culling が要る。culling の無い NGG（passthrough）だけでは速くならない（上の nonggc 41.7 ms）。

要る作業:

| 所 | 内容 |
| --- | --- |
| compiler（compiler/gfx10-*） | (a) 命令: `s_sendmsg`（GS_ALLOC_REQ）、primitive の export（export の target 20）、`v_mbcnt_lo_u32_b32`・`s_bcnt1_i32_b32`（生き残りの数え）、`s_barrier`・DS の読み書き（compute の共有 memory で既にある）。(b) NGG の wave の入口: merged wave info（vertex の数・primitive の数）の SGPR、primitive の vertex の番号（packed の VGPR）、vertex id・instance id の VGPR の位置が legacy の VS と違う。thread の役（vertex の lane、primitive の lane）で分ける。(c) passthrough: 始めに GS_ALLOC_REQ、vertex の lane が今の VS の本体と position・parameter の export、primitive の lane が primitive の export。(d) culling: vertex の lane が position を LDS に、barrier、primitive の lane が 3 つの position を読み、w の符号・画面外（bbox）・裏（行列式の符号と cull mode）・小さい（viewport の scale で bbox を量子化して sample を跨がない）を判定。(e) compaction: 生き残りの vertex と primitive を数えて（ballot・mbcnt、wave を跨ぐなら LDS の prefix sum）GS_ALLOC_REQ、vertex の番号を詰め直し、生き残りの vertex だけで VS の残り（attribute）を走らせる（RADV の deferred attribute culling: VS を position だけの部分と全体に分けて 2 回置く）。(f) GFX10 の不具合の回避: 全部捨てた subgroup も 1 vertex・1 primitive を出す（has_ngg_fully_culled_bug）。 |
| executor・register（vk/vk-state.c・vk-execute.c・vk-pipeline.c） | VGT_SHADER_STAGES_EN の PRIMGEN_EN と GS の wave32、program を GS の register（SPI_SHADER_PGM_LO_ES/GS・RSRC1〜4_GS・USER_DATA）と LDS の大きさで、VGT_GS_ONCHIP_CNTL（subgroup の vertex・primitive の数）、GE_NGG_SUBGRP_CNTL、GE_MAX_OUTPUT_PER_SUBGROUP、GE_PC_ALLOC、SPI_SHADER_IDX_FORMAT、VGT_PRIMITIVEID_EN の NGG の bit、VGT_GS_INSTANCE_CNT、GE_CNTL の VERT_GRP_SIZE = vertex の数 − 5（GFX10 の hang の回避）、legacy と NGG の切り替えでの VGT_FLUSH（has_vgt_flush_ngg_legacy_bug）。draw ごとの user SGPR に culling の入力（viewport の scale・translate、cull の flag、小さい primitive の精度）。 |
| 試験 | host: compiler の単体（入口の layout、culling の判定の数値）。実機: mview の 1 体・6500 体の shot（Linux と比べる）、desktop（Keiland の合成も同じ経路を通る）、`measure.sh hang`（NGG の誤りは GE の hang になる）。 |

段の案（各段で実機の shot と bench）:

| 段 | 内容 | 期待 | 大きさ（実時間の目安） |
| --- | --- | --- | --- |
| A | NGG passthrough（culling なし、subgroup は 1 wave32 から） | 速さは今と同じ（41.7 ms 相当）。正しさの土台 | 4〜8 h（hang の調べを含む） |
| B | primitive の culling（捨てた primitive を null で export、vertex の compaction なし） | PA が見る三角形が減る。どこまで縮むかは未知（vertex の export は残る） | 3〜5 h |
| C | vertex の compaction と deferred attribute（VS を 2 回置く）、wave を跨ぐ subgroup | RADV 並み（約 20 ms）が目標 | 6〜10 h |
| D | HTILE（HiZ） | 約 0.7 ms | 別 Phase（p005〜の案のまま） |

注意: GPU が 20 ms になっても、zedBSD の mview は 1 frame ごとに記録と GPU の完了を待つので、record 2.3 ms・acquire 0.65 ms（p003 の残り 2）が frame に足される（今の 45.4 ms 対 GPU 42.2 ms）。Linux（frame 20.3 ms）を超えるには記録の費用（transaction の往復）も 0.5 ms 前後まで削る必要がある。

## 残り

- guard band（PA_CL_GB_*）を viewport から計算する形に（RADV との唯一の設定の差）。scene-6500 では効かない。kernel は浮動小数点を使わないので vk-float.c の固定小数点で割り算を書く。画面の端を跨ぐ scene の clip の費用だけに効く。
- NGG（上の段 A〜C）は Q1 の判定で Phase を立てる（ws.md の p005〜p009 の案を置き換えるか）。

## 記録

- 2026-10-27 04:33 P2: 着手。Mesa 25.0.7 の署名を確かめた。amdrdna の設定を RADV の preamble・draw の emit と照合（差は guard band だけ）。
- 2026-10-27 04:38 P2: Linux で RADV_DEBUG（nongg・nonggc・nobinning・nohiz・nodcc）と RADV_PERFTEST=gewave32 を測り、差が NGG の culling だけと分かった。Q1 へ中間の報告。
- 2026-10-27 04:50 P2: NGG の見積もりを記録。driver の変更なし。
