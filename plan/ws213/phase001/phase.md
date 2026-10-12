<!-- awesome-plan project=zedbsd record=ws213-p001 -->

# ws213-p001: compositor の thread 化の調査と設計

Parent: [WS213](../ws.md)
Status: planning（設計の第 2 版。[review-1](review-1.md) と 2026-10-12 のユーザーの判断（描く物の一覧＋pull）を反映、再 review の前）
Phase disposition: normal
Queue: P4（2026-10-12 Q1 が割り当て）

## ユーザー向けの要約

- **今の問題**: compositor は 1 本の thread で、1 コマを描く間（命令の記録・GPU への送り・表示の依頼）は入力も cursor も止まります。Pi 4 の記録では、普通のコマで 10〜20ms、App Home のような重いコマで 60〜120ms 止まっています。cursor の表示の依頼も、画面の書き換え（60 回/秒）より多い約 125 回/秒行っていて、main の時間をかなり使っている見込みです（測定を足しました、M0）。
- **形（2026-10-12 ユーザーの判断「描く物の一覧＋pull」）**: main thread は Wayland の protocol・入力・cursor と、今の描画の処理で「描く物の一覧」を作る所までを受け持ちます。描画 thread は「次をくれ」と言った時だけ main に一覧を 1 回作らせ、それを Vulkan の命令にして GPU に送り、表示を頼み、終わるのを待ちます。作った一覧は必ず描くので、作った時に決まった事（焼いた絵・画面の写し・client の画像の扱い）が無駄になったり食い違ったりしません。
- **cursor が画面で 60fps になるには、WS141 の cursor の速い経路（B-u、GPU を通らない位置だけの表示）が要ります**（review-1 C1）。今の表示の仕組みは、cursor の表示の依頼を画面のコマの依頼と同じ 1 本の列に並べて GPU の完了を待つので、main を軽くしても、重いコマの間は cursor の表示が後ろに並びます。この WS は main の止まりを無くし、B-u と合わせて 60fps にします。
- **段**: p002 測る＋cursor の依頼の間引き（実装済み、Pi の確認待ち）→ p003 描く物の一覧（thread はまだ使わない、見た目が同じことを確かめる）→ p004 描画 thread（pull）→ p005 作業 thread（文字の形・壁紙のぼかし）→ p006 規約の見直し。
- **SMP（4 core）が無い今の効果**: CPU の総量は減りませんが、入力が来ると main が描画 thread を割り込んで先に動けるので、入力の処理の遅れは減る見込みです。コマの速さ（fps）は SMP（WS208）か WS211 の描画の削減で上がります。

## 由来

- 2026-10-12 ユーザー:「コンポジタのマルチスレッド化も入れてください。ウィンドウへの通知の転送以外の処理はほとんどすべてスレッドにして、スレッドに通知を出して処理を委譲しましょう。ただ、描画のシリアライズはいるかも？と思いました。描画スレッドが別途あって、コマンドキューがシリアライズされるのかなあ。それとも描画はイベントキューに積むのか？」
- Q1 の推す形（main＝protocol・入力・cursor、描画 thread 1 本、作業 thread、最新の写しを受け箱に）にユーザー「了解です。おすすめがいいと思います。あとでできたらレビューさせてください。」
- 2026-10-12 ユーザーの判断（review-1 H6、Q1 経由）:「描く物の一覧＋pull」— main が今の描画の walk で display list を作り、描画 thread が「次をくれ」と言った時だけ 1 回作る。記録・submit・present・GPU の待ちは描画 thread。第 1 版の「受け箱の置き換え」はやめる。
- この Phase は設計だけ（p002 の M0・C0 は Q1 の指示で先に実装）。

## review-1 への対応

