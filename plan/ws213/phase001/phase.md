<!-- awesome-plan project=zedbsd record=ws213-p001 -->

# ws213-p001: compositor の thread 化の調査と設計

Parent: [WS213](../ws.md)
Status: planning（設計の第 4 版。[review-1](review-1.md)〜[review-3](review-3.md)、2026-10-12 のユーザーの判断（描く物の一覧＋pull）と Q1 の判断（2 段の pull、cursor の thread を p004 に）を反映、S2 の部分の再 review の前。S1（p003）は review-3 で条件付きで可）
Phase disposition: normal
Queue: P4（2026-10-12 Q1 が割り当て）

## ユーザー向けの要約

- **今の問題**: compositor は 1 本の thread で、1 コマを描く間（命令の記録・GPU への送り・表示の依頼）は入力も cursor も止まります。Pi 4 の記録では、普通のコマで 10〜20ms、App Home のような重いコマで 60〜120ms 止まっています。cursor の表示の依頼も、画面の書き換え（60 回/秒）より多い約 125 回/秒行っていて、main の時間をかなり使っている見込みです（測定を足しました、M0）。
- **形（2026-10-12 ユーザーの判断「描く物の一覧＋pull」）**: main thread は Wayland の protocol・入力・cursor と、今の描画の処理で「描く物の一覧」を作る所までを受け持ちます。main は「描く見込みがある」と知らせ、描画 thread は画面の画像を取り出してから「描いてよい」と返します。main は今どおり client の絵を少し待つなどの判断をしてから、一覧を 1 回作ります。描画 thread はそれを Vulkan の命令にして GPU に送り、表示を頼み、終わるのを待ちます。画像を取り出せた後にしか一覧を作らないので、作った一覧は必ず描かれ、作った時に決まった事（焼いた絵・画面の写し・client の画像の扱い）が無駄になったり食い違ったりしません。描画 thread は main が一覧を作っている間は何もしないので、その間 main は今のコードのまま GPU の物を作れます。
- **cursor が画面で 60fps になるには、WS141 の cursor の速い経路（B-u、GPU を通らない位置だけの表示）が要ります**（review-1 C1）。今の表示の仕組みは、cursor の表示の依頼を画面のコマの依頼と同じ 1 本の列に並べて GPU の完了を待つので、main を軽くしても、重いコマの間は cursor の表示が後ろに並びます。この WS は main の止まりを無くし、B-u と合わせて 60fps にします。
- **cursor の thread**: cursor の表示の依頼は、描画 thread の仕事を待つことがあるので、cursor の表示だけを受け持つ小さな thread を p004 から入れます。main は cursor の位置を渡すだけで待ちません。
- **この WS だけで保証できること**: main が描画と cursor の表示の待ちで止まらないこと（残るのは client の画像の取り込みの時の短い待ち）です。
- **段**: p002 測る＋cursor の依頼の間引き（実装済み、Pi の確認待ち）→ p003 描く物の一覧（thread はまだ使わない、見た目が同じことを確かめる）→ p004 描画 thread と cursor の thread（pull）→ p005 作業 thread（文字の形・壁紙のぼかし）→ p006 規約の見直し。
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

## review-3 への対応

| 指摘 | 対応（節） |
| --- | --- |
| H-1 ready で walk を強制すると frame pacing と食い違い want が古くなる | ready は「描いてよい」の知らせ、walk の時は M の schedule、want に番号、違えば取り消し（2・4.1・4.3） |
| H-2 R が heads の状態を読み書き、slot の再利用 | want に slot と世代、R はそれだけを timeout 0 で取り結果を返す、状態は M が当てる、世代の違う結果は捨てる（4.1） |
| H-3 cursor の shm の commit の時の作成が不変条件を破る | plane の大きさ以下の cursor 用の画像を 2 枚前もって作り、commit は memcpy だけ、C の copy と飛んでいるコマの両方を避ける（4.7） |
| M-1 acquire の失敗で R が空回り | 失敗で R が want を下ろし、M が立て直すまで acquire しない。want の持ち主の規則（4.1） |
| M-2 park の失敗の後始末と同期の park | 普通は非同期の park、同期は session の pause と終了だけ、失敗は後始末せず log と `_exit`（4.8） |
| M-3 H-e の判定が満たせない | cursor の専用の thread C を p004 に入れる（Q1 の推す形）、判定は walk の外の M の Vulkan の最長と C の全段の最長（3.1・6） |
| M-4 試験で競合が見つからない | p004 の受け入れに Linux の ThreadSanitizer、H-1・H-2・M-1・M-2 の場面（8・9） |
| M-5 park の後の同じ k と acquire の semaphore | swapchain を作り直したら semaphore も作り直す、k は swapchain の世代で（4.2） |
| M-6 起動時の cursor の作成で READY が遅れる | host image の batch でまとめる、READY の遅れの上限 +50ms（3.2・8） |
| Low L-1〜L-7 | L-1 7 節、L-2 3.2 の shm の行、L-3 3.2 の shot の行、L-4 5 節、L-5 3.2 の plane の行、L-6 4.5・11、L-7 8 節と p003〜p006 の phase.md |
| review-2 M-c の quiesce | R が ready で待つ時と park の中は即座に返る（3.2・11） |

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

