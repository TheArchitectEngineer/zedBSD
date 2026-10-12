<!-- awesome-plan project=zedbsd record=ws211-p001 -->

# ws211-p001: 調査と設計（draw の数え、クロスフェードの絵の持ち方と描き直し、draw の束ね、Pi の見積もり、試験）

Parent: [WS211](../ws.md)
Status: planning（設計の第 3 版。[review-2](review-2.md) の N1〜N16 を反映。段 0・段 0b は実装済み（agent/p3 5a65be827・e08c7d519、Q1 の N3 の直しは main 844b3e2a1）。段 1（Home の絵）は review-2 の可否どおり N5・N8 の直しで先に進める。クロスフェード（段 2）は第 3 版の re-review の後）
Phase disposition: normal
Queue: P3（Q1 が割り当て、2026-10-12）

## 由来

- [WS211 の由来](../ws.md#由来): 2026-10-12 ユーザー。App Home を開くアニメが 2 コマしか出ない。「デスクトップ1枚とHome1枚でクロスフェード」「App Homeのアイコンは、遷移のスムーズさのために常に保持するしかない」。Q1 の提案（(1) クロスフェード、(2) draw の束ね）をユーザーが承認。
- 範囲は compositor（`userland/desktop/wayland/`）の中だけ。kernel・libvulkan・UAPI・HAL は変えない。
- 第 1 版（7ef76d086）への [review-1](review-1.md) の結論は「実装は不可」。第 2 版で次のユーザーと Q1 の判断（2026-10-12、Q1 経由）を入れた。
  - ユーザー: icon の時間差の演出はやめる。
  - ユーザー: クロスフェードの間の次の見た目の変化は受け入れる。desktop が 1 枚の絵として薄くなる、窓の中身がアニメの間止まる、拡大縮小で少し柔らかくなる、Home の content が 1 枚の絵として薄くなる、画面の外へはみ出した窓と影が縁で切れる、窓の glass の blur の位置が layer と一緒に拡大縮小する。
  - ユーザー（M5、部分の描き直しで kernel が CPU で小さな変換と消去をする件）:「一番高速な方法で。PIで少しでも動作にひっかかりがあるなら許容しない。ただしSMPで問題なくなれば許容する。それを総合的に評価し、実装は何回か作り替えていいので、最適化する。」→ 方式は Pi の実測で選ぶ（下の 1.4）。
  - Q1: M3 は「アニメの間の窓だけを絵の上に直に描く」か、描き直しの安い方を採る。i915・Venus も同じ経路にして見た目を揃える。M8 は p002（クロスフェード）を先に行う。

## review-1 への対応

| 指摘 | 第 2 版での扱い |
| --- | --- |
| H1 部分の描き直しで古い画素が消えない | 1.3: GPU で消す。blend を切った透明の quad（新しい pipeline 1 つ）で矩形を (0,0,0,0) にし、矩形と交わる Home の形を全部描き直す。矩形は変わった icon の形が届く範囲の和。 |
| H2 後ろの submit が未転写の glyph や壁紙を焼き込む | 1.5: layer の command buffer の最初で `kwl_host_images_record` を行う。焼いた cell は submit が終わるまで glyph の cache から追い出さない。鍵から atlas の generation を外し、代わりに「焼いた時に字が欠けた」印と壁紙の `version` を入れる。 |
| H3 drag の間の desktop の絵の条件が毎コマ真 | 1.6: 専用の印 `layer_desktop_changed` を設ける。表示中の desktop の窓（cursor を除く）の commit、map・unmap・前面化・移動・大きさ・title・desktop の切り替え・壁紙・外観・accent の時だけ立てる。 |
| H4 見積もりの律速の取り違え、計測を先に | 段 0 で計測し、受け入れの数値は計測の後に決める。scene の取り込みの扱い（0.3）、アニメの時計の起点（1.7）、desktop の絵を閉じている間に先に撮る（1.6）。 |
| H5 束ねると頂点の詰め直しが 9 倍 | 2.1: mode ごとに頂点を小さくする（text 8・solid 系 12・image 11 float）。束にするのは小さな形だけにし、段 1 で kernel の `rec`・`prep` を前後で比べる。kernel でまとめて写す改善は WS141 の範囲。 |
| H6 flat の varying は Pi の fragment で安くない | 2.1: 面積の大きい形と glass・blur は今の push constant の pipeline のままにする。閾値は `v3d: gpu` の qfrag で決める。 |
| M1 vertex buffer の bind と push | 2.2 |
| M2 見た目の変化 | 由来とユーザーの判断（受け入れ済み） |
| M3 起動した app の窓が閉じる間に出ない | 1.6: `server->anim` の窓は desktop の絵から除き、絵の上に直に描く。i915・Venus も同じ。 |
| M4 主の pass の外で描く cache の pass の状態 | 1.4 |
| M5 「CPU で画素を触らない」は事実と違う | 1.4: 事実を書き直した。方式はユーザーの判断どおり Pi の実測で選ぶ。 |
| M6 鍵 | 1.2 |
| M7 試験 | 試験の計画: `--no-layer-cache`・`KWL_BATCH=0` と参照の絵との比較 |
| M8 順序 | p002（クロスフェード）を先に。検索とページの drag は p003 まで今の経路のまま。 |
| M9 desktop の絵を最初に開く時に作ると止まる | 1.1: 2 枚とも起動の時に作る。閉じ始めは印が無ければ使い回す。 |
| M10 Makefile | 段: `Makefile.linux`・`Makefile.freebsd` にも足し、その build も確かめる。 |
| L1〜L8 | 各所（L1 点はページが 2 つ以上の時だけ、L2 時計の帯は閉じている間は描かず開く時に、L3 WAR の依存、L4 拡大した tile の角、L5 影は縁の帯、L6 log の意味、L7 cached で組んでから写す、L8 アニメのコマは比べない） |
| 実測値の log が plan に無い | [pi-20261012-place2-session.log](pi-20261012-place2-session.log)（Home の開閉の前後の `KWL LAT`・`KWL HOME`・`KWL PERF`）と [pi-20261012-place2-dmesg.log](pi-20261012-place2-dmesg.log)（`v3d: perf`・`requests`・`gpu`・`tfu`・`memory`、`bcm2711: flip perf`）を置いた。 |

## 第 3 版の差分（review-2 への対応、2026-10-12 P3）

この節は、下の第 2 版の本文と食い違う所を置き換える。

| 指摘 | 第 3 版 |
| --- | --- |
| N1 段 0b のままだと全部のコマが全画面 | 実装済み（e08c7d519）。`capture = scene.buffer && !cursor_plane.shown` を 1 つ求め、region を NULL にする所・reuse・取り込みの 3 か所をこの値で決める。capture が 0 のコマは `scene.valid = 0` にする（plane が閉じた最初のコマは全体を描く）。`KWL SCENE` に `capture=` を出す。試験: Pi で段 0b の前後に `KWL DAMAGE` の部分のコマが残ること。 |
| N2 後ろの submit が client の buffer・host の画像の寿命と競合を守らない | 下の 3.1: 焼き直しは「present しない主のコマ」として出す。 |
| N3 アニメの各コマの後の最大 50ms の frame pacing の待ち | Q1 が main で直した（844b3e2a1: 自動のアニメの間は待たない）。段 0 の `KWL LAT pace frame= awaiting= waited_ms= wait_ms=` で効果を確かめる。 |
| N4 時計の起点を submit にすると進みが戻る | 3.4 |
| N5 cache の pass の状態 | 3.2 |
| N6 `layer_desktop_changed` の漏れ | 3.3: 事象の印をやめ、毎コマ CPU で desktop の層の署名を作って比べる。 |
| N7 後ろの焼き直しが引っかかる可能性 | 3.1 の条件（空き 100ms、回数の上限）と、段 1・2 の計測の項目（焼き直しの間の入力のコマの input→shown、cursor の at、焼き直しの回数と GPU の時間）。 |
| N8 字が欠けた印の無限ループ | 3.1: 焼き直しを主のコマにするので、glyph の cell を守る特別な期間をやめる。「字が欠けた」印も鍵から外す。 |
| N9 開く最初のコマの待ちが延びる | 3.4 |
| N10 焼いた後に始まった anim の窓が二重 | 3.3 |
| N11 head の pass の screen.xy | 2.2 に足す: pass ごとと、大きさが変わるたびに `screen.xy` を押し直す。 |
| N12 kernel は 16KiB を超える頂点の詰め直しを別に確保する | 2.2 に足す: 1 つの束の頂点の詰め直し（kernel 側で shader の入力 × 頂点 × 4 byte）が 16KiB を超えないように束を切る（solid 系 1 形 288B なら 56 形まで）。 |
| N13 影を縁の帯だけにすると見た目が変わる | 1.7 の L5 をやめ、全面の影 1 つを残す（今どおり）。 |
| N14 2 枚を起動の時に作る失敗の扱い | 3.2 |
| N15 reuse=0 と prep の数を plan の log で確かめられない | `pi-20261012-place2-session.log` に `KWL SCENE` を足し直した（下の 3.5）。prep は `pi-20261012-place2-dmesg.log` の `v3d: perf` の行。 |
| N16 試験の場面 | 3.5 |

### 3.1 焼き直しは present しない主のコマ（N2・N7・N8）

- Home の絵・desktop の絵の焼き直しは、主のコマと同じ command buffer（`compose->command`）・fence・`copy_command` で出す。swapchain の image は acquire せず、present もしない。印 `compose->frame_bake = 1` を立てる。
- 記録の中身は次の順にする。
  1. `kwl_import_layouts_record`（新しい client の画像の layout の移行。今の主のコマと同じ所）。
  2. 焼く pass（3.2）。
- submit の時の扱いは、普通のコマと次の点だけ違う。
  - 主のコマと同じく、`copy_command` を先に積み（`kwl_host_images_record`）、`in_flight = 1` にする。fence の fd を出し、event loop が完了を知る。
  - `compose_hold` は buffer の hold だけを行う（desktop の絵が sample する窓の buffer と popup の buffer）。frame callback は取らない（client には新しいコマを見せていない）。
  - 完了（`kwl_compose_complete`）では、`frame_bake` の時は `KWL LAT shown` を出さない。`server->frame` は進めず、`frame_done_ms`・`frame_wait_ms`（frame pacing）も変えない。shot の完了も呼ばない。代わりに `KWL LAYER baked what=home|desktop draws= ms=` を出す。
  これで client の buffer の寿命、shm の staging の上書き、host の画像の写し、layout の移行は、全部いまの主のコマの仕組みに乗る。glyph の cell の競合も、普通のコマと同じ扱いになる（新しい競合は無い）。そのため第 2 版の「cell を守る期間」と「字が欠けた」印はやめる（N8）。
- 出す条件（N7）: Home が閉じている、主のコマが in_flight でない、絵が無効、最後のコマの完了から 100ms 以上空いた、入力（pointer・key・touch）が最後に来てから 100ms 以上空いた、焼き直しは 1 秒に 2 回まで。条件を待つ間は timer（event loop の poll の timeout）で次を見る。
- 焼き直しのコマが in_flight の間に入力が来たら、次の主のコマは焼き直しの完了を待つ。その待ちは段 1・2 で測る（`KWL LAYER baked ms=`、入力のコマの input→shown、cursor の present の at）。引っかかりが出たら焼き直しを細切れにする（矩形ごと、Home の全体は行ごとに分ける）。

### 3.2 焼く pass の状態（N5・N14）

- 焼く pass の中で設定する物（主の pass の外なので、全部を自分で設定する）:
  - viewport（焼く画像の大きさ）。
  - scissor と `compose->scissor_now`。
  - binding 0 の `corners` の bind（compose.c:2351 と同じ）。
  - `view_x`・`view_y`・`view_width`・`view_height` を 0。
  - `backdrop_set = VK_NULL_HANDLE`、`backdrop_recording = 0`。`framebuffer_now = VK_NULL_HANDLE`（backdrop.c:82 で backdrop を作らない防御）。
  - `hover_recording` を保存して 0 にする。`draw_section` は `KWL_SECTION_OTHER`。
- `keep_colours`: Home の絵は 1（自分の色）、desktop の絵は 0（外観の色）。
- Home の絵の中身は、`kwl_home_draw` から舞台と layer の変換を外した、content だけを描く関数 `kwl_home_draw_content(server, command)` に分ける。`kwl_home_draw` は今どおり（舞台、layer の変換、`kwl_home_draw_content`）で、絵を使わない場面で使う。`kwl_home_draw_content` は `layer_on` を触らない。
- desktop の絵は、`home > 0` の時の規則で描く（`draw_desktop_layer(..., home, ...)` に Home の値を渡す）。こうすると `draw_backdrop`（shell.c:10149、10158）を通らず、backdrop の pass が主の pass を閉じて開き直すこと（backdrop.c:89・126）が起きない。layer の変数は恒等にする。
- 起動の時の作成の失敗（N14）:
  - 2 枚の片方だけ作れた時は、両方を捨てて `KWL LAYER unavailable` にし、今の描き方に戻る（片方だけの経路は作らない）。
  - 出力を開き直した時に作り直す。
  - descriptor の set は `KWL_DESCRIPTOR_MAX`（512）の pool から 2 つ取る。取れなければ unavailable。
  - Pi では scene の buffer（8MiB）を段 0b の後も持ち続ける。plane がある間はそれを解放して、絵の 2 枚の記憶にあてる案は、段 1 の計測の後に決める。

### 3.3 desktop の絵の署名（N6・N10）

- 事象ごとの印をやめる。毎コマ、CPU で desktop の層の署名（64 bit の hash）を作り、焼いた時の署名と比べる。違えば絵は古い。
- 署名に入れる物:
  - 表示中の desktop の窓の並び。窓ごとに id、位置、大きさ、`current` の buffer の版（commit ごとに進む数）、title、最小化と dock の状態、飾りの hover と押下の状態、`server->anim` の窓かどうか。
  - desktop の icon の世代、壁紙の `version`、暗い外観、accent、出力の大きさ、言語。
- 署名は CPU の数え上げだけで、窓の数に比例する（数十）。
- N10: desktop の絵を焼いた後に `server->anim` が始まった窓は、署名が変わるので焼き直しの対象になる。ただし自動のアニメ（280ms・240ms）の間は焼き直さない（1.6）。そのため、アニメの間に anim が始まった時は、そのコマから今の経路（絵を使わない）に戻る。二重に描かない。

### 3.4 アニメの時計と最初のコマ（N4・N9）

- 時計の起点（N4）: `home_start_ms` を submit の時刻に置き直すのはやめる。代わりに、開閉が言われた後の最初のコマの記録の開始の時刻に置く。その最初のコマ自身は、progress を「小さな正の値」（開く時 0.02、閉じる時 0.98）で描く。0 で描くと Home を描かないので、そうしない。progress は単調にする（前のコマの値より戻らない）。
- 最初のコマ（N9）: 開く最初のコマで 2 枚とも焼き直しが要る時は、そのコマは今の経路で描く（絵を使わない）。焼き直しは次の submit（present しない主のコマ、3.1）で行い、それ以後のアニメのコマが絵を使う。`KWL FIRST_FRAME ms` を前後で比べる。

### 3.5 試験の追加（N15・N16）

- 段 0b の前後: Pi の `--log-frames` で、`KWL DAMAGE`（部分のコマ）が段 0b の後も出ること、`KWL SCENE capture=0` の間に `region` が NULL にならないこと（部分のコマの `KWL DRAWS draws=` が全体より小さい）。
- 焼き直しの最中の競合（N2）: 焼き直しが in_flight の間に、窓の大きさを変える、窓を閉じる、shm の client が描き続ける。いずれも VULKAN_ERROR・壊れた画像・落ちが無いこと。Venus（T1）では検証の layer を有効にして validation error が無いこと。
- 段 0 の pace: アニメのコマの `waited_ms` が 0 に近いこと（N3 の直しの効果）。
- plan の log: `pi-20261012-place2-session.log` に `KWL SCENE` の行を足して置き直した（266 コマ全部 reuse=0）。

## 今の描き方（source と Pi の log で確かめた事実）

### 0.1 1 つの形が 1 つの draw

- glass の look の形は全部 `glass_shape_draw`（glass.c）を通る。1 つの形ごとに `vkCmdBindPipeline`・`vkCmdBindDescriptorSets`・`vkCmdPushConstants`（96 byte）・`vkCmdDraw(6)` の 4 command を記録する。頂点は共通の `corners`（vec2 の 6 頂点、binding 0、compose.c:2351、heads.c:1265）で、他は全部 push constant。pipeline は mode ごとに 7 つと、mode を分けない 1 つ。
- 窓の client の画像は `kwl_compose_surface_quad`（compose.c）で描く。別の pipeline layout（quad.vert/frag）で 1 draw。
- 1 コマは 1 つの command buffer で作る。前のコマの fence が signal してから次を記録する（`kwl_compose_complete`）。host の画像（atlas・壁紙・cursor）は staging に書き、主のコマの `copy_command` の中でだけ写す（compose.c の `compose_submit`、shm.c の `kwl_host_images_record`）。

### 0.2 App Home の 1 コマの draw（source から数えた。page 1 の 12 個の icon が全部 picture 付き、2026-10-12 の Pi の apps.conf）

| 種類 | 式 | 数 |
| --- | --- | --- |
| 舞台（全画面の黒い glass と上の光） | 2 | 2 |
| 床 | 4 本の線 × 24 片、2 行 | 192 |
| 時計（時刻 5 字と日付、例 18 字） | 1 字 1 draw | 約 23 |
| icon（光 1、穴の壁紙 1、tile 1、映り込み 8 片） | 11 × 12 | 132 |
| icon の名前の glyph（空白は draw しない） | 1 字 1 draw | 85 |
| 動作中の印 | 動作中の app ごと | 0〜12 |
| hover の tile の白 | hover 中 1 | 0〜1 |
| ページの点（ページが 2 つ以上の時、home.c:681） | ページごと | 2 |
| 合計 | | 約 436〜449 |

- 開閉のアニメの間は、これに desktop の layer と system bar の draw が加わる。WS211 の由来の 458〜916 draw はこの幅。種類ごとの正確な数は段 0 の `KWL DRAWS` で数える。

### 0.3 Pi の数（place2 の boot、2026-10-12。log は上の 2 つの file）

- Home の開閉の間のコマ: 記録（`acquired`→`recorded`）17〜64ms、submit→shown 53〜171ms。開く 280ms の間に 2〜5 コマ。
- kernel の perf（submission あたり）: `prep` 2.3〜10ms、`exec` 9〜35ms。flip は `wait` が平均 10〜11ms。
- submit→shown のうち prep＋exec で説明できるのは 11〜45ms で、40〜120ms が未説明（H4）。候補は 4 つ。client の GPU job の後ろで待つ、TFU の変換、scene の取り込み、vblank。段 0 で分ける。
- **scene の取り込みは Pi では毎コマの無駄**: この boot の 266 コマは全部 `KWL SCENE reuse=0` だった。cursor が plane にあるので、scene を再利用するコマ（cursor だけが動くコマ）は出ない。それでも scene を作るコマごとに、主の pass を終え、全画面の image を buffer に写し、cursor のために LOAD の pass をもう一度開いて閉じている（compose.c:2381-2393）。
- Pi の driver の制約（`src/drivers/gpu/bcm2711/`）:
  - vertex input は `VK_VERTEX_INPUT_RATE_VERTEX` だけで、DrawIndexed は無い。attribute は R32〜R32G32B32A32_SFLOAT で 16 個まで。
  - kernel は draw ごとに、shader の入力の scalar を頂点ごとに 1 つずつ写す（`prepare_fetch`）。読み元は uncached。
  - varying は 32 word まで、flat を扱う。varying 1 つごとに fragment で数命令かかる（shader-lower.c:117-137）。分岐は if-conversion。
  - 描いた raster の画像を sample する時は UIF の snapshot に変換する。変わった箱が 256KiB 以下なら CPU が箱だけ変換し、それより大きい時や箱が分からない時は TFU で全体を変換する（native-storage.c）。部分の CLEAR は CPU で消してから tile を読む（vulkan-native-pass.c:494）。

## 段 0: 計測（p002 段 0、review-1 の許可で先に始めてよい）

- `--log-frames` に 1 コマ 1 行の `KWL DRAWS frame= shapes= draws= pipelines= sets= home= layer= bar= other=` を足す。数えるのは `glass_shape_draw` と `kwl_compose_surface_quad` で、section は呼び出し元が印を付ける。
- 同じコマの時刻: `KWL LAT` の既存の点に、scene の取り込みの前後（`capture`）と copy_command の数を足す。
- Pi で、Home の開閉・hover・Music の操作の間の次を集め、submit→shown の内訳を出す。
  - kernel の perf: `rec`・`tex`・`tfu`・`copy`・`xfer`・`bin`・`render`・`snap`・`upd`、`requests` の `hold_us`。
  - `bcm2711: flip perf`。
  - client の GPU job との前後（`requests` の wait と、同じ窓の client の pass の数）。
- **0.3 の scene の取り込み**（計測で費用を確かめてから入れる、p002 段 0b）: cursor が plane に出ている間（`compose->cursor_plane.shown`）は scene を取り込まず、LOAD の pass も開き直さない。`scene.valid = 0` にする。plane が閉じたら次のコマで取り込みを再開する。i915・Venus は cursor の plane を持たないので変わらない。
- 受け入れの数値（コマの数・間隔・draw の数）は、この計測の後に Q1 とユーザーで決める（H4）。

## 設計 (1) クロスフェード（p002）

### 1.1 画像（新しい file `layer-cache.c`）

- **Home の絵**: 出力（anchor）と同じ大きさ・同じ format（`compose->format`）の color の画像 1 枚。今の pipeline と render pass がそのまま使える。透明（0,0,0,0）に clear して Home の content を描く。content は時計・床・icon・名前・動作中の印・選択の ring・点で、舞台は含めない。layer の変換は無し（scale 1、位置 0、opacity 1）。合成は premultiplied（blend は color・alpha とも ONE / ONE_MINUS_SRC_ALPHA）なので、透明の画像に描いてから重ねても、直接描くのと 8 bit の丸め以外は同じになる。
- **desktop の絵**: 同じ大きさ・format の画像 1 枚（中身は 1.6）。
- 作り方は backdrop と同じ形にする（画像・memory・view・framebuffer・linear の sampler の descriptor set）。render pass は次の 2 つ。
  - clear: initialLayout UNDEFINED、finalLayout SHADER_READ_ONLY_OPTIMAL。
  - load: initialLayout・finalLayout とも SHADER_READ_ONLY_OPTIMAL。
  - external の依存は backdrop.c:262-275 と同じ。前の sample の後に書く WAR も入れる（L3）。
- **2 枚とも起動の時、出力を開いた直後に作る（M9）**。Pi では 8MiB の連続の確保になるので、初めて開く時に作ると止まり、断片化で失敗もし得る。作り直すのは、出力の大きさの変化・device の喪失・出力の close の後に出力を開いた時だけ。作れない時は `KWL LAYER unavailable reason=` を出し、今の描き方（毎コマ全部を描く）に戻る。

### 1.2 Home の絵の鍵（M6）

毎コマ、今の状態と焼いた時の状態を比べる（CPU の比較だけ）。

| 鍵 | 違った時に描き直す範囲 |
| --- | --- |
| app の一覧、ページの数、表示中のページ（落ち着いた後）、言語（`kl_tr` の結果）、accent（点の色、ページが 2 つ以上の時だけ）、壁紙の `kwl_import.version`、出力の大きさ、tile の ready、「焼いた時に字が欠けた」印 | 全体 |
| 動作中の印（app ごとの窓の有無の bit）、hover の icon、選択の ring | その icon の矩形（1.3） |
| 時計の文字列（`%H:%M` の結果と日付の文字列） | 時計の帯 |

- 外観（明暗）は鍵に入れない。Home は自分の色で描く（`keep_colours = 1`）。atlas の generation も入れない。glyph が初めて使われるたびに全体を描き直すことになるからである。
- 「字が欠けた」印: 焼いている間に `glass_glyph_of` が NULL を返した（cache から追い出せなかった・描けなかった）時に立てる。次の機会に全体を焼き直す。
- 時計の帯は、Home が閉じている間は毎分は描かない。開く時に文字列が違えば描く（L2）。
- 絵を使わず今の描き方のままにする場面: 検索中（query か preedit がある）、ページをめくる・drag する間（`home_page_position` が整数でない）。これは p003 まで続ける（M8）。

### 1.3 部分の描き直し（H1）

- 矩形: 変わった icon の形が届く範囲の和。範囲は、tile、光（中心 ±73.6px、床 −26〜+34px）、映り込み、名前（中心 ±68px、名前の baseline の下まで）、動作中の印、選択の ring（±5px）。hover が変わった時は、前と今の icon の 2 つの矩形にする。
- 消す: blend を切った新しい pipeline（`erase`、solid の shader で色 (0,0,0,0)、blend off）で、矩形を 1 draw で透明にする。GPU で消すので、Pi で部分の CLEAR を CPU がする経路（vulkan-native-pass.c:494）を通らない。
- 描き直す: scissor を矩形にして、Home の content を全部描く関数を呼ぶ。`glass_shape_draw` は `compose->scissor_now` の外の形を CPU で捨てる（glass.c:1706-1714）。そのため記録されるのは矩形と交わる形だけで、隣の icon の光の端も含まれる。
- 矩形は Home の絵の中で重ならないように切らない。和の矩形の中を全部消してから全部描き直すので、隣の形も正しく残る。

### 1.4 cache の pass の状態と Pi の変換（M4・M5）

- 主の pass の外で cache の pass を記録する時は、次を設定し、終わったら元に戻す。
  - `compose->scissor_now`（矩形か全体）と `vkCmdSetScissor`。
  - `view_x`・`view_y`・`view_width`・`view_height` を 0。
  - `layer_on = 0`、`keep_colours = 1`（Home の自分の色、home.c:585。1 にしないと暗い外観で色が変わる、glass.c:1743-1753）。
  - `backdrop_set = VK_NULL_HANDLE`、`backdrop_recording = 0`。
  - hover の probe の記録は止める（`hover_recording` を保存して 0 にする）。
- Home が見えている間に矩形や帯を描き直したら、`kwl_damage_rect` でその範囲を damage に足す。主の pass はその範囲だけを描き直す（舞台と Home の絵の quad）。
- **Pi の変換の方式は実測で選ぶ（ユーザーの判断）**。候補は 3 つ。
  - (i) renderArea を矩形にする。kernel は 256KiB 以下の箱を CPU で UIF に変換する。icon の矩形は約 220×200×4 = 172KiB。
  - (ii) renderArea を全体にする（scissor は矩形）。kernel は TFU で全体を変換する（1920×1080 で約 3.5ms と見積もる。V9 の 256×256 は 112µs）。
  - (iii) GPU で部分を変換する。kernel の変更なので WS141 に頼む。
  - 段 2 で (i) と (ii) を作り替えて比べる。測るのは、hover の出入りを続けた時の `KWL LAT`（hover のコマの間隔）、perf の `upd`・`tfu`・`xfer`、cursor の plane の present の input→at、ユーザーの目での引っかかり。引っかかりが無い最速を採る。どちらも引っかかるなら (iii) を WS141 に頼むか、SMP の後に評価し直す。
- 第 1 版の「CPU で画素を触らない」は誤りだった。(i) は CPU が箱を変換する。消去は (i)・(ii) とも GPU の erase で行う。

### 1.5 焼く時（後ろで、最初の 1 コマを待たない）と host の画像（H2）

- **Home が閉じている間の焼き直し**は、専用の command buffer と fence で別に submit する（`layer_command`、`layer_fence`）。出す条件は 3 つ。
  - 主のコマが in_flight でない。
  - 前の layer の submit が終わっている（`vkGetFenceStatus`）。
  - 最後のコマから 100ms 以上空いた（入力の途中に割り込まない）。
- 焼く前に、Home の文字列を全部 `glass_text_width` で測って glyph を揃える。この時に初めて使う glyph は staging に書かれる。
- layer の command buffer の中の順:
  1. `kwl_host_images_record`（それまでに書かれた staging を全部写す）。
  2. barrier（TRANSFER → FRAGMENT_SHADER）。
  3. cache の pass。
- 焼いた cell を守る: layer の submit が飛んでいる間（`layer_in_flight`）は、`glass_cache_glyph` が追い出す候補として、その submit で使った cell（`used` が焼き始めの `clock` 以上）を選ばない。他に空きが無ければ NULL を返す（その字は描かれず、呼んだ側の次のコマで描き直される）。ASCII は固定の領域なので追い出されない。
- 同じ queue なので、後のコマの sample は submit の順で後になる。layer の command buffer の最後の barrier で同期が付く。主のコマの `copy_command` は、layer が写した物（`written` が 0）を写し直さない。
- **Home が見えている間の描き直し**（hover・時計・動作中の印）は、そのコマの command buffer で主の pass の前に cache の pass を記録する。前のコマは完了してから次を記録するので、sample 中の絵を書き換えない。
- 開くと言われた時に絵が無効なら（起動の直後など）、開く最初のコマで焼く。

### 1.6 desktop の絵（H3・M3・M9）

- 中身: 壁紙（角の丸め無し）、`kwl_desktop_draw`、`draw_desktop_layer` の窓。layer の変数は恒等にする（`layer_on = 1`、x・y 0、scale 1、opacity 1）。窓の見せ方（`window_shown`）は今の Home の時の規則（表示中の desktop の窓だけ）。
- **`server->anim` の窓（起動の窓が icon から育つ `ANIM_LAUNCH` など）は絵から除く**。主の pass で desktop の絵の quad の後に、`window_layer` の変換付きで直に描く（M3、i915・Venus も同じ）。起動した窓は最前面なので、上に描いて順序は崩れない。
- **変化の印 `layer_desktop_changed`（H3）**: 次の時だけ立てる。
  - `kwl_damage_commit` で、表示中の desktop の窓とその subsurface（cursor の surface を除く）が commit した時。
  - 窓の map・unmap・前面化・移動・大きさの変化・title の変化。
  - desktop の切り替え。
  - 壁紙・外観・accent の変化。
  `server->dirty` と `scene.changed` は使わない。drag は dirty を立て（home.c:517）、scene.changed は hover の probe や cursor の commit でも立つ（damage.c:129-130、305-318）からである。
- 焼く時:
  - Home が閉じている間に印が立っていれば、1.5 と同じ後ろの submit で焼く（H4: 開く最初のコマを軽くする）。client が絶えず描いている時（動画など）は印が立ち続けるので、空き 100ms の条件で焼かれないことがある。その時は開く最初のコマで焼く。
  - 閉じ始めでは、印が立っていなければ前の絵を使い回す（M9）。
  - drag（指を追う開閉）の間は、印が立ったコマだけ焼き直す（そのコマは今と同じ重さ）。
  - 280ms・240ms の自動のアニメの間は焼き直さない（窓の中身が止まる。ユーザーが受け入れた）。

### 1.7 開閉のアニメの 1 コマとアニメの時計

| 順 | 形 | draw |
| --- | --- | --- |
| 1 | 舞台（今どおり） | 2 |
| 2 | Home の絵（`kwl_edge_home_content` の x・y・scale 0.85→1、opacity は progress、MODE_IMAGE で `opaque` 0） | 1 |
| 3 | layer の影（縁の帯だけ 4 片、L5） | 4（今は全面 1） |
| 4 | desktop の絵（`kwl_home_layer` の x・y・scale 1→0.75・opacity、角の半径 18×scale、`opaque` 1） | 1 |
| 5 | `server->anim` の窓（1.6） | 窓の draw |
| 6 | popup・system bar など（今どおり） | bar の数 |

- **時間差の演出はやめる（ユーザーの判断）**。content は progress で一緒に来る（`home_content_ms = 0` の時の今の動き）。アニメは 280ms で終わり、その後の `home_content_rising` のコマも無くなる。
- 起動の時の閉じ: 起動する icon の tile を絵から中心を保って拡大した quad を 1 つ足す。tile の矩形を uv にし、半径は `HOME_ICON_RADIUS × 拡大率` で角を切る（L4）。
- **アニメの時計の起点（H4）**: 今は `home_start_ms` を開くと言われた時に置いている。そのため最初のコマが重いと、280ms の多くを 1 コマで使ってしまう。これを、アニメの最初のコマの submit の時刻に置き直す（drag から続く時は置き直さない）。`KWL HOME open at_ms`（言われた時刻）と `layer=cover after_ms`・`opened` の log の意味は保つ（L6）。最初のコマの submit の時刻は新しい field で出す。

### 1.8 他の場面

- head: 舞台だけ（`kwl_home_draw_head`）で変えない。
- fullscreen の窓が出力を覆う時、Wiseview、greeter・lock、画面が消えている時: 今の経路のまま。
- hover の probe（`kwl_hover_probe`）は描画と分ける。Home を絵から出すコマでも、icon ごとに `home_probe` で登録する（CPU だけ）。

## 設計 (2) draw の束ね（p003）

### 2.1 形（H5・H6）

- 1 つの形を 6 頂点（三角 2 つ）として頂点の buffer に書き、続く同じ鍵の形を 1 回の `vkCmdDraw(6 × n, firstVertex)` にする。Pi に instance の rate と DrawIndexed が無いため。
- **束にするのは面積の小さな形だけにする（H6）**。glass・blur の mode と、面積が閾値（仮に 64×64px）を超える形は、今の push constant の pipeline で 1 形 1 draw のままにする（その前に束を flush する）。閾値は段で `v3d: gpu` の qfrag と `render` を比べて決める。
- **頂点は mode ごとに小さくする（H5）**。opacity は CPU で色（またはその mode の alpha）に掛けてから入れる。

| mode | 頂点の attribute（float） | 補間 | flat |
| --- | --- | --- | --- |
| text | 位置 2、uv 2、色 4 | 位置・uv | 色 |
| solid・ring・shadow | 位置 2、box 4、色 4、半径・soft 2 | 位置 | box・色・半径・soft |
| image | 位置 2、uv 2、box 4、半径・opaque・opacity 3 | 位置・uv | box・半径・opaque・opacity |

  - 1 形あたりの kernel の詰め直しは text 48、solid 系 72、image 66 scalar になる（今は 12）。Home の 440 形が全部束になると、今の 5.3k に対して約 25k scalar。
  - draw ごとの準備（`prep`、今 1 draw 約 5〜20µs）が束で減る分と比べる。段 1 で `KWL DRAWS`、kernel の `rec`・`prep`、compositor の記録の時間を前後で比べる。詰め直しが勝つ mode は束にしない（mode ごとの切り替えを残す）。
  - kernel がまとめて写す改善（uncached の読み元から 1 つずつ読むのをやめる）は WS141 の範囲で、WS211 には入れない。
- 画素の位置（`pixel`）は vertex shader で位置と push constant の `screen.xy` から求める。出力の幅と高さは pass の中で変わらないので push constant（8 byte）に残す。
- 新しい shader `panel-batch.vert`・`panel-batch.frag` は、mode ごとの variant（text・solid・ring・shadow・image の 5 つ）を `regenerate.py` で作る。今の `panel.frag` と同じ式で、push constant の代わりに上の varying を読む。

### 2.2 束ねの仕組み（glass.c、M1）

- `glass_shape_draw` は、束にする形を頂点にして今の束に足す。束の鍵は pipeline・set・scissor の 3 つ。sample しない mode（solid・ring・shadow）は、set を今の束に合わせる。鍵が違う形、束にしない形（2.1）、glass の形以外の command の前で flush する。
- flush の手順:
  1. 束の vertex buffer を binding 0 に bind する。
  2. 束の pipeline layout が今と違えば `screen.xy` を push する。
  3. pipeline と set は前回と違う時だけ bind する。
  4. `vkCmdDraw(6n, 1, first, 0)`。
  1 形 1 draw の経路に戻る時は、`corners` を binding 0 に bind し直す。別の layout の後は push constant を全部 push し直す（今どおり）。
- **flush が要る所**: compositor の中の `vkCmd*` の所を全部一覧にし、主の pass の中で記録する物の前に `kwl_batch_flush` を置く。一覧は compose.c 11、backdrop.c 8、heads.c 8、scene.c 4、shm.c 4、shot.c 4、cursor-plane.c 6、import.c 2、shell.c 2、glass.c 4 か所。debug の build（`KWL_BATCH_CHECK`）では全部の呼び出しを wrapper に通し、「束が空でない時に別の vkCmd を記録した」を log する。
- 頂点の buffer: host-visible の VkBuffer を 2 つ持つ（主のコマ用と layer の submit 用）。大きさは各 1MiB から始める。コマの始めに先頭へ戻す。頂点はまず cached の配列で組み、束の flush の時に mapped の buffer へまとめて写す（L7）。溢れたら、そのコマの残りは 1 形 1 draw で描き、`KWL BATCH overflow` を 1 度出し、次のコマで倍にする（上限 8MiB）。
- fallback: 束の pipeline を作れない device では `KWL BATCH unavailable` を出して今の経路を使う。`KWL_BATCH=0`（環境変数）でも同じにする（試験の参照用）。

### 2.3 App Home を種類ごとの順に描く

- Home の icon の描く順を変える。今は icon ごとに「光・穴・tile・映り込み・名前・印」と描いている。これを「全部の光 → 全部の穴 → 全部の tile と映り込み → 全部の名前と印」にする。
- 絵が今と同じになる理由: 違う icon の形どうしは、光どうしを除いて重ならない。光どうしは同じ白の over なので順に依らない。寸法は次のとおり。
  - 光は中心から ±73.6px。隣の tile・映り込みは ±108px から始まる。
  - 名前は ±68px で、隣の光の端（+73.6）と離れる。
  - 縦では、光は床の −26〜+34px にある。名前の上端は床の +30 前後で、重なるのは同じ icon の中だけ。
  - 同じ icon の中の順は保つ。
- Home の絵を焼くコマ（全体・矩形）で効く。開いたままの検索とページの drag（今の経路で毎コマ描く）は、この順と束で軽くなる（M8: WS の目標「開いたままの操作も軽い」は p003 で満たす）。

## i915・Venus への影響

- クロスフェード（desktop の絵・Home の絵・`server->anim` の窓を直に描くこと）は、全部の device で同じ経路にする（Q1: 見た目を揃える）。
- 束ね: i915 の native compiler（flat の入力・attribute 16・varying 16）に新しい shader を通す。host の compiler の試験で確かめ、作れない時は fallback を使う。Venus は draw と bind の数が減る分だけ encode が減る。
- 段 0b（scene の取り込みを止める）は、cursor の plane がある時だけ効く。i915・Venus は変わらない。
- 5330（i915）は今も速いので、目標は「静止した絵が 2 LSB 以内で同じ、遅くならない」とする。

## 記憶の量

| 物 | 1920×1080 | 3840×2160 |
| --- | --- | --- |
| Home の絵 | 7.9MiB | 31.6MiB |
| desktop の絵 | 7.9MiB | 31.6MiB |
| Pi の UIF の snapshot（kernel の 128MiB の枠の中） | 2 × 7.9MiB | — |
| 頂点の buffer（2 つ、各 1〜8MiB） | 2〜16MiB | 2〜16MiB |
| 合計 | 約 34〜48MiB（Pi） | 約 65〜79MiB |

- Pi の Vulkan の予算は 512MiB で、この boot の最大は 191MiB だった。

## Pi の見積もり（仮説。数値は段 0 の後に置き直す）

- アニメの 1 コマの draw は、舞台 2、絵 2、影 4、bar（段 0 で数える）になる。今の 460〜916 から大きく減り、記録（今 1 draw 約 0.05ms）と `prep` はほぼ draw の数に比例して減る。
- それでも残る物:
  - GPU の fill（全画面 4 枚ほど、今の run あたり 8〜26ms の何割か）。
  - vblank の待ち（flip の wait 10〜11ms）。
  - 段 0 で分ける未説明の 40〜120ms（client の job の後ろの待ち、TFU、scene の取り込み）。
- review-1 の推測は 5〜8 コマ（開く 280ms の間）。最初のコマは、desktop の絵を先に焼き（1.6）時計の起点を置き直す（1.7）ことで、280ms を食わないようにする。
- 舞台を 1 枚の不透明の絵にする案（全画面の glass と光の計算を 1 回の sample に、記憶 +7.9MiB）は、段 0・段 2 の qfrag と `render` を見てから決める。

## 段

| 段 | 内容 | 確かめ |
| --- | --- | --- |
| p002 段 0 | 計測（`KWL DRAWS`、`capture` の時刻、kernel・flip の perf の集計の道具） | build、Pi の log で内訳 |
| p002 段 0b | cursor の plane がある間は scene を取り込まない | build、Pi の内訳の前後 |
| p002 段 1 | layer の cache（2 枚を起動時に、pass・descriptor・erase の pipeline）、Home の絵（鍵、全体を焼く、後ろの submit と host の画像、glyph の cell を守る） | host（鍵と範囲）、build |
| p002 段 2 | 開閉のアニメを絵で（desktop の絵と変化の印、`server->anim` の窓、起動の時の tile、時計の起点）、矩形・帯の描き直し、Pi の変換の方式の比較（1.4 の (i)・(ii)）、hover の probe の分離、時間差の廃止 | host、Pi、T1 |
| p003 段 1 | 束の shader（5 つの variant）・pipeline・頂点の buffer・flush と検出・fallback・`KWL_BATCH=0`、面積の閾値 | host、shader の host 試験、Pi の `rec`・`prep`・qfrag の前後 |
| p003 段 2 | Home を種類ごとの順に、検索とページの drag の計測 | Pi、T1 |

- 新しい file は `Makefile`・`Makefile.linux`・`Makefile.freebsd` に足し、rpi4（arm64）・amd64・Linux・FreeBSD の compositor の build で warning 0 を確かめる（M10）。

## 試験の計画

- **host**:
  - build（上の 4 つ）。
  - `plan/ws211/tests/layer-key-host-test.c`（新規）: 鍵の変化ごとに描き直す範囲が 1.2 の表どおりになること。矩形の和が 1.3 の届く範囲を覆うこと（隣の光を含む）。検索・ページめくりの間は絵を使わないこと。desktop の変化の印が 1.6 の事象でだけ立ち、drag・hover・cursor の commit では立たないこと。
  - `plan/ws211/tests/batch-host-test.c`（新規）: 頂点の値が今の push constant の経路の値と一致すること（NDC の角、uv、box、dark の色、layer の変換、head の差し引き、opacity を掛けた色）。鍵での切れ目、大きな形と glass の切れ目、flush の後の順、溢れの fallback。
  - shader: `plan/ws141/tests/shader-host-test.sh` に `panel-batch` の variant を足し、bcm2711 の compiler で通す。i915 の compiler の host 試験にも通す。
- **参照との比較（M7）**: compositor に `--no-layer-cache`（今の描き方）を足す。束ねには `KWL_BATCH=0` を使う。同じ操作の後の静止した絵を shot.c の capture で撮り、参照と比べて 2 LSB 以内であることを確かめる。T1 は Venus と i915（実機）で、Pi は P3 が撮る（Pi は readback がある）。アニメのコマは見た目の変化を受け入れたので比べない（L8）。比べる場面:
  - hover の出入り（前と後の icon、隣の光）。
  - 分の繰り上がり（時計の帯）。
  - app の起動・終了（動作中の印）。
  - 閉じている間の言語・壁紙・accent の変更の後に開く。
  - 初めての字の名前（glyph の追加と焼き直し）。
  - 起動の時の閉じ（拡大した tile と育つ窓）。
- **Pi（ユーザーの目と P3 の log の読み）**:
  - `KWL DRAWS`。
  - `KWL LAT` で開く 280ms・閉じる 240ms の間のコマの数と間隔（`plan/ws141/tests/kwl-latency.py`）。
  - dmesg の perf（draws・prep・rec・tfu・snap・upd・hold_us）と flip perf。
  - ユーザーの確認: 開閉が滑らか、最初に開く時に待たない、hover・時計・印・ページ・言語・壁紙の後の絵が正しい、起動の時の icon と窓、検索・ページめくり、引っかかりが無い。
- **T1**: Venus と i915 で、開閉のアニメが `KWL VULKAN_ERROR` 無しで終わること、`KWL BATCH unavailable`・`KWL LAYER unavailable` が出ないこと、上の参照との比較。
- 道具: `plan/ws141/tests/shader-host-test.sh` と `kwl-latency.py` は WS141（未完了）の物を使う。WS211 の回帰で使い続けるなら、master の Tools への登録を Q1 に頼む。
- **受け入れの数値は段 0 の後に Q1 とユーザーで決める**（H4）。目安は、開く 280ms の間のコマが今より多いこと、Home を焼く全体の draw、desktop の 1 コマの draw、Pi の記録の時間。

## 未決（Q1・ユーザー）

1. 1.4 の Pi の変換の方式: 段 2 の実測で選ぶ（ユーザーの判断の基準どおり）。
2. 舞台を 1 枚の絵にするか: 段 0・段 2 の後。
3. 2.1 の面積の閾値と、束にする mode: p003 段 1 の後。
4. 受け入れの数値: 段 0 の後。

## 範囲外（必要なら WS141 へ）

- Pi の driver について: instance の rate と DrawIndexed、頂点をまとめて写す（uncached の読み元から 1 つずつ読まない）、描いた画像を UIF で直接書く・GPU で部分を変換する（1.4 の (iii)）、render の書き込みを write ring に記す。
