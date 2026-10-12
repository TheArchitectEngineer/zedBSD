<!-- awesome-plan project=zedbsd record=ws213-p001 -->

# ws213-p001: compositor の thread 化の調査と設計

Parent: [WS213](../ws.md)
Status: planning（設計の第 1 版、design-reviewer の review の前。source は変えていない）
Phase disposition: normal
Queue: P4（2026-10-12 Q1 が割り当て）

## ユーザー向けの要約

- **今の問題**: compositor は 1 本の thread で、1 コマを描く間（命令の記録・GPU への送り・表示の依頼）は入力も cursor も止まります。Pi 4 の記録では、普通のコマで 10〜20ms、App Home のような重いコマで 60〜120ms 止まっています。もう 1 つ、**cursor の plane の表示の依頼そのものが 1 回約 3ms かかり、それを 1 秒に 125〜200 回（画面の書き換え 60 回より多く）行っていて**、cursor を動かすだけで main loop の時間の約 6 割を使っています。
- **形（Q1 の推す形、ユーザー了承済み）**: main thread は Wayland の protocol・入力・cursor だけを受け持ちます。描画の thread を 1 本置き、Vulkan の命令の記録・送り・表示の依頼はこの thread だけが行います。文字の形を作る・壁紙をぼかすなどの CPU の重い仕事は作業 thread に出します。
- **受け箱の中身**: main は「画面の状態」そのものを写すのではなく、今の描画の処理で「描く物の一覧」（命令の列、display list）を作り、それを受け箱に 1 つだけ置きます。今の描画のコード（26 個の file）をそのまま使えるので、作り直しが小さく、見た目が変わりません。描画 thread はそれを Vulkan の命令にして GPU に送ります。受け箱がまだ取られていない時に新しい一覧ができたら、古い方は捨てて置き換えます（描き始める前の予約だけを取り消す方針と同じ）。
- **cursor**: cursor は main に残し、表示の依頼を「1 回の書き換えに 1 回、最新の位置」に減らします。WS141 の cursor の速い経路（GPU を通らない位置だけの表示）と合わせて、重いコマの間も 60fps を目指します。
- **段**: (0) 測る → (1) cursor の依頼の間引き → (2) 描く物の一覧を作る（thread はまだ使わない、見た目が同じことを確かめる）→ (3) 描画 thread → (4) 作業 thread → (5) client の画像の取り込みも main の外へ。各段で Pi の入力→表示と cursor の間隔を測ります。
- **SMP（4 core）が無い今の効果**: CPU の総量は減りませんが、入力が来ると main が描画 thread を割り込んで先に動けるので、cursor と入力の遅れは大きく減る見込みです。コマの速さそのもの（fps）は SMP（WS208）か WS211 の描画の削減で上がります。

## 由来

- 2026-10-12 ユーザー:「コンポジタのマルチスレッド化も入れてください。ウィンドウへの通知の転送以外の処理はほとんどすべてスレッドにして、スレッドに通知を出して処理を委譲しましょう。ただ、描画のシリアライズはいるかも？と思いました。描画スレッドが別途あって、コマンドキューがシリアライズされるのかなあ。それとも描画はイベントキューに積むのか？」
- Q1 の推す形（ユーザーに提示、2026-10-12 ユーザー「了解です。おすすめがいいと思います。あとでできたらレビューさせてください。」）: main thread＝Wayland の protocol・入力の受けと配達・cursor の位置。描画 thread 1 本（Vulkan の queue・command buffer はこの thread だけ）。作業 thread 数本（glyph・画像の展開・縮小画像・blur の準備）。描画は event の queue に積まず、main が作る写しを受け箱に最新の 1 つだけ置く。client の buffer は描画 thread が frame の完了を知らせるまで main が保持する。
- この Phase は設計だけ。source（`userland/desktop/wayland/`）は変えない。実装は review の後、P3（WS211）との順を Q1 が決めてから。

## 1. 今の main loop の仕事の内訳

### 1.1 1 回の pass（`main.c` の run loop、`main.c:762-1074`）

1. tick（入力の scan、touchpad の timer、ping、bounds、settings、system、IME）。
2. poll の descriptor を毎回 calloc して作る（listener、client、evdev、frame の fence fd、client の acquire fence、OS の分）。timeout は 10ms、fence に fd が無い時 2ms、退役待ちがあれば 0。
3. `poll`。
4. accept、OS の poll の後始末、**frame の fence fd が読めたら `kwl_frame_done`**（`kwl_compose_complete`: `vkWaitForFences`＋`vkResetFences`、held buffer の解放、frame callback の送信、layer の完了、scene の valid、pacing の時刻）、`kwl_compose_poll`。
5. evdev をまとめて読む（`kwl_input_read_devices`、input.c・seat.c・pointer-accel.c・touchpad.c・touch.c）。pointer の位置、hover の判定（`damage.c` の probe）、client への wl_pointer・wl_keyboard の event。
6. client の request を読む（`kwl_read` → protocol.c・各 shell）。GPU の buffer の取り込みは backend（`libkeiland-backend-zedbsd/gpu-buffer-zedbsd.c`、Linux・FreeBSD は `libkeiland-backend/dmabuf/gpu-dmabuf.c`）が `vkCreateImage`・memory の import を行い、`gpu-host.c` → `kwl_import_adopt`（`import.c:38`、view と descriptor set）。
7. clipboard・data・shot の tick。
8. **`kwl_schedule`**（`display.c:105`）: sleep・output・displays・glass・desktop の tick → commit の採用（`kwl_fence_ready` で acquire fence を poll、`adopt_commit`）→ surface の output の通知 → game mode の判定 → `kwl_seat_focus` → **`kwl_shm_upload`**（wl_shm の行を CPU で staging に写す、frame が飛んでいない時だけ）→ **`kwl_cursor_plane_update`**（`cursor-plane.c`）→ pacing → 何も無ければ **`kwl_layers_tick`**（layer の絵を静かな時に焼く、`layer-cache.c:142`）→ **`kwl_compose_draw`**（`compose.c:473`）。
9. 退役（BUG-239）、全 client の flush、PERF。

