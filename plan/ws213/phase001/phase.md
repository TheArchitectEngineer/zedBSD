<!-- awesome-plan project=zedbsd record=ws213-p001 -->

# ws213-p001: compositor の thread 化の調査と設計

Parent: [WS213](../ws.md)
Status: planning（設計の第 6 版。[review-1](review-1.md)〜[review-5](review-5.md)、2026-10-12 のユーザーの判断（描く物の一覧＋pull）と Q1 の判断（2 段の pull、cursor の thread を p004 に、pointer の経路を入力の thread へ、review-5 の未決への答え）を反映。S1（p003）は review-3・review-4 で条件付きで可、S2（p004）は review-5 で条件付きで可（N-1〜N-4 を設計の文に反映、再 review は不要で Q1 の確認）。ユーザー向けの要約は [summary.md](summary.md)）
Phase disposition: normal
Queue: P4（2026-10-12 Q1 が割り当て）

## ユーザー向けの要約

- **今の問題**: compositor は 1 本の thread で、1 コマを描く間（命令の記録・GPU への送り・表示の依頼）は入力も cursor も止まります。Pi 4 の記録では、普通のコマで 10〜20ms、App Home のような重いコマで 60〜120ms 止まっています。cursor の表示の依頼も、画面の書き換え（60 回/秒）より多い約 125 回/秒行っていて、main の時間をかなり使っている見込みです（測定を足しました、M0）。
- **形（2026-10-12 ユーザーの判断「描く物の一覧＋pull」）**: main thread は Wayland の protocol・入力・cursor と、今の描画の処理で「描く物の一覧」を作る所までを受け持ちます。main は「描く見込みがある」と知らせ、描画 thread は画面の画像を取り出してから「描いてよい」と返します。main は今どおり client の絵を少し待つなどの判断をしてから、一覧を 1 回作ります。描画 thread はそれを Vulkan の命令にして GPU に送り、表示を頼み、終わるのを待ちます。画像を取り出せた後にしか一覧を作らないので、作った一覧は必ず描かれ、作った時に決まった事（焼いた絵・画面の写し・client の画像の扱い）が無駄になったり食い違ったりしません。描画 thread は main が一覧を作っている間は何もしないので、その間 main は今のコードのまま GPU の物を作れます。
- **cursor が画面で 60fps になるには、WS141 の cursor の速い経路（B-u、GPU を通らない位置だけの表示）が要ります**（review-1 C1）。今の表示の仕組みは、cursor の表示の依頼を画面のコマの依頼と同じ 1 本の列に並べて GPU の完了を待つので、main を軽くしても、重いコマの間は cursor の表示が後ろに並びます。この WS は main の止まりを無くし、B-u と合わせて 60fps にします。受け入れの分担（review-B4、Q1）: WS141 p016 (B) は「main loop が止まらない時の入力→表示の ioctl」、この WS は端から端（マウスの入力→画面の cursor）の 60fps を受け持ちます。
- **入力の thread と cursor の thread**（p004、review-4 H-B）: マウス・touchpad の読み取りと加速と cursor の位置の計算は、小さな入力の thread に移します。入力の thread は位置を cursor の thread の受け箱に直に書き、cursor の thread がそれを表示します。main が一覧を作っている間も cursor の位置は止まりません。main は入力の thread から順番どおりに届く event を受けて、窓への通知（wl_pointer の motion・enter など）と hover・cursor の形を決めるだけです（ユーザーの指示「ウィンドウへの通知の転送以外の処理はほとんどすべてスレッドに」）。
- **この WS で必ず達すること**: pointer が動いている間、マウスが動いた時刻（evdev の時刻）から cursor の受け箱に位置が書かれるまでの遅れ（box_lag）の p99 が 1 refresh（16.7ms）以内（App Home の開閉・drag の間も）。最長も 16.7ms 以内で判定し、超えた原因が kernel の中の長い処理なら WS208（SMP）・kernel への依存として返す（review-5 N-4、Q1 の判断）。画面に映る cursor の 60fps は、これと WS141 の B-u で決まります。
- **段**: p002 測る＋cursor の依頼の間引き（実装済み、Pi の確認待ち）→ p003 描く物の一覧（thread はまだ使わない、見た目が同じことを確かめる）→ p004 描画 thread・cursor の thread・入力の thread（pull）→ p005 作業 thread（文字の形・壁紙のぼかし）→ p006 規約の見直し。
- **SMP（4 core）が無い今の効果**: CPU の総量は減りませんが、入力が来ると入力の thread が描画 thread や main を割り込んで先に動き、cursor の位置を書くので、cursor の遅れは減る見込みです（kernel の中の長い処理の間は待つ、11 節）。コマの速さ（fps）は SMP（WS208）か WS211 の描画の削減で上がります。

## 由来

- 2026-10-12 ユーザー:「コンポジタのマルチスレッド化も入れてください。ウィンドウへの通知の転送以外の処理はほとんどすべてスレッドにして、スレッドに通知を出して処理を委譲しましょう。ただ、描画のシリアライズはいるかも？と思いました。描画スレッドが別途あって、コマンドキューがシリアライズされるのかなあ。それとも描画はイベントキューに積むのか？」
- Q1 の推す形（main＝protocol・入力・cursor、描画 thread 1 本、作業 thread、最新の写しを受け箱に）にユーザー「了解です。おすすめがいいと思います。あとでできたらレビューさせてください。」
- 2026-10-12 ユーザーの判断（review-1 H6、Q1 経由）:「描く物の一覧＋pull」— main が今の描画の walk で display list を作り、描画 thread が「次をくれ」と言った時だけ 1 回作る。記録・submit・present・GPU の待ちは描画 thread。第 1 版の「受け箱の置き換え」はやめる。
- 2026-10-12 Q1 の判断（review-2）: K1 は「2 段の pull」、H-a は「R は待ちの間 Vulkan を呼ばない、walk の中の M の作成・破棄は許す」の不変条件で同期の依頼をやめる、H-c は shm の memcpy を M の walk の中に残し release は写した直後、H-d は cursor の shm を M が同期で作成・書き込み、退役は plane の fence も待つ。
- 2026-10-12 Q1 の判断（review-4 H-B）: ユーザーの目標は「すべて 60fps」、cursor の 60fps は必達。pointer の経路（evdev の読み取り→加速→C の受け箱）を M から外す形を本案に、client への pointer の通知は M が受けて送る。walk の分割は代替として 11 節に。p004 の受け入れに M の 1 pass の最長と、動いている間の C の受け箱の更新の間隔の p99・最長 ≤ 1 refresh、p003 の閾値は p90 か最長。
- 2026-10-12 Q1 の判断（review-5、S2 条件付き可）: N-1〜N-4 を設計の文に反映し再 review は不要（Q1 の確認）。未決への答え: p003 の重いコマ（300 draw 以上）の閾値は walk の p90 ≤ 16.7ms・最長 ≤ 25ms。p004 の `pass_max` ≤ 33.3ms は場面 (1)〜(3) で判定し、(4) の app の起動は p99 を記録。box_lag の p99 は必達、最長は kernel の長い transaction が原因と分かれば uncleared として WS208（SMP）・kernel への依存で返す条件付き。box_lag の最長と `hold_us` の時刻の対応を記録の項目に。make が sysroot を作り直そうとしたら止まって main に報告、を p003〜p006 の build の注記に。
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

## review-4 への対応

| 指摘 | 対応（節） |
| --- | --- |
| H-A 取り消し・park の間に R が head の画像を持ち続ける規則が無い（二度 acquire、漏れ、NOT_READY の続き） | R が slot ごとに（世代、画像）を持ち、取り消しと作り直さない park を越えて使い回す。step 3 は持っていない slot だけ取る。walk は R の持つ head を全部描いて表示。want の番号は種類が変わった時だけ進め、heads の差では取り消さない（足りない head は dirty で次へ）。R の want 下ろしは compare-and-clear（4.1・4.2・4.3・9 (c)） |
| H-B walk の間 cursor も止まる | Q1 の方針で、pointer の経路（evdev の読み取り・touchpad の状態・加速・出力をまたぐ位置）を入力の thread I へ。I が C の受け箱に位置を書く。M は I からの順序付きの event で client への通知と hover・形を受け持つ（3.1・6）。p004 の受け入れに M の 1 pass の最長と受け箱の更新の間隔の p99・最長 ≤ 1 refresh、p003 の閾値は p90 と最長（8 節）。walk の分割は代替として 11 節 |
| H-C C と M で cursor plane の持ち物が分かれていない（command pool、memset の間の読み、perf） | plane 専用の command pool を park で作り park で壊す。struct を M の持ち物（open・shown・extent の写し・failed の写し）と C の持ち物（present の状態）に分ける。C は失敗で failed を立てて止まるだけ、M が知らせで composite に切り替え、次の park で壊す。計数は C が持ち受け箱の mutex の下で公開（3.2・6.2・4.8） |
| M-A cursor の 2 枚の画像の穴（大きさ、傷んだ行、version の競合、plane の元） | 中身の大きさを extent と別に持ち外側は透明、2 枚には常に全体を写す、C が受け箱を取ってから copy の fence の完了まで pin、M は「今の受け箱の画像でなく pin も飛んでいる copy も無い画像」にだけ書く、plane の元は 2 枚と cursor の形だけ（set_cursor の時は surface の staging の map から写す）（4.7・6.2・9 (f)） |
| M-B 飛んでいるコマの間の client の破棄 | client の後始末のうち GPU の object と保持した buffer に触れる部分を、その client の物を参照する写しの完了の記録の処理まで延ばす。protocol の object と fd はすぐ閉じる（4.9・9 (l)） |
| M-C 破棄の持ち主の食い違い | 破棄は R が step 1 で行う形に揃える。R は want が無くても FIFO の依頼で起きる（3.2・4.2・4.7） |
| M-D Linux の TSan は C を通らない、queue_lock は無い | 本物の pthread と、外部同期の object の同時使用を検出する stub の Vulkan で M・R・C・I を TSan の下で動かす host 試験を p004 の受け入れに。Linux の TSan は M・R・I と既存の thread を見る物と書き、既存の thread と Mesa の抑制の方針、guest の入力の自動化を書いた。queue_lock は新しく足す物と直した（3.3・9 (m)・8） |
| M-E park で C も止めるか | 全部の park で R と C の両方を止め、parked は両方の ack。park の間と os_paused・sleep_hold・game mode の間、C の「出すか」は 0（4.8） |
| L-a pacing の引用 | `display.c:203-223` に直した（2・4.3） |
| L-b build のコマンド | `BUILD=build/ws213-pNNN/...`、`make keiland-linux` に `KEILAND_ASK=n KEILAND_LINUX_BUILD=...`（p003〜p006 の phase.md） |
| L-c p003 の READY +50ms は誤差並み | `KWL STARTUP step=cursor-shapes` を足し、その ms で判定（8 節・p003） |
| L-d p004 の受け入れ 5・p005 の数値 | 場面・時間・log の行を書いた。p005 は受け箱の更新の間隔と M の 1 pass の最長の数値（8 節・p004・p005） |
| L-e lock の順 | 4.6 に: 3 つの mutex を同時に持たない、mutex を持ったまま Vulkan と止まる system call を呼ばない |
| L-f p003 の閾値を満たせない時の WS211 への依存 | p004 の依存・再開条件に（8 節・p004） |

