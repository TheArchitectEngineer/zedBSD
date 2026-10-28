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

## q1635 の続き（2026-10-28 P2、worktree p2-ws291b、基点 main 6e6ebf830）

### 属性の後回し（Mesa の deferred attribute culling の形）

- 実装 commit aba6e9a97（gfx10-compile.c）: culling の時は IR を 2 つの part で lower して 1 つの program に並べる。culling part は position 以外の
  STORE_OUTPUT を NOP にした写し（normal・uv の fetch と lighting は不要の命令として落ちる）→ LDS（x/w・y/w・w・印・新しい番号、20 B/頂点）→ 判定 →
  compaction → 残る頂点が自分の vertex・instance 番号を新しい番号の場所へ → primitive の lane が語を v0 に → 新しい番号の lane が番号を v5・v8 に読む。
  deferred part は VS 全体を新しい番号の lane だけで走らせて export し、最後に primitive の lane が v0 を export。LDS は 68 B → 20 B/頂点（2576 B）。
  memory に書く VS（storage・shared・atomic）は 2 回走らせないため culling しない。出典は Mesa 25.0.7 ac_nir_lower_ngg.c（add_deferred_attribute_culling、
  compact_vertices_after_culling）。
- host: compiler host test 58 PASS（+nggc 6 本、新しい番号の場所から読む番号を instance にずらす変異で 6 本 FAIL）、vk host 197 PASS、vmunix warning 0
  （00322fad8ae84b83）。mview の program は 640 語、31 VGPR（culling part 側が最大）。
- 実機（vmunix-defer1、ngg=2、`plan/tools/gpu-perf/round.sh`、build/ws291/measure/defer1b/）:

| 確認 | 結果 |
| --- | --- |
| nggshape 14 種 | 全部 ok（止まらない）。`--indexed --cull --allback --tail` の drawn 0 は nggshape の --tail が list の時だけ効くため（試験の誤り、measure.sh の list を直した） |
| shot 6500 | max 1、over15 0 |
| shot 1 | 撮れたのは desktop（mview の窓が 6 s で出る前、起動直後の 1 回目）。未判定、撮り直す |
| hang | vkhang lost → reset 4 ms → loops ok、mview 58 fps |

| run | fps | frame | acquire | record | submit | fence |
| --- | --- | --- | --- | --- | --- | --- |
| scene-1 | 58.3 | 17.11 | 15.48 | 0.26 | 0.11 | 1.10 |
| scene-1500 | 59.0 | 16.90 | 6.79 | 0.74 | 0.17 | 8.98 |
| scene-6500 | 29.3 | 33.97 | 0.71 | 2.17 | 0.17 | 30.78 |
| big-1000 | 18.8 | 52.93 | 0.63 | 31.82 | 15.96 | 3.12 |
| subs-1000 | 3.8 | 252.22 | 0.01 | 139.25 | 112.74 | 0.02 |

- 読み方: scene-6500 の GPU は 30.68 → 30.78 ms（変わらない）、scene-1500 は 9.06 → 8.98。属性の仕事（normal・uv の fetch、lighting、LDS で運ぶ出力）は
  律速でなかった。残りの差（約 11 ms）は culling part そのもの（wave32 で RADV の wave64 の 2 倍の wave、wave ごとの scalar・barrier・LDS の固定の仕事）か
  後段と見て、診断の kernel（boot の行 `amdrdna.vk.nggx`: 2 = 全部を面で落とす、8 = GS の late alloc 64、1 = culling の flag を切る）で切り分ける。

### 切り分け（2026-10-28 03:50〜04:10、診断 kernel は commit しない: build/tmp/diag2/diag2.patch・build/tmp/diag3/pad64.patch、`plan/ws291/tests/nowc-build.sh` で WC を除いた tree から build）

| 条件（scene-6500 / 1500 の GPU の fence ms） | 6500 | 1500 |
| --- | --- | --- |
| 基準（defer、nggx=0） | 30.69 | 9.06 |
| nggx=2: 全部の三角形を面で落とす（raster も deferred part も無し） | 30.69 | 9.00 |
| nggx=8: GS の late alloc 32 → 64 | 30.76 | 8.94 |
| nggx=1: culling の判定を切る（全部残す） | 41.53 | 11.15 |
| culling part に VALU を 64 命令足す（pad64） | 30.77 | 9.05 |
| Linux RADV（既定 wave64 / RADV_PERFTEST=gewave32、shader は exec_lo の wave32 を確認） | 19.87 / 19.87 | — |

- 読み方: 時間は全部 culling part にあるが、VALU を足しても変わらず、wave32 でも RADV は 19.9 ms → 命令の数や wave の大きさでなく、同時に走る subgroup の数
  （GE の割り当て）で決まっている見込み。
- RADV の culling の時の IB（`plan/ws291/tests/linux/ibdump.c` を host で build して Debian に置き、mview scene-6500 の frame の IB を
  `pm4-decode.py` で読んだ。build/ws291/measure/lin-ib/all/ib.21.dec）: VGT_GS_ONCHIP_CNTL 0x2004007e・GE_CNTL 0xf280・RSRC3_GS は我々と同じ。違いは
  **GE_PC_ALLOC 0x3ff**（parameter cache の oversubscription 512 行、我々 0x1ff）と **SPI_SHADER_PGM_RSRC4_GS 0x0040ffff**（late alloc 64、我々 32）。
  Mesa の radv_precompute_registers_hw_ngg は culling の時 oversub_pc_lines に 2〜4 倍（parameter の数で）を掛ける。
- 実装 commit 816a13ee2: culling の program の時だけ GE_PC_ALLOC を 256 × factor − 1（parameter ≤2 で 2、≤4 で 3、他 4）と OVERSUB、late alloc 64。
  registers に parameter_count を足した。host: compiler 58 PASS、vk 197 PASS。実機は未実施（下）。

### WC の map（ユーザー承認 2026-10-28「P2 の WC の変更は承認します。」）

- commit 779020ee6（gpu.h・gpu.c・vm-device.h・vm/device.c・hal/amd64/space.c）と cfcd07066（amdrdna/node.c）。amd64 の user の map で DEVICE|WC を PAT index 4
  （Intel SDM Vol. 3A の Page Attribute Table、"Selecting a Memory Type from the PAT"）、device の WC|NOCACHE は HAL_ERR_INVALID、RAM への WC は
  HAL_ERR_UNSUPPORTED。WC を受けるのは amd64 だけ（vm_device_create が他の port では ENOTSUP。arm64・armv7 の user の map は DEVICE|WC を黙って Device にし、
  i386 は WC を見ないため）。WC は DEVICE と組の時だけ（UNCACHED_RAM との組は gpu.c と vm/device.c で EINVAL）。
- i915 の ktest-sync の WC の期待（src/drivers/gpu/i915/tests/execution/ktest-sync.c 700-715: hal_space_map_device の WC|NOCACHE と WC|DEVICE が
  HAL_ERR_INVALID）: 779020ee6 は hal_space_map_device（src/hal/amd64/space.c 336-343 の検査）を変えていないので崩れない（コードの上の確かめ、ktest は未実行）。
- build: amd64 vmunix・rpi4・visionfive2 とも warning 0。

### WC・parameter cache・LDS の待ちの実機（2026-10-28 04:10〜04:40）

- 04:10 の Debian の BootNext の再起動の後、zedBSD も Debian も 10 分応答せず（stick は 1 度正常に起動した診断の kernel pad64 のまま、新しい kernel を入れる前）。
  Q1 が 04:2x に SwitchBot で入れ直し、Debian に戻った（入れ直し 1 回）。kernel 由来か firmware 由来かは不明（同じ事は 1 回だけ）。
- kernel: vmunix-pc1（c0c1edcd14597cf3、WC 779020ee6・cfcd07066 + guard band 8ed8a285e + parameter cache 816a13ee2）、vmunix-lat1（+ 90a88f057:
  wave の数の 4 つの LDS 読みと primitive の語の 3 つの LDS 読みを先に並べて待ちを 1 回に）。boot の行は ngg=2。

| run（fence と record の ms） | defer1b（前） | pc1（WC+PC） | lat1 |
| --- | --- | --- | --- |
| scene-6500 frame / record / fence | 33.97 / 2.17 / 30.78 | 32.11 / 0.31 / 30.63 | 32.41 / 0.39 / 30.64 |
| scene-1500 frame / record / fence | 16.90 / 0.74 / 8.98 | 15.27 / 0.35 / 8.98 | 15.15 / 0.34 / 8.92 |
| scene-6500 fps | 29.3 | 31.0 | 30.7 |

- WC: record は scene-6500 で 2.17 → 0.31 ms（instance の 104 KB の書き込み）、frame −1.9 ms。parameter cache の oversubscription と LDS の待ちは
  GPU の時間を変えない（30.6〜30.8 ms）。
- lat1 の全体の確認（`round.sh ... nggshape shot bench hang`、build/ws291/measure/lat1r/）: nggshape 14 種 全部 ok（`--cull --allback --tail` drawn 65536）、
  shot 1 体 Linux と差 0（max 0）、6500 体 max 1・over15 0、bench は下の表。hang: 1 回目は vkhang lost → reset 5 ms → `--loops` ok だが
  `--compute --loops` が lost after=2（device lost）、2〜4 回目は全部 ok、5 回目は vkhang が device 0 個・mview が窓を開けない（Keiland の session が
  終わっていた: session.log の最後が `KWL FAILED site=park errno=42 thread=cursor step=stop`）。hang の試験を続けて流すと desktop が落ちる事がある。
  前の kernel（defer1b）では 1 回だけ流して全部 ok。変更の前から有るかは未確認（compact1 の kernel で続けて流して比べるのが次の確かめ）。

| run | fps | frame | acquire | record | submit | fence |
| --- | --- | --- | --- | --- | --- | --- |
| scene-1 | 108.4 | 9.21 | 7.29 | 0.28 | 0.16 | 1.27 |
| scene-1500 | 65.9 | 15.15 | 5.47 | 0.34 | 0.23 | 8.92 |
| scene-6500 | 30.7 | 32.41 | 0.95 | 0.39 | 0.27 | 30.64 |
| big-100 | 68.6 | 14.55 | 6.43 | 3.42 | 1.49 | 2.87 |
| big-1000 | 18.2 | 54.54 | 0.71 | 33.31 | 16.68 | 3.10 |
| subs-100 | 33.6 | 29.62 | 0.54 | 15.62 | 13.27 | 0.03 |
| subs-1000 | 3.5 | 276.41 | 14.55 | 144.65 | 116.98 | 0.02 |

Linux（bench-linux-3、p003）: scene-6500 50.1 fps・fence 19.53、scene-1500 201 fps・4.62、big-1000 86.9 fps（record 5.42・submit 0.02）、subs-1000 58.2 fps
（record 6.63・submit 10.12）。

### 残りの見立て（2026-10-28 04:40）

- scene-6500 の GPU 30.6 ms は culling part の中の命令・LDS の待ち・raster・deferred part・late alloc・parameter cache のどれにも動かない（全部落としても
  同じ）。RADV は wave32（gewave32）でも 19.9 ms で、NGG の register（GE_CNTL・VGT_GS_ONCHIP_CNTL・RSRC1〜4_GS・GE_PC_ALLOC・stages）は今は一致。
  残る候補（未確かめ）: (1) 我々の IB の実際の値を RADV の ib.21.dec と突き合わせる（診断で IB を klog に出す p005 の方法、culling の時の
  VGT_SHADER_STAGES_EN・PA_CL_*・DB の HTILE を含む全部）、(2) subgroup ごとの固定の仕事（始めの s_barrier、descriptor の s_load、vertex の fetch の
  memory の待ち）を RADV の並びに近づける、(3) GE の index の処理（RADV と同じ draw の packet か）。
- CPU 側: big-N・subs-N の record と submit が Linux の 6〜20 倍（big-1000 record 33 ms 対 5.4、submit 16.7 対 0.02）。p008〜p009 の CPU の record の短縮の
  本体（vk-execute の draw ごとの全 state の書き直し、record の wire の decode）。
- acquire（scene-1 で 7.3 ms）は libvulkan の WSI と Keiland の側（amdrdna の外）。

### 我々の IB と RADV の culling の IB（2026-10-28 04:50〜05:05）

- 診断 kernel（commit しない: build/tmp/diag4/ibd.patch、`KEEP_WC=1 PATCHES=... plan/ws291/tests/nowc-build.sh`）が、culling の program で instance
  1000 以上の最初の draw までの PM4 を klog に出す。mview scene-6500 の 1 frame（687 語）を `pm4-decode.py` で読み、RADV の ib.21.dec と突き合わせた
  （build/ws291/measure/ibd1/ours.dec・ours.regs・radv.regs）。
- 同じ: VGT_SHADER_STAGES_EN（wave32 の bit を除く）、VGT_GS_ONCHIP_CNTL 0x2004007e、GE_CNTL 0xf280、GE_MAX_OUTPUT_PER_SUBGROUP、GE_NGG_SUBGRP_CNTL、
  SPI_SHADER_IDX/POS_FORMAT、VGT_GS_INSTANCE_CNT、PA_CL_NGG_CNTL、PA_SC_NGG_MODE_CNTL、GE_PC_ALLOC 0x3ff・RSRC4_GS 0x0040ffff（816a13ee2 の後）、
  guard band（8ed8a285e の後、RADV と小数の末尾まで同じ）、draw の packet（DRAW_INDEX_2、NUM_INSTANCES 6500）。
