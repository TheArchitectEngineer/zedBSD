<!-- awesome-plan project=zedbsd record=ws213-p001 -->

# ws213-p001: compositor の thread 化の調査と設計

Parent: [WS213](../ws.md)
Status: planning（設計の第 3 版。[review-1](review-1.md)・[review-2](review-2.md)、2026-10-12 のユーザーの判断（描く物の一覧＋pull）と Q1 の判断（2 段の pull ほか）を反映、再 review の前）
Phase disposition: normal
Queue: P4（2026-10-12 Q1 が割り当て）

## ユーザー向けの要約

- **今の問題**: compositor は 1 本の thread で、1 コマを描く間（命令の記録・GPU への送り・表示の依頼）は入力も cursor も止まります。Pi 4 の記録では、普通のコマで 10〜20ms、App Home のような重いコマで 60〜120ms 止まっています。cursor の表示の依頼も、画面の書き換え（60 回/秒）より多い約 125 回/秒行っていて、main の時間をかなり使っている見込みです（測定を足しました、M0）。
- **形（2026-10-12 ユーザーの判断「描く物の一覧＋pull」）**: main thread は Wayland の protocol・入力・cursor と、今の描画の処理で「描く物の一覧」を作る所までを受け持ちます。描画 thread は、画面の画像を取り出せてから「次をくれ」と言い、main はその時だけ一覧を 1 回作ります。描画 thread はそれを Vulkan の命令にして GPU に送り、表示を頼み、終わるのを待ちます。画像を取り出せた後にしか一覧を作らないので、作った一覧は必ず描かれ、作った時に決まった事（焼いた絵・画面の写し・client の画像の扱い）が無駄になったり食い違ったりしません。描画 thread は main が一覧を作っている間は何もしないので、その間 main は今のコードのまま GPU の物を作れます。
- **cursor が画面で 60fps になるには、WS141 の cursor の速い経路（B-u、GPU を通らない位置だけの表示）が要ります**（review-1 C1）。今の表示の仕組みは、cursor の表示の依頼を画面のコマの依頼と同じ 1 本の列に並べて GPU の完了を待つので、main を軽くしても、重いコマの間は cursor の表示が後ろに並びます。この WS は main の止まりを無くし、B-u と合わせて 60fps にします。
- **この WS だけで保証できること**: main の止まりを、描画 thread の 1 回の GPU への送り（数 ms〜最大二十数 ms）以内にすることです。cursor の表示の依頼がそれでも待つ場合は、cursor の表示だけを受け持つ小さな thread を足す案を用意しています。
- **段**: p002 測る＋cursor の依頼の間引き（実装済み、Pi の確認待ち）→ p003 描く物の一覧（thread はまだ使わない、見た目が同じことを確かめる）→ p004 描画 thread（pull）→ p005 作業 thread（文字の形・壁紙のぼかし）→ p006 規約の見直し。
- **SMP（4 core）が無い今の効果**: CPU の総量は減りませんが、入力が来ると main が描画 thread を割り込んで先に動けるので、入力の処理の遅れは減る見込みです。コマの速さ（fps）は SMP（WS208）か WS211 の描画の削減で上がります。

## 由来

- 2026-10-12 ユーザー:「コンポジタのマルチスレッド化も入れてください。ウィンドウへの通知の転送以外の処理はほとんどすべてスレッドにして、スレッドに通知を出して処理を委譲しましょう。ただ、描画のシリアライズはいるかも？と思いました。描画スレッドが別途あって、コマンドキューがシリアライズされるのかなあ。それとも描画はイベントキューに積むのか？」
- Q1 の推す形（main＝protocol・入力・cursor、描画 thread 1 本、作業 thread、最新の写しを受け箱に）にユーザー「了解です。おすすめがいいと思います。あとでできたらレビューさせてください。」
- 2026-10-12 ユーザーの判断（review-1 H6、Q1 経由）:「描く物の一覧＋pull」— main が今の描画の walk で display list を作り、描画 thread が「次をくれ」と言った時だけ 1 回作る。記録・submit・present・GPU の待ちは描画 thread。第 1 版の「受け箱の置き換え」はやめる。
- 2026-10-12 Q1 の判断（review-2）: K1 は「2 段の pull」、H-a は「R は待ちの間 Vulkan を呼ばない、walk の中の M の作成・破棄は許す」の不変条件で同期の依頼をやめる、H-c は shm の memcpy を M の walk の中に残し release は写した直後、H-d は cursor の shm を M が同期で作成・書き込み、退役は plane の fence も待つ。
- この Phase は設計だけ（p002 の M0・C0 は Q1 の指示で先に実装、記録は [p002](../phase002/phase.md)）。

## review-1 への対応

