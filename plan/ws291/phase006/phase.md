<!-- awesome-plan project=zedbsd record=ws291-p006 -->

# ws291-p006 RDNA の NGG B+C: primitive の culling と頂点の詰め直し（Mesa の形）

- 親: [WS291](../ws.md)。
- 状態: in-progress（2026-10-27 13:55 P2、q1611-i01）。
- 出典: 2026-10-27 04:44 ユーザー（Q1 の問い「NGG の A〜C をどう進めますか？」へのクリック）「A〜C を今進める（推し）」。
- 依存: p005（cleared 2026-10-27 13:52 Q1）。
- 範囲の変更（2026-10-27 14:50 Q1 の判断、承認済みの A〜C の範囲の中の技術の判断）: 「仮説 A を採り、culling は p007 の compaction と組にして
  Mesa の形（ac_nir_lower_ngg.c の culling + compaction、GS_ALLOC_REQ の数も生き残りに合わせる）で入れる。p006 は範囲を変えて p007 と一緒に進め」
  「Fable の助言は今回は取りません」。→ compaction 無しの culling は GFX10 で止まる（仮説 A、証拠は下の記録）ので p007 と統合した。下の「範囲」「設計」の
  1 と 5 は統合前の物で、新しい範囲は「範囲（統合後）」。

## 範囲（統合後、2026-10-27 14:50）

1. Mesa 25.0.7 の ac_nir_lower_ngg.c（add_deferred_attribute_culling、compact_vertices_after_culling）に忠実に: 位置だけを計算する前半 → LDS → 判定 →
   生き残った primitive が使う頂点だけを wave をまたいで詰め直す（prefix の数え方、exporter の番号）→ GS_ALLOC_REQ の数を生き残りに合わせる
   （0 なら GFX10 の fully culled の回避）→ 生き残った頂点だけが属性を含む後半を走らせて export。
2. host の模型に、参照されない頂点の export（export の領域が返らない形）を検出できるなら規則を足す。
3. 受け入れ: mview 1 体 pixel 差 0、6500 体 8 pixel から悪化しない、desktop、`measure.sh hang` の回復、scene-6500 の GPU の ms（目標 約 20 ms）。
   実機は 1 回にまとめる。

## 範囲（統合前）

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
- 2026-10-27 14:05 設計の敵対的レビュー（design-reviewer、範囲は設計の節だけ）の要点: (1) passthrough のまま null の bit が効くかは Mesa に前例が無い
  （Mesa は culling と passthrough を組まない。passthrough でも export の語は PA が読む: edge flag の OR）。PA も同じ primitive を落とすので pixel 差 0 では
  null の bit が効いた証明にならない → GPU の ms の変化か、全部 null にする診断で確かめる。(2) 順（export → barrier → late prim export）は Mesa の late
  export と同じ形。GS_ALLOC_REQ は Navi1x で必須のまま。(3) LDS は RSRC2_GS の LDS_SIZE だけ、頂点の番号の対応は Mesa と同じ（確認済み）。模型は 1 wave
  なので wave をまたぐ LDS は試験されない。(4) 無効にする条件（conservative・sample locations・複数 viewport・polygon mode）は今の executor に無い機能
  （塗りつぶしの三角形・1 sample・viewport 1 つ）なので、三角形の topology だけを条件にした。
- 2026-10-27 14:08 実装 commit b73115761: compiler（place_for_culling・cull_primitives、v_mbcnt_lo を encoder に、culling の user SGPR と LDS）、
  executor（amdrdna_vk_state_culling、s11〜s16 を draw ごと、`amdrdna.vk.ngg=2`）、模型（graphics の run に LDS、v_mbcnt、s16）、試験（+nggc の
  variant、参照の判定 reference_culled、小さい三角形が効く viewport の幅）。host: compiler host test 58 PASS（+nggc 6 本）。変異の確かめ: 参照の表の向きを
  反転すると +nggc が 5 本 FAIL、小さい三角形の判定を外すと 2 本 FAIL（試験が判定を見ている）。vk host test PASS。vmunix warning 0（sha256 先頭 43af3dfafe0bb94b）。
- 2026-10-27 14:12 実機 1 回目（ngg=2）: desktop は描ける。nggshape は全部 ok（`--indexed --triangles=16` drawn 58311、同 `--repeat=1000`、
  `--triangles=50000 --indexed --depth` drawn 64828、`--cull --allback` drawn 0、`--tiny` drawn 0、`--cull --allback --tail` drawn 65536）。
  mview の shot（1 体、FIFO、合成）の最初の描画で GPU が止まり、queue の reset も失敗（kiq の fence が進まない）、compositor は device lost で終わった。
  止まった時: GRBM_STATUS 0xa2611028（GE・SPI・PA busy、SX は立たない）、waves valid 0、PA_SU busy。kernel の mark: build/ws291/measure/p006/hang1-dmesg2.txt。
  → 入れ直しを Q1 に依頼。次は診断 kernel（commit しない、build/ws291/measure/p006/diag1.patch、vmunix-diag1）の boot 行 `amdrdna.vk.nggx`:
  1 = culling の flag を 0（LDS・barrier・late prim export の形だけ）、2 = 全部落とす、4 = 小さい三角形を切る、8 = 面の cull を切る。まず 1 で mview を見て、
  形が原因か null の判定が原因かを分ける。