## review-5 への対応

判定は S2（p004）条件付き可。N-1〜N-4 を設計の文に反映（再 review は不要、Q1 の確認）。N-5〜N-7 も書き込んだ（N-5 の持ち物の表は 6.1）。

| 指摘 | 対応（節） |
| --- | --- |
| N-1 High: I の evdev の fd と backend の pause・resume・gone の競合（閉じた fd の番号の再利用で I が client の socket を読む） | device の付け外しを I への依頼に: 付ける（新しい fd、初期化は I）と同期の外す（上限付きの ack、ack はその device の最後の event の後）。backend の 3 つの callback と session の pause で外し、resume は付ける。logind の resume の close の順は保険で入れ替える（6.1「device の付け外し」・9 (k1)） |
| N-2 Medium: 絶対の pointer（virtio-tablet）の経路が無い | I が出力の矩形の写しの anchor に写して受け箱に書く。warp の番号は進めない（6.1「絶対の pointer」・9 (k2)） |
| N-3 Medium: warp の一覧の誤り、M の遅れた位置からの計算 | `band_replay`（`shell.c:7827,7839`）を warp から外し一時の書き換えの側へ。M の warp は受け箱の位置と `pointer_output` を `cursor_lock` の下で読み、出力の矩形の写しの更新と同じ区間で書く。I は読み・`kwl_plane_move`・書きを 1 回の lock の中で、printf と dirty は外か M へ（6.1「位置の持ち主」・9 (k3)） |
| N-4 Medium: box_gap の最長は遅い pointer で満たせない | 判定を box_lag（`event_us` → 受け箱の書き込み）の p99・最長 ≤ 16.7ms に。時計の確かめ（11 節 3）が前提、確かめられなければ「書き込みの間隔 − `event_us` の間隔」の超過分を代替に。最長は Q1 の条件付き（6.4・8・11 節 9） |
| N-5 Low/Medium: device と server の欄の持ち主 | 持ち物の表（6.1）。report は FIFO に写す、設定の変更は I への依頼、touchpad の `release_all` と外しの ack はその device の最後の event の後（9 (k4)） |
| N-6 Low: p003・p004 の閾値に余裕が無い | Q1 の判断: p003 の重いコマは walk の p90 ≤ 16.7ms・最長 ≤ 25ms。`pass_max` ≤ 33.3ms は場面 (1)〜(3) で判定、(4) は p99 を記録（6.4・8） |
| N-7 Low: `set_cursor` の時の写し元が古い | 未処理の commit があればその commit の client の pool から写す（4.7・9 (f)） |
| 未決への答え (1)〜(4) | (1) 33.3ms は妥当（6.4・11 節 1）。(2) p99 は必達、最長は条件付き、`hold_us` との時刻の対応を記録（6.4・11 節 9）、pthread の優先度は p004 の始めに。(3) sched の見込みは 10 節。(4) build の注記は p003〜p006 の phase.md |

## WS141 p016 review-B4 の WS213 に関わる点への対応（2026-10-12 Q1 の依頼）

| 点 | 対応（節） |
| --- | --- |
| F1 (b) 像の変化の後に swapchain の全部の画像が copy の順番待ちになる | C の copied の gate をやめ、待つのは acquire した画像の前の copy の fence だけ。C は copy の fence を待ち続けず、pin は飛んでいる copy の数で下ろす（4.7 の 3・6.2・9 (i)） |
| 「WS213 との関係」受け入れの期待の循環 | 分担: p016 (B) は「main loop が止まらない時の入力→ioctl」、WS213 は端から端の 60fps（6.3・ws.md） |
| M-d・H-b zedBSD に queue_lock が無く C と R が同じ VkQueue を同時に使う | libvulkan の安全は p016 B-u が明記し fixture で確かめる前提として引用（3.3・6.3・7） |
| M-f L1 の位置付け | L1 は前提ではなく「p016 の計測の後の次の版」に揃えた（6.3・ws.md） |
| M-e import・shm の `vkQueueWaitIdle` が present_drain で worker を待つ | p004 で import の layout の溢れと shm の画像の layout を一覧の barrier にして M から外す（3.2） |

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

- **2 段の pull**（review-2 K1、review-3 H-1・H-2・M-1、review-4 H-A）:
  1. M は「描く見込みがある」の印 **want** を、番号と中身（種類: 出力のコマ / present しない layer の絵、要る heads の slot と世代）付きで立てる。**番号は種類が変わった時（と下ろした後に立て直した時）だけ進める**。要る heads の list が変わっても番号は進めない。
  2. R は最新の want を見て、出力のコマなら、まだ持っていない出力の画像と、want に書かれた heads のうちまだ持っていない slot だけを上限付きで acquire し、**持っている全部**（画像 k と、slot ごとの（世代、画像））を付けて **ready**（want の番号付き）を知らせる。layer の絵なら acquire せずに ready。
  3. **ready は「描いてよい」の知らせで、walk を強制しない**。M は今の自分の schedule（frame pacing `display.c:203-223`、`compose.c:725-731` の待ち、dirty・damaged）で walk の時を決め、その時の最新の want の番号が ready の番号と同じなら k と R の持つ heads の全部を描く walk を 1 回して一覧を渡す（want にあって R が持っていない head は dirty のまま次のコマへ）。番号が違えば（種類が変わった・要らなくなった）**取り消し**で答え、R は持っている画像を全部持ったまま新しい want を見直す。
- 写しは acquire の後にしか作られず、作った写しは必ず描かれる。acquire の失敗や park では写しが生まれないので、walk の副作用（layer の `decided_anim`・`keys`・`valid`・`inflight` `layer-cache.c:262,279,626-627`、shot の `SHOT_RECORDED` `shot.c:339`、import の layout の list `import.c:186-197`、shm の写しと release、`dirty`・`pointer_moved`・`home_start_pending`、damage の履歴）を巻き戻す経路は要らない。damage は今の buffer age（`compose_region`）がそのまま使える。
- 案 B の前提（p003 で測る、p004 に進む閾値は 8 節）: walk の費用は記録全体の一部。根拠: 記録の費用は draw の数にほぼ比例し（0〜100 draw で 3〜8ms、300 以上で 48ms）、中身は libvulkan の encode と End の decode の transaction（kernel の `hold_us decode` 最大 11〜19ms）が大半。

## 3. thread の分担と Vulkan の持ち主

### 3.1 分担

| thread | 受け持つ物 |
| --- | --- |
| **M（main）** | poll、Wayland の protocol、I から順に届く入力の event の処理（hover の判定、focus、compositor の grab・画面、client への wl_pointer・wl_keyboard などの配達）、入力の device の open・close（scan・hotplug・session）、cursor の像の決定（C に渡す）、server の状態の全部、want、walk（と walk の中の Vulkan、3.2）、frame pacing、client の buffer の参照の数、frame callback、log |
| **I（入力、1 本、p004 から、review-4 H-B）** | evdev の読み取りと frame の組み立て（今の `kwl_input_read_devices` の device の間の時刻の順、BUG-142）、touchpad の状態機械（`touchpad.c`、tap の期限の tick も）、pointer の加速（`pointer-accel.c`）、相対の動きの出力をまたぐ位置（`kwl_pointer_relative` `heads.c:1786`、出力の矩形の写しに対して）、**C の受け箱の位置を書いて C を起こす**、全部の event を順に M へ。Vulkan を呼ばない（6.1） |
| **R（描画、1 本）** | 出力と want の heads の acquire・present、一覧の再生（vkCmd*）、host image の copy の記録、`vkQueueSubmit`、fence の待ち、M が頼んだ object の破棄（4.2 step 1） |
| **C（cursor、1 本、p004 から、review-3 M-3）** | cursor の plane の present の状態の全部（swapchain・専用の command pool・acquire・copy・present、C0 の 1 refresh に 1 回、fence の待ち、計数）。受け箱から最新の（I の位置、M の像・version・hotspot・出すか）を読む（6.2） |
| **W（作業、p005、まず 1 本、SMP 後は core−1）** | glyph の raster、壁紙の描画とぼかし、大きな字 |

### 3.2 不変条件（review-2 H-a）と Vulkan の行き先

**不変条件**: R は ready を知らせてから一覧（か取り消し・park）を受け取るまでの間と park の間、Vulkan を一切呼ばない。だから **M は walk の中と park の中で、今のコードのまま Vulkan を呼んでよい**（command pool・descriptor pool・queue・host image の staging を含む）。M が walk と park の外で呼んでよいのは device 単位の作成・破棄・問い合わせ（backend の import、physical device の問い合わせ）と M 専用の object（hotplug の fence）だけ。C は自分専用の object（plane 専用の command pool・command buffer・fence・semaphore・swapchain、review-4 H-C）と queue（3.3）だけを使い、`compose->pool` を使わない。I は Vulkan を呼ばない。R が walk の外で呼ぶ破棄（4.2 step 1）は、M が walk も park もしていない間（R が ready を出す前）にしか起きない。