| 指摘 | 対応（節） |
| --- | --- |
| C1 cursor の画面の 60fps は C0・S2 では上がらない（WSI の present の worker は queue ごとに 1 本、cursor と出力のコマが同じ queue、`KWL CURSOR_PLANE present` の時刻は呼んだ時刻） | 受け入れの cursor の 60fps は WS141 p016 (B) の B-u への依存と明記（6 節）。M0 に present の内訳・最長、evdev の時刻 `event_us` を足した（p002）。表示の側の時刻（kernel の CURSOR_SET・MOVE）は WS141 に頼む（11 節） |
| C2 写しの置き換え・捨てで walk の副作用が巻き戻らない | pull に変えた。作った写しは必ず描く。出力の世代の変更は park の後にだけ起き、R が写しを捨てる経路は無い（4.2・4.8） |
| H1 shm の画像の寿命と walk が読む値 | import の記録と metadata は M が commit の時に同期で作り、R は Vulkan の object だけ。古い画像は参照する全部の写しの完了で退役（4.7） |
| H2 M が呼ぶ Vulkan の列挙の不足 | 全部を表にした（3.2）。第 3 版で「R への同期の依頼」はやめ、walk・park の中の M の作成にした（review-2 H-a） |
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

## review-2 への対応

| 指摘 | 対応（節） |
| --- | --- |
| K1 写しを捨てる経路が残る（acquire の失敗・park） | 2 段の pull: 写しは R の acquire の後にだけ作る。damage は今の buffer age、heads の条件の区間は不要（2・4.1・4.2） |
| H-a walk の中の同期の依頼は deadlock | 不変条件「R は ready から一覧を受けるまでと park の間 Vulkan を呼ばない、walk の中の M の作成・破棄は許す」、同期の依頼をやめた（3.2） |
| H-b walk の外の host image の書き込み（IME の tick の glyph、色の emoji、壁紙・landscape・resize）と `written` の競合 | staging への書き込みは walk と park の中だけ。walk の外の glyph は未着の list、R が写すのは walk で取り込んだ list だけ（4.3・5） |
| H-c R の shm の memcpy と pool の resize の競り | memcpy は M の walk の中、release は写した直後（3.2・4.7） |
| H-d cursor の plane が shm の staging を直に読む | cursor の surface の shm は M が同期で作成・書き込み、書き込みの前と退役で plane の fence を待つ（4.7） |
| H-e 「main の止まりが無くなる」は過大 | 保証を「M の止まりは R の 1 transaction 以内」にし、`present_max` で判定。超えたら cursor の専用の thread（6・ws.md） |
| M-a park の上限 | ack の待ちに上限、超えたら `KWL FAILED site=park`。R の待ちは上限付き、取った画像は持ったまま止まる（4.1・4.8） |
| M-b 完了と「次をくれ」の順 | R→M は 1 本の順序付きの通り道、ready は完了の記録の後（4.4） |
| M-c 表の漏れ | hotplug の fence、heads の sync、出力の移動、plane の fence の待ち、shot、起動の batch、scene、layer を 3.2 の表に足した |
| M-d Linux・FreeBSD でも plane が開く | Mesa の build の queue_lock は queue を取る全部の呼び出し（M の cursor・walk・park の中も）（3.3） |
| M-e glyph の待ちの上限切れ | 待たずに「未着」の印、その写しの layer の絵は無効、次の walk で描く（5） |
| M-f 試験 | 9 節 (b)〜(h)、M と R の手順を 1 歩ずつ交互に |
| Low: 行番号（main.c:1120、cursor-plane.c:121）、ws.md と §8 の古い記述、p002〜p006 の phase.md、`kwl_import` の世代、touchpad の `pointer_event_us`、plane の `held[]` の ABA、C1 の依存（L1・受け入れ B・A3） | 直した。phase.md の骨組みを作った（8 節）、世代は p003（4.7）、touchpad は直した（p002）、依存は 6 節 |

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
| `heads.c:908`・`compose.c:443,815`・`cursor-plane.c:121` | `vkDeviceWaitIdle`（出力の close・終了） | まれ |
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


## 2. 形: 描く物の一覧（display list）＋ 2 段の pull

| 案 | 内容 | 判断 |
| --- | --- | --- |
| 案 A 状態の写し | server の状態を写し、描画 thread が今の描画のコードを写しに対して動かす | 採らない。描画のコードは 26 file が server・client の object・glass・home の状態を直に読み、hover の probe も描画が記録する |
| **案 B 描く物の一覧** | M が今の描画の walk を行い、一番下（vkCmd* を呼ぶ約 60 か所）だけを「一覧に op を足す」に変える。R が一覧を Vulkan の命令にして送る | **採る**（ユーザーの判断）。描画の論理は変えない。M に残るのは walk（形の位置・push constant・間引き・hover の記録）だけ |
| 案 C secondary command buffer | M が secondary を記録、R が vkCmdExecuteCommands | 採らない。Pi で高い所（libvulkan の encode と End の transaction）が M に残る |