- **2 段の pull**（review-2 K1、review-3 H-1・H-2・M-1）:
  1. M は「描く見込みがある」の印 **want** を、番号と中身（種類: 出力のコマ / present しない layer の絵、要る heads の slot と世代）付きで立てる。
  2. R は最新の want を見て、出力のコマなら出力と want に書かれた heads だけを上限付きで acquire し、取れた画像 k と heads の結果を付けて **ready**（want の番号付き）を知らせる。layer の絵なら acquire せずに ready。
  3. **ready は「描いてよい」の知らせで、walk を強制しない**。M は今の自分の schedule（frame pacing `display.c:224-235`、`compose.c:725-731` の待ち、dirty・damaged）で walk の時を決め、その時の最新の want が ready の want と同じなら k と heads の結果を前提に walk を 1 回して一覧を渡す。違えば（種類が変わった・要らなくなった）**取り消し**で答え、R は k を持ったまま新しい want を見直す。
- 写しは acquire の後にしか作られず、作った写しは必ず描かれる。acquire の失敗や park では写しが生まれないので、walk の副作用（layer の `decided_anim`・`keys`・`valid`・`inflight` `layer-cache.c:262,279,626-627`、shot の `SHOT_RECORDED` `shot.c:339`、import の layout の list `import.c:186-197`、shm の写しと release、`dirty`・`pointer_moved`・`home_start_pending`、damage の履歴）を巻き戻す経路は要らない。damage は今の buffer age（`compose_region`）がそのまま使える。
- 案 B の前提（p003 で測る、p004 に進む閾値は 8 節）: walk の費用は記録全体の一部。根拠: 記録の費用は draw の数にほぼ比例し（0〜100 draw で 3〜8ms、300 以上で 48ms）、中身は libvulkan の encode と End の decode の transaction（kernel の `hold_us decode` 最大 11〜19ms）が大半。

## 3. thread の分担と Vulkan の持ち主

### 3.1 分担

| thread | 受け持つ物 |
| --- | --- |
| **M（main）** | poll、Wayland の protocol、入力（evdev、pointer の加速、hover の判定、focus、event の配達）、cursor の像と位置の決定（C に渡す）、server の状態の全部、want、walk（と walk の中の Vulkan、3.2）、frame pacing、client の buffer の参照の数、frame callback、log |
| **R（描画、1 本）** | 出力と want の heads の acquire・present、一覧の再生（vkCmd*）、host image の copy の記録、`vkQueueSubmit`、fence の待ち |
| **C（cursor、1 本、p004 から、review-3 M-3）** | cursor の plane の全部（swapchain・acquire・copy・present、C0 の 1 refresh に 1 回、fence の待ち）。M から最新の（像、version、位置、出すか）を受け取る |
| **W（作業、p005、まず 1 本、SMP 後は core−1）** | glyph の raster、壁紙の描画とぼかし、大きな字 |

### 3.2 不変条件（review-2 H-a）と Vulkan の行き先

**不変条件**: R は ready を知らせてから一覧（か取り消し・park）を受け取るまでの間と park の間、Vulkan を一切呼ばない。だから **M は walk の中と park の中で、今のコードのまま Vulkan を呼んでよい**（command pool・descriptor pool・queue・host image の staging を含む）。M が walk と park の外で呼んでよいのは device 単位の作成・破棄・問い合わせ（backend の import、physical device の問い合わせ）と M 専用の object（hotplug の fence）だけ。C は自分専用の object（plane の pool・fence・semaphore・swapchain）と queue（3.3）だけを使う。