| 指摘 | 対応（節） |
| --- | --- |
| C1 cursor の画面の 60fps は C0・S2 では上がらない（WSI の present の worker は queue ごとに 1 本、cursor と出力のコマが同じ queue、`KWL CURSOR_PLANE present` の時刻は呼んだ時刻） | 受け入れの cursor の 60fps は WS141 p016 (B) の B-u への依存と明記（6 節）。M0 に present の内訳・最長、evdev の時刻 `event_us` を足した（p002）。表示の側の時刻（kernel の CURSOR_SET・MOVE）は WS141 に頼む（11 節） |
| C2 写しの置き換え・捨てで walk の副作用が巻き戻らない | pull に変えた。作った写しは必ず描く。出力の世代の変更は park の後にだけ起き、R が写しを捨てる経路は無い（4.2・4.8） |
| H1 shm の画像の寿命と walk が読む値 | import の記録と metadata は M が commit の時に同期で作り、R は Vulkan の object だけ。古い画像は参照する全部の写しの完了で退役（4.7） |
| H2 M が呼ぶ Vulkan の列挙の不足 | 全部を表にし、作成は open・park の時か R への同期の依頼。非同期の作成の結果で walk を分けない（3.2） |
| H3 cursor の present は queue_lock があっても R を待つ | zedBSD の libvulkan の build では compositor の queue_lock を使わない（Mesa の build だけ）。待ちは M0 の `present_max` で測る。本当の解決は B-u（6 節） |
| H4 park の deadlock と上限の無い待ち | R の acquire・fence・W の待ちは上限付きの繰り返しで park・停止の印を見る。session の pause・sleep は先に park（4.8） |
| H5 置き換えの回数・1 core・damage の履歴 | pull で walk は R の 1 コマに 1 回。frame の番号は M が答える時に付ける（4.2・4.5） |
| H6 ユーザーの合意からのずれ | ユーザーが「描く物の一覧＋pull」を判断（由来） |
| M1 heads | head ごとの条件付きの区間、R が結果を返し M が完了で確定（4.3・4.4） |
| M2 submit の結果が server の状態を書く | 完了の記録の欄にし、M が完了で適用（4.4） |
| M3 glyph の細部 | 色の emoji と予備の font の open は M に残す、W の job の番号の規則（5 節） |
| M4 game mode | park の一覧に（4.8） |
| M5 R が region を狭める | p004 ではしない（4.5） |
| M6 「1 回約 3ms・6 割」は推定 | 推定と明記、M0 で確かめる（1.4・p002） |
| M7 Phase の分け方 | p002〜p006 に分けた。S5・S6 は Future Work へ（Q1 に依頼、8 節） |
| M8 試験の不足 | 9 節に足した |
| M9 bind の鍵が VkDescriptorSet の値 | 一覧の bind は（import の pointer、種類、世代）を鍵に、golden の比較で handle を正規化（4.3・9 節） |
| Low: C0 の shown の切り替え・最初と最後 | 確かめた（p002 の節） |
| Low: compose->command を直に使う所、LAT の意味、retire と shm の飢え | 11 節 |

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
2. **cursor の plane の present は pointer の event ごと（約 125Hz、画面の 60Hz の 2 倍）に行われる**。「1 回約 3ms、cursor を動かすだけで main の 6 割」は、work から compose を引いた残りを present の数で割った**推定**で、他の仕事も混ざる（review-1 M6）。M0 の `KWL PERF main` の `cursor=`・`present=` で確かめる。今の libvulkan の WSI は位置だけの present でも GPU の copy の job を送り、present の worker は queue ごとに 1 本で各 job の fence を待つ（`wsi-swapchain.c:2664-2710,2776-2782`）。
3. 残りの内訳は測らないと分からない（段 M0）。


## 2. 形: 描く物の一覧（display list）＋ pull

| 案 | 内容 | 判断 |
| --- | --- | --- |
| 案 A 状態の写し | server の状態を写し、描画 thread が今の描画のコードを写しに対して動かす | 採らない。描画のコードは 26 file が server・client の object・glass・home の状態を直に読み、hover の probe も描画が記録する |
| **案 B 描く物の一覧** | M が今の描画の walk を行い、一番下（vkCmd* を呼ぶ約 60 か所）だけを「一覧に op を足す」に変える。R が一覧を Vulkan の命令にして送る | **採る**（ユーザーの判断）。描画の論理は変えない。M に残るのは walk（形の位置・push constant・間引き・hover の記録）だけ |
| 案 C secondary command buffer | M が secondary を記録、R が vkCmdExecuteCommands | 採らない。Pi で高い所（libvulkan の encode と End の transaction）が M に残る |

- **pull**: R が「次をくれ」と知らせた時だけ、M は描く物があれば walk を 1 回して一覧を渡す。M が勝手に写しを作って置くことは無い。写しは作ったら必ず R が描く（置き換え・捨ては無い）。
- 案 B の前提（p003 で測る）: walk の費用は記録全体の一部。根拠: 記録の費用は draw の数にほぼ比例し（0〜100 draw で 3〜8ms、300 以上で 48ms）、中身は libvulkan の encode と End の decode の transaction（kernel の `hold_us decode` 最大 11〜19ms）が大半。外れて walk が重いなら効果は小さく、p004 に進む前に WS211 の削減を先にする。

## 3. thread の分担と Vulkan の持ち主

### 3.1 分担

| thread | 受け持つ物 | 持たない物 |
| --- | --- | --- |
| **M（main）** | poll、Wayland の protocol、入力（evdev、pointer の加速、hover の判定、focus、event の配達）、cursor の plane、server の状態の全部、R に頼まれた時の walk、frame pacing、client の buffer の参照の数、frame callback、log | 出力の command buffer・descriptor pool・出力の swapchain・出力の queue の submit |
| **R（描画、1 本）** | 出力と heads の acquire・present、一覧の再生（vkCmd*）、host image の staging への書き込みと copy の記録、`vkQueueSubmit`、fence の待ち、descriptor set、R が作る object の作成と破棄、M からの同期の依頼 | server の状態（読まない・書かない） |
| **W（作業、まず 1 本、SMP 後は core−1）** | glyph の raster、壁紙の描画とぼかし、大きな字 | Vulkan、server の状態 |