| 所 | 今（M） | p004 の後 |
| --- | --- | --- |
| 出力の swapchain・framebuffer・scene（`scene.c:41-67`）・layer の画像（`layer-cache.c:733,807`）・batch の buffer・load pass（`compose.c:383,2331`、`backdrop.c:216`） | 出力の open・初回 | park の中（出力の open）か walk の中（初回の遅延作成はそのまま可） |
| backdrop の target（`backdrop.c:69-79,395,459`） | walk の中の遅延作成 | walk の中のまま |
| cursor の形の画像（`cursor.c:262-270,378-388`、10 枚 24×24、今は 1 枚ごとに `vkQueueWaitIdle` `shm.c:879-881`） | 最初に使う時（walk の外もある） | p003 で起動時に host image の batch（`shm.c:354-392`）で 1 回の submit と wait にまとめて全部作る（review-3 M-6、READY の遅れの上限は 8 節）。C も読む（4.7） |
| 大きな字（`glass.c:2147-2152`、greeter `1633`） | 大きさが変わる時、`vkDeviceWaitIdle` | walk の中（`vkDeviceWaitIdle` は R の前のコマが終わっているので不要。WSI の worker の drain は残る、review-3 L-1）。p005 で W へ |
| 壁紙の描き直し（`glass.c:648`→821・1091）・landscape（451）・resize（493） | `vkDeviceWaitIdle` の後に CPU で書く | p004 では park の中。p005 で画素は W、staging への写しは walk の中 |
| client の import（backend の作成、`kwl_import_adopt` の view） | protocol の request の中 | M のまま（device 単位）。descriptor set（`import.c:258,276`）は walk の中で初めて使う時に。R へは F-094 |
| import の layout の list と溢れ（`import.c:186-197,248-252,348,353`） | 次のコマ、溢れは即時 | list は写しの中身。walk の外では溢れさせない（list を伸ばす）。溢れの即時の `vkQueueSubmit`＋`vkQueueWaitIdle`（`import.c:353`）もやめ、溢れた分は一覧の barrier として R の command buffer に載せる（review-B4 M-e） |
| import・shm の画像の破棄（`objects.c:830`、`shm.c` の `image_release`） | 破棄の時 | 参照する写しが全部完了してから、M が M→R の FIFO で頼み、**R が step 1 で壊す**（review-4 M-C。R は want が無くても FIFO の依頼で起きるので、静かな間にも溜まらない）。park の中は M が直に壊してよい |
| wl_shm の画像の作成と memcpy（`shm.c:555-631`） | frame の外 | 作成は walk の中。作成の時の layout の移し（`image_layout` の `vkQueueSubmit`＋`vkQueueWaitIdle` `shm.c:882`）は一覧の barrier として R の command buffer の copy の前に載せ、M は待たない（review-B4 M-e）。memcpy と release は walk の中、または飛んでいる写しの copy の list にその画像が無ければ walk の外でも（review-3 L-2: output_lost の間も client を待たせない） |
| cursor の surface の wl_shm（plane の大きさ以下） | 他の shm と同じ | 4.7（前もって用意した 2 枚に memcpy だけ、review-3 H-3） |
| shot（試験の image だけ: buffer の作成 `shot.c:445,472`、`vkMapMemory` 359、解放 414-415） | tick と完了 | buffer の作成・解放は walk の中か park の中。map した画素の読み出しは完了の記録の処理の時（R が次の ready の前で、その buffer を触らない時） |
| host image の起動時の batch（`shm.c:354-392`） | 起動 | R・C の起動の前 |
| heads の swapchain・view（`kwl_heads_sync`、`output-switch.c:89-91,149`）・出力の移動（`output-switch.c:441`） | output の tick | tick は「変更が要る」の印を立て、M が park（非同期）を頼んで parked の後に行う |
| hotplug の fence（`output-switch.c:98-104`） | output の tick | M のまま |
| cursor の plane（`cursor-plane.c` の全部、`vkWaitForFences` 534、close の `vkDeviceWaitIdle` 121・126、command buffer の確保 `cursor-plane.c:128,410-412` は今 `compose->pool`） | pass ごと | open（swapchain と **plane 専用の VkCommandPool** の作成）は park の中で M、present の全部は C、close は park の中で C が自分の fence を上限付きで待ってから自分の物を壊す（`vkDeviceWaitIdle` を使わない、review-3 L-5、review-4 H-C）。C の失敗は 6.2 |
| frame の fence の待ち | M | R |
| 出力の close・終了・session の pause・sleep・game mode の切り替え | M（`vkDeviceWaitIdle`・quiesce） | park（4.8） |
| `kwl_compose_quiesce` | frame の完了を待つ | R が ready で待っている時と park の中は即座に返る。飛んでいる写しがある時は 11 節の割り当て |

- **M の `vkQueueWaitIdle` と WSI の worker**（review-B4 M-e）: libvulkan の `vkQueueWaitIdle` は present_drain で WSI の present の worker を待つ（`wsi-swapchain.c:617-629,2853-2900`）。今は import の layout の溢れと shm の画像の layout の待ちの間、main loop も cursor も止まる。p004 ではこの 2 つを一覧の barrier にして M から外す（上の表）。残る M の `vkQueueWaitIdle`（起動時の host image の batch、park の中）は cursor の位置を止めない（位置は I→C）。

### 3.3 queue の lock（review-1 H3、review-2 M-d）

- 出力の queue は 1 本（bcm2711 の queueCount は 1、`vulkan-query.c:182`、i915 も 1）。C の cursor の plane と R の出力のコマと M の walk・park の中の即時の submit が同じ queue を使う。
- zedBSD の libvulkan の build では compositor の queue_lock を使わない（libvulkan が `queue->mutex`・`device->mutex`・`context->mutex` で中で直列にする）。C と R が同じ VkQueue に同時に submit・present する（B-u の fast path を含む）ことの安全は WS141 p016 B-u が明記して確かめる前提で、WS213 はそれを引用する（6.3、review-B4 M-d）。
- Linux・FreeBSD の host の Vulkan（Mesa）の build には、compositor の `queue_lock` を**新しく足す**（今は無い、review-4 M-D）。queue を引数に取る全部の呼び出し（R・C・M の `vkQueueSubmit`・`vkQueuePresentKHR`・`vkQueueWaitIdle`）の間だけ持つ。build の印で切り替え、置き場は p004 で決める。
- 事実（review-4 M-D）: compositor は plane を platform で分けずに開こうとする（`cursor-plane.c:43-100`）が、Linux・FreeBSD の `libvulkan-compat` の display は plane 0（不透明）しか作らない（`libvulkan-compat/wsi-display.c:142,400-401,412-413`）ので、今は Linux・FreeBSD で cursor の plane は開かず C は何もしない。R と M の queue の呼び出しは 3.2 の不変条件で重ならないので、`queue_lock` が今守るのは将来 plane が開く時の C と R。C の同時使用の確かめは host の試験（9 (m)）で行う。

## 4. pull の 1 周と写し

### 4.1 want の規則（review-3 H-1・H-2・M-1、review-4 H-A）

- **持ち主は M**。M は want を立てる・中身を置き換える・下ろす。**番号を進めるのは、種類（出力のコマ / layer の絵）が変わった時と、下ろした後に立て直した時だけ**。heads の list の置き換えでは番号を進めない（R が持っている head を取り消しで捨てる経路を作らない）。
- R が下ろすのは (a) 写しを受け取った時、(b) acquire が失敗した時（OUT_OF_DATE・SURFACE_LOST、他の失敗）だけで、**どちらも compare-and-clear**（`render_lock` の下で、want の番号が R の扱った番号と同じ時だけ下ろす。M がその間に立て直した want を消さない）。(b) の時は M が失敗の処理（`output_lost` など）の後に立て直すまで R は acquire しない（R の空回りを防ぐ、`wsi-swapchain.c:3060-3064`）。
- M が want を立てるのは「描く見込み」が出た時（dirty・damaged・layer の静かな時の条件、sleep_hold・os_paused・game mode でない）。pacing の待ちの間に立ててよい（R はその間に acquire を済ませる）。
- **heads**: want に、要る head の slot の番号とその世代の組の list を入れる（M が `heads_needs_frame` `heads.c:1111-1140` で決める。pointer の位置でも変わる `heads.c:1127-1129`）。R はその slot のうち**まだ画像を持っていない slot だけ**を timeout 0 で取り、slot ごとの結果（取れた画像・取れない・lost）を返す。`head->dirty` の上げ下げ・`lost`・missed は M が完了の記録と ready の結果で当てる（walk の時に下ろし、失敗の時だけ立て直す）。R は head の状態（`kwl_heads_acquire`・`kwl_heads_present` の `head->dirty=0`・`lost=1`、`heads.c:316-361,459-496`）を書かない。
- **R の持つ画像**（review-4 H-A）: R は出力の画像 k と、slot ごとの（世代、画像の番号）の組を自分の帳簿に持つ（今の `head->acquired` 1 本と `head->image` の組 `heads.c:340` は、取った画像を手放す手段が無い `heads.c:500-512`）。持っている画像は present するまで手放さず、**取り消しと、その swapchain を作り直さない park を越えて使い回す**。同じ slot・同じ世代を二度 acquire しない。walk は R の持つ head を**全部**描き、R は全部を present する（want の list から外れた head も。mirror は写し、extended は今の絵を描き直すだけ）。park で swapchain が作り直された slot（世代が変わった slot）と出力の k は帳簿から落とし、acquire の semaphore も作り直す（4.2）。

### 4.2 R の 1 周

1. M→R の FIFO の依頼（park、object の破棄）を順に処理。**object の破棄はここだけ**（review-4 M-C。参照する写しが全部完了した物だけが届く）。
2. 最新の want か FIFO の依頼を待つ（上限付きの繰り返し、100ms ごとに park・停止の印を見る）。FIFO の依頼が来たら 1 へ（want が無くても破棄は進む）。
3. 出力のコマなら、まだ画像を持っていなければ出力を `vkAcquireNextImageKHR` で上限付き（例 50ms）に繰り返し取る（間に park・停止を見る）。want の heads のうち**持っていない slot だけ**を timeout 0 で取る。出力の失敗なら want を compare-and-clear で下ろして「acquire の失敗」を知らせ、2 へ（持っている画像は持ったまま）。
4. ready（want の番号、k、持っている heads の全部と今回の結果）を知らせ、M の答え（一覧・取り消し・park）を待つ。**この間 R は Vulkan を呼ばない**。取り消しなら画像を全部持ったまま 2 へ（新しい want が出力のコマなら持っている k と heads を使う）。
5. 一覧を受けたら、写しの host image の copy の list を記録し、一覧を再生、`vkEndCommandBuffer`、`vkQueueSubmit`・`vkQueuePresentKHR`・持っている heads の全部の present。present した画像を帳簿から落とす。
6. fence を上限付きで繰り返し待つ → 完了の記録を M に渡す。
7. 1 へ。飛んでいるコマは 1 つ（今と同じ）。
- park の時に R が画像（k と heads）を持っていたら present せずに持ったまま止まる。park の中で swapchain が壊されればその画像は帳簿から消え、壊されなければ unpark の後に同じ画像を使う。有効性は出力と slot ごとの swapchain の世代で確かめる。**park の中で swapchain が作り直された時は、acquire の semaphore（`compose->acquired`、`compose.c:1386-1390`、heads の acquired）も作り直す**（Mesa では signal 済みのまま待たれない semaphore を次の acquire に使えない、review-3 M-5）。