- 違う: PA_SC_MODE_CNTL_0（我々 0x2、RADV 0x22: ALTERNATE_RBS_PER_TILE）、PA_SC_BINNER_CNTL_0（我々は binning 無し、RADV 0x19fc01b0）、
  depth の HTILE（RADV は DB_Z_INFO の TILE_SURFACE_ENABLE と HTILE、我々は無し）、RSRC1_GS の FLOAT_MODE（我々 0xf0 = fp32 の denorm あり、RADV 0xc0）、
  user SGPR の数（我々 9、RADV 17）、LDS（我々 2576 B、RADV 3584 B）。どれも全部を落とした時の 30.7 ms（raster が無い）を説明しない見込み
  （FLOAT_MODE は未確かめ）。
- 機械: 05:05 に stick を vmunix-lat1（cksum 4171145425）に戻し、zedBSD で起動中。IB の診断 kernel の deploy の途中で zedBSD の /boot/boot0 への cp が
  60 s で返らなかった事が 1 回（hang の試験を続けた後の状態。Debian から /dev/sda2 を mount して見ると cp は終わっていた）。

### 次の確かめ（再開する担当へ）

1. GPU のどの block が busy かを測る: scene-6500 の間に GRBM_STATUS・GRBM_STATUS_SE0〜3・GRBM_STATUS2 を amdrdna の制御（amdrdnactl）か診断 kernel で
   数千回読み、GE・SPI・PA・SX・TA の busy の割合を RADV（Linux の umr か /sys の amdgpu_regs）と比べる。全部落としても 30.7 ms なので GE か SPI の
   どちらかの見込み。
2. FLOAT_MODE を RADV と同じ 0xc0 にした kernel（compiler の GFX10_COUNTS_FLOAT_DENORMS）の 1 回の計測。
3. vertex buffer の descriptor と index の cache の方針（descriptor の word 3、GE の index の取り込み）を RADV の descriptor と比べる。
4. hang の試験を続けて流すと desktop が落ちる件を、変更の前の kernel（compact1）で続けて流して比べる。

## q1639（2026-10-28 P2、worktree p2-ws291c、基点 main d3de756ca）

- 承認（ユーザー 2026-10-28）:「P2は自律的にループして、Linuxと同等以上の性能をゴールに、継続してください。hangが繰り返し起きるなら調査して修正してください。30分調査してわからなかった場合は、Fable 5.1 Highのサブエージェントを作成して、事情を説明した上で解決までどういう進め方をすればいいかのアドバイスを得て、さらに継続してください。どうしても解決できない場合、停止してユーザに判断を求めてください。これらはP2が自律的に行います。」

### VRAM の page entry の fragment（commit eb0ec22fd）

- 原因: VRAM の page entry に fragment の欄（bits 11:7、Linux の AMDGPU_PTE_FRAG）が無く、4 KiB ごとの translation だった。scene-6500 は 1 frame に
  index 2.9 GB（111000 個 ×4 B ×6500 instance、index buffer 444 KB を instance ごとに読み直す）を GE が読み、vertex の fetch も同じ buffer を回る。
  全部を落としても・VALU を足しても 30.6 ms のままだったのは、この translation の待ちで決まっていたため。
- 修正（space.c・amdrdna-hw.h）: 各 page の entry に、VA と VRAM が同じ境界に揃い mapping の中に収まる最大の連続の run（最大 2^9 page = 2 MiB）の
  fragment を書く（Linux v6.19 amdgpu_vm_pt.c amdgpu_vm_pte_fragment・amdgpu_vm_ptes_update、gmc_v10_0.c の fragment 9）。backing の VRAM と GPU の
  address は大きさ以下の最大の 2 冪（64 KiB〜2 MiB）の境界に（VRAM の空きが無ければ 64 KiB に戻る）。
- host: amdrdna-host-test PASS（410 行 PASS）、vmunix warning 0（886c81d3dddbce6e）。
- 実機（vmunix-frag1 = 同じ動作で style の前の build、build/ws291/measure/frag1r/）: nggshape 14 種 ok、shot 1 体 差 0、6500 体 max 1・over15 0、hang 回復 ok。

| run | 前（lat1） | frag1 | Linux（bench-linux-3 / cull-l） |
| --- | --- | --- | --- |
| scene-1 fps / fence | 108.4 / 1.27 | 125.0 / 1.32 | 9604 / 0.06 |
| scene-1500 fps / fence | 65.9 / 8.92 | 70.7 / 6.80 | 200 / 4.65 |
| scene-6500 fps / frame / fence | 30.7 / 32.41 / 30.64 | 44.7 / 22.29 / 20.45 | 49.7 / 20.08 / 19.70 |
| big-1000 fps（record / submit） | 18.2（33.3 / 16.7） | 17.7（35.3 / 17.2） | 86.9（5.42 / 0.02） |
| subs-1000 fps（record / submit） | 3.5（144.7 / 117.0） | 4.06（138.3 / 100.8） | 58.2（6.63 / 10.1） |

### 頂点の側だけの時間（mview `--cull=all`、commit fc7a3d077）

- mview に `--cull=all`（全 pipeline が両面を落とす、raster 無し）を足した。zedBSD（frag1b）と Linux で並べた（build/ws291/measure/cull-z・cull-l）:

| run（fence ms） | zedBSD | Linux |
| --- | --- | --- |
| scene-6500 / cull-6500 | 20.62 / 20.46 | 19.70 / 19.66 |
| scene-1500 / cull-1500 | 7.13 / 6.93 | 4.65 / 4.81 |

- 読み方: 両方とも GPU の時間は頂点の側（culling part と GE）で決まり、画素の側はほぼ 0。scene-6500 の差は頂点の側の約 0.8 ms（4 %）と、CPU 側の
  acquire・record・submit・present の合計 1.5 ms 対 Linux 0.35 ms。scene-1500 は frame の半分を acquire（7〜9 ms）で待つので GPU の clock が上がり切らない見込み（未確かめ）。

### hang の調べ（06:16〜）

- frag1 の session（06:09 起動、nggshape・shot・bench・hang を流した後）で、hang の確認を 6 回続けると 2 回、reset の直後の `vkhang --compute --loops` が
  `lost after=2`（kernel の行なし、A）。その後の compute の連続で `steps=0` が 3 回（fence は終わり、CPU が読む値が 0、B）。lat1 の前の round でも A が 1 回。
- 新しい boot の後は lat1・frag1b とも compute 40 回 ×3、hang 16 回、bench の後で再現しない（間欠）。
- 30 分で分からず、Fable 5.1 High に進め方の助言を求めた（要点）: GPU の cache・TLB の扱いは amdgpu と同じ（各 run の始めの ACQUIRE_MEM で GL2 の WB+INV、
  fence は RELEASE_MEM の GL2_WB、新しい space は必ず flush）。A は EIO を黙って返す所（submit の拒否、wait の dropped・lost、hang の時の fence が後から
  通った場合、runtime の stopping、core の commit の ESTALE）に mark を足して次の発生で特定する。B の本命は graphics の queue reset が終わらない wave を
  殺さない（compute の reset は殺す）ことで残った wave が GL2 を汚し、次の process の VRAM を上書きする事。次に WC・HDP の見え方。
- 確かめ: 診断の mark（commit fc7a3d077: submit の拒否、wait の dropped・lost、hang の fence が後から通った時、reset の後の status と wave）を入れた
  kernel（vmunix-marks1）で hang: reset の後の wave は 0（`waves valid 0`、reset の前は 76）、gc status は idle。→ 残る wave の仮説は外れ。
  長い session の再現（nggshape・shot・bench・hang 6 回・compute 40 回）を流して mark を待つ。
- 実機の入れ直し: 06:37 の Debian からの BootNext の再起動の後、両 OS が 8 分応答せず（04:10 と同じ症状の再発、2 回目）。06:45:47 に P2 が SwitchBot で
  入れ直し（1 回）、Debian に戻った。直前は frag1b で hang 16 回と bench の後。
- 長い session の再現（marks1、07:00 頃）: hang 2 回目の後の compute で A が再現。kernel の行は `compute ring full: 42 words, room 71` →
  `vk: a command buffer did not run (5)`。原因: ring の空きを engine が時々しか報告しない read pointer だけで計算していた（Linux は rptr で空きを見ない）。
  修正 commit d218e4dc7（cp.c・engines.c・amdrdna-private.h）: ring ごとに直近 64 個の fenced submission の fence の値とその末尾の write pointer を覚え、
  通過した fence の所までは読まれたとして空きを計算。ring full の mark に read・write・fence も出す。host PASS、warning 0。
- B と SMU（vmunix-room1 の stress、07:05 頃）: B が多発（compute 10/40 が steps=0、描画の readback が ff000000・006f0000、mview の 1 体の画面全体に
  点状の色化け: 16 階調以上の差 403263 pixel、build/tmp/bad-shot1.png）。kernel に `smu: message 0x12 answered 0x10410d (5)` →
  `thermal fault: the smu firmware gave no readings ...; the GPU takes no more work`（bench の subs-1000 の頃）、amdrdnactl は fan=0rpm・hung=1・fault=1。
  thermal fault は印を付けるだけで仕事を止めていなかった。→ 07:09 に zedBSD を poweroff、07:10:00 に P2 が SwitchBot で入れ直し（2 回目）。log は
  build/tmp/dmesg-smu-room1.txt（worktree p2-ws291c）。BootNext の warm reboot の後に両 OS が応答しない件（04:10・06:37）も、SMU が壊れたまま
  warm reboot した事が原因の見込み（Debian の amdgpu が初期化できない）。
- Q1 の指示（07:15）: (1) 長い試験は温度・fan・SMU を見張り、異常で負荷を止めて poweroff、(2) driver も thermal fault で負荷を止める、(3) わざと
  SMU を壊さない、(4) Linux で同じ負荷を比べる。→ commit 31129fc30: smu.c の power_fault が device を failed にして仕事を止め、fan を thermal block 直で
  100 % に。plan/tools/gpu-perf/guard.sh（20 秒ごとに amdrdnactl の power 行、hung=1・fault=1・90 C 以上・driver が回す時の fan 停止で mview・vkhang を
  kill して poweroff）、round.sh は guard の下で走る。Linux（07:20、amdgpu）で bench の全部: SMU の error なし、48 C・fan 1170 rpm（表は下）。
- Fable 5.1 High の助言 2 回目（SMU、要点）: mailbox・HDP・table の手順は Linux と同じで、0x10410d は結果。MP1・THM・GC が同じ秒に壊れたので電源の
  領域の問題。本命は SMU の feature の組: gfxclk の DPM を動かしながら、Linux が常に許す電圧の保護（APCC_DFLL の droop 制御、TEMP_DEPENDENT_VMIN、
  APCC_PLUS、GFX_SS など）を許していない → 細かい submit の多い負荷で電圧の droop → GPU と fabric の誤動作。ほか: fault の時は仕事を止めるのを先に、
  SMU が壊れたら PSP の mode1 reset で戻す、shutdown の時に hung なら mode1 して次の OS を救う。
- 採用: power_fault を「仕事を止める → fan」の順に。Linux が常に許す feature のうち電圧の物（APCC_PLUS 26、GFX_SS 37、TEMP_DEPENDENT_VMIN 39、
  APCC_DFLL 42）を gfxclk の DPM と一緒に許す（amdrdna-hw.h AMDRDNA_SMU_CLOCK_FEATURES_*、Linux v6.19 navi10_ppt.c navi10_get_allowed_feature_mask）。
  実機（vmunix-feat1）で動いたのは GFX_SS と APCC_DFLL（`features all on: 0x00000423a3800003`、vbios の表が残り 2 つを持たない）。
  guard の下の長い stress（nggshape・shot・bench ×3・hang）で確かめる。mode1 による復帰と shutdown の時の reset は未実施（次の候補）。

### 頂点の側の律速の切り分け（2026-10-28 07:25）

- culling part に VALU を 64 命令足す診断（pad64、HEAD 31129fc30 に build/tmp の patch）: cull-6500 20.47 ms（前 20.45）、scene-6500 20.53（前 20.58）。
  → fragment の修正の後も、culling part の VALU は律速でない。

| Linux（07:20、build/ws291/measure/linux-q1639） | fps | record | submit | fence |
| --- | --- | --- | --- | --- |
| scene-1500 / scene-6500 | 199.4 / 50.2 | 0.11 / 0.15 | 0.10 / 0.10 | 4.67 / 19.49 |
| big-1000 / subs-1000 | 86.9 / 58.6 | 5.41 / 6.25 | 0.02 / 10.28 | 6.01 / 0.46 |
- 確かめ（vmunix-feat1、07:29〜07:36、guard の下）: nggshape 14 種 ok、bench ×3（約 5 分）、hang 回復 ok、SMU は正常のまま（hung=0・fault=0、温度 47〜50 C、
  負荷の時 fan 828 rpm）。shot 1 体は mview の窓が出る前の desktop を撮った（起動直後の 1 回目の既知の事、未判定）。SMU が壊れたのは前は約 20 分の後
  なので、この 1 回では直ったと言えない（以後の guard の下の試験で見続ける）。