### 3.2 M が今呼んでいる Vulkan の全部と行き先（review-1 H2）

規則: **非同期の作成の結果で walk を分けない**。walk が「あるか」を見る object は、open・park の時か、R への同期の依頼（M が R の返事を待つ、R の 1 コマ以内）で作る。

| 所 | 今（M） | p004 の後 |
| --- | --- | --- |
| 出力の swapchain・framebuffer・scene・layer の画像・batch の buffer・load pass（`compose.c:383,2331`、`backdrop.c:216`） | 出力の open | park の中で M が作る（今のまま） |
| backdrop の target（`backdrop.c:69-79,395,459`、walk の中で最初の時に作る） | walk の中 | 出力の open で作るか、walk が R に同期の依頼。最初の 1 回だけ M が待つ |
| cursor の形の画像（`cursor.c:262-270,388`、`image_make` 722 → `shm.c` の作成・`vkQueueSubmit`＋`vkQueueWaitIdle` 877-882・`compose->pool` 842・`set_get` 801・`host_images` 796-797） | 最初に使う時 | R の起動の前に全部作る（形は十数個、64×64） |
| 大きな字の画像（`glass.c:2147-2152`、greeter `1633`、`vkDeviceWaitIdle`） | 大きさが変わる時 | 画像は glass の open で作る。新しい大きさの digits は W が描き R が写す（5 節） |
| 壁紙の描き直し（`glass.c:648` の `vkDeviceWaitIdle`）・glass の大きさ（451・493） | M | 壁紙は W＋R（5 節）。大きさの変更は park |
| client の import（backend の `vkCreateImage`・memory・bind、`kwl_import_adopt` の view） | protocol の request の中 | M のまま（device 単位の作成、Vulkan の規則で並行してよい）。R へ移すのは Future Work（8 節） |
| import の descriptor set（`import.c:258,276`、`compose.c:2005-2083`） | import の時 | R が初めて再生する時に割り当てる。一覧は（import の pointer、種類（普通・linear）、import の世代）で指す（M9） |
| import の layout の移動（list、`import.c:229-242`、溢れた時は `import.c:248-252,348,353` の即時の submit＋WaitIdle） | 次のコマ、溢れは即時 | list を写しの中身にし、上限を無くす（伸ばす）。溢れの即時の経路は無くなる |
| import の破棄（`objects.c:830`） | buffer の破棄 | M が参照 0 を確かめて R に破棄の依頼（4.7） |
| wl_shm の画像（`shm.c` の `image_create`・`image_release`、`kwl_shm_image_destroy` の quiesce 187） | commit の後・frame の外 | 記録と metadata は M、Vulkan の object は R（4.7、H1） |
| shot の buffer（`shot.c:445,472`、試験の image だけ） | shot の open | R への同期の依頼 |
| heads の swapchain・view（`heads.c`） | heads の sync・hotplug | park |
| cursor の plane（`cursor-plane.c`） | pass ごと | M に残す。command pool を M 専用に分ける（今は `compose->pool`） |
| frame の fence の待ち（`kwl_compose_complete`・`kwl_compose_poll`） | M | R |
| 出力の close・終了・session の pause・sleep・game mode の切り替え | M（`vkDeviceWaitIdle`・quiesce） | park（4.8） |

### 3.3 queue の lock

- 出力の queue は 1 本（bcm2711 の queueCount は 1、`vulkan-query.c:182`、i915 も 1）。cursor の plane の submit・present（M）と出力の submit・present（R）が同じ queue を使う。
- **zedBSD の libvulkan の build では compositor の queue_lock を使わない**（review-1 H3）。libvulkan が `queue->mutex`（`queue.c:333`、`wsi-swapchain.c:557`）・`device->mutex`（`queue.c:422-460`）・`context->mutex`（`context.c:496`）で中で直列にするので、compositor の lock を足しても待ちは減らず、trylock で避けることもできない（M の present は R の vkQueueSubmit・End の decode・capacity の待ち（`queue.c:362-366`）を待ち得る）。Linux・FreeBSD の host の Vulkan（Mesa）の build だけ、Vulkan の外部同期の規則のために compositor の `queue_lock` を submit・present の呼び出しの間だけ持つ（zedBSD の libvulkan の build か host の Vulkan の build かで切り替える。印の置き場は p004 の実装で決める）。
- 待ちの長さは M0 の `present=`・`present_max=` で測る。本当の解決は B-u（6 節）。

## 4. pull の 1 周と写し

### 4.1 R の 1 周