| 所 | 今（M） | p004 の後 |
| --- | --- | --- |
| 出力の swapchain・framebuffer・scene（`scene.c:41-67`）・layer の画像（`layer-cache.c:733,807`）・batch の buffer・load pass（`compose.c:383,2331`、`backdrop.c:216`） | 出力の open・初回 | park の中（出力の open）か walk の中（初回の遅延作成はそのまま可） |
| backdrop の target（`backdrop.c:69-79,395,459`） | walk の中の遅延作成 | walk の中のまま |
| cursor の形の画像（`cursor.c:262-270,378-388`、10 枚 24×24、今は 1 枚ごとに `vkQueueWaitIdle` `shm.c:879-881`） | 最初に使う時（walk の外もある） | p003 で起動時に host image の batch（`shm.c:354-392`）で 1 回の submit と wait にまとめて全部作る（review-3 M-6、READY の遅れの上限は 8 節）。C も読む（4.7） |
| 大きな字（`glass.c:2147-2152`、greeter `1633`） | 大きさが変わる時、`vkDeviceWaitIdle` | walk の中（`vkDeviceWaitIdle` は R の前のコマが終わっているので不要。WSI の worker の drain は残る、review-3 L-1）。p005 で W へ |
| 壁紙の描き直し（`glass.c:648`→821・1091）・landscape（451）・resize（493） | `vkDeviceWaitIdle` の後に CPU で書く | p004 では park の中。p005 で画素は W、staging への写しは walk の中 |
| client の import（backend の作成、`kwl_import_adopt` の view） | protocol の request の中 | M のまま（device 単位）。descriptor set（`import.c:258,276`）は walk の中で初めて使う時に。R へは F-094 |
| import の layout の list と溢れ（`import.c:186-197,248-252,348,353`） | 次のコマ、溢れは即時 | list は写しの中身。walk の外では溢れさせない（list を伸ばす） |
| import・shm の画像の破棄（`objects.c:830`、`shm.c` の `image_release`） | 破棄の時 | 参照する写しが全部完了してから、次の walk か park の中 |
| wl_shm の画像の作成と memcpy（`shm.c:555-631`） | frame の外 | 作成は walk の中。memcpy と release は walk の中、または飛んでいる写しの copy の list にその画像が無ければ walk の外でも（review-3 L-2: output_lost の間も client を待たせない） |
| cursor の surface の wl_shm（plane の大きさ以下） | 他の shm と同じ | 4.7（前もって用意した 2 枚に memcpy だけ、review-3 H-3） |
| shot（試験の image だけ: buffer の作成 `shot.c:445,472`、`vkMapMemory` 359、解放 414-415） | tick と完了 | buffer の作成・解放は walk の中か park の中。map した画素の読み出しは完了の記録の処理の時（R が次の ready の前で、その buffer を触らない時） |
| host image の起動時の batch（`shm.c:354-392`） | 起動 | R・C の起動の前 |
| heads の swapchain・view（`kwl_heads_sync`、`output-switch.c:89-91,149`）・出力の移動（`output-switch.c:441`） | output の tick | tick は「変更が要る」の印を立て、M が park（非同期）を頼んで parked の後に行う |
| hotplug の fence（`output-switch.c:98-104`） | output の tick | M のまま |
| cursor の plane（`cursor-plane.c` の全部、`vkWaitForFences` 534、close の `vkDeviceWaitIdle` 121・126） | pass ごと | C。close は C が自分の fence を待って行い、`vkDeviceWaitIdle` を使わない（review-3 L-5） |
| frame の fence の待ち | M | R |
| 出力の close・終了・session の pause・sleep・game mode の切り替え | M（`vkDeviceWaitIdle`・quiesce） | park（4.8） |
| `kwl_compose_quiesce` | frame の完了を待つ | R が ready で待っている時と park の中は即座に返る。飛んでいる写しがある時は 11 節の割り当て |

### 3.3 queue の lock（review-1 H3、review-2 M-d）

- 出力の queue は 1 本（bcm2711 の queueCount は 1、`vulkan-query.c:182`、i915 も 1）。C の cursor の plane と R の出力のコマと M の walk・park の中の即時の submit が同じ queue を使う。
- zedBSD の libvulkan の build では compositor の queue_lock を使わない（libvulkan が `queue->mutex`・`device->mutex`・`context->mutex` で中で直列にする）。
- Linux・FreeBSD の host の Vulkan（Mesa）の build は、compositor の `queue_lock` を queue を引数に取る全部の呼び出し（R・C・M の `vkQueueSubmit`・`vkQueuePresentKHR`・`vkQueueWaitIdle`）の間だけ持つ。cursor の plane は platform で分けずに開く（`cursor-plane.c:43-100`）。build の印で切り替え、置き場は p004 で決める。

## 4. pull の 1 周と写し

### 4.1 want の規則（review-3 H-1・H-2・M-1）

