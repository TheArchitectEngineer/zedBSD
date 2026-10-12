<!-- awesome-plan project=zedbsd record=ws211-p001 -->

# ws211-p001: 調査と設計（draw の数え、クロスフェードの絵の持ち方と描き直し、draw の束ね、Pi の見積もり、試験）

Parent: [WS211](../ws.md)
Status: planning（設計の第 1 版、design-reviewer の review の前。review を通るまで実装しない）
Phase disposition: normal
Queue: P3（Q1 が割り当て、2026-10-12）

## 由来

- [WS211 の由来](../ws.md#由来)（2026-10-12 ユーザー: App Home を開くアニメが 2 コマ、「デスクトップ1枚とHome1枚でクロスフェード」「App Homeのアイコンは、遷移のスムーズさのために常に保持するしかない」、Q1 の (1) クロスフェード (2) draw の束ねの提案を承認）。
- 範囲は compositor（`userland/desktop/wayland/`）の中だけ。kernel・libvulkan・UAPI・HAL は変えない。

## 今の描き方（source と Pi の log で確かめた事実）

### 1 つの形が 1 つの draw

- glass の look の形は全部 `glass_shape_draw`（glass.c）を通り、1 つの形ごとに `vkCmdBindPipeline`・`vkCmdBindDescriptorSets`・`vkCmdPushConstants`（96 byte）・`vkCmdDraw(6)` の 4 command。頂点は共通の `corners`（vec2 の 6 頂点）で、位置・uv・box・色・mode などは全部 push constant。pipeline は mode ごとの 7 つ（`panel_pipelines[]`、glass・shadow・image・solid・ring・text・blur）と mode を分けない 1 つ。
- 窓の client の画像は `kwl_compose_surface_quad`（compose.c）で別の pipeline layout（quad.vert/frag）の 1 draw。
- 1 コマは 1 つの command buffer、1 コマずつ（前のコマの fence が signal してから次を記録、`kwl_compose_complete`）。

### App Home の 1 コマの draw（source から、page 1、12 個の icon が全部 picture 付き、2026-10-12 の Pi の apps.conf）

| 種類 | 式 | 数 |
| --- | --- | --- |
| 舞台（stage）: 黒い glass（全画面、MODE_GLASS）＋上の光（全画面、MODE_SHADOW） | 2 | 2 |
| 床（`home_draw_floor`）: 4 本の線 × 24 片、行ごと | 2 行 × 96 | 192 |
| 時計: 時刻 5 字＋日付（例 18 字）の glyph | 1 字 1 draw | 約 23 |
| icon ごと: 光（spotlight）1、穴の壁紙 1、tile 1、映り込み 8 片 | 11 × 12 | 132 |
| icon の名前の glyph（Files 5、Notes 5、Settings 8、Terminal 8、PDF Viewer 9、Image Viewer 11、Video Player 11、Music 5、Photos 6、Phone 5、Calendar 8、Mail 4。空白は draw しない） | 1 字 1 draw | 85 |
| 動作中の印（短い線） | 動作中の app ごと | 0〜12 |
| hover の tile の白（MODE_TEXT） | hover 中 1 | 0〜1 |
| ページの点 | ページごと | 2 |
| 合計 | | 約 436〜449 |

- 文字の tile（picture の無い app）は 1 つで影 1＋帯 24＋縁 1＋文字 1＝27＋名前で、今の Pi の一覧には無い。
- 開閉のアニメの間はこれに desktop の layer（壁紙 1、layer の影 1、desktop の icon、窓ごとに影・本体・glass の panel・title bar の glass・title の glyph・control）と system bar（Kei の mark 7 層、pill の glass、時計の glyph、status の icon、apps bar の icon）が加わる。WS211 の由来の 458〜916 draw はこの幅。desktop の layer と bar の正確な数は source の分岐が多く、p002 の段 0 の計測（下の `KWL DRAWS`）で種類ごとに数える。
- `home_content` の時間差（HOME_CONTENT_STEP_MS 30ms、1 つ 180ms）で、12 個目の icon は開き始めの 510ms 後に揃う。開くアニメ（HOME_OPEN_MS 280）の後も `home_content_rising` が 230ms ほどコマを求め続ける（BUG-252）。

### Pi の数（place2 の boot、2026-10-12、`--log-frames` と dmesg）

- App Home を開く・閉じる間の compositor のコマ: 記録（`KWL LAT acquired`→`recorded`）17〜64ms、submit→shown 53〜171ms。開く 280ms の間に 2〜5 コマ（前の boot は 1〜2）。
- 記録の時間は draw の数にほぼ比例（450〜900 draw で 20〜60ms、1 draw 約 0.05ms: libvulkan の command の encode と request）。kernel の準備（perf の `prep`）は submission あたり 2.3〜10ms、GPU の実行（`exec`）は 9〜35ms。
- GPU の counter（`v3d: gpu runs=`）: compositor のコマの頃の run あたり 4〜13M cycle（500MHz で 8〜26ms）。draw の数を減らしても全画面を何枚塗るか（fill）は残る。
- Pi の driver の制約（kernel の source、`src/drivers/gpu/bcm2711/`）:
  - vertex input は `VK_VERTEX_INPUT_RATE_VERTEX` だけ（instance の rate は ENOTSUP、`vulkan-pipeline-state.c` `fetch_state`）。`vkCmdDrawIndexed` は無い。attribute は R32〜R32G32B32A32_SFLOAT、16 個まで。
  - 頂点は draw ごとに kernel が CPU で詰め直す（`vulkan-native-draw.c` `prepare_fetch`、scalar ごと）。
  - varying は 32 word まで（`BCM2711_SHADER_INTERFACE_WORDS`）、flat を扱う。分岐は if-conversion（両方を計算して選ぶ）。
  - 描いた画像（raster）を sample すると、kernel が UIF に変換した snapshot を作る（`bcm2711_native_snapshot_get`、TFU が使える時は TFU、上限 128MiB、generation が進むまで再利用）。
- i915 の native compiler も flat の入力と 16 個の attribute を扱う（`src/drivers/gpu/i915/compiler/compile.c` `i915_compile_input_flat`、`I915_GFX_MAX_VERTEX_ATTRIBUTES`）。Venus は host の Mesa。

## 設計 (1) クロスフェード（p002）

### 1.1 画像（layer の cache、新しい file `layer-cache.c`）

- **Home の絵**: 出力（anchor）と同じ大きさ・同じ format（`compose->format`、今の pipeline と render pass が互換）の color の画像 1 枚。透明（0,0,0,0）に clear して、Home の content（時計・床・icon・名前・動作中の印・点、舞台は含めない）を layer の変換なし（scale 1、位置 0、opacity 1）で描く。合成は premultiplied（今の blend: color と alpha とも ONE / ONE_MINUS_SRC_ALPHA）なので、透明の画像に描いてから重ねても、直接描くのと 8 bit の丸め以外は同じ（over の結合則）。
- **desktop の絵**: 同じ大きさ・format の画像 1 枚。開く（閉じる）アニメの始めのコマで、desktop の layer（壁紙・desktop の icon・窓、今 `kwl_glass_draw` が layer の変換付きで描く物）を変換なしで描く。
- 作り方は backdrop（backdrop.c）と同じ形: 画像・memory・view・framebuffer・descriptor set（linear の sampler、`kwl_compose_set_get`）。render pass は 2 つ: clear（initialLayout UNDEFINED、finalLayout SHADER_READ_ONLY_OPTIMAL）と load（部分の描き直し用、initialLayout・finalLayout とも SHADER_READ_ONLY_OPTIMAL）。external の依存は backdrop の pass と同じ（COLOR_ATTACHMENT_OUTPUT → FRAGMENT_SHADER）。
- 作る時: Home の絵は起動の後の最初の空き時間（`home_prepare` の後、tile の atlas が ready の後）、desktop の絵は最初に Home を開く時。出力の大きさの変化・device の喪失・出力の close で捨てて作り直す。作れない時（memory 不足など）は `KWL LAYER unavailable` を出して今の描き方（毎コマ全部を描く）に戻る。

### 1.2 Home の絵の描き直し（無効化）

Home の絵は「状態の鍵」と描いた時の鍵を毎コマ比べ（CPU の比較だけ）、違う所だけ描き直す。

| 変化 | 描き直す範囲 |
| --- | --- |
| app の一覧（読み込み）、ページの数、表示中のページ（落ち着いた後）、言語（`kl_tr`・日付）、外観・accent（点の色）、壁紙（穴）、出力の大きさ、glyph の atlas の generation（初めての字の追加）、tile の ready | 全体（clear の pass） |
| 動作中の印（app ごとの窓の有無の bit）、hover の icon、選択の ring | その icon の cell（load の pass、scissor を cell に） |
| 時計の分（`%H:%M`、日付は日が変わる時） | 時計の帯（load の pass、scissor） |

- scissor を cell・帯にすると、`glass_shape_draw` の既存の cull（`compose->scissor_now` の外の形を CPU で捨てる）で、描く draw は範囲の中の形だけになる（cell 1 つで光・穴・tile・映り込み・名前・印と床の片の数個、今の形で約 30）。
- **hover は Home の絵の中に持つ**（上に重ねる案は、光が tile の下にあるので重ねると tile の下端と映り込みが白くなり、今と同じ絵にならない）。hover が変わったコマは、前と今の hover の 2 つの cell を描き直す。
- hover の probe（`kwl_hover_probe`、damage.c）は描画と分け、Home を絵から出すコマでも icon ごとに登録する（`home_probe`、CPU だけ）。
- **絵を使わない（今の描き方のまま）場面**: 検索中（`home_query_length` か preedit がある）、ページをめくる・drag する間（`home_page_position` が整数でない）。

### 1.3 描き直しの時（後ろで、最初の 1 コマを待たない）

- Home が閉じている間（progress 0）の描き直しは、**専用の command buffer と fence の別の submit** で行う（`layer_command`・`layer_fence`）。出す条件: 絵が無効、コマが in_flight でない、前の layer の submit が終わっている（`vkGetFenceStatus`）、最後のコマから 100ms 以上空いた（入力の途中に割り込まない）。同じ queue なので後のコマの sample は submit の順で後になり、command buffer の最後の barrier（render pass の external 依存）で同期が付く。
- Home が見えている間（開閉・開いたまま）の描き直し（hover・時計・動作中の印）は、**そのコマの command buffer の主の pass の前**に cache の pass を記録する（同じ command buffer の中で順序が付く）。前のコマは完了してから次を記録するので、sample 中の絵を書き換えない。
- Home を開くと言われた時に絵が無効（起動の直後など）なら、開く最初のコマで描く（そのコマは今と同じくらいの重さ、それ以後は軽い）。
- Pi では描いた後の最初の sample で kernel が絵を UIF に変換する（1920×1080 で TFU 約 3.5ms と見積もる、V9 の 256×256 112µs から）。CPU で画素を触らない（Guardrail の「描画の毎 frame の経路で画素を CPU で触らない」、変換は内容が変わった時だけ TFU）。

### 1.4 desktop の絵

- 開く時: 最初のアニメのコマ（progress が 0 から動いた最初）で、主の pass の前に desktop の絵の clear の pass に、壁紙（角の丸め無し）・`kwl_desktop_draw`・`draw_desktop_layer` の窓を描く。layer の変数は恒等（`layer_on = 1`、x・y 0、scale 1、opacity 1）にし、窓の見せ方（`window_shown`）は今の Home の時の規則（表示中の desktop の窓だけ）。
- 閉じる時: 閉じ始めのコマで同じく描く（Home が開いている間は layer を描いていないので、その間の窓の変化を拾う）。
- drag で開く・閉じる（指を追う、長く続き得る）間は、窓の client の commit（`scene.changed`・`server->dirty`）があったコマだけ描き直す（そのコマは今と同じ重さ）。280ms・240ms の自動のアニメの間は描き直さない（client の絵はその間止まる）。
- アニメが終わり layer が消えた（opacity 0、`hidden`）後は使わない。画像は次に使うまで持つ（作り直しの費用を避ける。記憶は下の表）。

### 1.5 開閉のアニメの 1 コマ

| 順 | 形 | draw |
| --- | --- | --- |
| 1 | 舞台（今どおり、glass＋光） | 2 |
| 2 | Home の絵: `kwl_edge_home_content` の x・y・scale（0.85→1）、opacity は content の opacity（下の 1.6）、MODE_IMAGE（premultiplied、`opaque` 0） | 1（1.6 の (b) なら 3） |
| 3 | layer の影（今どおり） | 1 |
| 4 | desktop の絵: `kwl_home_layer` の x・y・scale（1→0.75）・opacity、角の半径 18×scale、MODE_IMAGE `opaque` 1 | 1 |
| 5 | popup・system bar・メニューなど（今どおり、layer の外） | bar の数 |

- 起動の時の閉じ（`home_launch_app`）: 起動する icon の tile は今 `1 + 0.3 × (1 - opacity)` に大きくなる。絵から tile の矩形（uv）を中心を保って拡大した quad を 1 つ足す（拡大した tile は元の tile を覆う）。
- 拡大縮小は linear の sample で行う（0.75〜1 なので折り返しは出ない）。アニメの間だけ今より少し柔らかく見え、止まった時（scale 1、画素に揃う）は同じ。

### 1.6 icon の時間差の演出（ユーザーの判断が要る）

- (a) **やめる**（推奨）: content 全体が progress で一緒に来る（`home_content_ms = 0` の時の今の動き、opacity＝progress）。アニメは 280ms で終わり、後の 230ms のコマも要らない。draw 1。
- (b) **行ごとに置き換える**: Home の絵を 3 つの帯（時計の帯、1 行目、2 行目。行の間は cell の境で重ならない）に分けて、帯ごとに 60ms ずらして上がりながら現れる（HOME_CONTENT_RISE 12px）。draw 3。
- (c) icon ごと（今の 12 個の時間差）は、床の線が行をまたぐので cell で切ると床が icon と一緒に動いてしまい、絵の持ち方と合わない（採らない）。

### 1.7 見た目が今と変わる所（ユーザーに示す）

1. 時間差の演出（1.6）。
2. 消えていく desktop の layer は 1 枚の絵として薄くなる（今は窓・影・glass が 1 つずつ薄くなり、重なった所が透けて見える）。
3. 280ms・240ms のアニメの間、窓の中身が止まる（drag の間は動く）。
4. アニメの間の拡大縮小が少し柔らかい（止まった絵は同じ）。

### 1.8 他の場面

- head（2 つ目以降の display）: 舞台だけ（`kwl_home_draw_head`）で変えない。Home の絵は anchor だけ。
- fullscreen の窓が出力を覆う（`fullscreen_whole`）、Wiseview、greeter・lock、画面の消灯: 今の経路のまま（Home の絵・desktop の絵を使わない）。
- 出力が無い・失われた時は絵を捨て、次に出力が開いた時に作る。

## 設計 (2) draw の束ね（p003）

### 2.1 形: 頂点の列（instance は使わない）

- Pi の driver は instance の rate と DrawIndexed を持たないので、**1 つの形 = 6 頂点（三角 2 つ）を頂点の buffer に書き、同じ pipeline・descriptor set・scissor の続く形を 1 回の `vkCmdDraw(6 × n)` にする**。kernel と libvulkan は変えない。
- 頂点（18 float = 72 byte、形あたり 432 byte）: 位置（NDC、layer の変換・head の view の差し引きを CPU で済ませた角）2、uv 2、box 4、色 4（dark の外観の変換も CPU で済ませた物）、shape（半径・mode・soft・opaque）4、edge・opacity 2。attribute は 6 個（vec2・vec2・vec4・vec4・vec4・vec2）。
- 新しい shader `panel-batch.vert`・`panel-batch.frag`: 今の `panel.frag` と同じ計算で、push constant の代わりに flat の varying（box・色・shape・edge と opacity、14 word）と補間の varying（uv 2、画素の位置 2）を読む。varying は 18 word（Pi の上限 32 の中）。出力の幅と高さ（`screen.xy`）は pass の中で変わらないので push constant（8 byte）に残す。mode ごとの variant（`KWL_PANEL_MODE`）も今と同じ 7 つ作る（`regenerate.py` に足す）。
- pipeline: binding 0（stride 72、RATE_VERTEX）の新しい 7 つ＋mode を分けない 1 つ、新しい pipeline layout（push constant 8 byte、set は今と同じ 1 つの sampler2D）。今の pipeline は残す（下の fallback）。

### 2.2 束ねの仕組み（glass.c の中）

- `glass_shape_draw` は draw を記録せず、形を頂点にして「今の束」に足す。束の鍵は（pipeline、set、scissor）。鍵が変わる形が来たら今の束を flush（bind・draw を記録）してから新しい束を始める。
- set を sample しない mode（shadow・solid・ring）は set を「どれでもよい」として、今の束の set に合わせる（pipeline が同じなら続く）。
- **flush する所**（glass の形以外の command を記録する前に必ず `kwl_batch_flush`）: `kwl_compose_surface_quad`（窓の client の絵）、`vkCmdSetScissor` を呼ぶ所（compose.c・backdrop.c・heads.c）、render pass の終わり（主の pass、scene の capture の前、backdrop の begin・end、layer の cache の pass、heads の pass）、cursor（`compose_cursor`）、shot の記録の前。compositor の中の `vkCmd*` の全部の所（compose.c 11、backdrop.c 8、heads.c 8、scene.c 4、shm.c 4、shot.c 4、cursor-plane.c 6、import.c 2、shell.c 2、glass.c 4）を一覧にして、主の pass の中で記録する物の前に flush を置く。flush を忘れた所は順序が崩れるので、debug の build（`KWL_BATCH_CHECK`）で「束が空でない時に別の vkCmd を記録した」を検出して log する（実装の段で、全部の呼び出しを wrapper に通す）。
- 頂点の buffer: host-visible・coherent の VkBuffer を 2 つ（主のコマ用と layer の cache の submit 用、別の submit が同時に飛ぶため）、それぞれ 4096 形（1.7MiB）。コマの始めに先頭へ戻す（前のコマは完了してから次を記録するので上書きしない）。溢れたらそのコマの残りは今の 1 形 1 draw の経路で描き、`KWL BATCH overflow` を 1 度出し、次の確保で倍にする（上限 16384 形）。
- **fallback**: 束の pipeline の作成が失敗した device（compiler が扱えないなど）は `KWL BATCH unavailable` を出して今の経路（1 形 1 draw）を使う。

### 2.3 App Home を種類ごとの順に描く

- 束は連続した同じ鍵の形しか結べないので、Home の icon を「icon ごとに光・穴・tile・映り込み・名前・印」から「全部の icon の光 → 全部の穴 → 全部の tile と映り込み → 全部の名前と印」の順に変える。
- 絵が今と同じである理由: 違う icon の形どうしは、光どうし（同じ白の over なので順に依らない）を除いて重ならない（光は中心から ±73.6px、隣の tile・映り込みは ±108px から、名前は ±68px で隣の光の端 +73.6 と離れる。縦は光が床 −26〜+34px、名前の上端は床 +30 前後で同じ icon の中だけ）。同じ icon の中の順は保つ。
- 束にした後の Home の draw（見積もり）: 床と時計（solid と text で pipeline が違うので 2）、光 1、穴 1（壁紙の set）、tile と映り込み 1（tiles の set）、名前・印・点 2（text と solid）、選択の ring 0〜1 → 約 8〜9。舞台 2 は別。
- 任意（計測の後）: shadow・solid・ring・text の 4 つを 1 つの「軽い」variant（mode を flat で選び、if-conversion で 4 つを計算、atlas を 1 回 sample）にすると、色だけの形と文字が同じ束になり Home は約 4〜5 draw。Pi では fragment の計算が増えるので、全画面の形（舞台の光・layer の影）は今の mode ごとの pipeline のまま（面積で選ぶ）。Pi の `v3d: gpu` の qfrag の比較で採るか決める。

### 2.4 pipeline と descriptor の変更の数

- 今: 形ごとに pipeline・set の bind（Home の 1 コマで約 440 回ずつ）。
- 束の後: 束ごとに 1 回ずつ（Home 約 8〜9、desktop の 1 コマは窓の client の絵ごとに束が切れるので窓の数の 2〜3 倍程度）。bind が同じ物の連続は記録しない（最後に bind した pipeline・set を覚える）。

## i915・Venus への影響

- 変わるのは compositor の記録だけ。クロスフェードは i915・Venus でも同じ経路（全部の device で絵を使う。device ごとの分岐を作らない）。見た目の変化は 1.7 と同じ。
- 束ね: i915 の native compiler に新しい shader（flat の varying、6 個の attribute）を通す。host の compiler 試験で確かめ、作れない時は fallback。Venus は host の Mesa で、draw と bind の数が減る分の encode が減る。
- 5330（i915）では今も速いので、目標は「見た目が同じ（2 LSB 以内）で遅くならない」。

## 記憶の量

| 物 | 1920×1080 | 3840×2160（i915 の 4K） |
| --- | --- | --- |
| Home の絵 | 7.9MiB | 31.6MiB |
| desktop の絵 | 7.9MiB | 31.6MiB |
| Pi の UIF の snapshot（sample する時の変換の写し、kernel の 128MiB の枠の中） | 2 × 7.9MiB | — |
| 頂点の buffer（2 つ、4096 形） | 3.4MiB | 3.4MiB |
| 合計 | 約 35MiB（Pi） | 約 67MiB |

- Pi の Vulkan の予算は 512MiB（`BCM2711_VULKAN_MEMORY_BUDGET`）で、この boot の最大は 191MiB。足しても 230MiB 程度。

## Pi の見積もり（見積もり、実測は p002・p003）

| | 今（place2） | p002 の後（クロスフェード） | p003 の後（束ね） |
| --- | --- | --- | --- |
| 開閉のアニメの 1 コマの draw | 約 460〜916 | 舞台 2＋絵 2〜4＋影 1＋bar（約 40〜90） | 約 15 |
| 記録（CPU） | 17〜64ms | 3〜6ms | 1ms 程度 |
| kernel の準備 | 2.3〜10ms | 1ms 未満 | 1ms 未満 |
| GPU（fill: 全画面 約 4 枚） | 8〜26ms | 10〜16ms | 10〜16ms |
| コマの間隔（1 コマずつ＋vblank） | 75〜200ms | 25〜40ms | 20〜35ms |
| 開く 280ms の間のコマ | 2〜5 | 7〜11 | 8〜14 |
| 開いた最初のコマ | 今と同じ | 今と同じ（desktop の絵を描く） | 今の 1/5 程度 |

- 60fps の残りは GPU の fill（舞台の全画面の glass と光、2 枚の絵）。p002 の実測の後、舞台を 1 枚の不透明の絵にする（glass と光の 2 枚の全画面の計算を 1 回の sample に）か決める。記憶 +7.9MiB。

## 段

| 段 | 内容 | 確かめ |
| --- | --- | --- |
| p002 段 0 | 計測: `--log-frames` に `KWL DRAWS frame= shapes= draws= pipelines= sets= home= layer= bar= other=`（`glass_shape_draw` と `kwl_compose_surface_quad` で数える、section は呼び出し元が印を付ける） | build、Pi の log で今の種類ごとの数を記録 |
| p002 段 1 | layer の cache（画像・pass・descriptor・作る・捨てる）と Home の絵（全体の描き直し、状態の鍵、後ろの submit） | host（鍵と範囲の試験）、build |
| p002 段 2 | 開閉のアニメを絵で（desktop の絵、1.5 の表、起動の時の tile）、cell・時計の帯の描き直し、hover の probe の分離、時間差（ユーザーの判断） | host、Pi、T1 |
| p003 段 1 | 束の shader・pipeline・頂点の buffer・flush と検出・fallback | host（頂点の一致、flush の規則）、shader の host 試験 |
| p003 段 2 | Home を種類ごとの順に、任意の「軽い」variant の計測 | Pi、T1 |

## 試験の計画

- **host**:
  - build: rpi4（arm64）と amd64 の compositor、warning 0。
  - `plan/ws211/tests/layer-key-host-test.c`（新規）: 状態の鍵の変化ごとに描き直す範囲（全体・cell・時計の帯・なし）が 1.2 の表どおり、検索・ページめくりで絵を使わない。
  - `plan/ws211/tests/batch-host-test.c`（新規）: 形から作る頂点の値が、今の push constant の経路の値（NDC の角、uv、box、色の dark の変換、layer の変換、head の差し引き）と一致、鍵（pipeline・set・scissor、set を sample しない mode）での切れ目、flush の後の順、溢れの fallback。
  - shader: `plan/ws141/tests/shader-host-test.sh` に `panel-batch` の 2 つと 7 つの variant を足し、bcm2711 の compiler で通す。i915 の compiler の host 試験（`src/drivers/gpu/i915/tests/render/`）にも通す。
- **Pi（実機、ユーザーの目と P3 の log の読み）**: `KWL DRAWS` の数、`KWL LAT` で開く 280ms・閉じる 240ms の間のコマの数と間隔（`plan/ws141/tests/kwl-latency.py`）、dmesg の perf（draws・prep・tfu・snap）。ユーザーの確認: 開閉が滑らか、最初に開く時に待たない、hover の光・時計の更新・動作中の印・ページ・言語・外観・壁紙の変更の後の絵が正しい、起動の時の icon の拡大、検索・ページめくりが今どおり。
- **T1（QEMU の Venus、実機の i915）**: 開いた Home と desktop の静止した絵の screenshot（shot.c）を今の build と比べて 2 LSB 以内、開閉のアニメが error 無く終わる（`KWL VULKAN_ERROR` 無し）、`KWL BATCH unavailable` が出ない。
- 受け入れの目標（仮、ユーザーと確定）: p002 は Pi で開く 280ms の間に 7 コマ以上（最初のコマを除く間隔の中央値 40ms 以下）・最初のコマは今の 1.2 倍以内・アニメのコマの draw は bar を除き 10 以下。p003 は Home の絵の全体の描き直しが 20 draw 以下、desktop の 1 コマの draw が今の 1/5 以下、Pi の記録が 1 コマ 5ms 以下。i915・Venus は静止した絵が 2 LSB 以内。

## 未決（ユーザー・Q1 の判断）

1. icon の時間差の演出: (a) やめる（推奨）か (b) 行ごと（1.6）。
2. 1.7 の見た目の変化（layer が 1 枚で薄くなる、アニメの間の窓の中身の停止）を受け入れるか。
3. 舞台を不透明の 1 枚の絵にするか（p002 の実測の後）。
4. 「軽い」variant（2.3 の任意）を採るか（p003 の Pi の計測の後）。

## 範囲外（必要なら WS141 へ）

- Pi の driver の instance の rate と DrawIndexed（頂点を 6 倍に書く今の形を 1 倍に）、描いた画像を UIF で直接書く（sample の変換を無くす）、render の書き込みを write ring に記して部分の変換にする。どれも WS211 の目標には要らない。