### 1.2 `kwl_compose_draw` の中（1 コマ、全部 main）

| 段 | source | 内容 |
| --- | --- | --- |
| 窓の一覧 | `compose_windows`・`compose_split`・popup・subsurface | server の状態を読む |
| acquire | `vkAcquireNextImageKHR(..., UINT64_MAX)` | 画像が空くまで待ち得る |
| heads | `kwl_heads_acquire`（heads.c） | 副の出力の acquire |
| damage | `compose_region`（buffer age で damage の履歴を合わせる） | 取った画像の番号が要る |
| 記録 | `compose_record`（compose.c:2347）→ `kwl_import_layouts_record`、`kwl_layers_inband`（Home・デスクトップの絵を焼く pass）、`kwl_glass_draw`（shell.c。glass.c・home.c・titlebar・bar・menu・notify・popup などの描画へ）、`kwl_backdrop_begin/end`（窓の下の blur の pass）、`compose_cursor`、`kwl_batch_flush`（batch.c）、`kwl_shot_record`、`kwl_heads_record`、`vkEndCommandBuffer` | 描画の walk（形の位置・push constant・scissor での間引き、hover の probe の記録 `kwl_hover_begin/end`）と libvulkan の encode が混ざる。glyph の初出は `glass_cache_glyph`（glass.c:3052）が truetype で描いて atlas の staging に書く |
| submit | `compose_submit`（compose.c:2748）: `kwl_host_images_record`（書かれた host image の staging→sampled の copy、shm.c:327）＋ `vkQueueSubmit` ＋ `vkQueuePresentKHR` ＋ heads の present ＋ fence の fd の export | |
| hold | `compose_hold`: 窓・dnd・desktop・cursor の buffer を `kwl_buffer_get`、frame callback を集める | |

libvulkan（bcm2711）の encode は command buffer の中に貯め、1MiB（`commands.c:15`）を超えた時と `vkEndCommandBuffer` で kernel の transaction（decode、controller の mutex）になる。`vkQueueSubmit` は device の mutex の下の transaction（`queue.c:422-479`）。

### 1.3 main で Vulkan を呼ぶ他の所（thread 化で全部を割り振る）

| 所 | 呼ぶ物 | 頻度 |
| --- | --- | --- |
| `cursor-plane.c` `plane_present` | `vkGetFenceStatus`・`vkAcquireNextImageKHR`・（像が変わった時）command の記録・`vkQueueSubmit`・`vkQueuePresentKHR` | **pointer が動く pass ごと** |
| `shm.c` `image_create`・`image_layout` | 画像の作成、`vkQueueSubmit`＋`vkQueueWaitIdle`（shm.c:882） | wl_shm の surface の大きさが変わった時 |
| `import.c` `import_layout` | `vkQueueSubmit`＋`vkQueueWaitIdle`（import.c:353、普段は frame の中で記録） | buffer ごと 1 回 |
| backend の import | `vkCreateImage`・memory の import・bind | buffer ごと 1 回 |
| `kwl_compose_set_get/put` | descriptor pool | import・shm の画像の作成と破棄 |
| `glass.c` | `vkDeviceWaitIdle`（451・493・648・2152）: 壁紙の描き直し、出力の大きさ、時計の大きな字の作り直し | まれ |
| `heads.c:908`・`compose.c:443,815`・`cursor-plane.c:117` | `vkDeviceWaitIdle`（出力の close・終了） | まれ |
| `layer-cache.c` `kwl_layers_tick` | 焼く記録と `kwl_compose_submit_quiet` | 静かな時 |

CPU の重い所（main）: glyph の raster（glass.c:3052、`KWL GLASS glyph` の行。英語の UI ではまれ、日本語では初出の字ごと）、壁紙の描画とぼかし（`wallpaper_draw`・`blur_*`、glass.c:735・1034〜。読みと展開は既に thread `loader_run`）、時計の大きな字（`large_fill`）、tile（`tiles_create`、起動時）、shm の行の写し（`surface_upload`、shm.c:555）。既にある thread: 壁紙の loader と prefetch（glass.c）、system・machine・settings の writer・sysmon・keyboard-hand。

### 1.4 Pi の実測（既存の log を読んだ。P3 の boot の b4・b6・b10、`KWL LAT`・`KWL PERF`・`KWL DRAWS`・`KWL CURSOR_PLANE`。at_us の分解能は 1ms）

集計（解析の script は scratchpad、数は中央値 / p90 / 最大、ms）:

| 区間 | b6（662 コマ） | b10（168 コマ、v3d.perf=1） |
| --- | --- | --- |
| acquired→recorded（記録＋End） | 10 / 27 / 93 | 3 / 8 / 53 |
| 同、draw 300 以上のコマ（App Home） | 48 / 62 / 93 | — |
| recorded→queued（copy の記録＋vkQueueSubmit） | 5 / 9 / 24 | 3 / 7 / 15 |
| queued→submit（vkQueuePresentKHR） | 4 / 7 / 31 | 4 / 6 / 14 |
| submit→shown（GPU、main は止まらない） | 60 / 148 / 533 | 56 / 166 / 251 |
| 入力→shown | 107 / 257 / — | 81 / 193 / 260 |
| cursor の present の間隔（動いている間） | 8 / 18 / 178 | 8 / 14 / 76 |
| cursor の入力→present | 5 / 7 / 176 | 5 / 7 / 57 |

`KWL PERF` の 5 秒の窓ごとに、main の work（poll の外の時間）から compose の draw_ms の合計を引いた残り:

- pointer だけを動かしている窓（b10、1087 回の present、frame 7）: work 66%、compose 0.12s、**残り 3.18s ＝ present 1 回あたり約 2.9ms**。
- 他の窓でも残りは 1.5〜3.2s / 5s。cursor の present の数と強く連動するが、client の commit が多い窓では present あたり 10〜20ms になり、cursor 以外の何か（commit の採用・shm の写し・hover・client の request）も大きい。今の log では分けられない。

分かったこと:

1. 1 コマで main が止まる時間は、記録＋submit＋present の合計で、普通のコマ 10〜20ms、App Home のコマ 60〜120ms（b6 の最大 176ms の cursor の遅れはこれに当たる）。
2. **cursor の plane の present は pointer の event ごと（約 125Hz、画面の 60Hz の 2 倍）に行われ、1 回約 3ms**（今の libvulkan の WSI は位置だけの present でも GPU の copy の job を送る。WS141 p016 (B) の B-u で速い経路になる予定）。cursor を動かすだけで main の 6 割を使う。
3. 残りの内訳は測らないと分からない（段 M0）。

## 2. 写しの形の選び方

| 案 | 内容 | 判断 |
| --- | --- | --- |
| 案 A 状態の写し | server の状態（窓・位置・glass・Home・menu・通知…）を写し、描画 thread が今の描画のコードを写しに対して動かす | **採らない**。描画のコードは 26 file が server・client の object・glass・home の状態を直に読む。全部を写しの型に書き換える必要があり、hover の probe（描画が記録し入力が使う）も分かれる。危険が大きすぎる |
| 案 B 描く物の一覧（display list） | main が今の描画の walk を行い、一番下（vkCmd* を呼んでいる 13 file・約 60 か所）だけを「一覧に op を足す」に変える。描画 thread が一覧を Vulkan の命令にして送る | **採る**。描画の論理は変えない。main に残るのは walk（形の位置・push constant の計算・間引き・hover の記録）だけで、libvulkan の encode・kernel の transaction・submit・present・fence の待ちは全部描画 thread へ |
| 案 C secondary command buffer | main が自分の command pool で secondary を記録し、描画 thread が vkCmdExecuteCommands | 採らない。Vulkan の正統な多 thread の形だが、Pi では高い所（libvulkan の encode と End の transaction）が main に残る |

案 B の前提（段 S1 で測って確かめる）: walk の費用は記録全体の一部。根拠: 記録の費用は draw の数にほぼ比例し（0〜100 draw で 3〜8ms、300 以上で 48ms、1 draw 約 0.1ms）、その中身は libvulkan の encode（WS211 p003 の見積もり 1 draw 約 0.05ms）と End の decode の transaction（kernel の `hold_us decode` 最大 11〜19ms）が大半。walk の 1 形は数 µs の計算。**これが外れて walk が重いなら、案 B の効果は小さくなる**（段 S1 の結果で次の段に進むかを決める）。

## 3. thread の分担と Vulkan の持ち主

| thread | 受け持つ物 | 持たない物 |
| --- | --- | --- |
| **M（main）** | poll、Wayland の protocol（読み・書き・flush）、入力（evdev、pointer の加速、hover の判定、focus、event の配達）、cursor の plane（位置と像、下の 6 節）、server の状態の全部、描画の walk（一覧を作る）、frame pacing、client の buffer の参照の数、frame callback、log | Vulkan の command buffer・descriptor pool・出力の swapchain・queue の submit（cursor の plane を除く） |
| **R（描画、1 本）** | 出力と heads の swapchain の acquire・present、一覧の再生（vkCmd*）、host image の staging への書き込みと copy の記録、`vkQueueSubmit`、fence の待ち、descriptor set の割り当てと返却、R が作る object（layer の絵・backdrop・scene・batch の buffer・shm の画像・framebuffer）の作成と破棄 | server の状態（読まない・書かない） |
| **W（作業、まず 1 本、SMP 後は core−1）** | glyph の raster、壁紙の描画とぼかし、大きな字、tile、（後で）icon の展開 | Vulkan、server の状態 |

規則:

- **Vulkan の object は R が持つ**。R が動いている間に M が呼んでよい Vulkan は (a) cursor の plane（自分の command pool・fence・semaphore、下の 6 節）、(b) backend の client の画像の import（`vkCreateImage`・memory の import・bind。device 単位の作成で Vulkan の規則では他の thread と並行してよい。段 S5 で R へ移す）だけ。`kwl_import_adopt` の view も device 単位の作成で M に残せるが、descriptor set は R が初めて使う時に割り当てる（pool は外部同期が要る）。
- **queue の lock**: 出力の queue は 1 本（bcm2711 の queueCount は 1、`vulkan-query.c:182`、i915 も 1）。`vkQueueSubmit`・`vkQueuePresentKHR`・`vkQueueWaitIdle` は Vulkan の規則で queue の外部同期が要る。compositor に 1 つの mutex（`queue_lock`）を置き、R の submit・present と M の cursor の plane の submit・present の呼び出しの間だけ持つ（記録の間は持たない）。zedBSD の libvulkan は中でも `queue->mutex`・`device->mutex` で直列にするが、Linux・FreeBSD の host の Vulkan（Mesa）のためにこの lock が要る。
- **command pool は thread ごと**: R は今の `compose->pool`、M（cursor の plane）は新しい自分の pool。今 plane は `compose->pool` から確保している（`cursor-plane.c` の `plane_objects`）ので分ける。
- **まれな全体の変更は R の park で**: 出力の open・close・移動（output-switch.c・displays）、heads の hotplug、glass の大きさ、session の pause（`backend-host.c:46`）、sleep、終了。M は R に park を頼み、R は今のコマを最後まで描いて fence を待ち、止まって知らせる。M はその間 Vulkan を自分で扱い（今のコードのまま）、終わったら unpark。今の `vkDeviceWaitIdle`・`kwl_compose_quiesce` の所はこれに置き換える。park の待ちは 1 コマ分で、まれなので許す。