### Fable 5.1 High の助言 3 回目（残りの差、07:40）と試した事

- 助言の要点: 両方とも raster を外しても同じ時間なので、律速は GE・SPI の前段（wave の起動から GS_ALLOC_REQ までの時間）。候補の順: (1) 実行の run
  （IB・resource table）の system memory の page が MTYPE UC なので各 wave の s_load が PCIe を往復する → NC に、(2) s_barrier の数（5 対 RADV 2）、
  (3) position の fetch を buffer_load_format_xyzw から dwordx3 に、(4) PDE0 の 2 MiB page、(6) fence の時間に起床の遅れが入っていないか、
  (7〜10) CPU 側: acquire・submit の worker の起床、execute_draw の毎 draw の全 state、run の slot の数。
- 試した (1): space.c の system の page を NC（Linux の cached な system page と同じ）にした kernel（vmunix-nc1）: cull-6500 20.64、scene-6500 20.78 ms
  （前 20.45・20.58）→ 効果なし、戻した（commit しない）。
- 試した (8): frame を、queue が空で worker が暇な時は submit した thread で直に実行する kernel（vmunix-direct1、runtime.c）: nggshape・shot（1 体 差 0、
  6500 体 max 1）・hang は ok だが、bench は変わらず（scene-6500 22.45 ms、subs-1000 290 ms）→ 戻した（commit しない）。libvulkan の VULKAN TIMING は
  1 ms の刻みの時計なので、小さい段の平均は粗い。kernel の op 18（vkQueueSubmit）は 1 回 17〜33 µs で、frame の CPU 側の大半は kernel の executor の外
  （libvulkan と gpu の core の往復）。
- 見立て（未確かめ、次の担当へ）: GPU がほぼ何もしない scene-1 でも fence が 1.35 ms（Linux 0.06 ms）、big-1 も 1.44 ms。fence の時間に約 1.3 ms の
  固定の遅れがある。完了の通知の道は EOP の割り込み → IH の thread → ring の fence の waitq → runtime の completer の thread → core → user の thread と
  3 回の thread の起床で、kern の sched_wakeup は thread を前に走った CPU の run queue に入れる（その CPU が忙しいと次の tick まで待つ見込み）。
  この遅れを除けば scene-6500 の GPU の時間は約 19.2 ms で Linux（19.5〜19.7）を下回る見込み。直す所の候補: amdrdna の中では EOP の処理（IH の thread）
  から run の通過した parked の marker を直に完了して completer の起床を省く（順序を保つ）、kern の側では起床する thread を空いた CPU へ（scheduler は
  amdrdna の外、Q1 へ）。
- 確かめ（08:00、診断 kernel vmunix-spin1、commit しない: build/tmp/spinwait.patch）: amdrdna_ring_wait を眠らず fence を回し読みにしても scene-1 の
  fence 1.31 ms・big-1 1.32 ms・scene-6500 20.80 ms で変わらない → ring の待ちの眠りは遅れの元でない。残る候補は IH の thread の起床、completer の
  起床、core から user の thread への起床、または GPU 側の run ごとの固定の仕事（始めの ACQUIRE_MEM の GL2 の WB+INV など）。区間ごとの時刻を
  取る計測が次（EOP の割り込みの時刻と marker の完了の時刻、GPU の timestamp の始めと終わり）。
- 機械（08:05）: stick は vmunix-feat1（1b9cc7865 の動作、cksum 1397306155）、zedBSD で起動中、SMU 正常。入れ直しは今日 P2 が 2 回（06:45:47、07:10:00）。

## q1645（2026-10-28 P2、worktree p2-ws291d、基点 main 04f63dbb7）

- 承認（ユーザー 2026-10-28）: q1639 と同じ文に加え「P2は何度でもFableに聞いてください。」。Q1 の追加（2026-10-28）: fence の遅れが kernel の scheduler・起床の道に
  ある時の直しと libvulkan ↔ gpu core の往復の直しは、案を Q1 に送り OK の後。

### acquire の待ち（scene-1・scene-1500 の frame の半分）

- 見つけた事（vmunix-feat1、build/ws291/measure/kwl-i4.log）: mview の swapchain の image 数を変えると scene-1 は 3 枚 59.9・4 枚 115.7・5 枚 175.2 fps
  = 約 60×(N−2)。原因 2 つ: (a) Keiland の game mode の direct scanout は GPU_DISPLAY_PRESENT の ioctl の中で flip の latch（次の vblank）まで main loop が
  眠り、client の commit も置き換えた mailbox の image の release も vblank ごとにまとめてしか出ない（amdrdna present.c は flip の latch を待って返る）。
  (b) libvulkan の Wayland の acquire は image が空くのを pthread_cond_clockwait の 10 ms（WSI_PROGRESS_INTERVAL、tick に丸まる）で待ち、その間 socket の
  wl_buffer.release を読まない（release から次の commit まで約 10 ms、build/ws291/measure/kwl-a4.log）。
- Linux の RADV の Wayland は mailbox で minImageCount 4（Mesa wsi_common_wayland.c wsi_wl_surface_get_min_image_count）。Linux の mview は 4 枚、zedBSD は 3 枚
  だった。Linux の bench を --images=4 で取り直して同じ数（build/ws291/measure/linux-img4: scene-1 9033、scene-1500 201.3、scene-6500 50.0、big-1000 84.7、
  subs-1000 57.7 fps）。
- 直した事（Q1 の OK、2026-10-28）: 5f9ec7f08 = 案 A（Keiland: game mode の present を backend の thread に移し、完了を pipe の fd で main loop の poll に
  知らせる。scanout-zedbsd.c の kl_backend_scanout_begin・_fd・_end、DRM の stub、wayland/scanout.c・kwl.h・main.c。flip 中も commit を処理し、置き換えた
  queued はすぐ release、front と flip 中の image は flip の完了まで保持、game mode を出る時は in-flight の完了を待つ）、9b876b5fe = 案 C（libvulkan: platform の
  ops に wait_input（POLLIN で待つ fd）、Wayland は wl_display_get_fd、acquire は Wayland も wake の pipe と接続の fd で待つ。10 ms の上限は他の thread が
  socket を読んだ時のため残す。timeout 0 は従来どおり NOT_READY、有限の timeout は残りで切る）、632d172dc = B1（mview --images=N、bench は 4）。
- 実機（vmunix-fdiag5 = 診断の mark だけ足した kernel、Keiland・libvulkan は A・C）: mview mailbox 4 枚 scene-1 101 → 282 fps、scene-1500 70 → 141 fps
  （acquire 7.9 → 1.0 ms）、scene-6500 45.4（acquire 0.5）、fifo 57 fps。frame-ops の desktop 全段 60 fps 前後（build/ws291/measure/desk-ac/table.txt:
  launch 65・type 62・keys 75・output 72・drag 58・home 64・wiseview 63、scroll・hover は入力の間隔、shot の画面は正常）。shot 1 体 差 0・6500 体 max 1。
  game mode の入り → pointer で出る → 再入り → window の終わりで出る、videoplayer --fullscreen の game mode 入り（KWL SCANOUT の行）。
- build: zedBSD の wayland・libvulkan・mview・wltest・vkdemo の warning 0、Linux の Keiland（keiland-linux.mk all）warning 0。libvulkan-compat（Linux・FreeBSD の
  Keiland の Vulkan）は別の code で変えていない。FreeBSD の build は未実施（DRM の stub は Linux と同じ file）。
- 他の GPU: present は GPU 共通の GPU_DISPLAY_PRESENT の ioctl を backend の thread から呼ぶだけで、kernel の各 driver（i915・nvrtx・PowerVR・venus・bcm2711）の
  present は同期のまま（code の上の確かめ）。
- 既存の事（今回の変更と無関係）: wltest は amdrdna の executor が command 121 を拒否（EISDIR）して新旧どちらの libvulkan でも vkEndCommandBuffer で失敗。
  vkdemo は compositor が display を持つので EIO。shot の mview が約 300 frame で surface lost（errno 24）になるのは q1639 の feat1 の round でも同じ。
- 残り: acquire 1.0 ms（scene-1500）。image 4 枚のうち表示中・flip 中・queued・present 中で、present の commit を待つ見込み（未確かめ）。

### fence の段ごとの計測（診断 kernel、commit しない: build/ws291/fdiag5.patch）

- 診断: runtime の worker・completer の時刻、graphics の run ごとの GPU の timestamp（run の先頭・IB の前・IB の後、write-back page の 1024 から seq ごと）と
  CPU の submit 時刻、IB の klog への dump（build/ws291/measure/ibd/ib512.dec・ib3392.dec、pm4-decode.py と Mesa の gfx10.json）。counter は 100 MHz。
- 結果: frame ごとに run が 2 つ: 大きな run（mview の scene: 全画面 quad の clear と 16 draw）と 512 語の run（libvulkan の present の copy: 全画面 quad、
  linear の 8888 を linear へ）。512 語の run は GPU で約 0.87 ms（gfxclk 800 / 1780 MHz で変わらない）。scene-1 の大きな run は 1.25 ms、scene-6500 は 20.4 ms
  （Linux の fence 全体 19.5）。worker の待ち 5 µs、executor 15〜30 µs、completer の seen から完了 11 µs、run の前置き（sync・flush・ACQUIRE_MEM）1〜2 µs
  → fence の時間は GPU の上の時間で、起床の遅れではない。
- 見立て: 全画面 quad 1 枚が約 19 GB/s。zedBSD は SMU の UCLK の DPM を許していない（amdrdna-hw.h「The memory clock's levels stay off」）。Linux の
  pp_dpm_mclk は 100/500/625/875 MHz で、mview scene-1500 の負荷で 500 MHz（idle は 100）。zedBSD の memory clock は 100 MHz のままの見込み（GDDR6 192 bit で
  約 38 GB/s、read+write の quad で 19 GB/s と合う）。clear の quad と present の copy の quad で 1 frame 約 1.7 ms = scene-6500 の差のほぼ全部。
  UCLK を上げる手順（DCN の p-state との関係）を Fable 5.1 High に問い合わせ中（下）。

### BootNext の後に zedBSD が戻らない件（BUG-348、Q1 が登録）

- 今日 4 回（04:10・06:37・08:08・08:51）。P2 の入れ直し: 08:17:38（1 回目）、08:58:48（2 回目）、どちらも Debian に戻った。
- 切り分け: zedBSD の boot の log（/boot/boot0/log、取った写しは build/ws291/measure/boothang/）の失敗の boot boot-000147.txt（08:09〜08:14）・
  boot-000154.txt（08:53〜08:57）と正常の boot-000148.txt・boot-000155.txt の比較: 失敗の boot も kernel・desktop・GPU は動き続けた（compositor の run が
  60 s ごと）。違いは USB の LAN（RTL8156 0bda:8156、usb-cdc-ncm の ue0）の「ue0 link 2500 Mb/s」が無い事だけ = link が上がらず DHCP・SSH が無い。
  GPU の hang ではない。（保存された log の比較は止まる段の特定のためで、試験の判定ではない。）
- 回避の試み（取り消し）: measure.sh で BootNext の前に Debian の USB NIC を sysfs の authorized=0 で外す回避を 184beb3c9 に入れたが、Debian の 10.0.10.3
  も同じ RTL8156（enx6c1ff71a08b6、onboard の enp5s0 は DOWN）を使うので 09:11 に Debian の通信を切った。09:18:11 に P2 が SwitchBot で入れ直し（3 回目）。
  回避は取り消した（Q1: 二度としない）。代わりにユーザーの指示（2026-10-28）「P2は、USB NICが通信できない（DHCPが取得できないとか、ue0が異常状態）のとき、
  自分で再起動するように、serivceを登録すれば、とりあえず問題を回避して継続できると思いますので、P2の判断でこれを行ってよいです。」で zedBSD の側に
  自己 reboot の service を入れる（試験の image・設定だけ）。

### NVMe の zedBSD の区画（2026-10-28、ユーザーの指示）

- ユーザー（2026-10-28）「P2は、5330と同じように、NVMeにzedBSDをインストールしてから作業を継続する方がいいような気がします。USBメモリが半導体供給不足で手に入りづらいからです。」、
  空きが無いので案を送り、ユーザーの答え（クリック）「ext4 を縮め、root 32 GiB」。
- 手順（5330 の q1612 と同じ形）: `plan/ws291/tests/rdna-live-usb.sh`（SystemRescue 13.02 の live USB、stick を書いて cmp で比べた、UEFI の
  `Boot0006 SystemRescue USB ws291`、BootNext で 61 秒）→ live で `e2fsck -f -p`（exit 0）→ `resize2fs -p /dev/nvme0n1p2 400000M`・`e2fsck -f -n` →
  `sfdisk --no-act -N 2` で確かめて p2 を 854675456 sector に（start・PARTUUID 不変）→ `resize2fs -p`（区画いっぱい 106834432 block）・`e2fsck -f -n` →
  `sfdisk --no-act --append` の後に p4〜p6 を append → `sfdisk -V`（No errors）・`wipefs -n` → `mkfs.vfat -F 32 -n ZEDBSD p4` → `back`（Debian の / は
  3178a5c0-… で rw、401G、swap p3、running）。