- **持ち主は M**。M は want を立てる・中身を置き換える（番号を進める）・下ろす。R が下ろすのは (a) 写しを受け取った時、(b) acquire が失敗した時（OUT_OF_DATE・SURFACE_LOST、他の失敗）だけで、(b) の時は M が失敗の処理（`output_lost` など）の後に立て直すまで R は acquire しない（R の空回りを防ぐ、`wsi-swapchain.c:3060-3064`）。
- M が want を立てるのは「描く見込み」が出た時（dirty・damaged・layer の静かな時の条件、sleep_hold・os_paused・game mode でない）。pacing の待ちの間に立ててよい（R はその間に acquire を済ませる）。
- **heads**: want に、要る head の slot の番号とその世代の組の list を入れる（M が `heads_needs_frame` `heads.c:1111-1140` で決める）。R はその slot だけを timeout 0 で取り、slot ごとの結果（取れた画像・取れない・lost）を返す。`head->dirty` の上げ下げ・`lost`・missed は M が完了の記録と ready の結果で当てる（walk の時に下ろし、失敗の時だけ立て直す）。park の後は世代が変わった slot の結果を捨てる。R は head の状態（`kwl_heads_acquire`・`kwl_heads_present` の `head->dirty=0`・`lost=1`、`heads.c:316-361,459-496`）を書かない。

### 4.2 R の 1 周

1. M→R の FIFO の依頼（park、object の破棄）を順に処理。
2. 最新の want を待つ（上限付きの繰り返し、100ms ごとに park・停止の印を見る）。
3. 出力のコマなら、まだ画像を持っていなければ出力を `vkAcquireNextImageKHR` で上限付き（例 50ms）に繰り返し取る（間に park・停止を見る）。want の heads を timeout 0 で取る。失敗なら want を下ろして「acquire の失敗」を知らせ、2 へ。
4. ready（want の番号、k、heads の結果）を知らせ、M の答え（一覧・取り消し・park）を待つ。**この間 R は Vulkan を呼ばない**。取り消しなら画像を持ったまま 2 へ（新しい want が出力のコマなら持っている k を使う）。
5. 一覧を受けたら、写しの host image の copy の list を記録し、一覧を再生、`vkEndCommandBuffer`、`vkQueueSubmit`・`vkQueuePresentKHR`・heads の present。
6. fence を上限付きで繰り返し待つ → 完了の記録を M に渡す。
7. 1 へ。飛んでいるコマは 1 つ（今と同じ）。
- park の時に R が画像 k を持っていたら present せずに持ったまま止まる。park の中で swapchain が壊されれば k は消え、壊されなければ unpark の後に同じ k を使う。k の有効性は swapchain の世代で確かめる。**park の中で swapchain が作り直された時は、acquire の semaphore（`compose->acquired`、`compose.c:1386-1390`、heads の acquired）も作り直す**（Mesa では signal 済みのまま待たれない semaphore を次の acquire に使えない、review-3 M-5）。

### 4.3 M の答え方

- ready を受けたら、M は自分の schedule で walk の時を決める（pacing の待ちの間は答えない、R は待つ）。walk の時に最新の want の番号が ready と同じなら walk、違えば取り消し（R が新しい want で ready を出し直す）。
- walk の始めに walk の中の仕事（shm の作成と memcpy、未着の glyph の bitmap の staging への書き込み、遅延作成、import の descriptor set）を行う。frame の番号は walk の時に付ける。M は R が描いている間に新しい写しを作らない。

### 4.4 写しの中身

| 物 | 内容 |
| --- | --- |
| 番号・種類 | frame の番号、want の番号、種類、画像 k、heads の slot と世代 |
| 一覧（op の列） | render pass の開始（相手は記号: 出力の画像 k・head の画像・layer の絵・backdrop の target・scene）と終わり、viewport、scissor、bind、push constant（inline）、vertex buffer の bind、draw、barrier・copy・clear。handle は walk の中で決まった値（写しの完了まで壊れない、4.7）。bind の省略の鍵は（import、種類、世代） |
| 頂点の配列 | WS211 p003 の束の頂点 |
| host image の copy の list | walk の終わりに `written` の付いた host image を写しに移して印を下ろす（R は `written` を読まない） |
| layer・shot | layer の世代、shot の依頼 |
| M だけの帳簿 | 保持した buffer の参照、frame callback、pacing の印 |

### 4.5 R→M の 1 本の順序付きの通り道（review-2 M-b）

- R→M の知らせは 1 本の FIFO（`render_lock` の下の list＋pipe）: 完了の記録・acquire の失敗・ready・parked・park の失敗。ready は必ず前のコマの完了の記録の後。M は積まれた順に処理する。
- 完了の記録: frame の番号、結果、heads の slot ごとの結果、出力の present の結果（`compose.c:2826-2853` の `switch_proving` の成否 → `kwl_displays_move_settled`、`output_lost`）、scene の written、layer の焼き、shot、計測。M は今の `kwl_compose_complete` と `compose_submit` の後ろ半分の server の状態の書き換えをここで行う。output_lost・switch_proving を M が知るのは完了の記録の時（今は submit の直後、review-3 L-6）。