- **2 段の pull**（review-2 K1、Q1 の判断）:
  1. M は「描く物がある」の印（want、種類: 出力のコマ / present しない layer の絵）を立てるだけ。
  2. R は want を見て、出力のコマなら出力と heads を上限付きで acquire し、取れた画像の番号 k と取れた heads の mask を付けて「次をくれ」（ready）を M に知らせる。layer の絵なら acquire せずに ready。
  3. M は ready を受けた pass で、k と mask を前提に walk を 1 回して一覧を渡す。
- これで**写しは acquire の後にしか作られず、作った写しは必ず描かれる**。acquire の失敗（OUT_OF_DATE・SURFACE_LOST、`compose.c:561-582`）や park では写しが生まれないので、walk の副作用（layer の `decided_anim`・`keys`・`valid`・`inflight` `layer-cache.c:262,279,626-627`、shot の `SHOT_RECORDED` `shot.c:339`、import の layout の list `import.c:186-197`、shm の写しと release、`dirty`・`pointer_moved`・`home_start_pending`、damage の履歴）を巻き戻す経路は要らない。damage は今の buffer age（`compose_region`）がそのまま使え、heads の条件付きの区間も要らない。walk と画像の取り出しの時刻のずれも無い。
- 案 B の前提（p003 で測る）: walk の費用は記録全体の一部。根拠: 記録の費用は draw の数にほぼ比例し（0〜100 draw で 3〜8ms、300 以上で 48ms）、中身は libvulkan の encode と End の decode の transaction（kernel の `hold_us decode` 最大 11〜19ms）が大半。外れて walk が重いなら効果は小さく、p004 に進む前に WS211 の削減を先にする。

## 3. thread の分担と Vulkan の持ち主

### 3.1 分担

| thread | 受け持つ物 |
| --- | --- |
| **M（main）** | poll、Wayland の protocol、入力（evdev、pointer の加速、hover の判定、focus、event の配達）、cursor の plane、server の状態の全部、ready の時の walk（と walk の中の Vulkan、3.2）、frame pacing、client の buffer の参照の数、frame callback、log |
| **R（描画、1 本）** | 出力と heads の acquire・present、一覧の再生（vkCmd*）、host image の copy の記録、`vkQueueSubmit`、fence の待ち |
| **W（作業、p005、まず 1 本、SMP 後は core−1）** | glyph の raster、壁紙の描画とぼかし、大きな字 |

### 3.2 不変条件（review-2 H-a）と Vulkan の行き先

**不変条件**: R は ready を知らせてから一覧を受け取るまでの間（M の walk の間）と park の間、Vulkan を一切呼ばない。だから **M は walk の中と park の中で、今のコードのまま Vulkan を呼んでよい**（command pool・descriptor pool・queue・host image の staging を含む。外部同期の要る object を使えるのは、その時 R が触らないから）。M が walk と park の外で呼んでよいのは、M 専用の object（cursor の plane の pool・fence・semaphore・swapchain、hotplug の fence）への呼び出しと、device 単位の作成・破棄・問い合わせ（backend の import、physical device の問い合わせ）だけ。第 2 版の「R への同期の依頼」はやめる（walk の中から R を待つと deadlock になる。walk の外の同期の依頼は R の 1 コマ全体を待つ）。