## 4. 描画 thread の受け箱（写し）

### 4.1 写しの中身

| 物 | 内容 | 持ち主 |
| --- | --- | --- |
| 番号・種類 | frame の番号（M が付ける）、種類（出力のコマ / present しない絵、今の `frame_bake`）、出力の世代（出力が変わった後の古い写しは R が捨てる） | R が読む |
| 一覧（op の列） | 固定の頭＋中身の byte 列: render pass の開始（相手は記号: 出力の画像・head k の画像・layer の絵・backdrop の target・scene）と終わり、viewport、scissor、pipeline と descriptor set の bind、push constant（中身を inline）、vertex buffer の bind、draw、barrier・copy・clear（scene・shot・backdrop）。pipeline・layout・sampler は起動時に作り R が動く間は変わらない handle の値をそのまま入れる。client の画像は `struct kwl_import *` と set の種類（普通・linear）で指し、R が再生の時に set を引く | R が読む |
| 頂点の配列 | WS211 p003 の束（batch.c）の頂点。今も cached の配列で組んで flush で buffer に写すので、写しに配列のまま入れ、R が再生の時に HOST_COHERENT の buffer に写す（前のコマの fence の後なので GPU と競らない） | R |
| damage | M が計算した region（下の 4.3）、全体かどうか、scene の再利用の判断（capture・reuse） | R |
| host image の更新 | 写しの前に R が staging に書く物の一覧: shm の写し（buffer の参照、行の範囲）、glyph の cell（W の bitmap の番号）、大きな字・壁紙・ぼかし（W の結果）、import の layout の移動（今の `kwl_import_layouts_record` の list） | R が適用 |
| layer・shot・heads | layer の世代（`frame_layers_generation`）、shot の依頼、heads の一覧 | R |
| M だけの帳簿 | 保持した buffer の参照の list、frame callback の list、pacing の印（awaited） | **M が持ち、R は触らない**。写しは帳簿への不透明な pointer を運ぶだけ |

server の状態・client の object・hover の probe・animation の時刻・cursor の位置は**写しに入らない**（walk の時に M が読み、結果の op だけが入る）。composite の cursor（plane が無い出力）は walk が op にする。

### 4.2 作り方と置き換え

- M は今の `kwl_compose_draw` の判断（dirty・damaged・pacing・`kwl_layers_tick`）で「描く時」を決め、walk で一覧を作る。保持（`compose_hold`）は写しの帳簿に入れる。
- 受け箱は 1 つ（mutex＋R の condvar）。**空なら置く。埋まっている（R がまだ取っていない）なら、M は古い写しを取り戻して新しい写しに置き換える**: 古い写しの frame callback を新しい写しの帳簿の先頭に移し（順序を保つ）、古い保持は新しい保持を取った後に外す（両方にある buffer は生きたまま）。古い写しの damage は履歴に残る（4.3 で新しい region に入る）。描き始めた写し（R が取った物）は取り消さない。
- 置き換えは walk を 1 回無駄にする。walk の費用と置き換えの回数を PERF に出し、費用が大きければ「受け箱が空になってから作る」に変える（段 S3 の計測で決める）。
- M が新しい写しを作るのは、受け箱が空か置き換えが要る時だけで、R が描いている写しの完了は待たない（walk と R の記録・GPU が重なる）。

### 4.3 damage と buffer age

- 今は取った画像の番号で、その画像が最後に描かれた frame からの damage を合わせる（`compose_region`）。walk は scissor で形を間引く（glass.c:1721、compose.c:2234）ので、M は walk の前に region を知る必要があるが、画像の番号は R の acquire まで分からない。
- 規則: R は完了の知らせで各画像の「最後に描いた frame の番号」と画像の数を M に返す。M は**全部の画像のうち一番古い物**からの damage の和を region にする（どの画像を取っても足りる）。描いたことの無い画像があれば全体。置き換えられて描かれなかった写しの damage も履歴にあるので和に入る。
- M の知る番号は 1 コマ古いことがある（飛んでいるコマの画像が分からない）が、古い情報は region を大きくする方向にしか働かない。FIFO で画像が順に回る普段は、今と同じ region か 1 コマ分大きい。R は acquire の後に正しい region を計算し、render area と scissor をそれに狭める（GPU の仕事は今と同じ、draw の数だけ少し増え得る）。

### 4.4 費用と lock

- 写しは M の walk が自分の buffer に書く。受け渡しは pointer の交換だけ（受け箱の mutex の下で O(1)）。server の状態は写さない。
- 一覧の大きさの見積もり: App Home の 460 draw で push 96B＋draw 16B＋bind ほか → 約 70KB、束の頂点が最大 1MiB（WS211 p003 の上限）。buffer は 3 つ（M が作る・受け箱・R が再生）を回し、大きさは最大に合わせて伸ばすだけ（op ごとの malloc は無し）。
- 共有の変数は受け箱・完了の list・park の印・R の状態（数）だけで、全部 1 つの mutex（`render_lock`）の下。R の handle（pipeline など）は R の起動前に作り、R が動く間は不変。

### 4.5 完了の知らせ

- R → M は完了の list（`render_lock` の下）＋ pipe（M の poll に入れる、今の frame の fence fd の代わり）。中身: frame の番号、結果（描いた・捨てた（出力の世代が古い）・失敗）、画像ごとの最後の frame、heads の missed、scene・layer の結果、計測（acquire・再生・End・submit・present・GPU の各時間）。
- M の処理は今の `kwl_compose_complete` の M の部分: 帳簿の buffer を `kwl_buffer_put`（wl_buffer.release）、frame callback の送信、`kwl_layers_complete`、scene の valid、pacing の時刻、shot の完了。shm の写しの完了もここで buffer を release する（今は M の写しの直後）。
- fence の fd の poll（`frame_fd`・`kwl_compose_waiting`・`kwl_compose_poll`）は M から無くなる。R が `vkWaitForFences` を上限付き（例 100ms ごとに停止と park の印を見る）で待つ。