1. FIFO の依頼（同期の依頼、R の object の作成・破棄、park）を順に処理。
2. 「次をくれ」を M に知らせる（R→M の pipe、M の poll に入る）。M の答え（写し）か park を待つ。待ちは上限付きの繰り返し（100ms ごとに停止・park の印を見る）。
3. 出力（と heads）の `vkAcquireNextImageKHR` を上限付き（例 50ms）で繰り返す。間に park・停止の印を見る（H4）。heads は取れなければその head の区間を飛ばす（M1）。
4. host image の更新を適用（W の結果を待つ物は上限付き、5 節）、copy を記録。
5. 一覧を再生して記録、`vkEndCommandBuffer`、`vkQueueSubmit`・`vkQueuePresentKHR`・heads の present。
6. fence を上限付きで繰り返し待つ → 完了の記録を M に渡す（4.4）。
7. 1 へ。飛んでいるコマは 1 つ（今と同じ）。「次をくれ」を知らせるのは前のコマの fence の後なので、M が walk する時には前のコマが終わっていて、walk は今と同じく最新の状態を読む。GPU と walk を重ねる（fence の前に知らせる、2 コマ飛ばす）は Future Work。

### 4.2 M の答え方

- M は R の「次をくれ」を印にして持つ。印がある pass で、今の `kwl_schedule` の判断（dirty・damaged・pacing・sleep_hold・os_paused・game mode）が「描く」なら walk を 1 回して写しを渡し、印を下ろす。描く物が無ければ印を持ったまま（R は待つ）。layer の静かな時の焼き（`kwl_layers_tick`）も同じ印で 1 回の写し（present しない種類）にする。
- frame の番号は M が答える時に付ける（H5）。写しは必ず描かれるので、walk の時に確定する副作用（review-1 C2 の列挙: layer の `decided_anim`・`keys`・`valid`・`inflight`、shot の `SHOT_RECORDED`、import の layout の list、shm の行と release、壁紙・glyph の staging の依頼、heads の印、scene の `written`）はそのまま正しい。ただし「完了で確定する物」（layer の `valid`・scene の `valid`・shot の完了・shm の release・heads の lost）は今どおり完了の記録で確定する（4.4）。
- M は R が描いている間に新しい写しを作らない。だから M の walk は R の 1 コマに 1 回（H5、約 125Hz の walk は起きない）。

### 4.3 写しの中身

| 物 | 内容 |
| --- | --- |
| 番号・種類 | frame の番号、種類（出力のコマ / present しない layer の絵） |
| 一覧（op の列） | render pass の開始（相手は記号: 出力の画像・head k の画像・layer の絵・backdrop の target・scene）と終わり、viewport、scissor、pipeline・descriptor set の bind、push constant（inline）、vertex buffer の bind、draw、barrier・copy・clear（scene・shot・backdrop）。pipeline・layout・sampler は open で作り R の間は不変の handle。client の画像は（import の pointer、種類、世代）、host image は pointer で指し、R が再生の時に set を引く。heads の区間は「head k が取れた時だけ」の印で囲む（M1） |
| 頂点の配列 | WS211 p003 の束の頂点（R が再生の時に HOST_COHERENT の buffer に写す） |
| damage | M が計算した region（4.5）、全体かどうか、scene の capture・reuse |
| host image の更新 | shm の写し（import、行の範囲、buffer の参照）、glyph の cell（W の job の番号）、大きな字・壁紙・ぼかし（W の結果）、import の layout の移動の list |
| layer・shot | layer の世代、shot の依頼 |
| M だけの帳簿 | 保持した buffer の参照、frame callback、pacing の印。R は触らず、不透明な pointer を運ぶだけ |

### 4.4 完了の記録（R→M、M2）

frame の番号、結果（描いた・失敗）、画像ごとの最後の frame と画像の数、heads ごとの結果（取れた・missed・lost）、出力の present の結果（`compose.c:2829-2853` の `switch_proving` の成否 → `kwl_displays_move_settled`、`output_lost`）、scene の written、layer の焼きの結果、shot、shm の写しの済み、計測（acquire・再生・End・submit・present・GPU の時間）。M は今の `kwl_compose_complete` と `compose_submit` の後ろ半分の server の状態の書き換えを、この記録で行う。

### 4.5 damage と buffer age

- R は完了の記録で各画像の最後の frame の番号を返す。M は全部の画像のうち一番古い物からの damage の和を region にする（どの画像を取っても足りる）。描いたことの無い画像があれば全体。pull で walk は R の 1 コマに 1 回なので、`KWL_DAMAGE_HISTORY`（8、`compose.h:270`）を超えることは今と変わらない。
- R が acquire の後に region を狭めることは p004 ではしない（M5: walk が `SetScissor` を直に記録する所 shell.c 2 か所・backdrop.c:136、backdrop の end の全体の renderArea backdrop.c:128-131 がある）。

### 4.6 費用と lock