| 所 | 今（M） | p004 の後 |
| --- | --- | --- |
| 出力の swapchain・framebuffer・scene（`scene.c:41-67`）・layer の画像（`layer-cache.c:733,807`）・batch の buffer・load pass（`compose.c:383,2331`、`backdrop.c:216`） | 出力の open・初回 | park の中（出力の open）か walk の中（初回の遅延作成はそのまま可） |
| backdrop の target（`backdrop.c:69-79,395,459`、walk の中の遅延作成） | walk | walk の中のまま（不変条件で可） |
| cursor の形の画像（`cursor.c:262-270,388`、`image_make` 722 → `shm.c` の作成・`vkQueueSubmit`＋`vkQueueWaitIdle` 877-882・`compose->pool` 842・`set_get` 801・`host_images` 796-797） | 最初に使う時（walk の外もある） | R の起動の前に全部作る（cursor の plane も読むので M の持ち物、4.7） |
| 大きな字（`glass.c:2147-2152`、greeter `1633`、`vkDeviceWaitIdle`） | 大きさが変わる時 | walk の中で行う（`vkDeviceWaitIdle` は不要になる。R は待っていて前のコマは終わっている）。p005 で W へ |
| 壁紙の描き直し（`glass.c:648`→821・1091）・landscape（451）・resize（493） | `vkDeviceWaitIdle` の後に CPU で書く | p004 では park の中（まれ）。p005 で壁紙の画素は W、staging への写しは walk の中 |
| client の import（backend の `vkCreateImage`・memory・bind、`kwl_import_adopt` の view） | protocol の request の中 | M のまま（device 単位）。descriptor set（`import.c:258,276`、`compose.c:2005-2083`、pool を使う）は walk の中で初めて使う時に割り当てる。R へ移すのは F-094 |
| import の layout の list と溢れ（`import.c:186-197,248-252,348,353`） | 次のコマ、溢れは即時の submit＋WaitIdle | list は写しの中身。溢れの即時の経路は walk の中なら不変条件で可、walk の外（protocol の中）では溢れさせない（list を伸ばす） |
| import・shm の画像の破棄（`objects.c:830`、`shm.c` の `image_release`） | buffer・surface の破棄 | 参照する写しが全部完了してから、次の walk の中か park の中で（4.7） |
| wl_shm の画像の作成と memcpy（`shm.c:555-631`） | frame の外 | walk の中（ready の後、walk の始め）。release は写した直後（review-2 H-c、今どおり） |
| shot（`shot.c:359` の `vkMapMemory`、`shot_finish` の解放 414-415、`kwl_shot_tick`、buffer の作成 445・472） | tick と完了 | walk の中か完了の記録の処理（R が待つ ready の間）か park。試験の image だけ |
| host image の起動時の batch（`shm.c:354-392`） | 起動 | R の起動の前 |
| heads の swapchain・view（`kwl_heads_sync`、`output-switch.c:89-91,149`）・出力の移動（`output-switch.c:441`） | output の tick | tick は「変更が要る」の印だけを立て、M が park を頼んで park の中で行う |
| hotplug の fence（`output-switch.c:98-104`） | output の tick | M 専用の fence なので M のまま |
| cursor の plane（`cursor-plane.c`、`vkWaitForFences` 534 を含む） | pass ごと | M のまま、command pool を M 専用に分ける |
| frame の fence の待ち（`kwl_compose_complete`・`kwl_compose_poll`） | M | R |
| 出力の close・終了・session の pause・sleep・game mode の切り替え | M（`vkDeviceWaitIdle`・quiesce） | park（4.8） |

### 3.3 queue の lock（review-1 H3、review-2 M-d）

- 出力の queue は 1 本（bcm2711 の queueCount は 1、`vulkan-query.c:182`、i915 も 1）。cursor の plane の submit・present（M）と出力の submit・present（R）が同じ queue を使う。
- zedBSD の libvulkan の build では compositor の queue_lock を使わない。libvulkan が `queue->mutex`（`queue.c:333`、`wsi-swapchain.c:557`）・`device->mutex`（`queue.c:422-460`）・`context->mutex`（`context.c:496`）で中で直列にするので、lock を足しても待ちは減らない。
- Linux・FreeBSD の host の Vulkan（Mesa）の build は、compositor の `queue_lock` を queue を引数に取る全部の呼び出し（`vkQueueSubmit`・`vkQueuePresentKHR`・`vkQueueWaitIdle`、M の cursor の plane の経路と walk・park の中の即時の submit も）の間だけ持つ。cursor の plane は platform で分けずに開く（`cursor-plane.c:43-100`）ので、Linux・FreeBSD でも plane が開く場合がある（M-d）。build の印（zedBSD の libvulkan か host の Vulkan か）で切り替え、印の置き場は p004 で決める。

## 4. pull の 1 周と写し

### 4.1 R の 1 周

1. FIFO の依頼（park、R の object の破棄）を順に処理。
2. M の want を待つ（上限付きの繰り返し、100ms ごとに park・停止の印を見る）。
3. 出力のコマなら、出力と heads を `vkAcquireNextImageKHR` で上限付き（例 50ms）に繰り返し取る。間に park・停止の印を見る（M-a）。出力が OUT_OF_DATE・SURFACE_LOST なら M に「acquire の失敗」を知らせ（写しは作られない）、M が今の `output_lost` の処理をする。取れなかった head は mask から外す。
4. M に ready（k、heads の mask）を知らせ、一覧（か park の依頼）を待つ。**この間 R は Vulkan を呼ばない**（3.2）。
5. 一覧を受けたら、写しの host image の copy の list（walk の時に写しへ取り込んだ物だけ、5 節）を記録し、一覧を再生、`vkEndCommandBuffer`、`vkQueueSubmit`・`vkQueuePresentKHR`・heads の present。
6. fence を上限付きで繰り返し待つ → 完了の記録を M に渡す。
7. 1 へ。飛んでいるコマは 1 つ（今と同じ）。GPU と walk を重ねる・2 コマ飛ばすは F-095。
- park が来た時に R が取った画像 k を持っていたら、R は present せずに k を持ったまま止まる。park の中で M が swapchain を壊せば k は消え（Vulkan は取った画像を持つ swapchain の破棄を許す）、壊さなければ unpark の後に R は k のまま ready を出し直す。

### 4.2 M の答え方