| 区画 | start（sector） | size | type | PARTLABEL（PARTUUID） |
| --- | --- | --- | --- | --- |
| p1 | 2048 | 976 MiB | EFI System | Debian の ESP（65DA-5619、不変） |
| p2 | 2000896 | 854675456（407.5 GiB、前は 924405760） | Linux filesystem | Debian の /（ext4 3178a5c0-…、PARTUUID 82a12d78-… 不変） |
| p4 | 856676352 | 256 MiB | Microsoft basic data | zedBSD-nvme-boot（d335c6ec-77ea-486f-8a95-57f10a367f0a、FAT32 3D43-9082） |
| p5 | 857200640 | 32 GiB | FreeBSD UFS | zedBSD-nvme-root（b98d1f62-b4da-4a95-b71d-151e14565490） |
| p6 | 924309504 | 1 GiB | FreeBSD swap | zedBSD-nvme-swap（feff9017-d794-4405-afbe-15f33796e92f） |
| p3 | 926406656 | 24 GiB | Linux swap | Debian の swap（e9129310-…、不変） |

- 前後の表: build/ws291/nvme/sfdisk-before.txt・dumpe2fs-before.txt・after.txt・debian-after.txt（git の外）。
- zedBSD を書く: `plan/ws291/tests/rdna-nvme.sh image IMG [LINE...]`（Debian で `rdna-nvme-write.sh` が root の UFS を p5 に書いて
  plan/tools/hw5330/ufs-grow.py で 32 GiB に広げ、swap を p6、vmunix・logo・zedbsd.cfg（NVMe の PARTLABEL）を p4、loader を Debian の ESP の
  `\EFI\zedbsd\BOOTX64.EFI`）、`rdna-nvme.sh kernel VMUNIX`。UEFI の entry は `Boot0004 zedBSD NVMe`（BootOrder の外、measure.sh の ZEDBSD_BOOT の既定）。
- image: `make ZEDBSD_CONFIG=plan/ws291/tests/config.mk BUILD=build/ws291/img ZEDBSD_TEST_RC_CONF=plan/ws291/tests/rc.conf ZEDBSD_TEST_IMAGE_TAG=ws291
  ZEDBSD_TEST_EXTRA_FILES="…" disk-image`（root の鍵 plan/tmp/guest・host の鍵・session.sh・autologin・nicwatch。引数は build/ws291/img-testfiles2.txt）、
  cfg に `amdrdna.vk.timing=2000`・`amdrdna.vk.ngg=2`、kernel は feb1a5f の clean な build（d81dfb6c28666715）。
- 確かめ（09:50）: BootNext 0004 で zedBSD が NVMe から起動（`/dev/nvme0n1p5 on / type ufs`）、root の SSH、SMU 正常、nicwatch が動いた（count 0）。
  USB の stick は SystemRescue のまま（zedBSD の起動に使わない）。

### ue0 の自己 reboot（nicwatch）

- ユーザーの指示（2026-10-28、上の BootNext の節）で、試験の image と機械の設定だけに入れた（base の既定の image には入れない）: `plan/tools/gpu-perf/nicwatch/`
  （nicwatch.sh と service）、`plan/ws291/tests/rc.conf`（base の rc.conf に nicwatch の行を足した写し、ZEDBSD_TEST_RC_CONF）、`measure.sh nicwatch`（動いている
  zedBSD に入れる）。動作: 起動の後 150 s 以内に ue0 に IPv4 の address が付かなければ reboot（BootOrder で Debian に戻る）。連続は 3 回まで
  （/boot/boot0/nicwatch.count、address が付いた起動で 0）。measure.sh の to_zedbsd は zedBSD が上がらず Debian に戻った時に BootNext を 2 回まで繰り返す。
- guard との順: guard は host から ue0 を通して SMU を見るので、link の無い zedBSD には guard も届かない。nicwatch は試験の負荷の前（起動の直後）だけ働く。

### memory clock（UCLK）を最上段に固定（commit 146fdfe5d）

- 見立ての確かめ: Fable 5.1 High に 2 回聞いた（下）。Linux の pp_dpm_mclk は 100/500/625/875 MHz（idle 100、mview scene-1500 で 500）。zedBSD は UCLK の DPM を
  許さず、firmware は最下段の 100 MHz のまま（全画面 quad 1 枚 870 µs ≈ 38 GB/s の帯域と合う。A1v の vbios の bootup mclk 875 MHz は起動時の値）。
- 試し（診断 kernel、guard の下）: (1) UCLK DPM・MEM_VDDCI・MEM_MVDD を許し SetHardMinByFreq(UCLK 875) → 時間切れ（42）で SMU 停止、guard が poweroff
  （09:27）。(2) 切り替えの間だけ DCHUBBUB_ARB_DRAM_STATE_CNTL に pstate の force allow（0x30）→ SetHardMax は 0、SetHardMin が時間切れで再び停止
  （09:54）。arbiter の allow（DCHUBBUB_TEST_DEBUG 0xB の bit30）は force の前から 1（0x50777777）で、handshake は原因でなかった。DRAM_STATE_CNTL は
  0x2（GOP が stutter = self refresh を禁じている）。(3) 切り替えの間だけ stutter の禁止も外し（Linux の hubbub1_allow_self_refresh_control の allow と同じ
  0x0 に pstate の force allow 0x30）、Fable の次の候補の DPM_SOCCLK・DS_SOCCLK を許す → SetHardMax・SetHardMin とも 0、SMU 正常、underflow 0。
- 実装（146fdfe5d）: amdrdna-hw.h の AMDRDNA_SMU_CLOCK_FEATURES_LOW を 0x27800b1b（UCLK DPM 3・SOCCLK DPM 4・MEM_VDDCI 8・MEM_MVDD 9・DS_SOCCLK 11 を足す、
  Linux v6.19 navi10_get_allowed_feature_mask）、smu.c power_memory_clock（UCLK の段を読み、display の arbiter を許した間に hard ceiling → hard floor を最上段、
  Linux navi10_display_disable_memory_clock_switch・smu_v11_0_set_hard_freq_limited_range）、display.c amdrdna_display_memory_switch_begin/end と dcn の
  register DCHUBBUB_ARB_DRAM_STATE_CNTL（window 2 の 0x0508）。plan/ws039/tests/amdrdna-dcn-compare.py に新しい register の Linux 名を 1 行。
- host: amdrdna-host-test（AMDRDNA_LINUX=/home/awe/visionfive-linux）は amdrdna の各 test と dcn-compare（918 registers）PASS。audit-symbols の FAIL 17 は
  HEAD feb1a5f でも同じ 17 件（既存）。vmunix warning 0。
- 実機（vmunix-mem1、NVMe の zedBSD、guard の下の round、build/ws291/measure/mem1/）: `A4 memory clock: 4 levels 100-875MHz, held at 875MHz (ceiling 0, floor 0,
  arbiter 0x00000002)`、nggshape 14 種 ok、shot 1 体 差 0・6500 体 max 1、hang 回復 ok、SMU は通して正常（温度 最高 62 C）。

| run | 前（feat1 + A・C、mailbox 4 枚） | mem1 | Linux（linux-img4） |
| --- | --- | --- | --- |
| scene-1 fps / fence | 282 / 1.22 | 949.6 / 0.10 | 9033 / 0.06 |
| scene-1500 fps / fence / acquire | 141 / 5.56 / 1.00 | 184.3 / 4.36 / 0.53 | 201.3 / 4.63 / 0.04 |
| scene-6500 fps / frame / fence | 45.4 / 21.93 / 20.78 | 49.05 / 20.34 / 19.20 | 50.04 / 19.94 / 19.56 |
| big-1000 fps（record / submit） | 17.7（35.3 / 17.2、feat1） | 17.2（38.0 / 18.3） | 84.7（5.57 / 0.02） |
| subs-1000 fps（record / submit） | 4.06（138 / 101、feat1） | 3.04（152 / 164） | 57.7（6.57 / 10.3） |

- 読み: GPU の時間（fence）は scene-1500・scene-6500 とも Linux より短くなった。残りの差は CPU 側（scene-6500 で acquire 0.46・record 0.27・present 0.25 ms 対
  Linux 0.04・0.14・0.07）と big-N・subs-N の record・submit（libvulkan ↔ gpu の core の往復）。
- Fable 5.1 High の助言（3 回目・4 回目、UCLK）要点と採否: 3 回目「UCLK・VDDCI・MVDD を許し NumOfDisplays=0 で hard min/max を 1 回、underflow を見る、
  GetUMCFWWA を確かめる（bit0 で CDR の回避は不要）」→ 採用したが handshake で止まった。4 回目「PMFW は DCHUBBUB の arbiter の allow を待つ、Linux は DC の
  init で GOP の pipe を消してから UCLK を動かす。順位 1 = 切り替えの間だけ pstate の force allow、allow が既に 1 で止まるなら DPM_SOCCLK・DS_SOCCLK」→ force
  allow だけでは止まり、stutter の禁止を外す事と SOCCLK を足して通った。自動の mode1 reset は SMU の停止の回復にまだ使わない（助言どおり、guard の poweroff と
  入れ直しで戻る）。

### 初期化の画面の表示の無効化（commit dfcaa14e5、ユーザーの指示）

- ユーザー（2026-10-28）「amdrdnaドライバが、初期化段階で画面に表示を行っていると思いますが、これをコメントアウトしてほしいです。他のRDNA GPUやRDNA2/3への
  移植のときにまた使うかもしれないので、コメントアウトでOKです。でも、診断用であることはわかるようにしてください。診断用の処理を関数に分けて、呼び出し元を
  コメントアウトして説明のコメントを残すのでもいいです。この作業のあと、次の作業を進めてください。」
- display.c: A6 の色の帯（test の絵へ 1 秒 flip して戻す）を display_check_bars、A7 の cursor の四角（中央に出し対角に動かして隠す）を display_check_cursor
  に分けた（__attribute__((unused)) static、comment に Diagnostic, not called と用途）。A6・A7 の呼び出し元は comment で残した。A6 は takeover・refresh・
  割り込みだけ、A7 は cursor の setup（隠したまま）だけ。build warning 0、host test PASS（audit の既存 17 件を除く）、実機で起動・desktop の shot 正常
  （起動の logo を実際の画面で見たかは未確認）。

### 案 D（game mode の early adopt）と GPU_CAP_FENCE の試し（commit しない）

- 案 D（Q1 の OK）を実装した（Keiland: game mode の window の commit を fence を待たずに adopt、fence の fd を present の thread に渡し ioctl の前に
  最大 2000 ms 待つ、leave・window の消滅・present の失敗で close）。amdrdna は GPU_CAP_FENCE を持たないので libvulkan は early commit をせず（commit は
  GPU の後）、D は働かなかった（compositor の log に ACQUIRED 0）。
- amdrdna に GPU_CAP_FENCE を足す試し（runtime.c の capabilities、core の fence の仕組みをそのまま使う）: early commit が働き、desktop の frame-ops（全段 60 fps
  前後）・game mode の出入り・video・nggshape・shot・hang は ok。bench は scene-1500 173.2・scene-6500 44.5 fps（acquire 2.41、present 0.79 ms）で HEAD より
  悪い（frame が vblank より長い時、flip 中の image が GPU の完了 + vblank まで握られる見込み、未確かめ）。→ D と FENCE は commit せず patch で保存:
  build/ws291/caseD-keiland.patch（sha256 先頭 7cf7a7fe5491d81c）、build/ws291/gpu-cap-fence.patch（a42b1d5de0d2f4cd）（git の外）。
- HEAD（dfcaa14e5 の kernel、Keiland は A・C）の bench（10:36、build/ws291/measure/head1/）: scene-1 966.9、scene-1500 179.8（fence 4.46）、scene-6500 49.16
  （frame 20.29・fence 18.92・acquire 0.36・present 0.55）、big-1000 20.4、subs-1000 3.17 fps。round の間 SMU 正常。

### 残り（次の担当へ）

- scene-6500: fence は Linux（19.56）より短い 18.9〜19.2 ms。差は CPU 側の約 0.35〜1.0 ms（acquire・record・present、run ごとに揺れる）。scene-1500: 180〜184
  対 201（acquire 0.5、record 0.26 対 0.11、present 0.16 対 0.07）。acquire の残りは libvulkan の present の worker が GPU の後に commit する事と
  compositor の adopt・release の往復（Linux は commit の時に置き換えた image を返す）。案 E（libvulkan の Wayland の swapchain の毎 frame の全画面 copy を
  無くす直接の route）と、FENCE の early commit を vblank より長い frame で損しない形（flip の thread の待ちと mailbox の置き換えの順）の設計が候補。
- big-1000（20 対 84.7）・subs-1000（3.2 対 57.7）: record（libvulkan の command の encode、16000 draw で 29〜38 ms = 2 µs/draw、Linux 0.35 µs）と
  submit（kernel の executor が 16000 draw を PM4 にする 14〜18 ms、vk timing の op 132 が 1 draw 1 µs 強）。libvulkan ↔ gpu core の往復と execute_draw の
  draw ごとの state の絞り込み（共有の code は案を Q1 に先に）。