### 4.6 damage・費用と lock

- damage は今の `compose_region`（画像 k の buffer age）。R は region を狭めない。
- 受け渡しは pointer の交換だけ。一覧は App Home の 460 draw で約 70KB＋束の頂点（最大 1MiB）。buffer は 2 つ。共有は want・ready・写し・2 本の FIFO・park の印だけで、1 つの mutex（`render_lock`）と condvar の下。C との共有は 6 節。

### 4.7 object の寿命

| object | 作る | 壊す | 守り |
| --- | --- | --- | --- |
| client の import（image・memory・view） | M（backend） | 参照 0 の後、次の walk か park の中 | 写しの帳簿の保持（参照 0 は完了の記録の後にしか来ない） |
| import の descriptor set | walk の中（初めて使う時） | import と一緒 | 同上 |
| wl_shm の画像 | walk の中 | 参照する写しが全部完了した後の walk・park の中 | surface と写しの帳簿 |
| **cursor の surface の wl_shm の画像**（review-3 H-3） | **plane の大きさ（64×64）以下の cursor 用の画像を 2 枚、R・C の起動の前か park の中で前もって作る**。commit では M が 2 枚のうち使える方に memcpy するだけ（作成も submit もしない）。使える＝C の plane の copy の fence がその画像について完了し、かつ出力の飛んでいるコマの copy の list にその画像が無い。両方使えなければ、その commit の memcpy は次の pass に延ばす（最新の commit だけを残す）。plane より大きい cursor の surface は composite で、画像の作成は普通の shm と同じく次の walk の中 | park の中 | C の `held[]`（世代で比べる）と写しの帳簿 |
| glass の host image・cursor の形 | R・C の起動の前 | park の中 | 起動から終了まで |
| backdrop・shot の buffer | walk の中 | park の中 | 世代 |

- `struct kwl_import` に作成ごとに増える世代の番号を足し（p003）、C の `held[]` と bind の鍵はそれで比べる（同じ address の新しい import で古い像を出さない、ABA）。

### 4.8 park（review-1 H4、review-2 M-a、review-3 M-2）

- **非同期の park**（普通）: M が park を頼み、自分の loop を続ける。R は今のコマを最後まで描いて fence を待ち（上限付き）、parked を FIFO で知らせて止まる。M は parked を受けてから変更（出力の open・close・移動、heads、glass の大きさ・landscape・壁紙、game mode の入りと出）を行い、unpark する。その間 want は立てない。C も同じ印で止まる（plane を開き直す時）。
- **同期の park**（session の pause `backend-host.c:46` と終了だけ）: M は parked を上限（例 2 秒）付きで待つ。sleep の依頼（`display.c:160-166`）は非同期で、parked の後に sleep に答える。
- **park の失敗**（上限を超えた: R か C が libvulkan の上限の無い待ち（`queue.c:345-363`、`sync.c:1103-1108`）の中）: 後始末（`compose.c:443,815` の `vkDeviceWaitIdle`、`cursor-plane.c:121`）はせず、`KWL FAILED site=park` を log に出して `_exit`。他の thread が Vulkan の中にいるので、後始末の Vulkan は呼べない。
- os_paused・sleep_hold・game mode の間、M は want を立てない。出力の世代が変わるのは park の中だけ。

## 5. host image と作業 thread（review-2 H-b、M3、M-e、review-3 L-4）

- **staging への書き込みは walk と park の中だけ**（p004 から）。今 walk の外で書く所: IME の tick（`main.c:801` → `input-method.c:307` → `ime_current` 2621 → `kwl_home_field_state` home.c:1236 → `glass_text_width` glass.c:1847 → `glass_cache_glyph` → `atlas_put` → `kwl_host_image_written` glass.c:2983）、色の emoji（glass.c:3234）、壁紙の loader（glass.c:648→821・1091）、landscape（451）、resize（493）。p004 では:
  - `glass_cache_glyph` は walk の外では cell の選びと metrics と raster までを行い、bitmap を M の「未着の list」に置く。次の walk の始めに、**cell を割り当てた順に**書く（同じ cell が 2 度割り当てられていれば後の方が残る、L-4）。walk の中で初めて出た字は今どおりその場で書く。色の emoji も同じ。
  - 壁紙・landscape・resize は park の中（まれ）。cursor の surface の shm は 4.7。
  - R が写すのは walk の終わりに写しへ移した host image の list だけ。