- want: 今の `kwl_schedule` の判断（dirty・damaged・pacing・`kwl_layers_tick` の静かな時の条件、sleep_hold・os_paused・game mode でない）が「描く」なら立てる。
- ready を受けた pass で、M は必ず walk を 1 回して一覧を渡す（R は画像を持って待っている）。例外は M が park を頼む時だけで、その時は一覧の代わりに park を渡す。
- walk の始めに、walk の中の仕事（shm の作成と memcpy、溜まった glyph の bitmap の staging への書き込み（5 節）、遅延作成、import の descriptor set）を行う。
- frame の番号は walk の時に付ける。M は R が描いている間に新しい写しを作らない（walk は R の 1 コマに 1 回）。

### 4.3 写しの中身

| 物 | 内容 |
| --- | --- |
| 番号・種類 | frame の番号、種類（出力のコマ / layer の絵）、画像 k、heads の mask |
| 一覧（op の列） | render pass の開始（相手は記号: 出力の画像 k・head の画像・layer の絵・backdrop の target・scene）と終わり、viewport、scissor、pipeline・descriptor set の bind、push constant（inline）、vertex buffer の bind、draw、barrier・copy・clear。handle は walk の中で決まった値をそのまま入れてよい（walk の中で作られ、写しの完了まで壊れない、4.7）。bind の省略の鍵は（import、種類、世代）（M9） |
| 頂点の配列 | WS211 p003 の束の頂点（R が再生の時に HOST_COHERENT の buffer に写す） |
| host image の copy の list | walk の終わりに `written` の付いた host image を写しに移し、印を下ろす（R は `written` を読まない、review-2 H-b の競合を消す） |
| layer・shot | layer の世代、shot の依頼 |
| M だけの帳簿 | 保持した buffer の参照、frame callback、pacing の印。R は触らない |

### 4.4 R→M の 1 本の順序付きの通り道（review-2 M-b）

- R→M の知らせは 1 本の FIFO（`render_lock` の下の list＋pipe）: 完了の記録 → acquire の失敗 → ready → parked の順に積まれ、M は積まれた順に処理する。**ready は必ず前のコマの完了の記録の後**なので、M は前のコマの結果（画像ごとの最後の frame、scene・layer・shot・shm・heads の結果）を反映してから walk する。
- 完了の記録の中身（M2）: frame の番号、結果、heads ごとの結果（lost を含む）、出力の present の結果（`compose.c:2829-2853` の `switch_proving` の成否 → `kwl_displays_move_settled`、`output_lost`）、scene の written、layer の焼き、shot、計測（acquire・再生・End・submit・present・GPU の時間）。M は今の `kwl_compose_complete` と `compose_submit` の後ろ半分の server の状態の書き換えをここで行う。

### 4.5 damage

- 今の `compose_region`（取った画像の buffer age）をそのまま使う。画像 k は ready で分かる。R が region を狭めることはしない（review-1 M5）。

### 4.6 費用と lock

- 受け渡しは pointer の交換だけ。一覧は App Home の 460 draw で約 70KB＋束の頂点（最大 1MiB）。buffer は 2 つ（M が作る・R が再生）を回す。共有は want・ready・写し・R→M の FIFO・M→R の依頼・park の印だけで、1 つの mutex（`render_lock`）と condvar の下。

### 4.7 object の寿命

| object | 作る | 壊す | 守り |
| --- | --- | --- | --- |
| client の import（image・memory・view） | M（backend） | 参照 0 の後、次の walk の中か park の中 | 写しの帳簿の保持。参照 0 は完了の記録の後にしか来ない |
| import の descriptor set | walk の中（初めて使う時） | import と一緒 | 同上 |
| wl_shm の画像 | walk の中（大きさが変わった最初の walk） | 参照する写しが全部完了した後の walk・park の中 | surface と写しの帳簿の参照 |
| **cursor の surface の wl_shm の画像**（review-2 H-d） | M が commit の時に同期で作成・書き込み（64×64 以下、plane が staging を直に読む `cursor-plane.c:310,532,678`） | 写しの完了に加えて、plane の copy の fence の完了の後 | plane の `held[]` と写しの帳簿。書き込みの前に、その staging を読む plane の copy の fence を待つ（短い、上限付き） |
| glass の host image・cursor の形 | R の起動の前 | park の中 | 起動から終了まで |
| backdrop・shot の buffer | walk の中 | park の中 | 世代 |

- plane の `held[]` の ABA（review-2 Low）: 壊れた import と同じ address に新しい import が作られると、`held[image] == source` と version が偶然一致して古い像を出し得る。p003 で `struct kwl_import` に作成ごとに増える世代の番号を足し、plane の held と bind の鍵はそれで比べる。

### 4.8 park（review-1 H4、review-2 M-a）