### 4.3 M の答え方

- ready を受けたら、M は自分の schedule（`display.c:203-223`）で walk の時を決める（pacing の待ちの間は答えない、R は待つ）。walk の時に最新の want の番号が ready と同じなら walk、違えば取り消し（R が新しい want で ready を出し直す）。heads の list の違いでは取り消さない: walk は ready に載った R の持つ head を全部描き、want にあって R が持っていない head は `head->dirty` を立てたまま次のコマへ（review-4 H-A）。
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

- R→M の知らせは 1 本の FIFO（`render_lock` の下の list＋pipe）: 完了の記録・acquire の失敗・ready・parked・park の失敗。ready は必ず前のコマの完了の記録の後。M は積まれた順に処理する。C→M の知らせ（plane の失敗、C の parked）は受け箱の印（`cursor_lock` の下）と同じ pipe の 1 byte で起こす。I→M の入力の event は別の FIFO（`input_lock` の下の list＋pipe、6.1）。
- 完了の記録: frame の番号、結果、heads の slot ごとの結果、出力の present の結果（`compose.c:2826-2853` の `switch_proving` の成否 → `kwl_displays_move_settled`、`output_lost`）、scene の written、layer の焼き、shot、計測。M は今の `kwl_compose_complete` と `compose_submit` の後ろ半分の server の状態の書き換えをここで行う。output_lost・switch_proving を M が知るのは完了の記録の時（今は submit の直後、review-3 L-6）。

### 4.6 damage・費用と lock

- damage は今の `compose_region`（画像 k の buffer age）。R は region を狭めない。
- 受け渡しは pointer の交換だけ。一覧は App Home の 460 draw で約 70KB＋束の頂点（最大 1MiB）。buffer は 2 つ。R との共有は want・ready・写し・2 本の FIFO・park の印だけで、`render_lock` と condvar の下。C・I との共有は 6 節。
- **lock の規則**（review-4 L-e）: mutex は 3 つ — `render_lock`（M↔R）、`cursor_lock`（C の受け箱、M・I↔C、M→I の写しと device の付け外しの依頼）、`input_lock`（I→M の event の FIFO）。(1) どの thread も 2 つを同時に持たない（順序を決める必要を作らない）。(2) mutex を持ったまま Vulkan・`poll`・`read`・`write`（pipe の起こしも）・log の `printf` を呼ばない: 中身を写して放してから呼ぶ。(3) condvar の待ちは自分の mutex 1 つだけで。(4) 「pin」「failed」などの印は mutex の下で立て下ろすが、mutex は印の間ずっとは持たない。9 (m) の試験は lock を持ったまま stub の Vulkan が呼ばれたら失敗にする。

### 4.7 object の寿命

| object | 作る | 壊す | 守り |
| --- | --- | --- | --- |
| client の import（image・memory・view） | M（backend） | 参照 0 の後、M の依頼で R が step 1 で（park の中は M） | 写しの帳簿の保持（参照 0 は完了の記録の後にしか来ない） |
| import の descriptor set | walk の中（初めて使う時） | import と一緒 | 同上 |
| wl_shm の画像 | walk の中 | 参照する写しが全部完了した後、M の依頼で R が step 1 で（park の中は M） | surface と写しの帳簿 |
| **cursor の画像 2 枚**（review-3 H-3、review-4 M-A） | **plane の extent（64×64）の画像を 2 枚、R・C の起動の前か park の中で前もって作る**。規則は表の下 | park の中 | pin と写しの帳簿、C の `held[]`（世代で比べる） |
| glass の host image・cursor の形 | R・C の起動の前 | park の中 | 起動から終了まで |
| backdrop・shot の buffer | walk の中 | park の中 | 世代 |

- **cursor の画像 2 枚の規則**（review-4 M-A）:
  1. **大きさ**: 画像の extent は plane の extent（64×64）に固定し、cursor の中身の大きさ（`content_width`・`content_height`）を別に持つ（今は大きさが変わると作り直す `shm.c:578`、plane の copy は `source->width` を使う `cursor-plane.c:668-669`）。M は書く時に中身の外側を透明（0）で埋め、plane の copy と composite はどちらも中身の大きさで読む（plane は今も外側を clear する）。cursor の surface が extent より大きい時は 2 枚を使わず、普通の shm の画像で composite（作成は次の walk の中）。
  2. **常に全体**: 2 枚には commit ごとに中身の全体を写す（傷んだ行だけを写すと `shm.c:582-590`、2 枚が交互なので 2 つ前の行が残る）。64×64 で 16KiB なので費用は小さい。
  3. **pin と書いてよい画像**: C は受け箱から画像を取って copy する時に `cursor_lock` の下でその画像の pin の数を 1 増やし、その copy の fence の完了を見た時に `cursor_lock` の下で 1 減らす（0 で pin が無い）。C は copy の submit の後にその fence を待ち続けない（review-B4 F1 (b)、6.2）: 完了は次の pass の始めの `vkGetFenceStatus`（timeout 0）か、同じ swapchain の画像を次に acquire した時の待ちで見る。M が書いてよいのは、**今の受け箱の画像でなく、pin が無く、出力の飛んでいるコマの copy の list にも無い画像**だけ。書いたら受け箱をその画像に替え version を進める。2 枚とも書けなければ、その commit は M が持っておき次の pass に延ばす（最新の commit だけを残し、buffer は今どおり写した直後に release）。
  4. **plane の元**: plane に出す元は cursor の形（起動時に作り不変）と 2 枚だけに限る（今の `plane_state` は大きさの合う host の画像なら何でも出す、review-3 L-2 の経路で C の読み中に書かれる恐れ）。client の cursor の surface は commit の時に client の pool から 2 枚へ写す。`set_cursor` で surface を選んだ時は、その surface に M がまだ採用していない commit（acquire fence や pacing で延びている物）があれば、その commit の buffer の client の pool から写す。無ければ surface の staging の map（採用済みの最新の中身）から写す（review-5 N-7: staging は採用前の commit を反映しないので古い）。それ以外の画像の cursor（dmabuf、大きい物、dnd の icon）は composite。
- `struct kwl_import` に作成ごとに増える世代の番号を足し（p003）、C の `held[]` と bind の鍵はそれで比べる（同じ address の新しい import で古い像を出さない、ABA）。

### 4.8 park（review-1 H4、review-2 M-a、review-3 M-2、review-4 M-E・H-C）

- **park は R と C の両方を止める**（全部の park で。review-4 M-E）。parked は R と C の両方の ack が揃った時で、M はそれを待ってから変更を行う。park の間、受け箱の「出すか」は 0（C は present しない）。I は Vulkan を呼ばないので止めない（6.1）。
- **非同期の park**（普通）: M が park を頼み、自分の loop を続ける。R は今のコマを最後まで描いて fence を待ち（上限付き）、parked を FIFO で知らせて止まる。C は今の present を終えて自分の copy の fence を上限付きで待ち、parked を知らせて止まる。M は両方の parked を受けてから変更（出力の open・close・移動、heads、glass の大きさ・landscape・壁紙、game mode の入りと出、cursor の plane の open・close と 2 枚の作成）を行い、unpark する。その間 want は立てない。
- **plane の失敗の後**（review-4 H-C）: C が failed を立てて止まった後の最初の park で、C が自分の物（fence を上限付きで待ってから swapchain・専用の command pool・semaphore）を壊して ack し、M は自分の側（open・shown）を下ろす。fence が上限までに終わらなければ park の失敗と同じ扱い（下）。
- **os_paused・sleep_hold・game mode の間**（`display.c:118,169-175` で今も plane を更新しない）: その状態に入る park の中で M が受け箱の「出すか」を 0 にし、出る park の中で戻す。この間は unpark しない（R・C は parked のまま。os_paused・sleep は session の device が無く、game mode は R の描く物が無い）。
- **同期の park**（session の pause `backend-host.c:46` と終了だけ）: M は parked を上限（例 2 秒）付きで待つ。sleep の依頼（`display.c:160-166`）は非同期で、parked の後に sleep に答える。
- **park の失敗**（上限を超えた: R か C が libvulkan の上限の無い待ち（`queue.c:345-363`、`sync.c:1103-1108`）の中）: 後始末（`compose.c:443,815` の `vkDeviceWaitIdle`、`cursor-plane.c:121`）はせず、`KWL FAILED site=park` を log に出して `_exit`。他の thread が Vulkan の中にいるので、後始末の Vulkan は呼べない。
- os_paused・sleep_hold・game mode の間、M は want を立てない。出力の世代が変わるのは park の中だけ。

### 4.9 client の破棄と飛んでいる写し（review-4 M-B、review-2 M-c）

- 今は client の後始末の始めに `kwl_compose_quiesce`（`objects.c:554`）で飛んでいるコマを待つ。p004 では待たない: client の後始末を 2 つに分ける。
  1. **すぐ**: fd を閉じ、protocol の object を server の表から外し、以後その client に event を送らない（IME・通知・cursor の client の扱い `objects.c:558-565` も今どおりここ。cursor は受け箱を矢印に替える）。
  2. **延ばす**: その client の物（buffer の参照、import、shm の画像、frame callback）に触れる後始末は、その client の物を参照する写しが飛んでいる間は「死んだ client」の list に置き、その写しの完了の記録を M が処理した時に行う（写しの帳簿の保持が切れてから）。参照する写しが無ければ（R が ready で待っている・park の中・飛んでいるコマが無い）すぐ行う。GPU の object の破棄は 4.7 のとおり R の step 1 に頼む。
- 完了の記録の処理で死んだ client の frame callback は送らない（fd は閉じている）。C は client の物を参照しない（plane の元は cursor の形と 2 枚だけ、4.7）ので、C に延ばす物は無い。
- 9 (l) の場面: (1) 写しを R に渡した直後に client が切れる、(2) ready を出して M が walk の前に client が切れる、(3) 写しが cursor の surface の 2 枚を参照中に client が切れる、(4) 死んだ client の buffer の release・frame callback を送らない。

## 5. host image と作業 thread（review-2 H-b、M3、M-e、review-3 L-4）