受け渡しは pointer の交換だけ。一覧は App Home の 460 draw で約 70KB＋束の頂点（最大 1MiB）。buffer は 2 つ（M が作る・R が再生）を回す。共有は「次をくれ」の印・写し・完了の list・依頼の FIFO・park の印だけで、1 つの mutex（`render_lock`）の下。

### 4.7 object の寿命（H1）

| object | 作る | 壊す | 守り |
| --- | --- | --- | --- |
| client の import（image・memory・view） | M（backend） | M が参照 0 で R に依頼、R が次の周の始めに | 写しの帳簿の保持。参照 0 は完了の後にしか来ない |
| import の descriptor set | R（初めて再生する時） | R（import と一緒） | 同上 |
| wl_shm の画像 | `struct kwl_import` の記録と metadata（大きさ・`draw`（`shm.c:599-601`）・version）は M が commit の時に同期で作る（walk が読む `compose.c:2124-2128,2165,2257`）。Vulkan の object は R がその記録を初めて写す時に作る | 大きさが変わったら M が新しい記録を作り、古い記録は参照する全部の写しの完了の後に R に破棄の依頼（今の `kwl_shm_image_destroy` の quiesce は無くなる） | surface と写しの帳簿の参照 |
| glass の host image・cursor の形 | R の起動の前 | park の中 | 起動から終了まで |
| backdrop・shot の buffer | 3.2 の表 | R | 世代 |

### 4.8 park と、pause・sleep・game mode・出力の変更（H4・M4）

- park: M が依頼すると、R は今のコマを最後まで描いて fence を待ち、止まって知らせる。M はその間 Vulkan を今のコードのまま扱い、unpark。R の待ちはどれも上限付きの繰り返しで park の印を見るので、R が acquire や fence で止まっていても park は届く。
- park を使う所: 出力の open・close・移動（output-switch.c・displays）、heads の hotplug、glass の大きさ、**session の pause（`backend-host.c:46`、今の quiesce＋output close の前に park）**、**sleep の依頼（`display.c:160-166` の sleep_hold の前に park）**、**game mode の入りと出（`scanout.c:90,284`、`display.c:174-186`）**、終了。os_paused・sleep_hold の間、M は R の「次をくれ」に答えない。
- 出力の世代が変わるのは park の中だけなので、古い世代の写しは存在しない（R が捨てる経路は無い、C2）。

## 5. 作業 thread と glyph・host image（M3）

- staging に書くのは R だけ、書くのは R の周の 4（前のコマの fence の後）だけ。
- **glyph**: M の `glass_cache_glyph` は cell の選び（LRU）と metrics だけを同期で行い、raster の job（face・glyph id・大きさ・cell・job の番号）を W に出す。写しは「この写しが要る job の最大の番号」を持ち、R は周の 4 でその番号まで W が終えるのを（上限付きで）待ち、bitmap を staging の cell に書く。W は自分の truetype の face を持つ（libtruetype に file 単位の可変の状態は無い）。
  - **色の emoji**（`glass_cache_color`、`glass.c:3199-3240`）は metrics が画像から決まるので、p005 では M に残す（まれ）。
  - **予備の font・emoji の font の open**（`glass.c:3105-3125`、初めての字の時に file を読む）は M に残す（1 回だけ）。p005 で起動の時に先に開く案を測る。
  - 追い出し: cell の上書きは R が前のコマの後に行うので、飛んでいるコマの cell は書き換わらない。焼いた layer の絵は焼いた時に sample 済み。残るのは今と同じ「1 つの写しの中で cache より多い種類の字」で、guard の扱いは今のまま。記録の外の `glass_text_width` は metrics だけになる。
- **壁紙**: `wallpaper_draw` とぼかしを W に、結果を R が周の 4 で写す。
- **大きな字**: 新しい大きさの digits を W が描き、R が写す。届くまでは古い大きさで描く。
- **shm**: memcpy を R の周の 4 に。M は buffer の参照を持ったまま、R の完了の記録で release する。

## 6. cursor と入力（C1・H3）