- M が park を頼むと、R は今のコマを最後まで描いて fence を待ち（上限付きの繰り返し）、parked を知らせて止まる。ready を出して待っている R には、一覧の代わりに park を渡す（4.1）。
- **M の park の ack の待ちにも上限**（例 2 秒）。超えたら R は libvulkan の上限の無い待ち（capacity の待ち `queue.c:345-363`、`sync.c:1103-1108`）の中と見て、`KWL FAILED site=park` で compositor を失敗にする（device lost と同じ扱い）。R の Vulkan の待ちはどれも上限付きの繰り返しにし、libvulkan の中の上限の無い待ちは WS141 の範囲（記録だけ）。
- park を使う所: 出力の open・close・移動（output-switch.c・displays）、heads の hotplug と sync、glass の大きさ・landscape・壁紙の描き直し（p004）、session の pause（`backend-host.c:46`）、sleep の依頼（`display.c:160-166`）、game mode の入りと出（`scanout.c:90,284`、`display.c:174-186`）、終了。os_paused・sleep_hold・game mode の間、M は want を立てない。

## 5. host image と作業 thread（review-2 H-b、M3、M-e）

- **書く所は walk の中と park の中だけ**（p004 から）。今 walk の外で staging に書く所: IME の tick（`main.c:801` → `input-method.c:307` → `ime_current` 2621 → `kwl_home_field_state` home.c:1236 → `glass_text_width` glass.c:1847 → `glass_cache_glyph` → `atlas_put` → `kwl_host_image_written` glass.c:2983）、色の emoji（glass.c:3234）、壁紙の loader（glass.c:648→821・1091）、landscape（451）、resize（493）。p004 では:
  - `glass_cache_glyph` は walk の外では cell の選びと metrics と raster までを行い、bitmap を M の「未着の list」に置く。staging への書き込み（`atlas_put`）は次の walk の始めにまとめて行う。walk の中で初めて出た字は今どおりその場で書く。色の emoji も同じ（bitmap を list に）。
  - 壁紙・landscape・resize は park の中（まれ）。
  - R が写すのは walk の終わりに写しへ移した host image の list だけ（4.3）。`written` の印は M だけが読み書きする。
- **p005（W）**: raster を W に出す。M は cell と metrics だけを同期で決め、job（face・glyph id・大きさ・cell・job の番号）を W に出す。walk の始めに、終わった job の bitmap を staging に書き、終わっていない job の字は今回は描かず「glyph が未着」の印を立てる（その写しで layer を焼いていれば、その layer の絵は完了の後に無効、review-2 M-e）。次の walk で描く。W は自分の truetype の face を持つ（libtruetype に file 単位の可変の状態は無い）。色の emoji と予備の font の open（`glass.c:3105-3125`）は M に残す。壁紙の画素とぼかし・大きな字は W が作り、walk の中で staging に写す。
- 追い出し: cell の上書きは walk の中（前のコマの完了の後）にしか起きないので、飛んでいるコマの cell は書き換わらない。焼いた layer の絵は焼いた時に sample 済み。残るのは今と同じ「1 つの写しの中で cache より多い種類の字」で、guard の扱いは今のまま。

## 6. cursor と入力（review-1 C1・H3、review-2 H-e）

- **M に残す**: evdev、pointer の加速、hover、focus、event の配達、cursor の形の決定、cursor の plane の present（p002 の C0 で 1 refresh に 1 回、最新の位置）。
- **画面に映る cursor の速さは WS141 p016 (B) に依存する**: libvulkan の present の worker は queue ごとに 1 本で後の job は前を追い越さず、各 job の GPU の fence を待つ（`wsi-swapchain.c:2664-2710,2745,2776-2782`）。cursor の plane と出力のコマは同じ `compose->queue`。前提は (a) B-u（位置だけの present が worker も GPU も通らず ticket で追い越す）、(b) B-u の L1（worker が primary の `PRESENT_SYNC` の間 display_mutex を持ち、cursor の op が 1〜2 refresh 待ち得る、kernel の `output_present` の adoption の待ち）、(c) p016 の受け入れ B の前提（(b) decoder の request が executor の準備を待たない、A3 の lazy map）。
- **M の止まり（H-e）**: zedBSD の libvulkan では M の cursor の present は R の vkQueueSubmit の `device->mutex` の区間（`queue.c:422〜`、recorded→queued 最大 24ms）や End の decode の `context->mutex`（11〜19ms）を待ち得る。だから p004 の後もこの WS が単独で保証するのは「**M の止まりは R の libvulkan の 1 transaction 以内**」で、受け入れは M0 の `present_max` と `KWL PERF main` の区間で判定する（目安: Pi で `present_max` ≤ 1 refresh（16.7ms））。超えるなら、cursor の plane の present を専用の小さな thread（入力の位置だけを受け取る）に移す案を p004 の中で設計する。
- **測り方**: `KWL CURSOR_PLANE present` の `at_us` は M が呼んだ時刻。M0 の `event_us`（evdev の kernel の時刻、マウスと touchpad）と `present_cycles`。表示の側の時刻（kernel の `CURSOR_SET`・`MOVE`）は Q1 が WS141（P3）に依頼済み。
- **plane が無い出力**: cursor は composite。pointer の動きは damage → want → R の次の ready で walk。遅れは R の acquire＋M の walk＋R の 1 コマ。