- **p005（W）**: raster を W に出す。M は cell と metrics だけを同期で決め、job を W に出す。walk の始めに終わった job の bitmap を cell の割り当ての順に staging に書き、終わっていない job の字は今回は描かず「glyph が未着」の印を立てる（その写しで layer を焼いていれば、その layer の絵は完了の後に無効）。W は自分の truetype の face を持つ。色の emoji と予備の font の open（`glass.c:3105-3125`）は M に残す。壁紙の画素とぼかし・大きな字は W が作り、walk の中で staging に写す。
- 追い出し: cell の上書きは walk の中にしか起きないので、飛んでいるコマの cell は書き換わらない。焼いた layer の絵は焼いた時に sample 済み。

## 6. cursor と入力（review-1 C1・H3、review-2 H-e、review-3 M-3）

- **M に残す**: evdev、pointer の加速、hover、focus、event の配達、cursor の像と位置の決定（`kwl_compose_cursor_source`、`plane_state`、plane で出すか composite か）。
- **C（cursor の thread、p004 の範囲に最初から入れる、Q1 の推す形）**: M は動きのたびに C の受け箱（mutex の下の 1 組: 像の import、その世代と version、位置、出すか、M の時刻と event_us）を最新に書き換え、C の condvar を起こす。M は C を待たない。C は C0 の規則（1 refresh に 1 回、最初の動きと像の変化はすぐ）で present し、plane の fence も C だけが待つ（今の `vkWaitForFences` 100ms `cursor-plane.c:534` も C の中）。C の失敗（plane の close）は C→M の知らせ（R の FIFO と同じ pipe に種類を分けて）で M に届き、M は composite に切り替えて dirty を立てる。
  - C と共有する物: (a) cursor の形の画像（起動時に作り不変）、(b) cursor の surface の shm の 2 枚（4.7、M が書き C の copy が読む。C は image ごとに「copy の済んだ version」を公開し、M はそれを見て書く方を選ぶ）、(c) queue（3.3）、(d) park の印。
  - これで M の止まりから cursor の plane の全段（fence・acquire・copy・present）が外れる。
- **画面に映る cursor の速さは WS141 p016 (B) に依存する**: libvulkan の present の worker は queue ごとに 1 本で後の job は前を追い越さず、各 job の GPU の fence を待つ（`wsi-swapchain.c:2664-2710,2745,2776-2782`）。cursor の plane と出力のコマは同じ queue。前提は (a) B-u（位置だけの present が worker も GPU も通らない、ticket で追い越す）、(b) B-u の L1（display_mutex の待ち）、(c) p016 の受け入れ B の前提（(b)・A3）。C があっても、C の present はこれらで待つ。
- **この WS の保証と判定**（review-2 H-e、review-3 M-3）: M の止まりは「walk の外の M の Vulkan の最長」（backend の import の `context->mutex` の待ち、hotplug の fence の問い合わせ）で決まり、cursor の plane の待ちは C に移る。判定は Pi で `KWL PERF main` の区間（walk の外の M の Vulkan の最長を p004 で足す）と、C の present の全段の最長（`fence`・`acquire`・`copy`・`present` と最長、M0 の内訳を C に移す）。目安: walk の外の M の Vulkan の最長 ≤ 1 refresh。
- **測り方**: `KWL CURSOR_PLANE present` の `at_us` は C が呼んだ時刻。`event_us`（evdev の kernel の時刻、マウスと touchpad）、`present_cycles`。表示の側の時刻（kernel の `CURSOR_SET`・`MOVE`）は Q1 が WS141（P3）に依頼済み。
- **plane が無い出力**: cursor は composite。pointer の動きは damage → want → R の ready → M の walk。

## 7. libvulkan の thread の安全、Linux・FreeBSD

- zedBSD の libvulkan: session ごとの `context->mutex` が kernel との transaction を直列にする。command buffer の vkCmd* は貯めるだけで lock を取らない。同時に呼ぶのは (a) M の backend の import と R・C の transaction、(b) C の present と R の submit（3.3）、(c) M 専用の fence の問い合わせ。正しさは保たれ、問題は待ち時間。walk・park の中の `vkQueueWaitIdle`・`vkDeviceWaitIdle` は WSI の worker の present の drain（`queue.c:108-155` → `wsi-swapchain.c:617-647,2715-2790`）を待つので、M の止まりの見積もりに入れる（review-3 L-1）。
- Vulkan の規則（Mesa など）: 外部同期の要る object（command pool・descriptor pool・command buffer・queue・swapchain）は、R が使う時と M が walk・park の中で使う時が重ならない（3.2 の不変条件）。C は自分専用の pool と swapchain。queue は Mesa の build だけ `queue_lock`。
- Linux・FreeBSD の backend: 変わるのは compositor の中だけで、`libkeiland-backend` の API は変えない。pthread は既に使っている。新しい file は `Makefile`・`Makefile.linux`・`Makefile.freebsd` に足す。