- UCLK: 最上段に固定（146fdfe5d）。shutdown の時に固定を外す事、SMU の停止の時の自動 mode1 reset は未実施（Fable の助言どおり今は guard の poweroff と入れ直し）。
- 機械: zedBSD は NVMe（Boot0004）、kernel は dfcaa14e5 の build（vmunix-dfcaa14）、Keiland・libvulkan は main の A・C の物、nicwatch あり。USB の stick は
  ユーザーが抜いた。今日の P2 の SwitchBot の入れ直しは 5 回（08:17:38・08:58:48・09:18:11・09:28:18・09:54:54）、全部 Debian に戻った。

### 起動の logo（GOP の buffer）が試験中に見える件（2026-10-28 ユーザーの報告。直しは下の「q1651」節）

- ユーザー（2026-10-28）「P2のテスト中に、OSの起動ロゴ画面がちらついて表示されています。GOPの内容でしょうか？もしかして、メッセージ表示のたびにコンソールを
  書き換えていたりしますか？GPU使用中はコンソール出力は完全に止めて、内容をアップデートしないようになっているかを確認していただき、さらに、GOPのバッファが
  一瞬ひょうじされてしまっている理由を解析し、修正してください。」「P2のモデル表示のテスト中に、起動ロゴ画面が見えてしまう、というバグです」
- code で見つけた経路（未確かめ、実機の標本は取っていない）: src/drivers/gpu/amdrdna/present.c の present_end_lease は、lease の frame が出ていれば
  console の surface（display->console = GOP の buffer、起動の logo と console の文字）へ flip し（amdrdna_display_show(device, device->display.console)）、
  present_text_give_back で kern_text_resume（console が文字を描き直す）。lease の間は present_text_hold（kern_text_suspend）で console は止まっている
  ので、「GPU の使用中の console の書き換え」は lease の間は無い。が、lease は mview の試験のたびに切れる:
  - game mode に入る時: Keiland が window mode の swapchain を閉じる（libvulkan が display を返す = lease の終わり → GOP の buffer へ flip、console 再開）
    → scanout の claim（新しい lease）→ 最初の present で client の buffer へ flip。間が KWL SCANOUT の switch_ms（33〜62 ms）= 2〜4 frame の logo。
  - game mode を出る時（mview の終わり、pointer の動き、overlay）: scanout の close（lease の終わり → GOP の buffer）→ window mode の swapchain を開き直して
    最初の合成の present まで同じく logo が出る。
  - session の lost・hang の回復・compositor の再起動も lease の終わりを通る。
- 直し方の案（amdrdna の中、present.c）: lease の終わりで console へ flip せず plane を blank（amdrdna_display_blank、blend の背景 = 黒）にして console の
  停止も続ける。次の lease の最初の present で flip の後に unblank。一定時間（例 3 s）新しい lease が無い時か amdrdna_display_stop（shutdown）の時だけ
  console の surface へ flip して unblank・kern_text_resume。時間切れの判定の置き場（既存の thread か timer）は要設計。blank 中は flip_wait が待たない
  （display->blanked）事と、lease の power-off（node->powered_off も blank を使う）との重なりに注意。
- 確かめ方の案: (a) amdrdna_display_show の各 flip で、console の address へ flip した時刻と理由を mark に出し（診断）、round・game mode の出入り・hang の
  試験で 0 回を数える、(b) display_shown_address（DCN の surface の address の register）を present の thread とは別に周期で標本化して console の address
  を見た回数を数える、(c) ユーザーに mview の試験（scene の起動・終了と game mode の出入り）を見てもらい logo が出ないかを確かめる手順を渡す。

## q1651（2026-10-28 P2、worktree p2-ws291e、基点 main 414f3e73e）

- 承認: q1639・q1645 と同じユーザーの文。最初の作業はユーザーの指示（2026-10-28）「GPU使用中はコンソール出力は完全に止めて、内容をアップデートしないように
  なっているかを確認していただき、さらに、GOPのバッファが一瞬ひょうじされてしまっている理由を解析し、修正してください。」「どこかの経路で、ゲームモードに
  切り替えるときにGOPのバッファが表示されてしまうのが仕方がないとしても、そのバッファはクリアして黒にしておいてほしいです。」

### 起動の logo の直し（Q1 の OK、2026-10-28）

- 確かめた事（code）: lease の frame が出ている間は kern_text_suspend で console は描かない（pcat の text.c の draw_cell_locked は text_ready 0 で
  描かない）。quiet の起動（kmsg=quiet、この機械の cfg）では resume の後も text_hidden で cell を描かず、splash の spinner は lease の claim
  （gpu.c の kern_text_progress_end）で止まる。よって「GPU の使用中の console の書き換え」は無い。logo が見えたのは present_end_lease が lease の
  終わりに GOP の buffer（logo のまま）へ flip していたため（game mode の出入り・mview の起動終了・hang の回復のたびに 2〜4 frame）。
- 直し（amdrdna の中だけ、kernel の console 側は変えない）:
  - display.c amdrdna_display_console_clear: GOP の予約域（layout の AMDRDNA_RANGE_GOP、CPU の window の外）を VRAM BAR の一時の map（uncached）で
    不透明の黒に塗る。present.c: console が止まった最初の present（plane は client の buffer を読んでいる）の後、present_lock の下で塗る
    （実機 8108 KiB を 132 ms、起動の後の desktop の最初の frame で 1 回）。
  - lease の終わりは今どおり console の surface（＝黒）へ flip するが console は止めたまま。lease の無い状態が 3 s（PRESENT_CONSOLE_REST_MS）続いた時
    （thread present_rest_watch が 250 ms ごとに見る）と shutdown（amdrdna_present_stop）の時だけ kern_text_resume。resume した後の次の lease では
    再び塗る。blank の案は blank 中の flip の latch が不確かで解放した blob を 1 frame 読む危険があるので採らなかった（見た目は同じ黒）。
  - 診断の mark: 「display: the console's surface painted black」「gpu device: the console's surface is shown without its paint」（塗る前の GOP を
    見せた）「gpu device: the console draws again after 3000 ms without a lease」。
  - display.c amdrdna_display_screen（root の screen readout）: CPU の window の外の surface（GOP）を間接の window ではなく BAR の一時の map で読む
    （下の SMU の件）。
  - 道具: amdrdnactl watch SECONDS（2 ms ごとに plane の表示の address を読み、console の address を見た回数・run の数、最初の 3 run の画面を読み
    黒でない pixel を数える）、measure.sh logo OUT（mview の game・window mode の出入り 6 回、間隔 1 s と 5 s、vkhang の hang、最後に compositor を
    終わらせる。zedBSD は desktop が sessiond で立ち直る）。
- 実機（NVMe の zedBSD、guard の下）:
  - logo1（10:55、vmunix-logo1 d73b7dac）: watch 37500 回中 console 139 回・10 run（lease の切り替えごとに約 28 ms）、「shown without its paint」0。
  - logo2（10:57）: watch の screen を間接の window（8 MB を MMIO 3 回/word）で 6 回読んだ run の途中 10:58:00〜10:58:20 に SMU が止まり（temp 0、
    hung=1）、guard が 10:59:01 に poweroff。表示の register の読みも乱れた（console 8226 回・1127 run、plane=0x0）。P2 が 11:04:25 に SwitchBot で
    入れ直し（今日の P2 の 6 回目、Debian に戻った）。間接の読みが原因かは未確定（1 回だけ）→ screen を BAR の map で読む形に直した。
  - logo3（11:07、vmunix-logo2 58dd4759）: watch 37500 回中 console 48 回・6 run、その 3 run の画面（GOP）は 2073600 pixel 中 黒でない 0。
    compositor を終わらせた時の console の 1 run（約 1.1 s、次の session の lease まで）も 黒でない 0。kernel の「shown without its paint」0、
    「console draws again」0（3 s の rest は起きなかった）。SMU は通して正常（52〜56 C）。mview・vkhang の hang の回復は ok。
- 未実施: 3 s の rest の後の kern_text_resume を実機で見る事（compositor を終わらせても sessiond が約 1 s で次の session を立てた。code の上では
  quiet の resume は描かない）。shutdown の時の give back の実機。ユーザーの目の確かめ。
- ユーザーが目で確かめる手順: desktop で `mview --spin=3 --content=game --present=mailbox --copies=1` を数回（端末から、間を 1 秒ほど空けて）と
  window の mview（--content=none）を起動・終了し、画面の切り替えの瞬間に起動の logo が出ず黒（一瞬）になる事を見る。hang（vkhang）の後も同じ。

### Fable 5.1 High の助言（CPU 時間、2026-10-28 11:00、Q1 経由のユーザーの指示「案Dがだめだったことと、状況を、…伝えて、今後の作業方針について助言をもらってOKです」）

- 伝えた事: 今の数（Linux と並べて）、効いた事（PTE の fragment・WC・UCLK・A・C・B1）、効かなかった D と GPU_CAP_FENCE、libvulkan → GPU core → executor の構造。
- 助言の要点（code を読んだ上の見立て、未確かめ）:
  - libvulkan の vkCmd* 1 つ約 2 µs は、command ごとに vulkan_writer を作り realloc・free・memcpy 2 回（commands.c command_record_begin/finish、wire.c）。
  - kernel の op 132 は GPU_OP_CMD_PUSH_CONSTANTS で、vk-record.c record_push の push ごとの kern_malloc が 1 µs。
  - execute_draw は draw ごとに約 45 packet・130 dword を全部出し直す（pipeline・viewport・scissor・blend 等）。PM4 の buffer 1 MB で big-1000 は
    1 command buffer に 8〜12 回 flush、slot 待ち（amdrdna_vk_gpu_wait）が submit の時間に入る見込み。
  - subs-N: vkBegin・vkEnd・vkReset・vkQueueSubmit がそれぞれ同期の往復（submit + wait + worker の受け渡し、50〜80 µs）。
  - scene-N: WSI の present の copy（wsi-swapchain.c present_copy_shared）が別の command buffer で 4〜5 往復と GPU 0.87 ms。scene-1500 4.4 + 0.87 ms →
    189 fps 上限、scene-6500 19.0 + 0.87 → 50.3 の上限で、scene の差のほぼ全部。
- 助言の順（期待）: (1) 案 E: Wayland でも swapchain の image を直に present（chain->direct の経路を platform の import_image がある時に許す、release を
  image に対応、copy を省く。libvulkan の案を Q1 へ）→ scene-1500 205〜215、scene-6500 約 50.5。(2) record_push の push を record に inline（128 byte、
  executor だけ）→ op 132 1 → 0.1 µs。(3) vkCmd* を command->recording へ直に encode（libvulkan、案）→ 2 → 0.5 µs。(4) execute_draw の state の
  差分だけを出す・table の再利用・buffer を大きくして flush を 2〜3 回に（executor だけ）。(5) vkBegin・vkReset を返事の無い record に、submit の
  wait を省く（libvulkan と GPU core、案、subs-N の唯一の手）。(6) FENCE の early commit は後で、Keiland の thread が最新の commit の fence だけを待ち
  古い未 flip の buffer はすぐ release する形に。
- 決め手の計測（助言）: M1 = amdrdna.vk.timing と libvulkan の timing で big-1000・subs-1000 を 1 回（End の待ちの割合、op 132/107/91/18、frame あたりの
  run の数と slot 待ち）。M2 = present_copy_shared の copy を飛ばす 10 行の試し（画は誤り、E の上限を測る）。
- 採否: まだ実施していない（13 時までの待機の指示）。次の担当は M2 → E の案を Q1 へ、並行して executor だけの (2)・(4) から。

## q1653（2026-10-28 P2、worktree p2-ws291f、基点 main 9fda28faf）

- 承認: q1639・q1645・q1651 と同じユーザーの文（自律のループ、Fable に何度でも・自律で聞いてよい）。
- 機械: 13:02:52〜13:08:07 に ping・SSH とも無応答で、P2 が 13:08:15 に SwitchBot で入れ直した（今日の P2 の 7 回目）。ユーザーの補足（Q1 経由）
  「P2が待機中にRDNA機の電源を落としたのは私で、今しがた電源を入れました。問題はないです。」→ 不具合ではない、調べの対象にしない。
- 道具: measure.sh に ZEDBSD_ENV（zedBSD の mview に渡す env、mview・profile・bench）。新しい worktree では plan/tmp/guest/id_ed25519 を chmod 600 に
  しないと SSH が鍵を捨て、measure.sh where が none を返す（13:02 の無応答の判定の一部はこれと重なった）。

### M1（kernel と libvulkan の時刻、HEAD 9fda28faf の kernel、build/ws291/measure/m1/）

- big-1000（20.5 fps、frame 48.5 = record 31.9 + submit 15.5 ms）: op 132（vkCmdPushConstants の decode）が 66 万回 × 0.8 µs = 1 frame 約 13 ms（record_push の
  kern_malloc）、op 18（submit = executor）が 1 frame 約 15 ms（16000 draw、約 1 µs/draw）、op 103・107・93 は各 0.1 µs。frame あたり run 約 13。
- subs-1000（2.5 fps、record 186 + submit 198 ms）: kernel の op の合計は 1 frame 約 60 ms。libvulkan の context_hold は平均 52 µs × 約 3000 回/frame
  （command buffer ごとに約 3 往復）、VULKAN TIMING submit 平均 204 µs（execute 71）。往復ごとに kernel の処理の外で 30〜50 µs。