## 7. libvulkan の thread の安全、Linux・FreeBSD

- zedBSD の libvulkan: session ごとの `context->mutex` が kernel との transaction を直列にする。command buffer の vkCmd* は貯めるだけで lock を取らない。M と R が同時に呼ぶのは (a) M の backend の import と R の transaction（`context->mutex`）、(b) M の cursor の present と R の submit（3.3）、(c) M 専用の fence の問い合わせ。正しさは保たれ、問題は待ち時間。walk の中と park の中は R が呼ばないので競らない。
- Vulkan の規則（Mesa など）: 外部同期の要る object（command pool・descriptor pool・command buffer・queue・swapchain）は、R が使う時と M が walk・park の中で使う時が重ならない（3.2 の不変条件）。cursor の plane は M 専用の pool。queue は Mesa の build だけ `queue_lock`（3.3）。
- Linux・FreeBSD の backend: 変わるのは compositor の中だけで、`libkeiland-backend` の API は変えない。pthread は既に使っている（Linux の glibc、FreeBSD の `-pthread` `Makefile.freebsd:96`）。新しい file は `Makefile`・`Makefile.linux`・`Makefile.freebsd` に足す。

## 8. Phase の分け方（review-1 M7）

| Phase | 内容 | 状態・順 |
| --- | --- | --- |
| p001 | 調査と設計（この文書） | 第 3 版、再 review |
| [p002](../phase002/phase.md) | M0 と C0 | 実装済み（main 41bd7a5a4 まで）。Pi の確認待ち |
| [p003](../phase003/phase.md) | S1: display list、同じ thread で再生、等価の試験、walk の費用の計測、cursor の形の画像の起動時の作成、`struct kwl_import` の世代。review-2 の判断で op の list・同じ所での再生・試験・計測・遅延作成の移動に絞り、shm の memcpy の持ち主・host image の書き込みの規則・写しの目標は S1 の op の形に固定しない | review-2 で条件付きで可。着手は Q1 が WS211 の P3（p003 段 2・p006 の drag）と順を決めてから |
| [p004](../phase004/phase.md) | S2: R の thread（2 段の pull、不変条件、park、R→M の 1 本の通り道、host image の書き込みの規則） | p003 の Pi の walk の計測の後、再 review |
| [p005](../phase005/phase.md) | S3: W（glyph の raster、壁紙、大きな字） | p004 の後 |
| [p006](../phase006/phase.md) | 規約の全文の見直し | 最後 |

Future Work（2026-10-12 Q1 が記入）: F-094 client の画像の import を R へ（backend の API の変更）、F-095 GPU と walk を重ねる・2 コマ飛ばす、F-096 cursor の plane に別の VkDevice。kernel の CURSOR_SET・MOVE の時刻の log は Q1 が WS141（P3）に依頼。

## 9. 試験

- host（決定的、thread を使わずに M と R の手順を 1 歩ずつ交互に呼ぶ、review-2 M-f）:
  - (a) **再生の等価**（p003）: デスクトップ・窓 2 つと popup・App Home・drag の部分の描き直し・backdrop・heads 2 つ・layer の焼き・batch の on と off を、直の記録と一覧の再生で vkCmd* の stub に流し、命令の列が同じこと。descriptor set の handle は（import、種類）に正規化して比べる。golden は p003 の最初の commit で取る。
  - (b) **2 段の pull**（p004、K1）: want が無い間は acquire も walk もしない。acquire の失敗（OUT_OF_DATE）で写しが作られず、layer・shot・import の layout・shm・dirty・damage の履歴が変わらない。ready の後は必ず walk か park。park が ready の後に来た時、R は画像 k を持ったまま止まり、swapchain を壊さない unpark で同じ k の ready を出し直す。
  - (c) **不変条件**（H-a）: walk の中の backdrop の遅延作成・import の descriptor set の割り当て・shm の作成で R が Vulkan を呼ばない（stub の Vulkan が呼んだ thread の役を記録して確かめる）。
  - (d) **通り道の順**（M-b）: 完了の記録 → ready の順で、ready の walk が前のコマの結果（scene・layer・画像ごとの frame）を見ている。
  - (e) **shm**（H-c）: memcpy と release が walk の中で、release は写した直後。pool の resize と交互にしても写しが古い map を読まない。
  - (f) **cursor の shm**（H-d）: cursor の surface の shm の書き込みは plane の copy の fence の後、退役は plane の fence と写しの完了の後。plane の `held[]` が世代で比べられ、同じ address の新しい import で古い像を出さない。
  - (g) **host image**（H-b）: walk の外の `glass_text_width`（IME の tick）が staging を書かず、未着の list に置き、次の walk の始めに書く。R の copy の list は walk の終わりに取り込んだ物だけ。
  - (h) park（M-a）: fence の待ちの途中・acquire の途中の park、ack の上限を超えた時の `KWL FAILED site=park`、session の pause・sleep・game mode の前の park。
  - (i) 遅延作成の移動（p003）: cursor の形が起動時に全部作られ、walk の外で作成を呼ばない。
  - (j) glyph（p005）: 未着の job の字は描かず印、その写しの layer の絵は無効、次の walk で描く。
  - C0（p002）: `plan/ws213/tests/cursor-due-host-test.sh`（PASS）。