- 2026-10-27 14:20 診断（vmunix-diag1、Q1 の入れ直しの後）: `nggx=1`（flag 0: LDS・barrier・late prim export の形だけ、判定は飛ばす）で mview 1 体の shot は
  Linux と pixel 差 0（diag1-shot-1）、6500 体 over15 8 pixel（diag1-shot-6500）、scene-6500 fence 41.27 ms（passthrough 41.37 ms と同じ）、止まらない。
  → 形は正しく、止まるのは判定の側。`nggx=0`（判定あり）に戻し nggshape を危険の少ない順に流すと、最初の
  `--triangles=50000 --indexed --cull --allback`（多くの subgroup の primitive が全部 null）で止まった（2 三角形 1 subgroup の全部 null は通る）。
  → GFX10 の「NGG が primitive を出さない subgroup で止まる」（Mesa の has_ngg_fully_culled_bug、ac_gpu_info.c:1316。Mesa は数 0 の時に vertex 0 の
  退化三角形を 1 つ出す）と同種と見て、wave の primitive が全部落ちる時は先頭の lane を vertex 0 の退化三角形（語 0、null でない）で出す回避を入れた
  （commit bec5dc26f。試験の参照も同じ規則、規則を外すと +nggc が 6 本 FAIL）。vmunix sha256 先頭 a4d993852143aa70。再び入れ直しを Q1 に依頼。
- 2026-10-27 14:44 回避 bec5dc26f の実機: `--triangles=50000 --indexed --cull --allback` はまだ止まる（wave ごとの退化三角形では足りない）。
  → passthrough の mode のまま null を立てる事自体を疑い（Mesa は culling を passthrough と組まない: ac_nir_lower_ngg.c:1645 の assert、
  radv_shader_info.c:1729）、culling の program だけ passthrough を切った: 頂点の番号は v0 の 16 bit の半分 2 つと v1 の下半分（GS_VGPR_COMP_CNT 1、
  radv_shader.c:2165-2186）、primitive の語は shader が組む（9 bit ずつ 10 おき、edge flag 0: ac_nir_pack_ngg_prim_exp_arg、RADV の初期 edge flag 0）、
  VGT_SHADER_STAGES_EN の PRIMGEN_PASSTHRU_EN を落とす（内部の quad は passthrough のまま）。commit 9b7a462c3、host 58 PASS、vk host PASS、
  vmunix warning 0（17be55e347012025）。3 回目の入れ直しを Q1 に依頼。
- 2026-10-27 14:47 9b7a462c3（culling の時は passthrough を切る）の実機: `--triangles=50000 --indexed --cull --allback` は同じく止まる。止まった時
  GRBM_STATUS 0xa2711028（SX・GE・SPI・PA busy）、waves valid 0（build/ws291/measure/p006/hang3-dmesg.txt）。Q1 の指示（「これでも止まる場合は、
  闇雲に試行を重ねず、一度止めて phase006 に結果と仮説を書き、Q1 に報告」）でここで止めた。機械は止まったまま（入れ直しが要る）。stick は
  vmunix-cull3（17be55e347012025）と `amdrdna.vk.ngg=2` のまま → 次に起動する前に Debian から ngg=0 か 1 に戻すこと。

### 結果のまとめと仮説（2026-10-27 14:47）

| 条件 | 結果 |
| --- | --- |
| culling の形だけ（LDS・barrier・late prim export、判定を飛ばす、nggx=1） | 止まらない。mview 1 体 pixel 差 0、6500 体 8 pixel、fence 41.27 ms |
| 判定あり・passthrough のまま | nggshape 16 三角 ×1000・5 万 三角（見える）・2 三角の全部 null は ok。5 万 三角の全部裏（全部 null）と mview で止まる |
| + wave の primitive が全部落ちる時に先頭を退化三角形で残す | 5 万 三角の全部裏で止まる |
| + passthrough を切る（Mesa と同じ mode、語は shader が組む） | 5 万 三角の全部裏で止まる（SX も busy） |

- 仮説 A（最有力、推測）: GFX10 では、位置を export した頂点が null でない primitive から 1 つも参照されないと、その頂点の export の領域が返らない
  （p005 の「primitive の export が 1 つ落ちると SX_BUSY が残り、繰り返すと止まる」と同じ形）。hardware が自分で作る subgroup では全頂点が
  どれかの primitive に使われるので起きない。null を立てるとこの前提が崩れ、多くの subgroup で溜まって止まる（2 三角では溜まらない）。
  Mesa は culling を必ず compaction（生き残った primitive が使う頂点だけを詰め、GS_ALLOC_REQ の頂点数も減らす、ac_nir_lower_ngg.c の
  add_deferred_attribute_culling・compact_vertices_after_culling）と組にしており、compaction 無しの culling は Mesa に無い。