### 4.6 R の 1 周

1. FIFO の依頼（object の作成・破棄、host image の作成、park）を順に処理。
2. 受け箱から写しを取る（無ければ condvar で待つ）。出力の世代が古ければ捨てて知らせる。
3. 出力（と heads）の `vkAcquireNextImageKHR`（待ってよい、M は止まらない）。
4. host image の更新を適用（W の結果を待つ物は 5 節）、copy を記録。
5. 一覧を再生して記録、`vkEndCommandBuffer`。
6. `queue_lock` を取って `vkQueueSubmit`・`vkQueuePresentKHR`・heads の present、外す。
7. fence を待つ → 完了を知らせる。
8. 1 へ。飛んでいるコマは 1 つ（今と同じ）。2 つにするのは段 S6（WS211 p006 の案 3）。

### 4.7 Vulkan の object の寿命

| object | 作る | 壊す | 守り |
| --- | --- | --- | --- |
| client の import（image・memory・view） | M（backend、S5 で R） | M が参照 0 で R に破棄の依頼 → R が次の周の始めに破棄 | 写しの帳簿の保持。参照 0 は完了の後にしか来ない |
| import の descriptor set | R（初めて再生する時） | R（import の破棄と一緒） | 同上 |
| shm の画像（staging・sampled） | R（大きさが変わった最初の写し） | R | surface の参照 |
| glass の host image（atlas・大きな字・壁紙・ぼかし・tile） | R の起動前（今どおり `kwl_compose_open`） | park の中 | 起動から終了まで |
| cursor の形の画像 | M（起動時に全部作る、plane が staging から写す） | 終了 | M だけが使う。composite の cursor の時は R も読む（書き換えないので安全） |
| layer の絵・backdrop・scene・heads の view と framebuffer・batch の buffer | R（依頼、または park の中） | R | 世代の番号（layer は今の `layers.generation`） |
| WS211 の束の buffer | R | R | 頂点は写しの配列から R が写す |

## 5. 作業 thread と glyph・host image

- **原則**: staging（host image の CPU 側）に書くのは R だけで、書くのは R の周の 4（前のコマの fence の後、copy の記録の前）だけ。今は「frame が飛んでいない時に M が書く」（shm、shm.c:146）と「記録の途中に M が書く」（glyph の `atlas_put`）が混ざっているが、これで GPU の copy と CPU の書き込みが競わない。
- **glyph**: M の `glass_cache_glyph` は cell の選び（LRU）と metrics（`truetype_glyph_metrics`、文字の幅の計算に要る）だけを同期で行い、raster の job（face の番号・glyph id・大きさ・cell・job の番号）を W に出し、glyph を「未着」の印付きで返す。walk は普段どおり op を作る。写しは「この写しが要る job の最大の番号」を持ち、R は周の 4 でその番号まで W が終えるのを待ってから bitmap を staging の cell に書く（`atlas_put` は R へ）。
  - W は自分の truetype の face を持つ（同じ font の bytes から `truetype_open`。face は大きさの設定などの可変の状態を持つので M と共有しない。libtruetype に file 単位の可変の状態が無いことは確かめた）。raster の buffer も W のもの（今の関数内 static の `bitmap` は使わない）。
  - **追い出し**（WS211 review-2 N8・review-3 A1・review-4 H2 の問題）: cell の上書きは R が周の 4 で行い、その時には前の写しのコマは完了している（飛んでいるコマは 1 つ）。だから「飛んでいるコマが使う cell を書き換える」は起きない。焼いた layer の絵は焼いた時に sample 済みなので、後の上書きの影響を受けない。残るのは今と同じ「1 つの写しの中で cache（約 200〜300 cell）より多い種類の字」だけで、guard（`glass_glyph_guard_*`）の扱いは今のまま。記録の外の `glass_text_width`（home_prepare・menu・notify・IME など、review-3 A1 の列挙）も metrics だけになり staging を触らなくなる。
  - 2 つ飛ぶようにする段（S6）では、R は「飛んでいるコマの使う cell の job」を周の 4 で遅らせる必要がある。S6 の設計で扱う。
- **壁紙**: 読みと展開は今の loader の thread。`wallpaper_draw`（拡大・配置）とぼかし（`blur_*`）を W に移し、結果の画素を R が周の 4 で staging に写す。今の `vkDeviceWaitIdle`（glass.c:648）は要らなくなる。
- **大きな字**（`glass_large_prepare`、glass.c:2125、`vkDeviceWaitIdle` 2152）: 新しい大きさの digits を W が描き、R が写す。届くまでは古い大きさで描く（今は「全部できるまで使わない」）。
- **shm**: `surface_upload` の memcpy を R の周の 4 に移す。M は buffer の参照を持ったまま依頼を写しに入れ、R の完了で release する。pool の map は buffer の参照が生かす（実装の時に pool の破棄の順を確かめる）。
- W の結果の受け渡しは W→R の完了の list（job の番号の順）。W は Vulkan も server の状態も触らない。

## 6. cursor と入力