- **M に残す**: evdev、pointer の加速、hover、focus、event の配達、cursor の形の決定、cursor の plane の present（p002 の C0 で 1 refresh に 1 回、最新の位置）。
- **画面に映る cursor の速さは B-u に依存する**（C1）: libvulkan の present の worker は queue ごとに 1 本で後の job は前を追い越さず、各 job の GPU の fence を待つ（`wsi-swapchain.c:2664-2710,2745,2776-2782`）。cursor の plane と出力のコマは同じ `compose->queue`（`cursor-plane.c:468,500`、`compose.c:2813,2831`）。だから M を軽くしても、重いコマの present の後ろに cursor の present が並ぶ。WS141 p016 (B) の B-u（位置だけの present が worker も GPU も通らず ticket で追い越す）が入って初めて、重いコマの間の cursor が 60fps になる。この WS の受け入れ（cursor の 60fps）は B-u を前提にする。
- **測り方**: `KWL CURSOR_PLANE present` の `at_us` は M が呼んだ時刻で、画面に出た時刻ではない。M0 で `event_us`（evdev の kernel の時刻、`input.c` のマウスの report）と `present_cycles`（present の呼び出しの中の時間）を足した。表示の側の時刻（kernel の `CURSOR_SET`・`MOVE` の時刻）は WS141 に log を頼む。B-u の後は event_us→ioctl の差と present の間隔で判定する。
- **M の present の待ち**: zedBSD の libvulkan では M の present は R の vkQueueSubmit・End の transaction・capacity の待ちを待ち得る（3.3）。B-u の後も `device->mutex` の区間が残るので、M0 の `present_max` が 1 refresh を超えるなら、WS141 に WSI の cursor の状態を `device->mutex` の外に出す設計を頼む（B-u の L1 と合わせて）。
- **plane が無い出力**（i915・Venus・Linux・FreeBSD）: cursor は composite。pointer の動きは damage → R の次の「次をくれ」で walk。遅れは M の walk＋R の 1 コマ。

## 7. libvulkan の thread の安全、Linux・FreeBSD

- zedBSD の libvulkan: session ごとの `context->mutex` が kernel との transaction を直列にする。command buffer の vkCmd* は貯めるだけで lock を取らない（command buffer は R だけが使う）。M と R が同時に呼ぶのは (a) M の backend の import と R の transaction（`context->mutex`、R の 1 transaction の長さだけ待ち得る）、(b) M の cursor の present と R の submit（3.3）。正しさは保たれ、問題は待ち時間。
- Vulkan の規則（Mesa など）: command pool・descriptor pool・command buffer は thread ごと（R と M の cursor の plane で分ける）、queue は Mesa の build だけ `queue_lock`、device 単位の作成は並行してよい。
- Linux・FreeBSD の backend: 変わるのは compositor の中だけで、`libkeiland-backend` の API は変えない。pthread は既に使っている（Linux の glibc、FreeBSD の `-pthread` `Makefile.freebsd:96`）。新しい file は `Makefile`・`Makefile.linux`・`Makefile.freebsd` に足す。

## 8. Phase の分け方（M7）

| Phase | 内容 | 状態・順 |
| --- | --- | --- |
| p001 | 調査と設計（この文書） | 第 2 版、再 review |
| p002 | M0（main loop の区間・cursor の present の内訳・event_us の計測）と C0（cursor の present を 1 refresh に 1 回） | 実装済み（M0・C0 は main 33cdc3042、review-1 の追加は agent/p4）。Pi の確認待ち |
| p003 | S1: display list。`struct kwl_list` と op、描画の関数の `VkCommandBuffer command` の引数（37 file）を `struct kwl_list *` に、compose->command を直に使う所（`compose.c:2365〜`、`layer-cache.c:184`）も一覧へ、batch の頂点を一覧に、bind の鍵を（import、種類、世代）に（M9）。再生は同じ thread の同じ場所。先に 3.2 の遅延作成を open に移す | WS211 の P3 の作業（p003 段 2・p005・p006）と衝突するので、順は Q1 が決める |
| p004 | S2: R の thread（pull、依頼の FIFO、park、完了の記録、Vulkan の持ち主の移動、shm・descriptor set の R への移動、上限付きの待ち） | p003 の Pi の walk の計測の後 |
| p005 | S3: W（glyph の raster、壁紙、大きな字） | p004 の後 |
| p006 | 規約の全文の見直し | 最後 |

Future Work（2026-10-12 Q1 が記入）: F-094 client の画像の import を R へ（backend の API の変更、zedBSD・Linux・FreeBSD の 3 つの backend、別の設計）、F-095 GPU と walk を重ねる・2 コマ飛ばす（WS211 p006 の案 3 と合わせて）、F-096 cursor の plane に別の VkDevice（別の session・queue）。kernel の CURSOR_SET・MOVE の時刻の log は Q1 が WS141（P3）に依頼。

## 9. 試験