- 仮説 B（低）: null の primitive の数そのもの（PA 側の数え方）。仮説 A と区別するには「全部 null だが全頂点を参照する退化三角形を足す」形の試行が要る。
- 帰結（判断が要る）: 仮説 A が正しければ p006（compaction 無しの culling）は GFX10 で成り立たず、culling は p007 の compaction と一緒に入れる
  （Mesa の形: 位置だけの前半 → LDS → 判定 → 生き残った頂点の詰め直しと GS_ALLOC_REQ の数の更新 → 属性の後半）。p006 と p007 を 1 つにまとめるか、
  p006 を「Mesa の形の culling + compaction」に範囲を変えるかは Q1・ユーザーの判断。
- commit 済みの物（b73115761・bec5dc26f・9b7a462c3）は `amdrdna.vk.ngg=2` の時だけ効き、既定（legacy）と ngg=1 には影響しない。

### 統合後の実装と実機（2026-10-27 14:52〜15:08）

- 実装 commit f54203c87（Mesa の形、ac_nir_lower_ngg.c の add_deferred_attribute_culling・compact_vertices_after_culling、ac_nir.c の
  ac_nir_repack_invocations_in_workgroup、ac_nir_prerast_utils.c の fully culled の回避に合わせた）:
  頂点の lane が x/w・y/w・w と「残す」印 0 を LDS へ → barrier → primitive の lane が判定（三角形の時だけ。points・lines は全部残す）、残す primitive が
  自分の頂点（topology の数だけ）に印 → barrier → 頂点の lane が印を読み、wave ごとの残る数（s_bcnt1）を LDS の wave の語へ → barrier → 各 wave が全 wave
  の数を読み、前の wave の和（新しい番号の始まり）と subgroup の和（残る頂点数）→ wave 0 が GS_ALLOC_REQ（残る頂点数・全 primitive。0 なら 1・1 を要求し
  lane 0 が vertex 0 の三角形と NaN の位置を export）→ 残る頂点が新しい番号を自分の場所に、出力（位置・point size・parameter）を新しい番号の場所に
  書く → barrier → 先頭の lane（残る数だけ）が出力を読んで export → primitive の lane が頂点の新しい番号で語を組んで export（捨てた物は null）。
  Mesa と違う点: 属性の後回し（位置だけの前半と属性の後半で VS を 2 回走らせる）はせず、VS を 1 回走らせて出力を LDS で運ぶ（hardware に見える
  export の数・番号・要求の数は Mesa と同じ）。LDS は 128 頂点 × 場所の大きさ + 16 byte（64 KiB を超える program は culling しない）。
  `s_bcnt1_i32_b32`・`s_min_u32` を encoder と模型に足した。
- host: compiler host test 58 PASS（+nggc 6 本は 1 wave の subgroup で、参照の判定・残す頂点の番号・要求の数・出力の並び・primitive の語を比べる。
  「export した頂点はどれかの null でない primitive が使う」の規則も試験で見る）。変異の確かめ: 新しい番号を lane の番号にすると +nggc が FAIL、
  何も残らない時の要求の頂点数を 2 にすると FAIL。vk host test PASS。vmunix warning 0（sha256 先頭 24b0f273d50f74d7）。
- 実機（ngg=2、Debian から stick に入れて 1 回の起動）:

| 確認 | 結果 | 証拠 |
| --- | --- | --- |
| nggshape 13 種（16 三角 ×1000、5 万 三角の depth・全部裏・tiny・outside・cross・shuffle・cluster・mview・mview+cull・heavy、degenerate、texture） | 全部 ok、止まらない（全部裏の 5 万 三角は drawn 0） | SSH の出力 |
| mview 1 体の shot（Linux 比） | pixel 差 0 | build/ws291/measure/p006/compact-shot-1 |
| mview 6500 体の shot | max 1、over15 0（基準 8 pixel より良い） | compact-shot-6500 |
| scene-1 / 1500 / 6500（`--spin=8 --content=game --present=mailbox`） | 105.3 fps（fence 1.23 ms）/ 64.8 fps（9.06 ms）/ 29.2 fps（fence 30.68 ms、frame 34.08 ms） | compact-scene-1b・1500・6500（scene-1 の 1 回目は起動直後の 792 ms の frame を含む 53 fps） |
| `measure.sh hang` | vkhang lost → reset 4 ms → loops ok、mview 58 fps、desktop 正常 | compact-hang |

- scene-6500 の GPU: passthrough 41.37 ms → 30.68 ms（−26 %）。Linux（RADV の NGG culling）約 20 ms には届かない。残りの候補: 属性の後回し
  （捨てる頂点の属性の計算を省く）、LDS の大きさ（6500 体の mview の場所の大きさによる占有の低下）、VGPR の数、guard band・binning（p008）。
- 未実施: desktop の frame-ops の計測（既定を NGG にするかの判断の材料）、属性の後回し。stick は vmunix-compact1 と ngg=2 で zedBSD 起動中。