- **M に残す**: evdev の読み、pointer の加速と出力の境、pointer の制約、hover の判定（`damage.c` の probe、walk が記録）、focus、wl_pointer・wl_keyboard の配達、cursor の形の決定（`kwl_compose_cursor_source`）、**cursor の plane の present**。
- **present の間引き（段 C0）**: 今は pointer が動いた pass ごとに present（約 125Hz、1 回 約 3ms）。出力の refresh の周期（mode の refresh から）に 1 回までにし、周期の間に来た動きは最後の位置だけを次の機会に出す。M の poll の timeout を「次の present の時刻」までにする。遅れは最大 1 refresh 増えるが、今の main の 6 割の仕事が 1/2 以下になり、その分 main が早く次の入力を読む。
- **速い経路**: WS141 p016 (B) の B-u（libvulkan の位置だけの present が GPU を通らない、ticket で追い越し）が入ると、M の present は device の mutex の短い区間＋ioctl になる。ただし libvulkan の `queue_attempt` は QueueSubmit の transaction の間 `device->mutex` を持つ（queue.c:422-479）ので、M の位置だけの present は R の vkQueueSubmit（Pi で 3〜9ms、最大 24ms）を待ち得る。B-u の後の実機で測り、1 refresh を超えるなら B-u の L1（cursor を display_mutex から分ける）と合わせて、WSI の cursor の状態を `device->mutex` の外に出す設計を WS141 に頼む。
- **queue の lock**: M の present は `queue_lock` を取る。R が持つのは submit と present の呼び出しの間だけ。M は `pthread_mutex_trylock` で取れなければ今回は出さず、次の pass（poll の上限 2ms）で再試行する（最新の位置だけを出す）。
- **像が変わった時の copy**（`plane_record`、形が変わる・client の cursor）: M の自分の command pool で記録して submit（今どおり）。この時は libvulkan の context の mutex を取る transaction になり、R の End の transaction（最大十数 ms）を待ち得るが、形の変化はまれなので許す。client の wl_shm の cursor は、plane 用の staging を M が持ち M が写す（64×64×4＝16KB の memcpy）。composite 用の画像は R が持つ（R の shm の写しと M の plane の copy が同じ staging を競わないように分ける）。
- **plane が無い出力**（i915・Venus・Linux・FreeBSD）: cursor は composite。pointer の動きは今どおり damage（cursor の所）→ 写し → R。scene の再利用（cursor の無い絵、compose.c の `scene`）の判断は M が完了の知らせの scene の状態で行う。遅れは M の walk＋R の 1 コマ。
- **入力の遅れの目標**: Pi で、重いコマ（App Home の開閉・drag・app の起動）の間も、cursor の present の間隔が refresh に従い（16.7ms）、入力→present が 1 refresh 以下。M の 1 pass が 16ms を超えないことが条件で、それは walk の費用（段 S1 で測る）と protocol の処理（M0 で測る）に依る。

## 7. libvulkan の thread の安全、Linux・FreeBSD

- zedBSD の libvulkan（`userland/desktop/libvulkan/`）: session（VkDevice）ごとの `context->mutex` が kernel との transaction を直列にする（context.c:496-505）。`vkQueueSubmit` は `queue->mutex`（queue.c:333）の下で `device->mutex`（422）を取り transaction。command buffer の vkCmd* は command buffer の中に貯めるだけ（commands.c:600-650、1MiB で flush）で lock を取らない（command buffer は外部同期、R だけが使う）。sync・WSI・display は自分の mutex を持つ。M と R が同時に呼ぶのは (a) M の backend の import と R の transaction → `context->mutex` で直列（M は R の 1 transaction の長さだけ待ち得る。End の decode は最大 11〜19ms、b10 の hold_us）、(b) M の cursor の present と R の submit → 上の 6 節。どちらも正しさは保たれ、問題は待ち時間だけ。待ちは段 S3 の計測で見る。
- Vulkan の規則（Mesa など他の実装）: command pool・descriptor pool・command buffer は thread ごとか外部同期 → 3 節の分担で守る。queue は `queue_lock`。device 単位の作成・破棄（import）は並行してよい。fence の待ちは R だけ。
- **Linux・FreeBSD の backend への影響**: 変わるのは compositor の中（`userland/desktop/wayland/`）だけで、`libkeiland-backend` の API は段 S5 まで変えない。backend の dmabuf の import（`gpu-dmabuf.c`）は M のまま、device 単位の作成なので Mesa でも並行の規則に合う。pthread は Linux（glibc）・FreeBSD（`-pthread`、Makefile.freebsd:96）とも既に使っている（glass.c の loader）。新しい file は `Makefile`・`Makefile.linux`・`Makefile.freebsd`（keiland-linux-sync）の 3 つに足す。i915・Venus は zedBSD の libvulkan で、bcm2711 と同じ lock の構造。
- 段 S5（import を R へ）は `keiland-backend-gpu.h` の API を「fd と記述を受け取る」と「Vulkan の object を作る」に分ける変更になり、zedBSD・Linux・FreeBSD の 3 つの backend に及ぶ。S5 の前に別の設計（review）にする。

## 8. 段（小さく安全な順）

| 段 | 内容 | 確かめ |
| --- | --- | --- |
| **M0** | 計測だけ: `KWL PERF` に main の pass の区間ごとの時間（入力の読み、client の request、commit の採用、shm の写し、cursor の plane の present（回数と時間）、layer の tick、compose の walk・encode・End・submit・present、frame の完了、flush）。動作は変えない | host build（warning 0）。Pi の 1 boot（P3 か Q1 経由、App Home の開閉・drag・app の起動・pointer だけ） |
| **C0** | cursor の plane の present を 1 refresh に 1 回（最新の位置）にする。cursor-plane.c と main.c の poll の timeout だけ | host（間引きの判断の決定的な試験）、Pi（present の間隔と main の work%） |
| **S1** | display list: `struct kwl_list` と op、描画の関数の `VkCommandBuffer command` の引数（37 file）を `struct kwl_list *` に変える（型の違いで vkCmd の取り残しを compiler が見つける）、batch の頂点を一覧に。**再生は同じ thread の同じ場所**（`compose_record` の最後で一覧を command buffer に再生）。thread はまだ無い | host: 再生の等価試験（下の 9 節 (a)）、T1（i915・Venus の見た目と `KWL VULKAN_ERROR` なし）、Pi（walk と再生の時間を分けて測る → S2 の効果の見込みを確かめる） |
| **S2** | R の thread: 受け箱・置き換え・完了の list と pipe・park・R の 1 周・`queue_lock`・cursor の plane の command pool の分離・descriptor set の R への移動・object の作成と破棄の依頼・damage の region の規則（4.3）・host image の staging は R だけが書く（CPU の仕事はまだ M で行い、結果を写しに入れる）・shm の写しを R へ。`vkDeviceWaitIdle`・`kwl_compose_quiesce` の所を park か retire に | host（9 節 (b)〜(d)）、T1、Pi（入力→shown、cursor の間隔、M の pass の時間） |
| **S3** | W: glyph の raster、壁紙の描画とぼかし、大きな字。置き換えの費用の計測で「受け箱が空になってから作る」への変更を決める | host（9 節 (e)）、Pi（日本語の UI で初出の字の多い画面、壁紙の変更） |
| **S4** | 規約の全文の見直し（p003） | — |
| S5（別の設計） | client の画像の import を R へ（backend の API の変更） | S3 の計測で M の import の待ちが問題なら |
| S6（別の設計） | 飛んでいるコマを 2 つに（WS211 p006 の案 3） | WS211 p006 の計測の後 |