- host（決定的、thread を使わずに手順を 1 歩ずつ呼ぶ）:
  - (a) **再生の等価**（p003）: デスクトップ・窓 2 つと popup・App Home・drag の部分の描き直し・backdrop・heads 2 つ・layer の焼き・batch の on と off を、直の記録と一覧の再生で vkCmd* の stub に流し、命令の列（引数と push constant の bytes）が同じこと。**descriptor set の handle は（import、種類）に正規化して比べる**（M9）。golden は p003 の最初の commit で取る。
  - (b) pull（p004）: 「次をくれ」が無い間は walk しない、描く物が無ければ印を持ったまま、1 回の印に 1 回の walk、frame の番号。
  - (c) **副作用の確定**（M8）: layer の焼き（`decided_anim`・`keys`・`valid`）、shot の依頼と完了、import の layout の list、shm の行の写しと release の順、heads の missed と lost、`switch_proving` の結果が、完了の記録の時に今と同じ状態になる。
  - (d) damage: 画像 3 つと 2 つ、描いたことの無い画像で全体、M の知る番号が 1 コマ古い時に足りる。
  - (e) park: R が acquire・fence の待ちの途中の park、park の間に M が出力を作り直し、unpark の後の最初の写しが全体。session の pause・sleep・game mode の入りの前の park。
  - (f) 寿命: shm の大きさの変更で古い記録が参照する全部の写しの完了まで生きる、import の破棄の依頼は完了の後。
  - (g) 遅延作成の移動（p003）: backdrop・cursor の形・load pass・大きな字が open で作られ、walk が作成を呼ばない。
  - (h) glyph（p005）: 未着の glyph の写しで R が job を待ってから staging に書く、cell の上書きは飛んでいるコマの後。
  - C0（p002）: `plan/ws213/tests/cursor-due-host-test.sh`（PASS）。
- build: rpi4・amd64・`make keiland-linux`・FreeBSD で warning 0。p004 の時に Linux の build を ThreadSanitizer 付きで WS131 の Debian の QEMU+KVM guest で動かすかを Q1 と決める。
- T1（p003・p004・p005 の終わりにまとめて）: i915（5330）と Venus の QEMU で shot.c の画面の写しを前と比べる、`KWL VULKAN_ERROR`・`KWL FAILED` が無い、App Home の開閉・drag・終了。
- Pi: `KWL PERF main`（区間、`cursor presents=`・`present_max=`）、`KWL CURSOR_PLANE present` の `event_us`→`at_us` と間隔、`KWL LAT`（入力→shown）、`SCHED_WAKE_LATENCY`（必要なら）。場面: pointer だけ・App Home の開閉・drag・Music・Settings の起動。

## 10. SMP が無い間の効果の見込み

- Pi の kernel は 1 core（WS208 まで）。thread にしても CPU の総量は同じで fps は上がらない。
- 効くのは M の遅れ: kernel は起きた thread が同じ優先度なら走っている thread を次の安全な点で横取りする（`src/kern/sched.c` の `wake_enqueue_locked` 1800-1834）。evdev が読めて M が起きると、R の CPU の仕事（encode）を横取りして先に入力を処理する。R が kernel の中（transaction の decode）にいる時は安全な点まで待つ（kernel の GPU の thread も同じ既定の優先度 `render-worker.c:115,121`）。
- 見込み（p002・p003 の実測で直す）: 1 コマで M が止まる時間は今 10〜20ms（App Home 60〜120ms）→ p004 の後は walk だけ（数 ms と見込む）。M の present の待ち（3.3）は残る。画面の cursor の 60fps は B-u の後（6 節）。

## 11. 未決と危険

1. walk の費用（p003 で測る）。重ければ p004 の効果は小さく、WS211 の削減が先。
2. M の present の待ち（R の submit・End・capacity）。M0 の `present_max` で測り、大きければ WS141 へ（6 節）。
3. 表示の側の時刻（kernel の CURSOR_SET・MOVE）の log を WS141 に頼む。`event_us` の時計（kernel の `clock_milliseconds`、`src/drivers/generic/input.c`）と M の `kwl_microseconds`（CLOCK_MONOTONIC）が同じ基準かを Pi で確かめる。
4. `KWL LAT`・`KWL FIRST_FRAME` の意味の定義し直し（p004）: draw＝M の walk の始め、recorded＝R の End、queued・submit＝R、shown＝R の fence の後に M が完了を受けた時。
5. retire（`objects.c:739`）と shm の写し（`shm.c:146`）は今「frame が飛んでいない時」に止まる。p004 では in_flight の意味が「R が描いている」になり、R が続けて描くと飢え得る。retire は R の完了の記録の後に 1 つずつ、shm の写しは写しの中身（R の周の 4）にして、in_flight を見ない形にする。
6. `kwl_compose_quiesce` の 4 か所（`objects.c:554`、`backend-host.c:46`、`main.c:1097`、`shm.c:187`）と `->in_flight` の 16 か所は、p004 の実装の前に 1 つずつ park・retire・写しの帳簿のどれかに当てる。client の破棄で M が 1 コマ待つのは避け、retire に寄せる。
7. 動きの時刻: walk の時に animation の時刻を読むので、表示されるのは R と GPU の時間だけ後（今も同じ）。

## 12. 結果の記録（この Phase）

- 調べたこと: 1 節（source の地図、Pi の既存の log の集計。P3 の boot の b4・b6・b10）。
- 変えた file: この phase.md、[review-1](review-1.md)（design-reviewer の要点の写し）、ws.md。p002 の分は下の節。
- 次: 第 2 版の再 review → Q1 が p003 の順を WS211 と調整。

## p002（M0・C0）の実装（2026-10-12 P4、Q1 の「review を待たずに M0・C0 を実装してよい」、main 33cdc3042）