- build: rpi4・amd64・`make keiland-linux`・FreeBSD で warning 0。p004 の時に Linux の build を ThreadSanitizer 付きで WS131 の Debian の QEMU+KVM guest で動かすかを Q1 と決める。
- T1（p003・p004・p005 の終わりにまとめて）: i915（5330）と Venus の QEMU で shot.c の画面の写しを前と比べる、`KWL VULKAN_ERROR`・`KWL FAILED` が無い、App Home の開閉・drag・終了。
- Pi: `KWL PERF main`（区間、`cursor presents=`・`present_max=`）、`KWL CURSOR_PLANE present` の `event_us`→`at_us` と間隔、`KWL LAT`、`SCHED_WAKE_LATENCY`（必要なら）。場面: pointer だけ・App Home の開閉・drag・Music・Settings の起動。

## 10. SMP が無い間の効果の見込み

- Pi の kernel は 1 core（WS208 まで）。thread にしても CPU の総量は同じで fps は上がらない。
- 効くのは M の遅れ: kernel は起きた thread が同じ優先度なら走っている thread を次の安全な点で横取りする（`src/kern/sched.c` の `wake_enqueue_locked` 1800-1834）。evdev が読めて M が起きると、R の CPU の仕事（encode）を横取りして先に入力を処理する。R が kernel の中（transaction の decode）にいる時は安全な点まで待つ（kernel の GPU の thread も同じ既定の優先度 `render-worker.c:115,121`）。
- 見込み（p002・p003 の実測で直す）: 1 コマで M が止まる時間は今 10〜20ms（App Home 60〜120ms）→ p004 の後は walk だけ（数 ms と見込む）＋ M の present の待ち（R の 1 transaction 以内、6 節）。画面の cursor の 60fps は WS141 p016 (B) の後（6 節）。

## 11. 未決と危険

1. walk の費用（p003 で測る）。重ければ p004 の効果は小さく、WS211 の削減が先。
2. M の present の待ち。`present_max` が 1 refresh を超えるなら cursor の専用の thread（6 節）か WS141 への依頼。
3. `event_us` の時計（kernel の `clock_milliseconds`、`src/drivers/generic/input.c`）と M の `kwl_microseconds`（CLOCK_MONOTONIC）が同じ基準かを Pi で確かめる。
4. `KWL LAT`・`KWL FIRST_FRAME` の意味の定義し直し（p004）: want＝M が立てた時、acquired＝R の acquire、draw＝M の walk の始め、recorded＝R の End、queued・submit＝R、shown＝M が完了の記録を受けた時。
5. retire（`objects.c:739`）と shm の写し（`shm.c:146`）は今「frame が飛んでいない時」に止まる。p004 では shm の写しは walk の中になる。retire は完了の記録の処理の後に 1 つずつ（in_flight を見ない）。
6. `kwl_compose_quiesce` の 4 か所（`objects.c:554`、`backend-host.c:46`、`main.c:1120`、`shm.c:187`）と `->in_flight` の 16 か所は、p004 の実装の前に 1 つずつ park・retire・写しの帳簿のどれかに当てる。client の破棄で M が 1 コマ待つのは避け、retire に寄せる。
7. compose->command を直に使う所（`compose.c:2365〜`、`layer-cache.c:184`）は p003 の型の変更で見つからないので、grep で全部を挙げて一覧へ移す。
8. 動きの時刻: walk の時に animation の時刻を読むので、表示されるのは R の記録と GPU の時間だけ後（今も同じ）。

## 12. 結果の記録（この Phase）

- 調べたこと: 1 節（source の地図、Pi の既存の log の集計。P3 の boot の b4・b6・b10）。
- 変えた file: この phase.md、[review-1](review-1.md)・[review-2](review-2.md)（design-reviewer の要点の写し）、ws.md、p002〜p006 の phase.md（骨組み）。p002 の source は下の節。
- 次: 第 3 版の再 review。p003 の着手は Q1 が WS211 と順を決めてから。