- M0 と C0 は小さく、WS211（P3）の変更と当たりにくいので先に入れられる。S1 は描画の 37 file の引数の型を変えるので、**WS211 p003（束ね）の段 2 が main に入った後に始める**（Q1 が順を決める）。S1 の中の batch.c の頂点の扱いは p003 の形に合わせる。
- WS141 p016 (B) の B-u（libvulkan の cursor の速い経路）は P3 が担当。C0 と S2 はそれを前提にしない（入ればさらに速くなる）。

## 9. 試験

- host（決定的、thread を使わずに手順を外から 1 歩ずつ呼ぶ）:
  - (a) **再生の等価**: 描画の場面（デスクトップ、窓 2 つと popup、App Home、drag の途中の部分の描き直し、backdrop、heads 2 つ、layer の焼き、batch の on と off）を、S1 の前の直の記録と S1 の一覧の再生のそれぞれで、vkCmd* を記録する stub の dispatch に流し、命令の列（引数と push constant の bytes まで）が同じことを比べる。S1 の前の列は S1 の最初の commit で golden に取る。
  - (b) 受け箱: 置く・取る・置き換え（callback の順、保持の付け外し、両方にある buffer が release されない）、取った写しは置き換えない、出力の世代の古い写しを R が捨てる。
  - (c) damage: 画像 3 つと 2 つ、置き換えで描かれなかった写しの damage が後の region に入る、描いたことの無い画像で全体、M の知る番号が 1 コマ古い時に足りる（R の正しい region ⊆ M の region）。
  - (d) park: R が描いている途中の park の依頼、park の間に M が出力を作り直し、unpark の後の最初の写しが全体。R の fence の待ちの上限と停止。
  - (e) glyph: 未着の glyph の写しで R が job を待ってから staging に書く、cell の上書きは飛んでいるコマの後、記録の外の `glass_text_width` が staging を触らない。
  - (f) C0 の間引き: 同じ refresh の中の 3 回の動きで present 1 回・最後の位置、次の周期の始めに出る、trylock の失敗で次の pass。
  - 置き場: `plan/ws213/tests/`（compositor の file を stub と一緒に host で compile、WS211 の `bind-host-test.sh` と同じ形）。
- Linux の host build（Makefile.linux）と FreeBSD の build で warning 0。可能なら Linux の build を ThreadSanitizer 付きで WS131 の Debian の QEMU+KVM guest で動かす（T1 に頼むかを S2 の時に Q1 と決める）。
- T1（S1・S2・S3 の終わりに 1 回ずつ、まとめて）: i915（5330）と Venus の QEMU で、shot.c の画面の写しを変更の前と比べる（静止した絵が同じ）、`KWL VULKAN_ERROR`・`KWL FAILED` が無い、App Home の開閉・drag・終了の流れ。
- Pi（実機、P3 かユーザーの手で配置）: `KWL LAT`（入力→shown）、`KWL CURSOR_PLANE present` の間隔と入力→present（B-u の `event_us` が入ればそれで）、M0 の区間の時間、`KWL PERF` の work%。場面: App Home の開閉、窓の drag、Music・Settings の起動、pointer だけ。完了の条件の確認はユーザー。

## 10. SMP が無い間の効果の見込み

- Pi の kernel は 1 core（WS208 まで）。thread にしても CPU の総量は同じで、fps（コマの速さ）は上がらない。context switch の分だけわずかに下がり得る。
- 効くのは **M の遅れ**: kernel は起きた thread が同じ優先度なら走っている thread を次の安全な点で横取りする（`src/kern/sched.c` の `wake_enqueue_locked`、1800-1834）。evdev が読めて M が起きると、R の CPU の仕事（encode）を横取りして先に入力と cursor を処理する。R が kernel の中（transaction の decode）にいる時は、kernel の安全な点まで待つ（kernel の GPU の thread（decoder・executor）も同じ既定の優先度、`render-worker.c:115,121`）。この待ちは `SCHED_WAKE_LATENCY` の計測で確かめられる。
- 見込み（M0・S1 の実測で直す）:
  - 1 コマで M が止まる時間: 今 10〜20ms（App Home 60〜120ms）→ S2 の後は walk だけ（数 ms と見込む、S1 で測る）。
  - cursor: C0 で present の仕事が半分以下。B-u が入れば present 1 回が ioctl 程度。重いコマの間の cursor の間隔の最大（今 76〜178ms）は、M の walk と R の submit の device の mutex の待ち（最大約 24ms）の大きい方まで下がる見込み。
  - App Home の開閉の fps（WS211 の領分）は変わらない。SMP の後は R・W・kernel の GPU の thread が別の core で動き、fps も上がる。