- **M0**（計測だけ、動作は変えない）: `struct kwl_perf`（kwl.h）に pass の区間の cycle を足し、`main.c` の event loop と `display.c` の `kwl_schedule` で測る。5 秒ごとの `KWL PERF` に 1 行 `KWL PERF main ticks= input= clients= frame_done= schedule= flush= other= | schedule ticks= adopt= shm= cursor= layers= draw= | cursor presents= held=` を足した（値は窓の中の ms の合計。ticks は poll の前の tick で work の外、other は work のうち区間に入らない分（descriptor の作成・accept・OS・clipboard ほか）、held は refresh を待った pass の数）。
- **C0**（cursor-plane.c）: 同じ像（source と version が同じ）の位置だけの present は、前の present から 1 refresh（`server->refresh`、無ければ 60Hz）の間は出さずに `deferred` にし、refresh の終わりに最新の位置を出す。最初の動き（静かな refresh の後）と像の変化はすぐ出す（plane と frame の cursor が 1 refresh 食い違わないように）。`kwl_cursor_plane_due_ms` が event loop の poll の timeout を次の present の時刻まで縮める（最小 1ms、さらに 1 refresh 過ぎても誰も出さなければ -1 に戻して 1ms ごとに起きない。pause・sleep・game mode で scheduler が plane まで来ない時のため）。`plane_present` の早い return（copy の fence が未完了・空き画像が無い）は今どおり次の pass で再試行（`deferred` は立てない）。WS141 p016 (B) の B-u（libvulkan の速い経路）とは独立で、B-u が入れば 1 回の present がさらに軽くなる。
- 変えた file: `userland/desktop/wayland/kwl.h`・`compose.h`・`cursor-plane.c`・`display.c`・`main.c`。試験: `plan/ws213/tests/cursor-due-host-test.c`・`.sh`（新規）。
- 確かめ:
  - host: `sh plan/ws213/tests/cursor-due-host-test.sh` → `cursor-due-host-test: PASS`（偽の Vulkan と時計で、最初の動きはすぐ、同じ refresh の中の 2 回は保留して due が 12・7ms、refresh の終わりに最新の位置、止まった pointer は何もしない、静かな後の動きはすぐ、像の変化は保留しない、誰も出さない保留は 1 refresh 後に -1、refresh 0 は 60Hz、perf の数）。
  - build: rpi4（`make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/bin/wayland`）rc 0・warning 0、amd64（`config/ci/config-amd64.mk`、`build/amd64/bin/wayland`）rc 0・warning 0、`make keiland-linux` rc 0・warning 0。`git diff --check` 空。
  - 注: 自分の worktree の `build/arm64/sysroot` は、この make が初めて作った（共有の build・toolchain の tree には触れていない）。最初の make は -j の順で sysroot より先に libc の math を compile して失敗し、2 回目で通った。
- 未実施: Pi・T1。Pi で見ること: `KWL PERF main` の区間（pointer だけ・App Home の開閉・drag・app の起動）、`cursor presents=` が 60/s 程度に減ること、`KWL CURSOR_PLANE present` の間隔（約 16.7ms）と入力→present、work% の減り。T1: i915・Venus は plane が無いので C0 は動かない（M0 の行が出て、`KWL FAILED` が無いことだけ）。

### review-1 の後の追加（2026-10-12 P4、agent/p4）

- **M0 の追加**（C1・H3）: `plane_present` の内訳を `struct kwl_perf` に（copy の fence の問い合わせ・acquire・copy の記録と submit・`vkQueuePresentKHR` の呼び出しの中の時間と、その最長）。`KWL PERF main` の行の末に `fence= acquire= copy= present= present_max=`（ms、早い return の pass は数えない）。`present` は libvulkan の `queue->mutex`・`device->mutex`・`context->mutex` の待ちを含む。`KWL CURSOR_PLANE present` の行に `event_us=`（そのマウスの report の kernel の時刻、`input.c` の `device->frame_time_us` を `server->pointer_event_us` に）と `present_cycles=` を足した。表示の側の時刻（kernel の CURSOR_SET・MOVE）は compositor からは取れないので WS141 に頼む（11 節）。
- **C0 の Low の確かめ**: (1) plane と composite の切り替え（`kwl_cursor_plane_update` の `shown` の変化）は間引きの前にあり、切り替わる時は像（`source`）が変わる（NULL ⇔ cursor の像）ので間引かれない。(2) 最初の動きは即時、周期の間の動きは保留して周期の終わりに最新の位置（host 試験の (1)・(4)・(6)）。直す所は無い。
- 確かめ: host `sh plan/ws213/tests/cursor-due-host-test.sh` PASS（`kwl_cycles` の stub を足した）。build: rpi4・amd64 の `bin/wayland` rc 0・warning 0、`make keiland-linux` rc 0・warning 0、`git diff --check` 空。