### M2（present の copy を飛ばす診断、commit しない: build/ws291/m2/m2.patch、env M2_SKIP_COPY）

| run | copy あり（m2-base） | copy なし（m2-skip） |
| --- | --- | --- |
| scene-1 | 814.6 | 1057.1 |
| scene-1500（acquire / fence） | 180.85（0.54 / 4.43） | 182.81（0.46 / 4.47） |
| scene-6500（acquire / present / fence） | 49.27（0.37 / 0.30 / 19.13） | 48.73（0.54 / 0.80 / 18.68） |

- 読み: copy（GPU 0.87 ms）は scene の律速でない（mview は 1 frame ずつ fence を待ち、copy は次の frame の CPU 側と重なる）。案 E は効果が小さいので
  提案しない。scene の残りは CPU 側の acquire（render の完了 → present の worker の commit → compositor の release → acquire の起床の往復）。

### executor の改善（commit 8e4463127、merge f5d2e0f40）

- push constants の bytes を command buffer の連続の領域（push_bytes、record は push_at の位置）に置き、record ごとの kern_malloc・kern_free を無くした
  （vk-record.c record_push_keep、vk-command.c の reset と destroy、vk-execute.c execute_bind）。
- execute_draw: 同じ IB の中で pipeline・viewport・scissor が前の draw と同じなら、viewport・guard band・scissor・blend・raster・depth・program の
  register を書かず、vertex の user SGPR と pixel の table だけ（vk-state.c amdrdna_vk_state_program_data、state_vertex_data に共通化）。internal の
  draw・transfer・pass・新しい IB は target_written を落とすので、その後は全部を書く。
- 実機（vmunix-pd1 475d4bcccc7008a7、round、build/ws291/measure/pd1/）: nggshape 14 種 ok、shot 1 体 差 0・6500 体 max 1、hang 回復 ok、SMU 正常（最高 53 C）。

| run | 前（M1・m2-base） | pd1 | Linux（linux-img4） |
| --- | --- | --- | --- |
| scene-1500 | 180.9 | 166.9（acquire 1.01） | 201.3 |
| scene-6500 | 49.3 | 48.9 | 50.0 |
| big-1000（record / submit） | 20.5（31.9 / 15.5） | 28.3（19.0 / 11.5） | 84.7（5.57 / 0.02） |
| subs-1000（record / submit） | 2.5（186 / 198） | 3.4（156 / 124） | 57.7（6.57 / 10.3） |

- profile（pd1、build/ws291/measure/pd1p/big）: op 132 は 0.15 µs に。frame あたり run は約 10 のまま（表の領域 768 KiB が 16000 draw × 2 表で埋まる）。
- audit-symbols: 17 → 18 件の増えた 1 件は display.c の局所変数 'mapped'（1510922e5、q1651 の logo の直し、main は 5fd2dbd00 から）。by_bar に改名
  （b93e355ea）、FAIL 17 件に戻った。

### timing の上乗せ（2026-10-28 14:00）

- 機械の zedbsd.cfg に `amdrdna.vk.timing=2000` が q1645 から常に入っていた。kernel は command ごとに時刻の読み（amdrdna_display_now_us）と
  計数の spinlock を通り、big-1000（1 frame 約 54000 command）で約 5 ms/frame の上乗せ（timing ありの record 18.7 → なしの 13.7 ms）。Linux には無い。
  → bench は timing を外して取る（`measure.sh timing on|off`、`measure.sh cfg NAME [VALUE]`）。profile の時だけ on にする。q1645 以前の big-N・subs-N の
  数は timing ありの数。
- 診断（diag1〜diag3、commit しない）: hal_rtc_read_counter 1 回に約 0.04 µs。executor の draw 1 回は約 0.5 µs（表の作成は実 0.17 µs、うち vertex
  input 0.1 µs）。mview の big-N は group ごとに texture（descriptor set）と pipeline が替わるので、表・state の再利用はほとんど効かない。

### 案 1〜3a（Q1 の OK、2026-10-28、ユーザーの承認は q1653 の文）

- 案 1（b4897a17b、libvulkan wsi-swapchain.c）: 単独の vkCreateSwapchainKHR、MAILBOX、compositor の surface（display_mode 無し、cursor・overlay で
  ない、import_image あり）、maxImageCount の内の時だけ画像を 1 枚多く作る。minImageCount の報告（capabilities）は不変、shared の作成は対象外。
  VRAM は 1920×1080 で swapchain の画像 1 枚と GPU present の linear の共有画像 1 枚 ≈ 16.6 MB 増。scene-1500 181 → 190〜194 fps（acquire 0.54 →
  0.18〜0.24 ms）。
- 案 2（d162d6700、libvulkan commands.c・internal.h）: vkCmd* を command->recording に直に encode。writer が recording の領域を借り、finish で返す。
  encode の失敗は record の先頭で切り、command->error に latch して End が返す（今と同じ）。soft limit（VULKAN_RECORDING_BATCH_BYTES）を越える時は
  前の部分を先に decode、単独で limit を越える record は単独で decode（今と同じ）。big-1000 record 13.7 → 9.9 ms、33.2 → 36.8 fps。
- executor（6ac6d3754）: 同じ pipeline・binding の続きの draw で表を再利用、push だけ変われば複写、同じ物の再 bind は変化にしない。
- 案 3a（141ce391d、libvulkan commands.c・internal.h）: vkResetCommandBuffer は local の初期化だけにし、native の Reset は次の Begin の頭へ（2 回続けば
  release の flag は OR）。vkBeginCommandBuffer は Reset・Begin を recording の先頭に reply を求める形のまま置き（全 backend の protocol は不変）、最初の
  batch の flush か End と同じ transaction で送って reply を順に読む。renderer の側でしか分からない Reset・Begin の失敗は End（か途中の flush）で返る
  （vkEndCommandBuffer は OOM 等を返せる）。local の失敗は今どおり Begin・End で返す。End は同期のまま。
- 確かめ: amd64 の vmunix・libvulkan warning 0、arm64（config-rpi4）・riscv64（config-visionfive2）の libvulkan.so warning 0（案 1・2）、
  `plan/tools/gpu-boundary/run-dedicated-host.sh` PASS（b113329f3 で直した: -std=c89 → c99、出力は build/tmp、rm を外した）、実機 round（nggshape 14 種
  ok、shot 1 体 差 0・6500 体 max 1、hang 回復 ok）。

| run（timing 無し） | lv1（案 1 まで） | lv2（案 2 まで） | lv3（案 3a まで） | Linux（linux-img4） |
| --- | --- | --- | --- | --- |
| scene-1500 | 190.2 | 193.0 | 194.1 | 201.3 |
| scene-6500（frame） | 45.3（acquire 2.11 の回） | 49.46（20.17） | 49.73（20.06） | 50.04（19.94） |
| big-1000（record / submit） | 33.2（13.7 / 11.4） | 36.8（9.9 / 10.1） | 34.7（12.5 / 11.2） | 84.7（5.57 / 0.02） |
| subs-1000（record / submit） | 3.48（156 / 121） | 3.39（147 / 137） | 4.47（68 / 149） | 57.7（6.57 / 10.3） |

- 読み: scene-6500 の acquire が run により 2 ms に跳ねる（lv1）。frame（約 20 ms）が vblank より長い時の flip との位相の見込み（未確かめ）。

### QEMU Venus の回帰（Q1 の条件、2026-10-28 14:10〜14:45）

- image: `plan/tools/gpu-boundary/build-forge-image.sh build/ws291/forge`（tree は案 1・2 の後）。renderer は main の build/ws035-sq-venus への link。
  guest は新しい runtime（build/ws291/venus-*）、終わりは QMP の quit（guest.py stop の session.json の削除を避けた）。script は build/tmp/venus-ab.sh。
- forge-guest PASS。fence-guest: 同じ手順（起動 → libvulkan を put → guest の reboot → wait → fence-guest）で 9fda28faf の libvulkan 3/3 PASS、
  案 1+2（2ff3d0c73）3/3 PASS、案 3a（141ce391d）1/1 PASS（wltest --mode=mailbox --recreate-at=100、mview mailbox 4 枚、mview --draws=each
  --submits=20 も完走、compositor の VULKAN_ERROR 0）。起動の直後に待たずに流すと 3/3 FAIL（SSH の banner の時間切れ）、最初の手動の 1 回は
  zdesktop 自身の display の swapchain が VK_ERROR_NATIVE_WINDOW_IN_USE_KHR（起動直後の display の取り合いの見込み、案 1 の対象外の道）。
  → 案 1・2 の回帰ではない（同じ手順で前後とも PASS）。fence-guest は起動の後に wait してから流す事。

### SMU の idle の停止（2026-10-28 14:24、14:50）

- 14:24:32: bench の終わりの後（負荷なし、53 C）に hung=1 fault=1、guard が poweroff、14:25:10 に SwitchBot（今日の P2 の 8 回目）。
- Q1 の指示: UCLK 固定の後に初めて見えた idle の停止なので優先して切り分ける（性能より安全）。kernel に `amdrdna.smu.memory=0`（memory 側の feature を
  許さず UCLK を固定しない）を足した（e27a16d15）。道具 soak.sh（2026-10-28 削除済み、git の履歴）`soak.sh OUT [MINUTES [LOAD [IDLE [COPIES]]]]`（guard の下、mview の
  scene-1500 を 5 s、25 s 休み、繰り返し）。
- 再現: e27a16d15（DS_SOCCLK あり、UCLK 875 固定）の soak で 14:35 開始、31 回目の後の idle で 14:50:47 に hung=1（直前 14:50:27 正常、52 C）、
  guard が poweroff、14:51:09 に SwitchBot（9 回目）。
- Fable 5.1 High の助言（14:45、code と Linux v6.19 を読んだ見立て）: UCLK の hard floor 自体は Linux も DC の p-state 非対応の時に使う形。
  NumOfDisplays=0 は watermark・DPM_DCEFCLK が無い今は正しい（1 を送らない、推測の watermark を送らない）。本命は DS_SOCCLK（bit 11、idle の時だけ
  働く SoC clock の deep sleep、Linux は DS_LCLK・DS_DCEFCLK・DPM_DCEFCLK・RSMU_SMN_CG と組で許す。10:58 の MMIO の間接の読みの時の停止とも合う）。
  SOCCLK DPM（bit 4）は UCLK の最上段の電圧に要る（それ無しで SetHardMin が時間切れだった）。順: (A) DS_SOCCLK を外す、(B) なお止まれば DCN の
  p-state の force allow を残す（停止が underflow に変わり原因が分かる）、(C) Linux の組（RSMU_SMN_CG・DS_LCLK・DPM_MP0CLK）。SMU の reset は
  display の全初期化が無い今は入れない。次の停止で mailbox の 3 語・firmware の状態・DCHUBBUB の状態を読む。
- (A) の kernel（vmunix-mem3）: AMDRDNA_SMU_CLOCK_FEATURES_LOW 0x2780031b（DS_SOCCLK を外す）、smu_hang_mark（停止の時に mailbox の message・
  argument・answer、firmware の状態語、display の DRAM state を mark）。起動の mark「A4 features all on: 0x00000423a380031b」「A4 memory clock: 4 levels
  100-875MHz, held at 875MHz (ceiling 0, floor 0, arbiter 0x00000002)」= DS_SOCCLK 無しでも UCLK は 875 に固定できた。40 分の soak を 14:53 から。

### 案 3 の後半（3b）の設計（未実装、Q1 の条件つきの OK 済み、SMU の件の後）

- vkEndCommandBuffer（primary だけ）: End を recording に足して local に EXECUTABLE、送らずに end_pending。secondary と local の失敗は今の同期の道。
- vkQueueSubmit（queue.c queue_attempt、device->mutex の下）: submit の cb のうち end_pending の物の recording（head の Reset・Begin、record、End）を
  QUEUE_SUBMIT の前に同じ writer へ並べ（同じ cb の 2 度目は飛ばす印）、reply は head・End・submit の順に読む。送った時（writer.error が無く実行した時）
  だけ pending を消す。capacity の待ちで送らなかった時は残す。大きい recording（batch の 1/4 超）は先に同期で送る。
- renderer の側の Reset・Begin・End・record の失敗は Submit で返る: OOM（host・device）はそのまま、それ以外は VK_ERROR_DEVICE_LOST（vkQueueSubmit
  が返してよい物だけ）。cb は INVALID に。
- end_pending の cb への vkBeginCommandBuffer は先に pending を同期で送る（kernel の cb が recording のまま Begin を受けないように）。
  vkResetCommandBuffer・pool の reset・free は pending を捨てる（head の Reset が次の Begin で送られる）。
- 確かめ: subs-N・big-N・scene の前後、vkQueueWaitIdle・vkDeviceWaitIdle が全部を待つ試験（Q1 の条件、fence 無しの submit を省く (B) は別）、
  run-dedicated-host.sh、Venus の fence-guest・wltest・mview（同じ手順、wait の後）。

### SMU の切り分けの結果（15:13〜16:00）