- **staging への書き込みは walk と park の中だけ**（p004 から）。今 walk の外で書く所: IME の tick（`main.c:801` → `input-method.c:307` → `ime_current` 2621 → `kwl_home_field_state` home.c:1236 → `glass_text_width` glass.c:1847 → `glass_cache_glyph` → `atlas_put` → `kwl_host_image_written` glass.c:2983）、色の emoji（glass.c:3234）、壁紙の loader（glass.c:648→821・1091）、landscape（451）、resize（493）。p004 では:
  - `glass_cache_glyph` は walk の外では cell の選びと metrics と raster までを行い、bitmap を M の「未着の list」に置く。次の walk の始めに、**cell を割り当てた順に**書く（同じ cell が 2 度割り当てられていれば後の方が残る、L-4）。walk の中で初めて出た字は今どおりその場で書く。色の emoji も同じ。
  - 壁紙・landscape・resize は park の中（まれ）。cursor の surface の shm は 4.7。
  - R が写すのは walk の終わりに写しへ移した host image の list だけ。
- **p005（W）**: raster を W に出す。M は cell と metrics だけを同期で決め、job を W に出す。walk の始めに終わった job の bitmap を cell の割り当ての順に staging に書き、終わっていない job の字は今回は描かず「glyph が未着」の印を立てる（その写しで layer を焼いていれば、その layer の絵は完了の後に無効）。W は自分の truetype の face を持つ。色の emoji と予備の font の open（`glass.c:3105-3125`）は M に残す。壁紙の画素とぼかし・大きな字は W が作り、walk の中で staging に写す。
- 追い出し: cell の上書きは walk の中にしか起きないので、飛んでいるコマの cell は書き換わらない。焼いた layer の絵は焼いた時に sample 済み。

## 6. cursor と入力（review-1 C1・H3、review-2 H-e、review-3 M-3、review-4 H-B・H-C）

### 6.1 入力の thread I（p004、review-4 H-B、Q1 の方針）

- **目的**: walk（App Home で数十 ms）と M の他の仕事の間も cursor の位置が 1 refresh 以内に受け箱へ届くこと。ユーザーの目標は「すべて 60fps」、指示は「ウィンドウへの通知の転送以外の処理はほとんどすべてスレッドに」。C に読ませない理由: C は plane の fence（最大 100ms `cursor-plane.c:534`）と present で止まるので、その間の入力が受け箱に届かない。I は Vulkan を呼ばず、止まるのは `poll` だけ。
- **I が行うこと**: M から渡された device の fd を `poll` し、今の `kwl_input_read_devices`（`input.c:312`）の読み取りと device の間の時刻の順（BUG-142）をそのまま行う。
  - 相対の pointer（マウス）: `kwl_pointer_accel_move`（`input.c:764`）、出力をまたぐ位置（`kwl_pointer_relative` の計算 `heads.c:1786`、`kwl_plane_move` を出力の矩形の写しに対して）。
  - 絶対の pointer（QEMU の virtio-tablet など、`apply_frame` の absolute）: 下の「絶対の pointer」（review-5 N-2）。
  - touchpad: 状態機械（`touchpad.c`、actions を返す純粋な module、`kwl_touchpad_tick` の tap の期限は I の `poll` の timeout）と、motion の action の位置（`input.c:1040-1080` の `pointer_move`、`touchpad_speed` の端数）。
  - 位置が変わったら、受け箱の読み・位置の計算・書き（x、y、pointer_output、event_us、I の時刻）を `cursor_lock` の 1 回の区間で行い（下の「位置の持ち主」）、C の condvar を起こす。
  - 全部の event（位置の変わった motion は動いた後の位置と warp の番号付き、ボタン・key・軸・touchpad の action・絶対の device の report・touch・tablet・device の消失）を、device の報告の順に `input_lock` の FIFO へ積み、pipe で M を起こす。
- **M が行うこと**（今の処理のまま、元が I の FIFO になる）: motion を順に処理して `server->pointer_x/y`・`pointer_output`（M の見る位置）を event の位置にし、`kwl_seat_motion`（grab・画面・hover・client への wl_pointer の motion・enter・leave、`seat.c:484`）、`pointer_unmoved`・`kwl_damage_pointer`・`KWL POINTER output=` の log。ボタン・key 以下も今どおり。motion はまとめない（今と同じく 1 つずつ配る）。
- **I と M の共有**（`cursor_lock` の下、M→I の写しと依頼の列）: 設定の写し（`mouse_speed`・`mouse_acceleration`・`touchpad_speed` と touchpad の feel）、出力の矩形の写し（`kwl_outputs` と anchor の大きさ。変わるのは park の中か output の tick だけ、M の warp と同じ区間で写す）、device の付け外しの依頼（下）。M は依頼を列に積み、lock を放してから pipe で I を起こす。I は `poll` から戻るたびに列を処理する。
- **device の付け外し**（review-5 N-1）: I が `poll`・`read` する fd は I の device の表にある物だけで、表を書くのは I だけ。M は evdev の fd を読まない。
  - **付ける**: M が device を開いた時（scan・hotplug）と backend の resume で新しい fd を受けた時、M は「付ける」（slot、fd、種類と abs の範囲の写し）を頼む。`frame_count`・`discarding`・touchpad の状態の初期化は I が付ける時に行う（今 M が `backend-host.c:108-110` で書く物）。M は付けの ack を待たない。
  - **外す（同期）**: fd が閉じられる前に、M は「外す」を頼み、I の ack を `input_lock` の condvar で上限付き（100ms。I は列を見るたびにすぐ答える）で待つ。I は外した fd を以後 `poll` にも `read` にも使わず、その device の最後の event（touchpad の `release_all` の action、tablet・touch の消失の印）を FIFO に積んでから ack を積む（N-5）。上限を超えたら `KWL FAILED site=input-detach` を出して `_exit`（閉じた番号を I が読み続けて client の socket を読むより止まる方が安全）。M は ack を受けてから close する（zedBSD は `kl_backend_input_close`）か、backend の callback から戻る。
  - **backend の callback**（M の上で backend が呼ぶ）: `kwl_backend_input_paused`（`backend-host.c:79`、今は `input->fd = -1`）は同期の外す。`kwl_backend_input_gone`（:118、logind は戻った直後に `seat_release` で close `seat-logind-linux.c:683-685`）は同期の外す → `kwl_input_forget`。`kwl_backend_input_resumed`（:97）は新しい fd の付ける。`kwl_backend_session_paused`（:34）は今の quiesce の前に全部の device を同期で外す（resume の後は今どおり scan で開き直して付ける）。
  - logind の resume は古い fd を close してから callback を呼ぶ（`seat-logind-linux.c:727-741`）。logind は pause の後にしか resume を送らず、pause の callback で I は外し済みなので、閉じた番号を I が持つことは無い。pause を経ない resume への保険として、p004 で backend の実装の順を「callback の後に古い fd を close」に入れ替える（`libkeiland-backend` の API は変えない）。FreeBSD は session_paused、各 input_gone、その後に lease を close の順と comment（`seat-freebsd.c:17-18`、呼び出し `:516-524`）にあり同じ規則で足りる（source の順は p004 の始めに確かめる、review-5 の未確認）。
  - EOF・ENODEV（今の `input.c:404-408` の `kwl_input_close`）は I の読み取りで見つかるので、I が自分で外して「消失」の event（その device の最後）を積み、M がそれを処理する時に close する。
- **持ち物の表**（review-5 N-5）:

| 欄 | 持ち主 | 他の thread の扱い |
| --- | --- | --- |
| `struct kwl_input_device` の `fd`（付けてから外すまで）・`frame`・`frame_count`・`frame_time_us`・`discarding`・`abs_x`・`abs_y`・`accel`・`pad` | I | M は読まない。tablet・touch の処理（`tablet.c`・`touch.c`）が読む `device->frame` は、I が report を FIFO の event に写して渡す |
| 同じ struct の `live`・`pointer`・`keyboard`・`absolute`・`tablet`・`touch`・`touchpad`・`abs_*_minimum`・`abs_*_maximum`・`path` | M（開いた時に決める） | 付ける依頼に写して I へ。I は自分の写しを使う |
| `server->input_ms`（`input.c:398`）・`input_events`（`input.c:657`） | M | I は event の時刻を FIFO の event に載せ、M が処理する時に今どおり更新する |
| `server->pointer_event_us`・`pointer_remainder_x`・`pointer_remainder_y`（`input.c:1063-1066`） | I | `event_us` は受け箱と motion の event に写す。M は event の値を使う |
| 設定（`mouse_speed`・`mouse_acceleration`・`touchpad_speed`・`touchpad_acceleration`・`touchpad_natural`） | M が正本 | 変更は I への依頼。`kwl_input_touchpads_changed`（`input.c:992-1010`）の `kwl_touchpad_set_feel` は I が依頼を受けて自分の `pad` に行う |
| 受け箱の位置（x、y、`pointer_output`、`event_us`、warp の番号） | I と M の warp（`cursor_lock` の下） | 下の「位置の持ち主」 |

- **位置の持ち主**（review-5 N-3）: 位置（x、y、`pointer_output`）の正本は受け箱で、I が足していく。
  - **I の動き**: `cursor_lock` の下で受け箱の位置と出力の矩形の写しを読み、`kwl_plane_move`（`kwl_pointer_relative` の計算）を行い、受け箱に書くまでを 1 回の区間で行う（計算は純粋な関数）。`KWL POINTER output=` の printf と dirty は lock を放してから、FIFO の event で M へ（4.6 の規則）。
  - **M の warp**: 出力の消失と退避（`heads_evacuate` `heads.c:1876-1881`）、head と一緒の移動（`heads_windows_follow` `heads.c:1940-1954`）、出力の大きさの clamp（`output-switch.c:675-678`）、絶対の device の pen tablet（`tablet.c:1021`）・touch screen（`touch.c:2417`）。M の見る位置（I の FIFO を処理した所までで遅れている）からは計算しない。`cursor_lock` の下で受け箱の位置と `pointer_output` を読み、pointer の部分の計算（`kwl_plane_carry` など純粋な関数）をして、出力の矩形の写しの更新と同じ区間で受け箱に書き warp の番号を進める。M の見る位置も同じ値にする。窓の移動と printf・dirty は lock の外。
  - I の次の相対の動きはその位置から足す。M は warp の番号が古い motion の event の位置を使わない（ボタン・key は配る）。
  - **一時の書き換え**（M の見る位置だけ、受け箱に書かない。warp ではない）: `keyboard.c:835-838,917-923`、`band_replay`（`shell.c:7811-7841`、上端の帯の押下をその点で glass に渡して位置を戻す。前の版の warp の一覧の `shell.c:7827,7839` は誤り）。