## 11. 未決と危険

1. walk の費用（S1 で測る）。重ければ S2 の効果は小さく、walk を軽くする（WS211 の束ね、Home の絵の再利用）のが先になる。
2. 置き換えの方針（いつも置き換える / 受け箱が空になってから作る）。S3 の計測で決める。
3. M が R の transaction を待つ時間（import の `context->mutex`、cursor の `device->mutex`）。S2 の後の実機で測り、大きければ S5（import を R へ）や WS141 への依頼（WSI の cursor の状態の lock の分離）。
4. 1 本の queue を cursor の plane と共有する。別案は cursor の plane に別の VkDevice（別の session・別の queue。WS141 p016 の session ごとの列と表示の優先が効く）を使うこと。cursor の画像を 2 つの device で持つ必要があり、VK_KHR_display の plane を別の device から出せるかを確かめる必要がある。S2 の計測で queue の待ちが問題になった時の候補として残す。
5. 動きの時刻: walk の時に animation の時刻を読むので、表示されるのは R と GPU の時間だけ後。今も同じ（記録の後に submit と GPU）で、差は R の待ちの分。目で分かるかは Pi で見る。必要なら表示の予定時刻で animation を進める。
6. client の buffer の解放の遅れ: shm の release が R の写しの後になる（今は M の写しの直後）。FIFO の client の待ちが 1 周分増え得る。Pi の Settings などで見る。
7. `kwl_compose_quiesce` の 4 か所（objects.c:554 の client の破棄、backend-host.c:46、main.c:1097、shm.c:187）と `->in_flight` の 16 か所の意味を、S2 の実装の前に 1 つずつ「park」「retire（BUG-239）」「写しの未完了」のどれに当てるか決める。client の破棄で M が 1 コマ待つのは避け、retire に寄せる。

## 12. 結果の記録（この Phase）

- 調べたこと: 上の 1 節（source の地図、Pi の既存の log の集計）。Pi の今の `/run/user/1000/session.log` は ssh の host key が変わっていて読めなかった（`known_hosts` の書き換えは security の判定の回避になり得るので行っていない）。代わりに P3 の boot の log（b4・b6・b10、2026-10-12）を読んだ。
- 変えた file: この phase.md と ws.md の Phase の表だけ。source・試験は無し。
- 次: design-reviewer の review → 直し → ユーザーの確認 → Q1 が段の順（M0・C0 の先行、S1 は WS211 p003 の後）を決める。

## M0・C0 の実装（2026-10-12 P4、Q1 の「review を待たずに M0・C0 を実装してよい」）

- **M0**（計測だけ、動作は変えない）: `struct kwl_perf`（kwl.h）に pass の区間の cycle を足し、`main.c` の event loop と `display.c` の `kwl_schedule` で測る。5 秒ごとの `KWL PERF` に 1 行 `KWL PERF main ticks= input= clients= frame_done= schedule= flush= other= | schedule ticks= adopt= shm= cursor= layers= draw= | cursor presents= held=` を足した（値は窓の中の ms の合計。ticks は poll の前の tick で work の外、other は work のうち区間に入らない分（descriptor の作成・accept・OS・clipboard ほか）、held は refresh を待った pass の数）。
- **C0**（cursor-plane.c）: 同じ像（source と version が同じ）の位置だけの present は、前の present から 1 refresh（`server->refresh`、無ければ 60Hz）の間は出さずに `deferred` にし、refresh の終わりに最新の位置を出す。最初の動き（静かな refresh の後）と像の変化はすぐ出す（plane と frame の cursor が 1 refresh 食い違わないように）。`kwl_cursor_plane_due_ms` が event loop の poll の timeout を次の present の時刻まで縮める（最小 1ms、さらに 1 refresh 過ぎても誰も出さなければ -1 に戻して 1ms ごとに起きない。pause・sleep・game mode で scheduler が plane まで来ない時のため）。`plane_present` の早い return（copy の fence が未完了・空き画像が無い）は今どおり次の pass で再試行（`deferred` は立てない）。WS141 p016 (B) の B-u（libvulkan の速い経路）とは独立で、B-u が入れば 1 回の present がさらに軽くなる。
- 変えた file: `userland/desktop/wayland/kwl.h`・`compose.h`・`cursor-plane.c`・`display.c`・`main.c`。試験: `plan/ws213/tests/cursor-due-host-test.c`・`.sh`（新規）。
- 確かめ:
  - host: `sh plan/ws213/tests/cursor-due-host-test.sh` → `cursor-due-host-test: PASS`（偽の Vulkan と時計で、最初の動きはすぐ、同じ refresh の中の 2 回は保留して due が 12・7ms、refresh の終わりに最新の位置、止まった pointer は何もしない、静かな後の動きはすぐ、像の変化は保留しない、誰も出さない保留は 1 refresh 後に -1、refresh 0 は 60Hz、perf の数）。
  - build: rpi4（`make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/bin/wayland`）rc 0・warning 0、amd64（`config/ci/config-amd64.mk`、`build/amd64/bin/wayland`）rc 0・warning 0、`make keiland-linux` rc 0・warning 0。`git diff --check` 空。
  - 注: 自分の worktree の `build/arm64/sysroot` は、この make が初めて作った（共有の build・toolchain の tree には触れていない）。最初の make は -j の順で sysroot より先に libc の math を compile して失敗し、2 回目で通った。
- 未実施: Pi・T1。Pi で見ること: `KWL PERF main` の区間（pointer だけ・App Home の開閉・drag・app の起動）、`cursor presents=` が 60/s 程度に減ること、`KWL CURSOR_PLANE present` の間隔（約 16.7ms）と入力→present、work% の減り。T1: i915・Venus は plane が無いので C0 は動かない（M0 の行が出て、`KWL FAILED` が無いことだけ）。
