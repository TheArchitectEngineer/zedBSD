<!-- awesome-plan project=zedbsd record=ws211-p003 -->

# ws211-p003: draw の束ね（App Home・デスクトップ・bar の 1 コマの draw を減らす）

Parent: [WS211](../ws.md)
Status: planning（設計の第 1 版、2026-10-12 P3。review の前で、実装はしない）
Phase disposition: normal
Queue: P3（2026-10-12 ユーザー「WS211 p003, WS141 (b)、さらに上記アプリの改善をやりましょう。」、Q1 の割り当て）

## 由来と前提

- 設計の元は [p001 の設計 (2)](../phase001/phase.md#設計-2-draw-の束ねp003) です。review-1〜4 の H5・H6・N11・N12・C5 を、この版で取り込みます。
- 範囲は compositor（`userland/desktop/wayland/`）の中だけです。kernel・libvulkan・UAPI・HAL は変えません。
- p002（クロスフェード）は main に入っています（4c3a56cf9）。開閉のアニメのコマは絵を使い、1 コマ数十 draw です。p003 で軽くするのは、残りの次のコマです。
  - Home が開いて止まっている間（hover・ページ・検索）。
  - Home の絵を焼くコマ。
  - デスクトップ・bar のコマ。

## 実測（place6 の boot、2026-10-12、`KWL DRAWS`、662 コマ、log は scratchpad/p3g3/b6）

| 場面 | draws / コマ |
| --- | --- |
| 全部のコマの中央値 | 78 |
| Home が開いて止まっている全体のコマ | 459〜460（home 437〜438、bar 22） |
| Home の絵を焼く | 411〜413 |
| layer（デスクトップ）の中央値 / 最大 | 35 / 137 |
| bar の中央値 / 最大 | 46 / 90 |
| 窓の右上の hover の部分のコマ | 27〜68 |

- pipeline が前の draw と違う割合は 0.28、descriptor set が違う割合は 0.23 です。今は毎 draw で両方を bind しています。
- Pi の kernel の費用は、draw ごとの準備（shader の record、uniform の upload、texture の状態、頂点の詰め直し）です。今までの perf では prep が 1 draw 約 5〜20µs、compositor の記録（libvulkan の encode）が 1 draw 約 0.05ms でした。

## 段

### 段 1: 変わった時だけ bind する（shader は変えない、危険が小さい）

- `glass_shape_draw` と `kwl_compose_surface_quad` は、最後に bind した pipeline・descriptor set・pipeline layout を `compose->bound_*` に覚えます。同じなら `vkCmdBindPipeline`・`vkCmdBindDescriptorSets` を記録しません。push constant と draw は今どおり毎回記録します。
- 覚えた値を消す（次は必ず bind する）所:
  - command buffer の記録の始め。
  - 全部の `vkCmdBeginRenderPass`（compose.c・backdrop.c・heads.c・layer-cache.c）。
  - この 2 つの関数の外で `vkCmdBindPipeline`・`vkCmdBindDescriptorSets`・`vkCmdBindVertexBuffers` を記録する所（backdrop.c・heads.c・cursor-plane.c・shot.c など。一覧は実装の時に grep で全部を挙げる）。
  - Vulkan では、同じ command buffer の中なら bind は render pass をまたいで残ります。それでも、pass の始めに消す方を安全側として採ります。
- 効果の見積もり: bind の command が約 7 割減ります（実測の割合から）。記録の CPU と kernel の decode が少し減ります。draw の数は減りません。
- `KWL DRAWS` の `pipelines=`・`sets=` は、今の「違った回数」の数え方のまま使えます。

### 段 2: 束ね（p001 の 2.1〜2.3 に、review の指摘を入れた形）

- **束にするのは、面積の小さな形だけ**です（H6）。glass・blur の mode と、quad の面積（layer の変換の後の画素）が閾値を超える形は、今の push constant の pipeline のまま 1 形 1 draw にします。閾値の初めの値は 128×128 です。これは Home の名前の glyph・床の片・時計の字・穴の壁紙（72×72）・tile（72×72）が束に入り、光（約 147×68）が入る大きさです。段 2 の Pi の計測（`v3d: gpu` の qfrag と render の前後）で決め直します。
- **頂点は mode ごとに小さくします**（H5、p001 の 2.1 の表）。text は 8 float、solid・ring・shadow は 12 float、image は 11 float です。opacity は CPU で色（または alpha）に掛けます。
- **1 つの束の大きさの上限**（N12）: kernel は、1 draw の頂点の詰め直しが 16KiB（`NATIVE_UPLOAD_LARGEST`）を超えると、別の storage を確保します（確保・0 埋め・clean の費用）。そのため、束の頂点の詰め直しの大きさ（attribute の float × 4 byte × 6 頂点 × 形の数）が 16KiB 以下になるように束を切ります。text は 85 形、solid 系は 56 形、image は 62 形までです。
- **screen.xy**（N11）: 束の pipeline layout の push constant（8 byte）は、pass の始め、head の pass の始め、出力の大きさが変わった時、別の layout の後に押し直します。
- **頂点の buffer は 1 つ**（C5）: present しないコマ（Home の絵）と主のコマは同時には飛ばないので、1 つで足ります。host-visible の VkBuffer を 1MiB から始め、溢れたら次のコマで倍にします（上限 8MiB）。頂点は cached の配列で組み、束の flush の時にまとめて写します。
- **flush と検出**: p001 の 2.2 のとおりです（全部の `vkCmd*` の所の前で flush、`KWL_BATCH_CHECK` の build で順の崩れを log、`KWL_BATCH=0` と束の pipeline を作れない時の fallback）。段 1 の「覚えた bind」も、束の flush と同じ所で消します。
- **Home を種類ごとの順に**: p001 の 2.3 のとおりです（違う icon の形どうしが重ならないことは寸法で示してあります）。

### 段 2 の見積もり（Home が開いて止まっている全体のコマ、今 438 draw）

| 種類 | 今 | 束の後 |
| --- | --- | --- |
| 床の片（solid、192） | 192 | 4（56 形ずつ） |
| 名前と時計の glyph（text、約 108） | 108 | 2 |
| 穴（image・壁紙の set、12） | 12 | 1 |
| tile と映り込み（image・tiles の set、12＋96） | 108 | 2 |
| 光（shadow、面積が閾値を超える、12） | 12 | 12 |
| 動作中の印・点（solid） | 2〜14 | 1 |
| 舞台（glass・shadow、全画面） | 2 | 2 |
| 合計 | 約 438 | 約 24 |

- デスクトップと bar は、窓の client の絵（`kwl_compose_surface_quad`）と glass で束が切れます。bar の 46 は、glyph と icon が束になって約 10〜15 になる見込みです。

## i915・Venus

- 段 1 は bind の省略だけで、見た目は変わりません。
- 段 2 の新しい shader は、i915 の native compiler（flat・attribute 16・varying 16）と、bcm2711 の compiler の host 試験に通します。作れない時は fallback を使います。
- 目標は、静止した絵が `KWL_BATCH=0` と比べて 2 LSB 以内、遅くならないことです。

## 試験

- host:
  - rpi4・amd64・Linux・FreeBSD の build で warning 0。
  - `plan/ws211/tests/batch-host-test.c`: 頂点の値が push constant の経路と一致すること。束の切れ目（鍵・面積・16KiB・scissor）、flush の後の順、溢れの fallback、段 1 の bind の省略と消す所。
  - shader の host 試験（bcm2711・i915）。
- Pi: 段 1 と段 2 のそれぞれの前後で、次を比べます。
  - `KWL DRAWS`（draws・pipelines・sets）。
  - compositor の記録の時間（`KWL LAT acquired→recorded`）。
  - kernel の perf（v3d.perf=1 の boot で prep・rec・alloc・exec・qfrag）。
  - Home が開いて止まっている間の hover の input→shown、デスクトップの窓の右上の hover の input→shown。
- T1（Venus・i915）: `KWL_BATCH=0` との静止した絵の比較、`KWL VULKAN_ERROR` と `KWL BATCH unavailable` が出ないこと。

## 未決

1. 面積の閾値（128×128 から始め、qfrag で決め直す）。
2. 束にする mode（詰め直しの費用が勝つ mode は束にしない）。
3. 段 1 だけで Pi の記録の時間がどれだけ減るか。その結果を見て、段 2 を先に進めるか決めます。