- **絶対の pointer**（review-5 N-2。QEMU の virtio-tablet など `apply_frame` の absolute `input.c:735-783`、試験の `guest.py:99` もこれ）: I が `scale_absolute` で anchor の大きさ（出力の矩形の写し）に写し、`pointer_output` を anchor にし（`kwl_pointer_absolute` `heads.c:1814-1824` の計算）、同じ report の相対の差分を足して clamp し、受け箱に書く。**warp の番号は進めない**（相対の動きと同じく I の書いた順に motion の event が M に届くので、M の位置は古くならない）。`KWL POINTER output=0 absolute` の printf・dirty と `pointer_unmoved` の解除（`input.c:740-747`）は FIFO の event で M。
- **絶対の device**（pen tablet・touch screen、`tablet.c`・`touch.c`）: I は report を写して M に渡すだけで、位置は M が今の処理で決め warp で受け箱に書く（60fps の cursor の対象は相対の pointer、touchpad、絶対の pointer）。
- 1 core の間: I は evdev で起き、同じ優先度の起きた thread として M・R を次の安全な点で横取りする（10 節）。R・C が kernel の中の長い transaction（End の decode `hold_us` 最大 11〜19ms）にいる間は待つ（11 節）。

### 6.2 cursor の thread C と受け箱（review-3 M-3、review-4 H-C）

- **受け箱**（`cursor_lock` の下の 1 組）: I が書く（x、y、pointer_output、event_us、時刻、warp の番号）、M が書く（像: cursor の形か 2 枚のどれか、その世代と version、中身の大きさ、hotspot、出すか）、C が書く（2 枚の pin、failed、copy の済んだ version、計数）。C は最新を読み、plane の左上＝位置−hotspot。`pointer_output` が anchor でない時と「出すか」が 0 の時は空を出す（M は I の FIFO の motion を受けて composite に切り替える。切り替えのコマまで cursor が見えない間は今の出力をまたぐ時と同じ）。
- C は C0 の規則（1 refresh に 1 回、最初の動きと像の変化はすぐ）で present し、plane の fence も C だけが待つ（今の `vkWaitForFences` 100ms `cursor-plane.c:534` も C の中）。
- **copied の gate**（review-B4 F1 (b)）: 今は最後の copy が終わるまで次の acquire をしない（`cursor-plane.c:499-509` の `plane->copied`）ので、像が変わった後は swapchain の画像が 1 枚ずつ順番に copy を待ち、各 copy が primary のコマの後ろに並ぶ間 cursor が止まる。C ではこの gate をやめ、**待つのは acquire した画像の前の copy の fence だけ**にする（その画像の command buffer を書き直す前、今の `:532-536` の待ち）。別の画像への copy が飛んでいても、像の変わっていない画像の acquire と位置だけの present は止めない。pin の下ろしは 4.7 の 3。
- **持ち物の分け方**（review-4 H-C）: 今の `struct kwl_cursor_plane` を 2 つに分ける。
  - M の持ち物: `open`、`shown`（plane か composite か、`cursor-plane.c:184-187` の切り替え）、plane の extent の写し（`plane_state` の大きさの判定 `cursor-plane.c:296-300` と、`output.width` の読み `:150-162` の代わり）、failed の写し。
  - C の持ち物: present の状態の全部（`output` の swapchain と画像、**plane 専用の VkCommandPool**（今の `compose->pool` `cursor-plane.c:128,410-412` をやめる。M の walk の確保 `import.c:308` と同じ pool を使わない。zedBSD の libvulkan は `objects.c:254` の registry の lock で通るが Mesa では外部同期の違反）、command buffer、fence、semaphore、`next`、`held[]`・`held_version[]`、`copied`・`last_copy`、計数）。
  - C の失敗（fence・acquire・copy・present）: C は `cursor_lock` の下で failed を立て、それ以上 Vulkan を呼ばずに止まり、pipe で M を起こすだけ（今の `kwl_cursor_plane_close` の `memset(plane)` `cursor-plane.c:140` を C の中で行わない。M が読む物を C が消さない）。M は知らせで `shown` を 0 にして composite に切り替え（dirty）、次の park で C が自分の物を壊す（4.8）。
  - 計数（今の `server->perf.cursor_*` `cursor-plane.c:511-584`）は C が自分の中に持ち、present ごとに `cursor_lock` の下で受け箱の公開用の写しに足す。M の `KWL PERF main` はその写しを読む。
- C と M が共有する他の物: (a) cursor の形の画像（起動時に作り不変）、(b) 2 枚（4.7）、(c) queue（3.3）、(d) park の印。
- これで M の止まりと cursor の位置が切り離される: 位置は I→受け箱→C、M は通知と像だけ。

### 6.3 画面に映る cursor と WS141

- 画面に映る cursor の速さは WS141 p016 (B) に依存する: libvulkan の present の worker は queue ごとに 1 本で後の job は前を追い越さず、各 job の GPU の fence を待つ（`wsi-swapchain.c:2664-2710,2745,2776-2782`）。cursor の plane と出力のコマは同じ queue。前提は (a) B-u（位置だけの present が worker も GPU も通らない、ticket で追い越す）、(b) p016 の受け入れ B の前提（(b)・A3）。B-u の L1（display_mutex の待ち、acquire の `display_image_available` の待ちも）は前提ではなく、p016 の計測の後の次の版で扱う（review-B4 M-f、p016 第 4 版に揃えた）。C があっても、C の present はこれらで待つ。
- **受け入れの分担**（review-B4「WS213 との関係」、Q1）: p016 (B) が受け持つのは「main loop が止まらない時の入力→ioctl」。WS213 が受け持つのは端から端（evdev の入力 → 画面に映る cursor）の 60fps で、その判定は B-u が入った後に WS の完了の条件で行う（ws.md）。p004 の必達（box_lag、6.4）は WS213 の中の段の量で、端から端の判定を置き換えない。
- **C と R の同じ queue の同時使用**（review-B4 M-d・H-b）: zedBSD の libvulkan の build には `queue_lock` が無く（3.3）、C の copy の submit・present と R の submit・present が同じ VkQueue に同時に来る。B-u の fast path を含め、libvulkan がこれを安全に扱うことは p016 B-u が明記し、libvulkan の fixture（C の fast path・R の submit・worker の同時）で確かめる。WS213 はそれを前提として引用し、自分では確かめない（9 (m) の stub は compositor の側の規則だけ）。fast path の `device->mutex` の待ち（上限が無い）は p016 の測定の項目。

### 6.4 この WS の必達と判定（review-2 H-e、review-3 M-3、review-4 H-B・L-d、review-5 N-4・N-6）

- **必達**（Q1 の方針。量は review-5 N-4 で変えた）: pointer が動いている間の **box_lag**（evdev の report の時刻 `event_us` → I が受け箱に書いた時刻）の **p99 ≤ 1 refresh（16.7ms）**。App Home の開閉と窓の drag の間も。
  - **最長 ≤ 16.7ms**も判定に入れるが条件付き（Q1 の判断）: 超えた原因が kernel の中の長い transaction（R の End の decode・C の present の `hold_us`）と分かれば、p004 を uncleared にして WS208（SMP）・kernel への依存として Q1 に返す。原因の切り分けのため、box_lag が 16.7ms を超えた書き込みごとに `KWL CURSOR_LAG` の行を出し、kernel の `hold_us` の行の時刻と対応させて p004 の結果に記録する（下の log、11 節 9）。
  - **前提**: `event_us` の時計と I の書き込みの時刻（`kwl_microseconds`、CLOCK_MONOTONIC）が同じ基準であること（11 節 3）。p004 の始めに Pi で確かめる。
  - **代替**（時計が確かめられない時だけ）: 動いている間の「受け箱の書き込みの間隔 − 続く 2 つの `event_us` の間隔」の超過分の p99・最長 ≤ 16.7ms。入力そのものの間隔は数えない。
  - 前の版の box_gap（書き込みの間隔）の最長は、遅い pointer（evdev が 20〜50ms おき）で入力の間隔そのものが 16.7ms を超えるので満たせない（review-5 N-4）。box_gap は記録だけにする。
- **記録して上限を持つ**: M の 1 pass の最長（walk を含む）。client への motion・hover・cursor の形の遅れを決める。上限は 2 refresh（33.3ms、Q1 が確かめた）。場面 (1)〜(3) で判定し、(4) の app の起動（client の起動の大きな commit・import が重なる）は `pass_p99_us` を記録する（review-5 N-6、Q1 の判断）。walk の外の M の Vulkan の最長 ≤ 1 refresh（review-3 M-3 の判定）。
- **log**（p004 で足す）:
  - `KWL PERF main` に `pass_max_us=`・`pass_p99_us=`（M の 1 pass の最長と p99、walk を含む）、`walk_max_us=`、`vk_outside_max_us=`（walk の外の M の Vulkan の最長）。
  - `KWL PERF cursor`（5 秒の窓ごと、I と C の計数）: `box_writes=`、`box_lag_p99_us=`・`box_lag_max_us=`・`box_lag_over=`（`event_us` → 受け箱の書き込みの時刻、必達の量。over は 16.7ms を超えた書き込みの数）、`box_excess_p99_us=`・`box_excess_max_us=`（代替の超過分）、`box_gap_p99_us=`・`box_gap_max_us=`（記録だけ。動いている間の書き込みの間隔。「動いている間」は、続く 2 つの書き込みの `event_us` の差が 50ms 以下の組だけ）、C の `presents=`・`present_gap_max_us=`・`present_max_us=` と内訳（M0 の `fence`・`acquire`・`copy`・`present` を C に移す）。
- **場面と時間**（Pi、1 boot、ユーザーか Q1 の手で、Pi には入力の自動化が無い）: (1) pointer だけを 10 秒動かす、(2) pointer を動かしながら App Home を 5 回開閉、(3) 窓を 10 秒 drag、(4) Music・Settings の起動を 1 回ずつ。各場面の `KWL PERF main`・`KWL PERF cursor`・`KWL LAT` の行で判定する（(4) は `pass_max_us` で判定せず `pass_p99_us` を記録）。
  - `KWL CURSOR_LAG event_us= write_us= lag_us=`: box_lag が 16.7ms を超えた書き込みごとに 1 行（5 秒の窓に最大 20 行）。kernel の `hold_us` の行の時刻と並べ、最長の原因が kernel の長い transaction かを見る。
- **測り方**: `KWL CURSOR_PLANE present` の `at_us` は C が呼んだ時刻。`event_us`（evdev の kernel の時刻、マウスと touchpad）、`present_cycles`。表示の側の時刻（kernel の `CURSOR_SET`・`MOVE`）は Q1 が WS141（P3）に依頼済み。

### 6.5 plane が無い出力

- cursor は composite。pointer の動きは I → M の FIFO → damage → want → R の ready → M の walk。この場合の cursor の速さは M の 1 pass と walk の速さで決まる（11 節の walk の分割の対象）。Linux・FreeBSD は今これ（3.3）。