## 8. Phase の分け方と受け入れ（review-1 M7、review-3 L-7）

| Phase | 内容 | 受け入れ（各 phase.md に同じ物） | 状態・順 |
| --- | --- | --- | --- |
| p001 | 調査と設計（この文書） | design-reviewer の review を通る（S2 の部分は第 4 版で再 review） | 第 4 版、再 review |
| [p002](../phase002/phase.md) | M0 と C0 | host 試験 PASS、build warning 0、Pi で `cursor presents=` が約 60/s 以下、present の間隔が約 16.7ms | 実装済み、Pi・T1 の確認待ち |
| [p003](../phase003/phase.md) | S1: display list（同じ thread で再生）、cursor の形の起動時の batch の作成、`kwl_import` の世代 | 9 節 (a) の再生の等価が全場面で一致、build warning 0、T1 で shot の画が前と同じ、Pi で KWL READY の遅れが +50ms 以内（cursor の形の batch）、walk と再生の時間を分けて測る。**p004 に進む閾値**: Pi で walk（M の時間）が普通のコマ（100 draw 未満）の中央値 4ms 以下、App Home のコマ（300 draw 以上）の中央値 15ms 以下（今の記録の約 1/3 以下）。超えたら WS211 の削減を先にする | 着手は Q1 の指示（P3 との順） |
| [p004](../phase004/phase.md) | S2: R と C の thread（2 段の pull、want の規則、不変条件、park、2 本の FIFO、host image の書き込みの規則、cursor の shm の 2 枚） | 9 節 (b)〜(h) の host 試験 PASS、Linux の build を ThreadSanitizer 付きで動かして報告 0（WS131 の Debian の QEMU+KVM guest、T1）、T1（i915・Venus）で回帰なし、Pi で walk の外の M の Vulkan の最長 ≤ 1 refresh と `KWL LAT` の入力→walk の中央値の改善 | p003 の閾値を満たした後、p001 の再 review の後 |
| [p005](../phase005/phase.md) | S3: W（glyph・壁紙・大きな字） | 9 節 (j) PASS、日本語の UI で初出の字の多い画面で walk の最長が p004 より下がる、T1 で回帰なし | p004 の後 |
| [p006](../phase006/phase.md) | 規約の全文の見直し | WS213 の C の全部を plan/coding-style.md の全文と照らして直す、build warning 0、host 試験 PASS | 最後 |

Future Work（2026-10-12 Q1 が記入）: F-094 client の画像の import を R へ、F-095 GPU と walk を重ねる・2 コマ飛ばす、F-096 cursor の plane に別の VkDevice。kernel の CURSOR_SET・MOVE の時刻の log は Q1 が WS141（P3）に依頼。

## 9. 試験

- host（決定的、thread を使わずに M・R・C の手順を 1 歩ずつ交互に呼ぶ）:
  - (a) **再生の等価**（p003）: デスクトップ・窓 2 つと popup・App Home・drag の部分の描き直し・backdrop・heads 2 つ・layer の焼き・batch の on と off を、直の記録と一覧の再生で vkCmd* の stub に流し、命令の列が同じこと。descriptor set の handle は（import、種類）に正規化。golden は p003 の最初の commit で取る。
  - (b) **2 段の pull**（K1・H-1・M-1）: want が無い間は acquire も walk もしない。acquire の失敗で写しが作られず状態が変わらず、R が want を下ろし M が立て直すまで acquire しない。ready の後に pacing で待つ間 R は待つ。ready の後に want が変わった時（種類の違い・取り消し）に取り消しで答え、R が k を持ったまま出し直す。古い番号の ready で walk しない。
  - (c) **heads**（H-2）: want の slot だけを取り、R が head の状態を書かない。park で slot が再利用された時に古い世代の結果を捨てる。出力だけ作り直した時に head の画像を持ち越さない。
  - (d) **不変条件**（H-a）: walk の中の遅延作成・descriptor set・shm の作成で R・C が Vulkan を呼ばない（stub の Vulkan が呼んだ thread の役を記録）。
  - (e) **通り道の順**（M-b）: 完了の記録 → ready の順で、walk が前のコマの結果を見ている。
  - (f) **shm と cursor の shm**（H-c・H-3）: memcpy と release が walk の中か、copy の list に無い時は walk の外。cursor の 2 枚: C の copy の済む前の画像と飛んでいる写しの画像には書かない、両方使えない時は次の pass、commit で作成も submit もしない。C の `held[]` が世代で比べる。
  - (g) **host image**（H-b・L-4）: walk の外の `glass_text_width` が staging を書かず、未着の list を次の walk の始めに cell の割り当ての順に書く。R の copy の list は walk で取り込んだ物だけ。
  - (h) **park**（M-2・M-5）: 非同期の park の間 M が進む、同期の park（session の pause・終了）、上限を超えた時の `_exit`（後始末を呼ばない）、park で swapchain を作り直した後に acquire の semaphore も作り直す、画像を持ったままの park と unpark。
  - (i) **C**: M の受け箱の書き換えが C を待たない、C0 の規則（今の cursor-due-host-test を C に移す）、C の失敗で M が composite に切り替える。
  - (j) glyph（p005）: 未着の job の字は描かずに印、layer は無効、次の walk で描く。
  - C0（p002）: `plan/ws213/tests/cursor-due-host-test.sh`（PASS）。
