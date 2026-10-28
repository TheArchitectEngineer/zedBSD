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
- 実機（vmunix-defer1、ngg=2、`plan/ws291/tests/round.sh`、build/ws291/measure/defer1b/）:

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
  100 % に。plan/ws291/tests/guard.sh（20 秒ごとに amdrdnactl の power 行、hung=1・fault=1・90 C 以上・driver が回す時の fan 停止で mview・vkhang を
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

- ユーザーの指示（2026-10-28、上の BootNext の節）で、試験の image と機械の設定だけに入れた（base の既定の image には入れない）: `plan/ws291/tests/nicwatch/`
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