## 7. libvulkan の thread の安全、Linux・FreeBSD

- zedBSD の libvulkan: session ごとの `context->mutex` が kernel との transaction を直列にする。command buffer の vkCmd* は貯めるだけで lock を取らない。同時に呼ぶのは (a) M の backend の import と R・C の transaction、(b) C の present と R の submit（3.3。安全は p016 B-u の前提、6.3）、(c) M 専用の fence の問い合わせ、(d) R の step 1 の破棄と M の backend の import（device 単位、別の object）。I は Vulkan を呼ばない。正しさは保たれ、問題は待ち時間。walk・park の中の `vkQueueWaitIdle`・`vkDeviceWaitIdle` は WSI の worker の present の drain（`queue.c:108-155` → `wsi-swapchain.c:617-647,2715-2790`）を待つので、M の止まりの見積もりに入れる（review-3 L-1）。
- Vulkan の規則（Mesa など）: 外部同期の要る object（command pool・descriptor pool・command buffer・queue・swapchain）は、R が使う時と M が walk・park の中で使う時が重ならない（3.2 の不変条件）。C は自分専用の command pool と swapchain（review-4 H-C）。queue は Mesa の build だけ `queue_lock`（新しく足す）。zedBSD の libvulkan が command pool の同時使用を registry の lock（`objects.c:254`）で通すことに頼らない。
- Linux・FreeBSD の backend: 変わるのは compositor の中だけで、`libkeiland-backend` の API は変えない。pthread は既に使っている。I の読む device の fd は backend（`libkeiland-backend/evdev/input-evdev.c`、Linux の logind `seat-logind-linux.c`、zedBSD の `input-zedbsd.c`）が開いた物で、開け閉めは今どおり M。新しい file は `Makefile`・`Makefile.linux`・`Makefile.freebsd` に足す。

## 8. Phase の分け方と受け入れ（review-1 M7、review-3 L-7、review-4 H-B・L-c・L-d・L-f）

| Phase | 内容 | 受け入れ（各 phase.md に同じ物） | 状態・順 |
| --- | --- | --- | --- |
| p001 | 調査と設計（この文書） | design-reviewer の review を通る（S2 の部分は第 5 版の差分の確認） | 第 6 版（review-5 で S2 条件付き可、N-1〜N-4 の反映は Q1 が確認） |
| [p002](../phase002/phase.md) | M0 と C0 | host 試験 PASS、build warning 0、Pi で `cursor presents=` が約 60/s 以下、present の間隔が約 16.7ms | 実装済み、Pi・T1 の確認待ち |
| [p003](../phase003/phase.md) | S1: display list（同じ thread で再生）、cursor の形の起動時の batch の作成、`kwl_import` の世代 | 9 節 (a) の再生の等価が全場面で一致、build warning 0、T1 で shot の画が前と同じ、Pi で `KWL STARTUP step=cursor-shapes` の ms（batch の作成だけを独立に出す、review-4 L-c）が 50ms 以下、walk と再生の時間を分けて測る。**p004 に進む閾値**（review-4 H-B: 中央値でなく p90 と最長）: Pi で walk（M の時間）が普通のコマ（100 draw 未満）の p90 8ms 以下かつ最長 16.7ms 以下、App Home のコマ（300 draw 以上）の p90 16.7ms 以下かつ最長 25ms 以下（review-5 N-6 の Q1 の判断。33.3ms では p004 の `pass_max` ≤ 33.3ms に余裕が無い。今の記録＋End は 48 / 62 / 93）。満たさなければ WS211 の削減を先にする（p004 の依存、review-4 L-f） | 着手は Q1 の指示（P3 との順） |
| [p004](../phase004/phase.md) | S2: R・C・I の thread（2 段の pull、want の規則と R の持つ画像、不変条件、park、FIFO、host image の書き込みの規則、cursor の 2 枚、C の持ち物の分け方、pointer の経路の I への移動、client の破棄の延ばし） | 9 節 (b)〜(i)・(k)・(l) の決定的な host 試験と (m) の thread と TSan の host 試験が PASS、Linux の build を ThreadSanitizer 付きで動かして WS213 の thread と lock の報告 0（WS131 の Debian の QEMU+KVM guest、T1、9 節の方針）、T1（i915・Venus）で回帰なし、Pi で 6.4 の場面: **box_lag の p99 ≤ 16.7ms（必達）と最長 ≤ 16.7ms（原因が kernel の長い transaction なら uncleared で WS208・kernel への依存、6.4）**、M の 1 pass の最長 ≤ 33.3ms（場面 (1)〜(3)、(4) は p99 を記録）、walk の外の M の Vulkan の最長 ≤ 16.7ms、`KWL LAT` の入力→walk の中央値が p003 より改善、C の present の全段の最長を記録 | p003 の閾値を満たした後（満たさなければ WS211 の削減の後）、p001 の S2 の確認の後 |
| [p005](../phase005/phase.md) | S3: W（glyph・壁紙・大きな字） | 9 節 (j) PASS、日本語の UI で初出の字の多い画面で walk の最長が p004 より下がる、壁紙の変更の間も box_lag の p99・最長 ≤ 16.7ms（6.4 と同じ条件）と M の 1 pass の最長 ≤ 33.3ms、T1 で回帰なし | p004 の後 |
| [p006](../phase006/phase.md) | 規約の全文の見直し | WS213 の C の全部を plan/coding-style.md の全文と照らして直す、build warning 0、host 試験 PASS | 最後 |

Future Work（2026-10-12 Q1 が記入）: F-094 client の画像の import を R へ、F-095 GPU と walk を重ねる・2 コマ飛ばす、F-096 cursor の plane に別の VkDevice。kernel の CURSOR_SET・MOVE の時刻の log は Q1 が WS141（P3）に依頼。

## 9. 試験

- host（決定的、thread を使わずに M・R・C・I の手順を 1 歩ずつ交互に呼ぶ）:
  - (a) **再生の等価**（p003）: デスクトップ・窓 2 つと popup・App Home・drag の部分の描き直し・backdrop・heads 2 つ・layer の焼き・batch の on と off を、直の記録と一覧の再生で vkCmd* の stub に流し、命令の列が同じこと。descriptor set の handle は（import、種類）に正規化。golden は p003 の最初の commit で取る。
  - (b) **2 段の pull**（K1・H-1・M-1、review-4 H-A）: want が無い間は acquire も walk もしない。acquire の失敗で写しが作られず状態が変わらず、R が want を下ろし M が立て直すまで acquire しない。R の下ろしは compare-and-clear（R の失敗と M の立て直しが交差しても M の want が残る）。heads の list だけの変更で番号が進まず取り消しにならない。ready の後に pacing で待つ間 R は待つ。ready の後に want が変わった時（種類の違い・取り消し）に取り消しで答え、R が k を持ったまま出し直す。古い番号の ready で walk しない。
  - (c) **heads**（H-2、review-4 H-A）: want の slot のうち持っていない物だけを取り、R が head の状態を書かない。**取り消しの後に同じ head を二度 acquire しない**（stub の swapchain が present されていない画像の 2 度目の acquire を失敗にする）、取り消しを越えて持った head を次の walk が描き present する、want から外れた head も present される、pointer の動きで `heads_needs_frame` が変わっても漏れない。park で slot が再利用された時に古い世代の画像を捨て semaphore を作り直す。作り直さない park を越えて head の画像を持ち越す。
  - (d) **不変条件**（H-a）: walk の中の遅延作成・descriptor set・shm の作成で R・C が Vulkan を呼ばない、I は一度も呼ばない（stub の Vulkan が呼んだ thread の役を記録）。破棄は R の step 1 だけで、want が無くても FIFO の依頼で進む（review-4 M-C）。
  - (e) **通り道の順**（M-b）: 完了の記録 → ready の順で、walk が前のコマの結果を見ている。
  - (f) **shm と cursor の 2 枚**（H-c・H-3、review-4 M-A）: memcpy と release が walk の中か、copy の list に無い時は walk の外。cursor の 2 枚: 今の受け箱の画像・pin の立った画像・飛んでいる写しの画像には書かない、両方書けない時は次の pass（最新の commit だけ）、commit で作成も submit もしない、常に全体を写し外側は透明（大きさが 24×24 → 48×48 → 24×24 と変わる場面、傷んだ行だけの commit が続く場面）、plane の元が形と 2 枚だけ（大きさの合う普通の shm の画像を plane に出さない）、`set_cursor` で surface を選んだ時に、未処理の commit があればその client の pool から、無ければ staging から写す（review-5 N-7）。C の `held[]` が世代で比べる。
  - (g) **host image**（H-b・L-4）: walk の外の `glass_text_width` が staging を書かず、未着の list を次の walk の始めに cell の割り当ての順に書く。R の copy の list は walk で取り込んだ物だけ。
  - (h) **park**（M-2・M-5、review-4 M-E）: 非同期の park の間 M が進む、parked は R と C の両方の ack の後、park の間と os_paused・sleep_hold・game mode の間 C が present しない、同期の park（session の pause・終了）、上限を超えた時の `_exit`（後始末を呼ばない）、park で swapchain を作り直した後に acquire の semaphore も作り直す、画像を持ったままの park と unpark。
  - (i) **C**（review-4 H-C）: M・I の受け箱の書き換えが C を待たない、C0 の規則（今の cursor-due-host-test を C に移す）、C が `compose->pool` を使わない（stub が pool ごとの使用者を記録）、C の失敗で C が failed を立てて止まり M の持ち物を書かない、M が composite に切り替え次の park で C が壊す、計数が `cursor_lock` の下の写しから読める。copied の gate（review-B4 F1 (b)）: 像が変わった後、ある swapchain の画像への copy が飛んでいても、別の画像の acquire と位置だけの present が止まらず、待つのは acquire した画像の前の copy の fence だけ。pin が飛んでいる copy の数で下ろされる。
  - (j) glyph（p005）: 未着の job の字は描かずに印、layer は無効、次の walk で描く。
  - (k) **I**（review-4 H-B）: M が止まっている間（walk の途中で M の手順を止める）も I の motion が受け箱に届き C が present する、FIFO の順（motion → ボタン → motion）が M で保たれる、M の warp の後に I の相対の動きがその位置から足され、warp の番号の古い motion の位置を M が使わない、touchpad の tap の期限が I の tick で進む、device の外しの ack の前に M が fd を閉じない、設定と出力の矩形の写しの変更が次の motion から効く。review-5 で足す場面:
    - (k1) **fd の番号の再利用**（N-1）: stub の backend が pause・gone・resume の callback の中で fd を閉じ、同じ番号で別の file（client の socket の代わりの pipe）を開く。I が外しの ack の後にその番号を `poll`・`read` しない、ack の上限切れで `_exit`、resume の新しい fd から読む、session の pause で全部を外す、EOF で I が自分で外し M が close する。
    - (k2) **絶対の pointer**（N-2）: virtio-tablet の report で I が anchor に写して受け箱に書き、warp の番号が進まず、相対の motion と交互でも M の位置が古くならない。
    - (k3) **hotplug の退避と I の動きの交差**（N-3）: M の `heads_evacuate`・`heads_windows_follow`・clamp の計算の途中で I が出力をまたぐ motion を書いても、M は受け箱の位置から計算し、出力の矩形の写しと位置が同じ区間で替わる（I が古い矩形で新しい位置を、新しい矩形で古い位置を読まない）。`band_replay` の一時の書き換えが受け箱に書かない。
    - (k4) **持ち物**（N-5）: touchpad の設定の変更が I への依頼で効く、`release_all` の action と外しの ack がその device の最後の event の後に FIFO に並ぶ、tablet・touch の処理が FIFO の写しの report を読む（I の `frame` を読まない）、M が I の持ち物を触ったら stub が失敗にする（(m) でも）。
  - (l) **client の破棄**（review-4 M-B）: 4.9 の 4 つの場面で、protocol の object はすぐ消え、GPU の object・buffer の後始末は参照する写しの完了の記録の後、死んだ client に event・release・frame callback を送らない。
  - (m) **thread と TSan**（review-4 M-D）: 本物の pthread で M・R・C・I を動かし、stub の Vulkan が外部同期の object（command pool・descriptor pool・command buffer・queue・swapchain）ごとに入口と出口で使用中の thread を atomic で記録し、2 つの thread が重なったら失敗、mutex を持ったままの呼び出しも失敗（4.6 の規則）。場面は (b)〜(l) を乱数の種で混ぜて 10 万歩。`-fsanitize=thread` で build し、TSan の報告 0、重なり 0。C の present の経路を通るのはこの試験だけ（Linux では plane が開かない、3.3）。
  - C0（p002）: `plan/ws213/tests/cursor-due-host-test.sh`（PASS）。