- build: rpi4・amd64・`make keiland-linux`・FreeBSD で warning 0。p004 は Linux の build を ThreadSanitizer 付きで（受け入れ）。
- T1（p003・p004・p005 の終わりにまとめて）: i915（5330）と Venus の QEMU で shot.c の画を前と比べる、`KWL VULKAN_ERROR`・`KWL FAILED` が無い、App Home の開閉・drag・終了。p004 は ThreadSanitizer の Linux の guest も。
- Pi: `KWL PERF main`（区間、cursor の内訳と最長）、`KWL CURSOR_PLANE present` の `event_us`→`at_us` と間隔、`KWL LAT`。場面: pointer だけ・App Home の開閉・drag・Music・Settings の起動。

## 10. SMP が無い間の効果の見込み

- Pi の kernel は 1 core（WS208 まで）。thread にしても CPU の総量は同じで fps は上がらない。
- 効くのは M の遅れ: kernel は起きた thread が同じ優先度なら走っている thread を次の安全な点で横取りする（`src/kern/sched.c` の `wake_enqueue_locked` 1800-1834）。evdev が読めて M が起きると、R の CPU の仕事（encode）を横取りして先に入力を処理する。R・C が kernel の中にいる時は安全な点まで待つ。
- 見込み（p002・p003 の実測で直す）: 1 コマで M が止まる時間は今 10〜20ms（App Home 60〜120ms）→ p004 の後は walk だけ＋walk の外の M の Vulkan（import の時）。cursor の plane の待ちは C に移る。画面の cursor の 60fps は WS141 p016 (B) の後。

## 11. 未決と危険

1. walk の費用（p003 で測る、閾値は 8 節）。
2. C の present の待ち（R の submit・End・WSI の worker）。画面の速さは WS141 に依存。
3. `event_us` の時計（kernel の `clock_milliseconds`）と `kwl_microseconds`（CLOCK_MONOTONIC）が同じ基準かを Pi で確かめる。
4. `KWL LAT`・`KWL FIRST_FRAME` の定義し直し（p004）: want＝M が立てた時、ready＝R の acquire の後、draw＝M の walk の始め、recorded＝R の End、queued・submit＝R、shown＝M が完了の記録を受けた時（output_lost・switch_proving もこの時に知る）。
5. retire（`objects.c:739`）は完了の記録の処理の後に 1 つずつ（in_flight を見ない）。
6. `kwl_compose_quiesce` の 4 か所（`objects.c:554`、`backend-host.c:46`、`main.c:1120`、`shm.c:187`）と `->in_flight` の 16 か所は、p004 の実装の前に 1 つずつ park・retire・写しの帳簿のどれかに当てる。R が ready で待っている時と park の中の quiesce は即座に返る。
7. compose->command を直に使う所（`compose.c:2365〜`、`layer-cache.c:184`）は p003 の型の変更で見つからないので、grep で全部を挙げて一覧へ移す。
8. 動きの時刻: walk の時に animation の時刻を読むので、表示されるのは R の記録と GPU の時間だけ後（今も同じ）。

## 12. 結果の記録（この Phase）

- 調べたこと: 1 節（source の地図、Pi の既存の log の集計。P3 の boot の b4・b6・b10）。
- 変えた file: この phase.md、[review-1](review-1.md)・[review-2](review-2.md)・[review-3](review-3.md)（design-reviewer の要点の写し）、ws.md、p002〜p006 の phase.md。p002 の source は [p002](../phase002/phase.md)。
- 次: 第 4 版の再 review（S2 の部分）。p003 の着手は Q1 が WS211 と順を決めてから。