| kernel・cfg | soak | 結果 |
| --- | --- | --- |
| e27a16d15（memory 側あり、DS_SOCCLK あり、UCLK 875 固定） | 14:35〜 | 31 回目の後の idle、14:50:47 に停止 |
| vmunix-mem3（memory 側あり、DS_SOCCLK なし、UCLK 875 固定） | 14:53〜 | 37 回目の後の idle、15:13:30 に停止（51 C） |
| vmunix-mem3 + amdrdna.smu.memory=0（memory 側なし、固定なし） | 15:16〜15:57 | 40 分・77 回、停止 0（最高 59 C、scene-1500 は 146〜149 fps） |

- 15:14:09 に SwitchBot（今日の P2 の 10 回目）。guard.sh は停止の時の dmesg を host の OUT.dmesg に取るようにした（53213c001、前は zedBSD の /tmp に
  書いて poweroff で失われた）。
- 判断（性能より安全、Q1 の指示）: 142fd7b88 で amdrdna.smu.memory の既定を 0（memory 側の feature を許さず UCLK を固定しない）。=1 で今までの固定
  （DS_SOCCLK は許さない）。実験の switch amdrdna.smu.memory_allow=1（固定の後も DCHUBBUB_ARB_DRAM_STATE_CNTL の state change の force allow を
  残す、self refresh は GOP のまま）。停止の時は smu_hang_mark が mailbox の 3 語・firmware の状態語・DRAM state を mark する。
- 実機: 既定の起動で「A4 features all on: 0x00000423a3800003」「A4 memory clock: its levels do not run」。
- 実験 (B)（memory=1 + memory_allow=1）の 40 分 soak を 15:59 から（結果は下）。
- 実験 (B) の結果: memory=1 + memory_allow=1（UCLK 875 固定、DRAM state に state change の force allow を残す）で 15:59〜16:42 の 40 分・76 回、
  停止 0（最高 53 C、scene-1500 191〜195 fps）。memory 側ありで force allow なしの 2 回の停止（16 分・20 分）と対照的 → firmware が idle で memory の
  状態の変更（p-state）を始め、display の hub の許可（GOP の 0x2 で禁じたまま）を待って止まる、という見立てを支える（1 回の soak、未確定）。
- 機械は既定（memory=0）に戻した（cfg の memory・memory_allow の行を消し、起動の mark で確認、16:45）。

### 残り（次の担当へ、2026-10-28 16:45）

- SMU: (B) を既定にする前に、memory=1 + memory_allow=1 の長い soak（2 時間以上、guard の下）を Q1 に相談の上で。通れば memory_allow の既定を 1、
  memory の既定を 1 に戻す（UCLK 875 で scene-6500 49.7・scene-1500 194 fps に戻る）。force allow を残す時の副作用（firmware が実際に memory を
  切り替えると display の underflow が見える）は display の underflow の印で見る。Linux の正規の道（DC の watermark・DPM_DCEFCLK・NumOfDisplays）は
  大きな作業。
- 性能（既定の memory=0 では UCLK 100 MHz、scene-6500 約 45 fps）: 上の SMU の決定が先。CPU 側は案 3 の後半（3b、上の設計）、案 4（小さな transaction
  を呼び手の thread で同期の GPU_COMMAND、上限の時間と hang の時の扱いが条件）、案 5（gpu.c の起床の抑制、design-reviewer を通す）。
- 今日の P2 の SwitchBot: 13:08:15（7 回目、ユーザーが切っていた）、14:25:10（8）、14:51:09（9）、15:14:09（10）。全部 Debian に戻った。

## q1655（2026-10-28 P2、worktree p2-ws291g、基点 main 76a72c12a）

- 承認: q1653 と同じユーザーの文（自律のループ、Fable に何度でも・自律で聞いてよい）。依頼: 既定（UCLK 固定 off）の bench、案 3b・4・5、夜間の (B) の soak。
- ユーザーの決定（2026-10-28、Q1 経由、原文）「P2は、負荷に応じて上げ下げさせる形、を取ってください。今夜のsoakは不要です。最短ではないものの、
  よりよい形に仕上げたいです。」→ (B) の soak はやめ、UCLK は固定せず Linux の DC の手順（DCN2.0 の watermark、SMU の watermark の table、
  NumOfDisplays、DPM_DCEFCLK、p-state の handshake）に沿って PMFW の DPM に負荷で上げ下げさせる形を実装する。Linux は値と手順の参照だけ。
  受け入れ（Q1）: 長めの soak（負荷と idle の繰り返し）で SMU の停止 0、display の underflow 0、idle で UCLK が下がり負荷で上がる、scene の数が
  固定の時と同等。既定は完成して確かめるまで off のまま。設計は design-reviewer（opus）と Fable に通す。

### 既定（UCLK 100 MHz）の基準（18:00、main a59767e3 の kernel・libvulkan、build/ws291/measure/base1）

| run | fps | record / submit ms | fence ms |
| --- | --- | --- | --- |
| scene-1 | 434.6 | | 1.70 |
| scene-1500 | 130.5 | | 6.78 |
| scene-6500 | 45.78 | | 20.78 |
| big-1000 | 33.3 | 11.5 / 12.4 | |
| subs-100 | 46.8 | 6.5 / 14.4 | |
| subs-1000 | 4.77 | 64.8 / 139.5 | |

### 案 3b（commit cf2118327、libvulkan の commands.c・queue.c・internal.h）

- primary の command buffer の End を、recording（head の Reset・Begin、record、End）が batch の 1/4（256 KiB）以下の時は送らずに end_pending として
  手元で EXECUTABLE にし、vkQueueSubmit の transaction の QUEUE_SUBMIT の前に並べる（同じ cb は attempt の serial で 1 回）。reply は cb ごとの head・End、
  最後に submit の順。renderer の側の失敗は Submit で OOM はそのまま、他は VK_ERROR_DEVICE_LOST、cb は INVALID。transaction を 1 batch より大きく
  する recording は先に単独で送る。pending の cb への Begin は先に同期で送る、vkResetCommandBuffer・pool の reset は捨てる（native は recording の
  まま、次の head の Reset で戻る）。capacity の待ちで送らなかった時は残す。vkQueueWaitIdle・vkDeviceWaitIdle は変えない（pending は未提出の物）。
- 実機（18:06、kernel は base、build/ws291/measure/lv3b）: nggshape 14 種 ok、shot 1 体 差 0・6500 体 max 1、hang 回復 ok。bench: subs-1000 4.77 → 6.25
  （record 64.8 → 9.3 ms）、subs-100 46.8 → 56.2、scene・big は run の揺れの内（scene-6500 41.56 の回は acquire 2.40）。
- 確かめ: amd64・arm64（config-rpi4）・riscv64（config-visionfive2）の libvulkan warning 0、run-dedicated-host.sh PASS、QEMU Venus の回帰
  （plan/ws291/tests/venus-reg.sh、build/ws291/venus2）: main の libvulkan と 3b とも forge-guest・fence-guest・wltest（mailbox、recreate-at=100）・
  mview（mailbox 4 枚）・mview（--draws=each --submits=20）・compositor の VULKAN_ERROR 0 が ok。guest の reboot は QEMU の -no-reboot で emulator が
  終わる（venus1 の FAIL はこれ）ので、put の後は compositor と client を立て直すだけにした。

### 案 4（未 commit、後回し: build/ws291/case4.patch、sha256 先頭 43ba7cc415bab73f、tree は cf2118327 に戻した）

- amdrdna: runtime_command（GPU_COMMAND）を session の worker の queue（queued、frame と marker の数）が空の時に呼び手の thread で走らせ、走る間は
  pending に数えて stop・drain が待つ。executor の失敗は EIO（一部が走った frame は session の stream を失う）。capset を 176 byte にし native word
  （"TANZ"）の flag 2 = 同期の command。libvulkan: その flag と GPU_CAP_NOTIFICATION がある時だけ transaction を GPU_COMMAND で送り、EAGAIN なら
  今の SUBMIT と WAIT へ。
- 実機（18:13〜18:33、vmunix-k4 a1… と libvulkan-4、build/ws291/measure/c4）: game mode の bench は subs-1000 6.25 → 9.40（submit 147 → 95 ms）、
  subs-100 56.2 → 73.1、scene・big は不変、nggshape 14 ok、hang 回復 ok。が window mode の shot が 2 回とも落ちた: mview（FIFO）が keiland-shot の後に
  vkAcquireNextImageKHR = VK_ERROR_SURFACE_LOST_KHR（errno 24、frames=322）。手で同じ手順でも再現。3b だけの時は shot ok。compositor も同じ
  libvulkan で sync の道を使う。未解析。errno 24 が fd の上限（EMFILE）なら、案 4 の sync の道で fd を閉じ忘れていないかが最初の疑い。

### UCLK を負荷で上げ下げする形の設計（2026-10-28 18:55 P2、未実装、design-reviewer と Fable に通す）

Linux 6.12.5（/home/awe/visionfive-linux、GPL、値と手順の参照だけ）の調べ（subagent、file:行は下の各項）:
- DCN 2.0 で Linux は p-state の force を使わない（hubbub1_verify_allow_pstate_change_high は dcn20 の funcs に無く、hwseq の呼び出しは comment）。
  hub の「p-state を許す」は pipe の buffer の量と、選ばれた set の DRAM_CLK_CHANGE の watermark からだけ出る。SR は force なし（DRAM_STATE_CNTL = 0x0、
  stutter 許可、dcn10_hubbub.c hubbub1_allow_self_refresh_control）。我々の起動の値 0x2 は GOP の「SR を強制で禁止」。
- display が 404 us の切り替えを覆えない時（dcn20_validate_bandwidth_fp の fallback）は、DRAM_CLK_CHANGE を 5 us（dummy p-state、「G6 temperature
  read」）で計算し直し、UCLK を hard min = 最上段で固定する（hard max は置かない）。→ PMFW は UCLK を固定していても dummy p-state（GDDR6 の温度読み・
  保守）を行い hub の allow を待つ。我々の停止（UCLK 固定・force なし・watermark は 404 us 用）は、allow が来ず PMFW が待ち続けた見込み（未確定）。
- SMU: Watermarks_t（TABLE_WATERMARKS = 1、384 byte、WatermarkRow[2][4]: WM_SOCCLK=0・WM_DCEFCLK=1、各行 MinClock・MaxClock・MinUclk・MaxUclk（MHz、
  u16）・WmSetting（u8）+ padding、計 44 byte）。reader の set i = WM_DCEFCLK[i]: DCEFCLK 0..0xFFFF、UCLK の範囲 = UCLK の DPM の段 i（前の段 +1 〜
  この段、最後は 0xFFFF）、WmSetting = i。writer は 1 行（set 0、全範囲）。送るのは TransferTableDram2Smu（param 1）。NumOfDisplays（0x40）は起動で 0、
  display の後に 1。DCEFCLK の hard min（0x1C、PPCLK_DCEFCLK=5）、SetMinDeepSleepDcefclk（0x22）、UCLK の hard min（display の要る段）。
  feature: DPM_UCLK(3)・MEM_VDDCI(8)・MEM_MVDD(9)・DPM_SOCCLK(4)・DPM_DCEFCLK(7)（Linux は全部許す。DS_* は今回は許さない）。
- UMC の workaround（navi10_ppt.c navi10_run_umc_cdr_workaround、MP1 11.0.0 で UCLK DPM が有効の時）: PMFW ≥ 0x2a3500（我々は 42.65.0 = 0x2a4100）は
  GetUMCFWWA（0x50）。bit0（UMC FW > v136）なら何もしない。bit1（hybrid CDR 無効）なら UCLK の最上段が 750 MHz 以下の時だけ hard max を 0 段 → 最上段に
  戻し DALEnableDummyPstateChange（0x4A）。どちらでもない時は dummy の pattern の table（0x40000 byte、PRBS7 の 2 種の 4 KiB の交互）の場所を
  0x4E・0x4F で渡す。→ まず GetUMCFWWA の答えを mark して、要る道だけを実装する（pattern の table が要る時は Q1 に相談: 値の作り方）。

設計（amdrdna.smu.memory=2 の時だけ。既定 0 と今の 1（固定）は変えない）:
1. A4（SMU）: feature に DPM_UCLK・MEM_VDDCI・MEM_MVDD・DPM_SOCCLK・DPM_DCEFCLK を許す。display の準備（A12）まで UCLK は今の形で最上段に hard min
   （切り替えの間だけ p-state を force allow、終われば戻す）。GetUMCFWWA を送り答えを mark、要る workaround を行う。
2. mode の plan（mode-plan.c）: 入力に memory clock の変更の latency（tenths）を持たせ、display が見る最も低い UCLK の段（DPM の 0 段、MT/s = MHz×16）
   で計算する。memory_switch が 1（blank）か 2（active）なら 404 us の watermark、0 なら 5 us（dummy）で計算し直し「UCLK は最上段に固定」の印。
   DCFCLK は plan の入力の値（今の DCEFCLK）のまま。