- build: rpi4・amd64・`make keiland-linux`・FreeBSD で warning 0（コマンドは各 phase.md、`BUILD=build/ws213-pNNN/...`）。
- **Linux の TSan**（p004 の受け入れ、review-4 M-D）: Linux の build を `-fsanitize=thread` で（置き場と make の変数は p004 で決める）。見る物は M・R・I と既存の thread（壁紙の loader と prefetch、sysmon、settings の writer、keyboard-hand、machine・system の job）。C は通らない（3.3）。
  - 判定: WS213 の thread（R・I・C）と lock（`render_lock`・`cursor_lock`・`input_lock`）と共有の物に触れる報告が 0。
  - 抑制の方針: Mesa と system の library の中の報告は `called_from_lib:`（Mesa の Vulkan の driver、`libLLVM`）で抑える。既存の thread どうしの報告は抑制の file（`plan/ws213/tests/tsan.supp`）に 1 行ずつ理由を付けて置き、Q1 に Bug の ticket を頼む（WS213 の物に触れなければ p004 を止めない）。WS213 の物を抑制しない。
  - 入力の自動化: guest の中で uinput（Debian の `ydotool`、無ければ p004 で作る小さな uinput の C の道具）を SSH（`plan/tools/guest/guest.sh`）で動かし、pointer の円の動き 60 秒、super の key で App Home の開閉 10 回、title の drag 30 秒。QEMU の console・serial の log は読まない。
- T1（p003・p004・p005 の終わりにまとめて）: i915（5330）と Venus の QEMU で shot.c の画を前と比べる、`KWL VULKAN_ERROR`・`KWL FAILED` が無い、App Home の開閉・drag・終了。p004 は上の TSan の Linux の guest も。
- Pi: 6.4 の log の行と場面（`KWL PERF main` の `pass_max_us`・`walk_max_us`・`vk_outside_max_us`、`KWL PERF cursor` の受け箱の間隔と遅れ・C の present、`KWL CURSOR_PLANE present` の `event_us`→`at_us`、`KWL LAT`）。

## 10. SMP が無い間の効果の見込み

- Pi の kernel は 1 core（WS208 まで）。thread にしても CPU の総量は同じで fps は上がらない。
- 効くのは入力と cursor の遅れ: kernel は起きた thread が同じ優先度なら走っている thread を次の安全な点で横取りする（review-5 が読んだ: `src/kern/sched.c` の `wake_enqueue_locked` 1800-1834 は同じか高い優先度の起きた thread で `cpu->preempt` を立て、user に戻る時 763-773 か tick 945-962（`preempt_count==0`）で切り替える。遅れは 1 tick 程度の見込み。decode が preempt を止めるかと tick の周期は未確認で p004 の始めに確かめる）。evdev が読めると I が起き、M の walk や R の encode を横取りして受け箱を書き、C を起こす。I の仕事は 1 event で数十 µs の見込み。R・C が kernel の中にいる時は安全な点まで待つ（11 節 9）。
- 見込み（p002・p003 の実測で直す）: 1 コマで M が止まる時間は今 10〜20ms（App Home 60〜120ms）→ p004 の後は walk だけ＋walk の外の M の Vulkan（import の時）。cursor の位置は M の止まりと無関係になり（I）、cursor の plane の待ちは C に移る。画面の cursor の 60fps は WS141 p016 (B) の後。

## 11. 未決と危険

1. walk の費用（p003 で測る、閾値は 8 節）。
   - **代替: walk の分割**（review-4 H-B、Q1 の方針で本案ではなく代替）: 重い walk（App Home の 300 draw 以上）を途中で区切り、区切りごとに M が I の FIFO と client の request を処理する形。walk の途中の server の状態の変更が一覧の前半と後半で食い違わないこと（区切りの間は状態を変える request を溜める）が要り、複雑なので、p004 の後に M の 1 pass の最長が 33.3ms を超え client の入力の遅れが問題になった時にだけ考える（plane の無い出力の composite の cursor にも効く、6.5）。
   - M の 1 pass の最長の上限 33.3ms（2 refresh）は Q1 が確かめた（review-5 の答え (1)）。判定は場面 (1)〜(3)、(4) は p99 を記録（6.4）。
2. C の present の待ち（R の submit・End・WSI の worker）。画面の速さは WS141 に依存。
3. `event_us` の時計（kernel の `clock_milliseconds`）と `kwl_microseconds`（CLOCK_MONOTONIC）が同じ基準かを Pi で確かめる。box_lag（6.4 の必達）の前提なので p004 の始めに確かめ、違えば代替の超過分で判定する（review-5 N-4）。Linux は logind の resume で `EVIOCSCLOCKID` を CLOCK_MONOTONIC にしている（`seat-logind-linux.c` の `seat_resume`）。
4. `KWL LAT`・`KWL FIRST_FRAME` の定義し直し（p004）: want＝M が立てた時、ready＝R の acquire の後、draw＝M の walk の始め、recorded＝R の End、queued・submit＝R、shown＝M が完了の記録を受けた時（output_lost・switch_proving もこの時に知る）。
5. retire（`objects.c:739`）は完了の記録の処理の後に 1 つずつ（in_flight を見ない）。
6. `kwl_compose_quiesce` の 4 か所（`objects.c:554` は 4.9 で決めた、`backend-host.c:46` は同期の park、残る `main.c:1120`、`shm.c:187`）と `->in_flight` の 16 か所は、p004 の実装の前に 1 つずつ park・retire・写しの帳簿のどれかに当てる。R が ready で待っている時と park の中の quiesce は即座に返る。
7. compose->command を直に使う所（`compose.c:2365〜`、`layer-cache.c:184`）は p003 の型の変更で見つからないので、grep で全部を挙げて一覧へ移す。
8. 動きの時刻: walk の時に animation の時刻を読むので、表示されるのは R の記録と GPU の時間だけ後（今も同じ）。
9. **1 core の間の I の起きる遅れ**: R の End の decode や C の present が kernel の中で長い transaction（`hold_us decode` 最大 11〜19ms）にいると、I は安全な点まで待つ。box_lag の最長（16.7ms）を 1 core で満たせない原因がこれなら（`KWL CURSOR_LAG` と `hold_us` の時刻の対応で確かめる）、p004 を uncleared にして WS208（SMP）・kernel への依存として Q1 に返す（Q1 の判断、p004 の再開条件）。p99 は必達のまま。pthread の優先度（I を上げる手段が kernel にあるか）は p004 の始めに調べる。
10. cursor の形（hover で決まる resize の矢印など）と client の `set_cursor` は M の 1 pass ごとに変わる。位置は 60fps でも、形の変化は M の 1 pass の最長だけ遅れ得る（6.4 の上限で抑える）。
11. Pi の入力の自動化は無い（6.4 の場面は人の手）。WS131 の guest は uinput（9 節）。
12. 未確認（review-5）: `seat-freebsd.c` の fd を閉じる順（6.1、comment では callback が先）、zedBSD の evdev の fd を別の thread から `poll` してよいか、decode が preempt を止めるか（10 節）、Pi の数値。p004 の始めに確かめる。

## 12. 結果の記録（この Phase）

- 調べたこと: 1 節（source の地図、Pi の既存の log の集計。P3 の boot の b4・b6・b10）。
- 変えた file: この phase.md、[summary.md](summary.md)（ユーザー向けの要約）、[review-1](review-1.md)〜[review-5](review-5.md)（design-reviewer の要点の写し）、ws.md、p002〜p006 の phase.md。p002 の source は [p002](../phase002/phase.md)。
- 第 5 版（2026-10-12）: review-4 の H-A〜H-C、M-A〜M-E、L-a〜L-f を反映。Q1 の方針で pointer の経路を入力の thread I へ（6.1）。
- 第 6 版（2026-10-12）: review-5 の N-1〜N-7 と Q1 の答え（p003 の重いコマの閾値、`pass_max` の場面、box_lag の最長の条件、`hold_us` の対応、build の注記）、WS141 p016 review-B4 の WS213 に関わる点（C の copied の gate、受け入れの分担、C と R の queue の同時使用は B-u の前提、L1 の位置付け、M の `vkQueueWaitIdle` を外す）を反映。ユーザー向けの要約 [summary.md](summary.md) を足した。
- 次: Q1 が N-1〜N-4 の反映を確かめる（再 review は不要）。ユーザーの設計の review。p003 の着手は Q1 の指示（WS211 との順）。
