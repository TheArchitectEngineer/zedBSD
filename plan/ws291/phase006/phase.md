<!-- awesome-plan project=zedbsd record=ws291-p006 -->

# ws291-p006 RDNA の NGG B: primitive の culling（w の符号・画面外・裏面・小さい三角形）

- 親: [WS291](../ws.md)。
- 状態: in-progress（2026-10-27 13:55 P2、q1611-i01）。
- 出典: 2026-10-27 04:44 ユーザー（Q1 の問い「NGG の A〜C をどう進めますか？」へのクリック）「A〜C を今進める（推し）」。
- 依存: p005（cleared 2026-10-27 13:52 Q1）。

## 範囲

1. primitive shader に culling を足す: 頂点の lane が位置（x/w・y/w・w）を LDS に置き、subgroup の s_barrier の後、primitive の lane が 3 頂点を読んで
   判定し、落とす primitive は export の語の null の bit（bit 31）を立てる。頂点の詰め直し（compaction）と属性の後回しは p007。
2. 判定は Mesa 25.0.7 の ac_nir_cull.c（ac_nir_cull_triangle）と同じ: w が全部 0 以下（か NaN）、面（裏・表と面積 0、行列式の有限の時だけ）、
   bbox の画面外（NDC の ±1）、小さい三角形（viewport の変換の後の bbox が sample の中心を含まない）。w が負の頂点を持つ primitive は bbox で落とさない。
3. executor が draw ごとに culling の設定を user SGPR に書く（viewport の scale・translate、小さい三角形の精度、表の向き・落とす面・有効の bit）。
   三角形でない topology では無効。
4. 切り替え: boot の行 `amdrdna.vk.ngg=2`（NGG と culling）。0 legacy、1 NGG（passthrough）は今のまま。
5. 受け入れ: host の模型で culling の結果（null の bit）が参照の判定と一致、mview 1 体で Linux と pixel 差 0、6500 体で 8 pixel から悪化しない、
   desktop、`measure.sh hang` の回復、scene-6500 の GPU の ms を記録（p005 の passthrough は 41.37 ms）。

## 設計（2026-10-27 13:55 P2）

- 形: hardware の mode は passthrough のまま（v0 の packed の語に頂点の番号 9 bit ×3）。compaction をしないので頂点の数・primitive の数・割当の要求は変わらない。
  Mesa は culling を必ず compaction と組にする（passthrough を切る）が、p006 は null の primitive だけで PA の仕事を減らす段。passthrough で null の bit を
  立てる事が hardware に効くかは実機で確かめる（効かなければ p007 の compaction と一緒に passthrough を切る）。
- 順: setprio → s_barrier → wave 0 の GS_ALLOC_REQ → 頂点の lane: 頂点の program、位置と parameter の export、位置を LDS（頂点ごとに 12 byte、
  subgroup の頂点の番号 = wave 番号 × 32 + lane）→ 全 lane で `s_waitcnt lgkmcnt(0)`・s_barrier → primitive の lane: v0 の 3 頂点を LDS から読み判定、
  `exp prim`（v0 | 落とすなら 0x80000000）done → s_endpgm。primitive の export は頂点の export の後（Mesa の late prim export と同じ順）。
- user SGPR: s8〜s10 は今のまま（base vertex・first instance・instance mask）、s11〜s15 = viewport の scale x・y、translate x・y、精度（float）、s16 = bit
  0 表を落とす・1 裏を落とす・2 表は CCW（viewport の y の反転を含む）・3 小さい三角形・4 culling 有効。
- LDS: RSRC2_GS の LDS_SIZE（bit 19〜26、512 byte 単位）に 128 頂点 × 12 byte = 3。
- 必要な命令: v_mbcnt_lo_u32_b32（lane の番号）を encoder に足す。他（ds_read/write_b32、v_rcp、v_cmp、v_cndmask、v_rndne、v_min/max、s_barrier）はある。
- 模型（plan/ws039/tests/gfx10-machine.c）: graphics の run に LDS、v_mbcnt、s_barrier（1 wave では何もしない）。試験は primitive の番号を同じ wave の
  頂点にして、参照の判定（試験の C）と null の bit を比べる。

## 記録

- 2026-10-27 13:55 P2: 着手。Mesa 25.0.7 の ac_nir_lower_ngg.c（add_deferred_attribute_culling）、ac_nir_cull.c、radv_cmd_buffer.c
  （radv_emit_ngg_culling_state、radv_get_ngg_culling_settings、NGG の LDS_SIZE）を読んだ。