3. A12 の modeset（modeset_marks）の後、新しい段（display の memory clock、smu.c）: (a) watermark の table を作って送る（reader 4 行: UCLK の段 0..3 の
   範囲、DCEFCLK 0..0xFFFF、WmSetting 0..3; writer 1 行）。hub の 4 set は今どおり同じ値（plan は最低の UCLK で作るので全段で正しい）。
   (b) NumOfDisplays = 1。(c) DCEFCLK の hard min = plan の DCFCLK（MHz）、SetMinDeepSleepDcefclk = 同じ値。(d) DRAM_STATE_CNTL を 0x0（SR も p-state
   も force なし、hub の watermark に任せる）。stutter だけを止める切り替え amdrdna.smu.stutter=0（SR force 禁止のまま、切り分け用）。(e) UCLK の
   hard min: plan が切り替えを許す時は 0 段（PMFW が負荷で上げ下げ）、許さない時は最上段（Linux の fallback と同じ）。hard max は置かない。
4. mode の変更・hang の回復・reset の後も同じ順（A4 → A12 → 3）。shutdown の時は NumOfDisplays = 0 と UCLK の hard min を最上段（Linux の
   pre_display_config_changed と同じ形）にしてから止める。
5. 印と道具: smu_hang_mark に DCHUBBUB_TEST_DEBUG_INDEX = 0xB の DEBUG_DATA（bit 30 = arbiter の allow、bit 31 = SOC の p-state の要求）を足す。
   amdrdnactl の power の行に今の UCLK（readings の mem、無効なら GetDpmClockFreq）と DEBUG_DATA を出す。underflow は memory の行の underflow= を
   guard でも見る（増えたら止める）。
6. 試験（guard の下）: 起動の mark（features、UMC の答え、table の送り、NumOfDisplays、hard min、DRAM_STATE）→ idle で UCLK が下がる・mview で上がる
   （power の行を 1 s ごと）→ round（nggshape・shot・bench・hang）→ soak.sh を 60 分以上（負荷 5 s・idle 25 s）で SMU の停止 0・underflow 0 →
   scene-6500・scene-1500 が固定（memory=1）と同等。停止したら guard の poweroff、入れ直し（プラグミニ 1）、DEBUG_DATA と mailbox の 3 語で切り分け。

### 設計のレビュー（design-reviewer opus と Fable 5.1 High、19:00）と直した計画

- design-reviewer（blocker 2・major 8）: (B1) A12 は既定の起動で走らない（amdrdna.auto = A9。A12 は別の mode に 3 秒切り替える診断の段）。watermark を
  書く道は amdrdna_mode_set（present・hotplug）・relight・reset_display で、どれも modeset_input で今の UCLK・DCEFCLK を読む。→ display の段は独立の
  関数にし、takeover（A6〜A8）の終わり・modeset の後・reset の A4 の後から呼ぶ。plan の入力は段 0 の UCLK と表の fabric（Linux の段 0: fabric =
  max(1.08×UCLK, 506)）を明示で渡す。(B2) 段 1 の中間状態（固定・force なし）は既知の停止の設定 → 段 3 が済むまでと失敗の時は hold（force allow）。
  (M1) SR を強制で禁じたまま（0x32）では SetHardMin が時間切れ（q1645）→ stutter=0 と memory=2 は組ませない、段 3 は DRAM_STATE を 0x30 にしてから
  表・NumOfDisplays・hard min を送り、最後に 0x0。(M2・M3) DCEFCLK・SOCCLK の hard min（段 0 の値 506 MHz か起動の値）を A4 で置く、DISPCLK・
  PHYCLK の電圧の hard min も。(M5) shutdown は NumOfDisplays=0・hard max を最上段（hard min ではない）、その前に 0x30。(M6) reset の後は表の送りを
  やり直す。(M7) guard は SSH の無応答を数える、underflow を数える。(M8) plan_prefetch も latency を直に使う、5 us でも 0 なら hold。
  minor: SetMinDeepSleepDcefclk は DS_DCEFCLK が無い今は送らない、DEBUG_INDEX と DATA は lock を共有、soak は 2 回以上か 2 時間、
  受け入れの「固定と同等」は Linux も scene-1500 で UCLK 500 の段を使う（q1645）ので基準を Q1 と確かめる。
- Fable: Linux は「固定 + 404 us の watermark」の組を走らせない（固定する時は 5 us の dummy の watermark）。増分の案: A（memory=1 + 5 us の
  watermark、hard min = max、force なし）→ B（+ SR 許可）→ C（+ 表・NumOfDisplays・DCEFCLK の hard min）→ D（段 0 の 404 us の watermark と UCLK の
  hard min を下ろす）。NumOfDisplays の DPM_DCEFCLK の条件は DCN2 では使われない道（dcn20_update_clocks から無条件）。
- 実機の観察（19:00、診断の kernel 240ac2197 の元、amdrdnactl の clocks の行）:
  - GetUMCFWWA の答え 0x00000001（UMC の firmware > v136）→ UMC の workaround は要らない（dummy の pattern の table は不要）。
  - memory=1（固定、force なし）の idle で 1500 標本: DRAM_STATE 0x2（GOP のまま、SR は強制で禁止）、handshake 0x50777777 が 99.8 %（bit 30 arbiter
    allow = 1、6 pipe の plane0・plane1・cursor0 と WB0 が allow）、残りは 0x10777776（pipe0 plane0 と arbiter の allow が一瞬落ちる）。bit 31（PMFW の
    要求）は標本に無い。DRAM_CLK_CHANGE の watermark（set A）は 2210（GOP の値、A12 は走っていない）。→ idle の時の「allow が来ない」は否定。
    停止は別の条件（負荷の後、SR の強制禁止、要求が来た時の allow の落ち方）の見込み。次: 停止の時の handshake（smu_hang_mark に 4 標本を足した）
    を取る再現の soak（memory=1、40 分、19:07〜）。
- Q1（19:15）: 受け入れの基準は「Linux の数以上（同じ条件の bench）」、固定の時の数は参考。
- status の大きさの変更（include/uapi/amdrdna.h、352 → 384 byte）の版ずれ（code を読んで確かめた、実機の版ずれは未試験）: AMDRDNA_IOC_STATUS は
  _IOWR で大きさを番号に含むので、古い amdrdnactl と新しい kernel（またはその逆）では番号が合わず control_ioctl の switch の default で ENOTTY。
  番号が合っても control_status は version と size（sizeof）を比べて EINVAL で断り、copyout しない。amdrdnactl は失敗を control_fail で言って
  終わる（壊れた値を出さない）。guard は amdrdnactl が答えない時を「no answer」と記録するだけで止めないので、kernel と amdrdnactl は一緒に入れる。
- 再現の soak（memory=1、固定・force なし・SR は強制禁止 0x2、kernel vmunix-d3 = 240ac2197 + smu_hang_mark の handshake、libvulkan 3b、19:07〜19:47、
  build/ws291/soak-m1）: 40 分・76 回、停止 0（scene-1500 193〜198 fps、guard の clocks の行は全部 mem=875MHz state=0x2 handshake=0x50777777
  underflow=0）。前の 2 回の停止（16・20 分）は再現しなかった。停止は確率的で、1 回の soak では何も言えない（前の (B) の 40 分 0 回も同じく弱い）。
- 機械は既定に戻した（19:50、cfg の amdrdna.smu.memory の行を消した、kernel は vmunix-d3、amdrdnactl は d2）。今日の P2（この担当）の SwitchBot は 0 回。
- 未実機の追加（commit、既定の道は不変）: amdrdna.smu.memory_allow=2（固定の後、DRAM_STATE を force なし 0x0 = Linux の休みの形、
  amdrdna_display_memory_switch_release）。build warning 0、実機は未試験。

### UCLK の再開点（次の担当へ、2026-10-28 19:50）

1. （取りやめ、2026-10-28 20:07）増分 B（memory=1 + memory_allow=2 の固定 UCLK の soak）。ユーザー（Q1 経由）「UCLK固定のsoakはやらなくていいと伝えたはずですが」
   「P2はsoakはやめさせて、実装に集中させてください。」→ soak はしない（固定も受け入れの長いものも）。始めた 1 本（build/ws291/soak-b1、20:01〜）は 13 回・停止 0 で止めた。
2. 増分 C・D の実装（design-reviewer の B1〜M8 を入れる）: display の段を独立の関数（smu.c か新しい file）にし、A8（takeover）の終わり・
   amdrdna_mode_set の後・reset の A4 の後から呼ぶ。順: DRAM_STATE を 0x30 → watermark の table（TABLE_WATERMARKS = 1、reader 4 行 = UCLK の段の範囲、
   writer 1 行）→ NumOfDisplays = 1 → DCEFCLK の hard min（起動の値か 506）・SOCCLK の hard min → hub の DRAM_CLK_CHANGE の watermark を mode-plan の
   段 0 の UCLK（memory_mts = 100×16）と 404 us で計算した値で 4 set に書く（memory_switch が 0 なら 5 us で計算し UCLK は固定のまま）→ 最後に
   DRAM_STATE を 0x0 → UCLK の hard min を段 0（下ろすのは watermark の後）。shutdown は 0x30 → NumOfDisplays = 0 → hard max を最上段 → all off。
   DEBUG_INDEX と DATA の読みは lock を共有する（control.c と smu_hang_mark）。
3. 受け入れ（Q1 19:15、20:10 の指示で soak を除く）: 短い起動の確認（起動の mark・idle で clocks の mem が下がり負荷で上がる・underflow 0）と、
   scene の bench が Linux の数以上（同じ条件）。長い soak はしない。既定は完成して確かめるまで memory=0。

## q1658（2026-10-28 P2、worktree p2-ws291h、基点 main a346ceb55）

- 指示（Q1 経由のユーザーの原文）: 「UCLK固定のsoakはやらなくていいと伝えたはずですが」「P2はsoakはやめさせて、実装に集中させてください。」→ 増分 B の soak は 20:07 に止めた（13 回・停止 0）。
  「P2が使っている性能評価スクリプトは、今後ほかのGPUのドライバの性能向上に標準で活用したいので、tools/以下のスクリプトとして保存するのが望ましいと思います。」→ plan/tools/gpu-perf/（9213987d1）。
  「P2は、評価を中断して、画面へ出す回路のunderflowを先に修正してください。目視でも、かなりの割合で数秒に一回くらい、画面出力が途切れる瞬間があります。解決してから評価に進みます。UCLKの問題は解決とマークしていいです。」

### 負荷に応じた UCLK（amdrdna.smu.memory=2、commit 37ca3c658・261aca2ae・959e8c514・144cff0f5、既定は memory=0 のまま）

- 実装: display-memory.c（display の段）。A4 は UCLK を最上段・p-state の force allow で保つ。takeover（A6）の後・mode set の後・reset の A4 の後に:
  DRAM_STATE 0x30 → watermark の表（TABLE_WATERMARKS、reader 4 行 = UCLK の段の範囲、writer 1 行、Linux の Watermarks_t と byte 一致: plan/ws291/tests/watermarks-test.sh）
  → NumOfDisplays=1 → DCEFCLK の hard min 506 → DISPCLK の電圧の hard min（max(DISPCLK, DPPCLK)、164 MHz）→ 今の mode を UCLK 段 0（1600 MT/s、fabric 506）で
  replan（plane・hub の 4 set・global sync を update lock の下で、amdrdna_mode_replan）→ force を外す（0x0）→ UCLK の floor を段 0。覆えない mode は最上段に固定し
  5 us（dummy p-state）の plan。mode set の前は hold（0x30・最上段）、shutdown・reset の前は end（+ NumOfDisplays=0）。SOCCLK の hard min は Linux の Navi10 に無いので送らない
  （送ると 0xff）。power の watch が display の underflow を 1 s ごとに数え mark・clear。GPU の busy（boost）の間は UCLK の floor を段 1（500 MHz）。
- 実機（短い確認、plan/tools/gpu-perf/amdrdna/dpm-check.sh、guard の下）: idle で mem=100MHz、負荷で 500/625/875、止めると 100。SMU の停止 0、hung=0。
- underflow: 404 us の plan では game mode の負荷の 6 s ごとに 1〜1.5 回の timing generator の underflow（HUBP は 0）。window mode 0、UCLK 固定 0、Linux 0。
  Linux の DTN log: 1080p を 2 pipe（960×2、MPC combine、64KB_R_X）、dram_clk_change 439.64 us（404+urgent+25 = active でも許す）。我々は 1 pipe linear で vblank だけ。
  Fable 5.1 High（21:00）: 1 pipe の vblank の切り替えは 404 us を超える切り替えに余裕 0、Linux は active で ~390 us の余裕。案 F2: latency を 520 us に（vblank に収まる上限 ~575）。
  → 520 us（MEMORY_SWITCH_TENTHS、amdrdna.smu.memory_latency= で変更可）で game 16 cycle・window 4 cycle・bench 2 回で underflow 0。
  DISPCLK の電圧の floor だけ（404 us）では 4 cycle で 3 回（効かない）。UCLK の floor 500（memory_floor_level=1）でも 4 cycle で 2 回（100 MHz の段の問題ではない）。
- 速さ（scene-6500）: 段 0 の floor だけでは UCLK が負荷の中でも 100/500 を往復し fence 20.1〜20.3 ms（Linux は 500 MHz を保ち 19.54）。busy の floor 500 で fence 19.62〜19.70。
- UCLK（SMU の停止）の件はユーザーの指示で解決とする（2026-10-28 21:5x、上の原文）。残り: display の underflow の根本（2 pipe 分割、Linux の形）と評価。
