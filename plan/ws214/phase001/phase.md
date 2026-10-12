<!-- awesome-plan project=zedbsd record=ws214-p001 -->

# ws214-p001: 調べと設計（HVS の plane の能力と制約、口、compositor の載せ方、fallback、試験）

Parent: [WS214](../ws.md)
Status: planning（設計の第 3 版、2026-10-12 P2。[review-2](review-2.md)（判定「条件付きで p003 に進める。致命なし」）の N1〜N11・軽微・9 節の文を反映。Q1 の確かめの前。コードは書いていない）
Phase disposition: normal
Queue: P2（2026-10-12 Q1 の割り当て）

## review-2 への対応（第 3 版）

[review-2](review-2.md)（design-reviewer、第 2 版への再 review、判定「条件付きで p003 に進める。致命なし」）の指摘と、この版での直し。Q1 の判断（2026-10-12）は「Q1」と書いた。

| 指摘 | 直し | 場所 |
| --- | --- | --- |
| **N1 重大** 「次の primary」が kernel に届く順の次なので、前のコマの primary（lane で GPU を待つ）が保留を取り込み、窓が 2 重・消える | 保留に「保留した時にその display で発行済みの primary の ticket＋1」（`min_primary`）を付け、ticket がそれ以上の primary の job だけが取り込む。primary の ticket は `vkQueuePresentKHR` の呼び出しの順に swapchain_mutex の下で振り、lane は ticket の順に kernel へ出す。p004 の fixture に「前のコマの primary が lane に残っている時の切り替え」 | 4.1 規則 2、4.4 (3)、7 p004、8 |
| **N2 重大** 保留中の plane への後続の present、保留の寿命、primary が来ない時の期限 | (a) 保留がある plane へのどの present も保留に入れて置き換える（C の fast path も直に出さない）。(b) 置き換えられた画像はすぐ空く、退役した chain の保留は捨てて commit に入れない、`vkDestroySwapchainKHR` は保留を捨て出ている plane を単独で隠して採用を待つ、`vkQueueWaitIdle`・`vkDeviceWaitIdle` は単独で flush。(c)「primary を伴わない flush」（期限・WaitIdle・破棄・release）と期限（2 refresh の間に `min_primary` 以上の primary が present されなければ単独で commit し `why=expired`）。park は `RETURNING` を先に、出来なければ M が隠す present と WaitIdle | 4.1 規則 3・6、4.4 (3)、5.8 |
| **N3 中** 番号の意味（入口で振ると後の番号が先に採用され、WAIT(前) がすぐ返る） | 番号は guard の下で状態を記録する時に振る（`state_serial`）。`adopted` は「その番号以下が全部採用済み」。番号は controller ごとに 1 本（今の `completed` と同じく lease をまたいで増え戻らない）。p003a は外に見せる番号を今のまま、p003b で COMMIT・PRESENT・WAIT の番号を `state_serial` に | 4.2、4.3 (2)(4)、7 |
| **N4 中** lease の失効と mutex を取らない commit、ETIMEDOUT の「回復」の主体 | release・close・fault の始めに guard の下で「失効中」を立てて待ちを起こし、記録の時に lease・generation・失効を guard の下で確かめ直す。眠る COMMIT_WAIT・WAIT・PRESENT の待ちは EBUSY か ENXIO で返る。UAPI の ETIMEDOUT の文を「以後の commit は EBUSY、release か close が console に戻して借りを解く」（今の uncertain と同じ）に直した | 4.2、4.3 (1)(6) |
| **N5 中** pin が長い、`table_lock`（mutex）は waitq の条件の lock に使えない | fence は core が lookup の前に待つ（`gpu.c:5925` と同じ）。pin は lookup から backend の記録の op の終わりまで（採用の待ちは pin を放した後の別の op `adoption_wait`）。`table_lock` は spinlock にし、pin の数と DESTROY の待ちはその下、`session->resources` を歩く・変える所は全部その lock | 4.3 (1) |
| **N6 中** clock を下げる競争 | Q1: 今の方針を保つ（lease の間は下げず、console に戻る時だけ下げる）。上げるのは記録の前の thread の文脈だけなので、記録と下げの競争は無い | 4.3 (5) |
| **N7 中** 3 slot の直し漏れ | cursor の画像 4→5 枚、`cursor_named` を 3 slot で数える、`check_pipeline` の next を「今の list か未採用の list」に、差し替えでは PV の status（0x28）を書かない、underrun を止めずに IRQ で W1C して数える、attach の大きさの検査 `0x42c4`→`0x5000` | 4.3 (2)(9) |
| **N8 中** C の窓の acquire が timeout 0 で RETRY、入力が止まると最後の位置に着かない | RETRY の期限を「前の present から 1 refresh」に立て、空いたらその時の最新の place で出し、最後に出した place が最新の place と同じになるまで続ける | 5.2 |
| **N9 中** Phase の分け方 | Q1: p003a（承認不要: gate 外し、3 slot、coalesce、IRQ の採用の記録、既存の op、WS141 p016 B-k2）と p003b（承認後: COMMIT・OVERLAY_INFO・WAIT の変更・scaled・`hvs-load`）に分けた。p003a は承認に依らない。ws.md の表と依存も直した | 7、[ws.md](../ws.md) |
| **N10 中** ENOSPC の再 commit で窓が消える | Q1: 窓入りの primary を R が描くまで前の状態を保つ案を採る（保留ごと捨て、primary も出さない）。理由は 4.4 (6) | 4.4 (6) |
| **N11 中** copy の経路の PRESENT（`copies[2]`、今かもしれない list が 2 つ） | Q1: p003a で安い方を選ぶ → 2 枚のまま、controller の mutex を残し、copy 先を名指す list が今かもしれない間は採用を待つ。理由は 4.3 (2) | 4.3 (2) |
| 軽微 `drv_gpu_display_ops` に flags が無い | `flags` の欄を足し、`gpu_ops_validate` の「optional の組」を決めた | 4.2 末尾、4.3 (1) |
| 軽微「lock を持たずに waitq で眠る」の形 | `adopted_serial` と `waitq_sequence` を `flip.guard` の下で読み `waitq_sleep(&wq, &guard, observed, deadline, flags)`、WAIT は `WAITQ_INTERRUPTIBLE` | 4.3 (2)(4) |
| 軽微 UAPI の不足 | COMMIT_WAIT 無しの signal_fd は EINVAL、同じ plane を 2 回で EINVAL、SHOWN で IMAGE 無し・前の画像も無しで EINVAL、1 つの commit の一部 DROPPED（原子性の例外）を文に | 4.2 |
| 軽微 5.1 の srcRect＝dstRect＝窓の矩形 | copy 先は画像の (0,0) なので srcRect は (0,0,w,h)（端の clip で offset が動く）、dstRect が窓の矩形 | 5.1、5.2 |
| 軽微 8-4 は C0 の間引きで一致しない | 「drop の位置と C の最後の present の位置の差が 1 回の間引きの間の pointer の移動以内」に | 8 |
| 軽微 8-1 の cadence の中央値は pointer の速さ次第 | 判定は「入力の時刻から、その位置を含む list の採用までの遅れ」と「動いている区間だけの採用の間隔」、`pair` は採用の時に数える | 8、4.3 (9) |
| 軽微 scaled の LBM を幅 2048 固定 | 縦の拡大縮小をする overlay は src の幅 2048 まで、4K の mode の `max_src_width` は 2048 以下 | 4.3 (7) |
| 9 節の問い | 1 の文に「bcm2711 で今の表示の命令の振る舞い（EBUSY が無くなる、WAIT が眠る）も変わる」を足し、「cursor の時と同じ種類の追加」を削った。3 に「動画は離すまで止まり、離すと飛ぶ（音は続く）」を足した。2・4・5 は review-2 で妥当 | 9 |

review-1 の項目の review-2 での判定（F1 部分、H1 概ね、H2 解決＋中、H3 概ね、H4 解決、H5 部分、H6 部分、M1 部分、M2 部分、M3・M4 解決、M5 部分、M6 部分、M7 部分、軽微 解決）は、残りを上の N1〜N11・軽微として直した。第 2 版までの対応は次の表のまま残す（その後の版で変わった所は上の表が優先）。

## review-1 への対応（第 2 版）

[review-1](review-1.md)（design-reviewer、判定「p003 以降に進めない」）の指摘と、第 2 版での直し。

| 指摘 | 直し | 場所 |
| --- | --- | --- |
| **F1** 1 回の `vkQueuePresentKHR` で plane ごとに違う `srcRect`・`dstRect` を渡せない（`VkDisplayPresentInfoKHR` は 1 つで全部の swapchain に当たる）、cursor の chain は全体の矩形でないと失敗、2 つ以上の present は ordinary の lane | present は **plane ごとに 1 回**にした。原子性は libvulkan の規則「overlay の**見える・隠れるの切り替え**の present は、同じ display の次の primary の present まで kernel に出さず、その primary と 1 つの commit にする」と kernel の commit（状態から作る 1 つの list）で作る。R は切り替えの overlay を先に、primary を後に出す。C の窓と cursor は別の present で、同じ refresh は kernel の「pending の list の置き換え」で狙う努力目標として測る | 4.1、4.4、5.1、8 |
| **H1** core の session の gate（`gpu_session_enter`）が PRESENT_SYNC の採用の待ちの間 busy、connection は 1 つの fd を共有、lease は open をまたげない | bcm2711 では display の op（COMMIT・PRESENT・PRESENT_SYNC・WAIT・CURSOR_*）を gate から外す。handle の表は短い lock で引き、resource に pin を付けて backend の呼び出しの間だけ持つ。backend の宣言（`DRV_GPU_DISPLAY_UNGATED`）で選び、i915・Venus は今どおり | 4.3 (1)、7 p003 |
| **H2** settle と EBUSY が mutex を取らない overlay・cursor の commit と両立しない（採用の IRQ が次の list を出し続け、100ms で uncertain、pending で EBUSY） | primary も状態の 1 つにして coalesce する。settle と EBUSY をやめ、primary の commit の待ちは「その commit 以降の状態から作った list の採用まで」に（lock を持たずに waitq で眠る）。**WS141 p016 の段 B-k2 にも同じ穴がある**（4.3 の末尾）。kernel の段は 1 つの Phase（p003）にまとめ、B-k2 の範囲を含める（Q1 の判断） | 4.3 (2)(3)、7 |
| **H3** plane を隠す操作を標準の Vulkan で表せない（透明の画像でも HVS は読む） | `srcRect` と `dstRect` がともに (0,0) からの 1×1 の present を libvulkan が「隠す」と解釈する（swapchain は壊さない）。kernel は隠した plane を list に入れない（帯域 0、画像を持たない） | 4.1、4.4 (2)、6 |
| **H4** R が COLOR_ATTACHMENT で直に描くと位置だけの fast path に入らない（`transfer_only`、why=usage） | R は焼きを専用の offscreen（p007 の `KWL_LAYER_WINDOW` の memory を流用）に描き、swapchain の画像（usage は TRANSFER_DST だけ）へ copy する。試験は compositor と同じ usage の chain で | 5.1、8 |
| **H5** R と C の swapchain の受け渡しが WS213 の不変条件（C は自分専用の swapchain だけ、park は R と C の両方）に反する、C→R の返しの手順が無い | 窓の plane の持ち主を移す専用の message（C だけを止めて ack、park は使わない）を足し、WS213 の不変条件の変更として文を書いた（WS213 への伝達は Q1） | 5.8 |
| **H6** 非同期の採用と画像の解放の経路が無い（WAIT は眠らない、同期の primary だけ数える、libvulkan は WAIT の間 display_mutex、kernel の holds は mutex の下の retire だけ） | kernel は IRQ で採用した list の作りの番号を記録して waitq で起こす。借りの解放は thread の文脈（待っていた caller・次の commit・WAIT・release）で行う。WAIT は眠る形に意味を変える（UAPI の承認の対象）。libvulkan は WAIT と COMMIT の ioctl の間 display_mutex を持たない | 4.2、4.3 (4)、4.4 (5) |
| **M1** 帯域 1.5GB/s は Linux の BCM2835 の頃の値、1024x600 でも約 0.74GB/s、縦の縮小の倍は切り上げで 2 を超えうる | 予算を「未確認」にし、p003 の終わりに Pi で underrun を実測する試験（`hvs-load`）を置く。能力（overlay の最大の大きさ・縦の縮小の可否）は実測の予算から決める。未決 5 は実測の後 | 1.6、6、7 p003、9 |
| **M2** UAPI: fence が無い、`gpu_display_present` を入れると lease・generation・sequence が二重、cursor が commit に入らない、plane ごとの serial が無い、ENOSPC を OUT_OF_DATE にすると primary も作り直し、flags の bit 未定義、src_x が int32 | 4.2 を直した: plane ごとの wait の fence と commit の signal、primary も plane の状態の 1 つ（二重の持ち方をやめた）、plane ごとの serial と結果（APPLIED・DROPPED）、ENOSPC は overlay の chain にだけ失敗を返す、flags を全部定義、src は uint32。cursor を commit に入れない理由も書いた | 4.2、4.4 (6) |
| **M3** C の位置の計算に M の制約（y の下限、double click の slop BUG-265）が無い | 位置の計算を純粋な関数（`kwl_drag_place`）にして M と C が同じ物を使い、M は規則（ずれ・下限・tap の状態）を受け箱に書く。出力をまたいだら C は従うのをやめる | 5.2 |
| **M4** GPL の出典（HVS5 の scaled の plane の word に GPL でない出典が無い） | 値ごとに出典を記録する表（4.3 (8)）を設計に入れ、p003 の終わりに監査を置く。規則の当てはめは Q1（WS141 の 2026-10-04 の決定「事実として使う」の範囲） | 4.3 (8)、7 |
| **M5** 試験: overlay の present の why= の log と本当の usage の fixture、受け入れは kernel の採用の cadence、drag の間の submit 0 と時計・帯の描き直し、HVS と GPU の画素の比較 | 8 を書き直した | 8 |
| **M6** 非同期の経路で clock・cache を触らない | clock は commit の thread の文脈で上げてから状態を記録、下げるのも thread の文脈だけ。overlay の画像は uncached だけを受け付け、IRQ の再発行では clean しない | 4.3 (5) |
| **M7** 依存（p003 と B-k2 は担当 1 つ、p004 は B-u の Pi の受け入れ、p005 は WS213 p004 の clear、p015 の UIF） | 7 の Phase の表に依存の列を書いた | 7 |
| 軽微: lease の release・fault・restore で overlay の状態と holds を戻す | 4.3 (6) | 4.3 |
| 軽微: 1 つの buffer に画像が複数の時の retire_holds の物理アドレスの移し | 借りを slot ではなく状態の項目に持たせ、物理アドレスで移さない | 4.3 (4) |
| 軽微: p005 で影（最大 60px）が上の帯（44px）と交わり合成に戻る場面が多い | 上の帯の plane（bar だけ）を p005 に入れた。dock のアニメの title bar の通り道は p006 | 5.4、7 |
| 軽微: dock の始めの焼き直しは 2 枚とも | 5.3 に書いた | 5.3 |
| 軽微: ws.md の目標と完了の条件の「前後の入れ替え」 | 未決 4（ユーザーに聞く）。答えの後に Q1 が ws.md を直す | 9 |
| 9 節への意見 1〜7 | 1 は直した UAPI で承認を頼む、2・3 はユーザーに聞く、4 はユーザーに聞く、5 は実測の後、6 は Q1 の判断で決まり（1 つの Phase、担当 1 人）、7 は 4.1 の規則として p004 で README に書く | 9 |

## 由来

- [WS214 の由来](../ws.md#由来): 2026-10-12 ユーザー「画面更新の分割って、もっと高度化できそうですか？」→ Q1 の案 2（窓を HVS の plane に載せる）に「ぜんぶやりましょう。」。
- 目標（ws.md）: Pi 4 で、窓の drag・dock と undock の拡大縮小・前後の入れ替えの間、GPU が窓の中身を描き直さない。HVS の plane に窓の絵を載せ、位置・大きさ・順だけを変える。
- 関係: [WS141 p014](../../ws141/phase014/phase.md)（cursor の plane、ユーザーの決定「VK_KHR_display＋cursor 専用の UAPI」「vendor の Vulkan 拡張は作らない」）、[WS141 p016](../../ws141/phase016/phase.md)（(B) B-u の libvulkan の fast path・ticket・cursor の lane、段 B-k2）、[WS213](../../ws213/ws.md)（M・R・C・I の thread）、[WS211](../../ws211/ws.md)（p006 drag の案 4 がこの WS、p007 dock・undock の絵、p008 damage）。

## 0. 結論（要約）

1. **載せる物**: 常時ではなく、**操作の間だけ**（drag、dock・undock・pull back のアニメ）、操作中の窓 1 枚を「窓の絵」（影・title bar・body・panel・sub-surface を焼いた premultiplied の画像）にして overlay の plane に載せる。primary（今の合成の frame）はその窓と system bar を除いて描き、操作の間は（他の変化が無ければ）変えない。system bar は窓の上に要るので、もう 1 枚の overlay（上の帯）に載せる。
2. **口**: compositor は今どおり標準の Vulkan だけ（guardrail）。libvulkan が **VK_KHR_display の overlay の plane** を列挙し、位置と拡大縮小は `VkDisplayPresentInfoKHR` の `srcRect`・`dstRect`、順は plane の stack の順。**present は plane ごとに 1 回**。primary と overlay を同じ refresh に揃えるのは libvulkan の規則（見える・隠れるの切り替えは保留に入れ、保留より後に present された primary と 1 つの commit に）と kernel の commit。隠すのは 1×1 の present（libvulkan の約束）。kernel には新しい UAPI（overlay の commit と能力の問い合わせ、WAIT の意味の変更）が要る → **ユーザーの承認が要る**（差分の案は 4.2、適用しない）。HAL は変えない。
3. **kernel（bcm2711）**: display list を「状態（primary・overlay・cursor）から作る」形にし、primary も含めて coalesce する。settle・EBUSY・mutex の下の採用の待ちをやめ、IRQ が採用を記録して waitq で起こす。display の op は core の session の gate からも controller の mutex からも外す（WS141 p016 B-k2 を含む）。ここまでは今の命令の約束の中の直しで、承認を待たずに p003a で行う。新しい UAPI・overlay（unscaled・scaled・LBM）・帯域の予算・実測は承認の後の p003b。担当は 1 人（Q1）。
4. **compositor**: R が絵を offscreen に焼いて overlay の swapchain（TRANSFER_DST だけ）へ copy し、切り替えの present を出す。drag の移動は窓の plane の持ち主を C に移して C が pointer の箱から動かす（位置だけの fast path、GPU 0）。条件を外れたら今どおりの合成（fallback）。i915・Venus・Linux・FreeBSD は overlay の plane が無いので今と同じ。
5. **見た目の妥協（ユーザーの判断が要る、9）**: 操作の間、窓の glass の blur は**始めの場所のまま**、窓の中身は**止まる**。drop で正しい blur・中身に戻る。undock の拡大縮小の形は帯域の実測の後に決める。

## 1. 調べ: bcm2711 の HVS（HVS5）の plane

出典: Linux 6.19 の vc4（`drivers/gpu/drm/vc4/vc4_plane.c`・`vc4_hvs.c`・`vc4_kms.c`・`vc4_regs.h`、`plan/ws141/temp/linux`、GPL のため理解だけに使い、文書と code に写さない＝WS141 のライセンスの規則）、zedBSD の `src/drivers/gpu/bcm2711/display-*.c`、Vulkan の公開の reference page（`VkDisplayPresentInfoKHR`）。Pi 実機での確かめはしていない。

### 1.1 plane の数と display list

- HVS には固定の数の plane の register は無い。plane は display list（SRAM、HVS の register の窓の後半 16 KiB＝4096 word、zedBSD は `0x4000` から）の中の「plane の word の列」で、数の上限は SRAM の大きさと、1 line の間に HVS が処理できる量（core clock）と、帯域で決まる。Linux は overlay を 16 枚＋primary＋cursor として見せている（`VC4_NUM_OVERLAY_PLANES`）。
- list は plane の列と終わりの word。順は list の中の順（後ろほど上）。channel ごとに「次の list」の register（channel 0 は `0x20`）に先頭の位置を書くと、HVS は次の frame の始めにそれを「今の list」（`0x30`）にする。plane の word の一部（context）は HVS が走査中に書くので、動いている list を書き換えずに新しい list を空いた場所に作る（zedBSD の今の方式）。「次の list」の register は 32 bit の 1 回の書き込みなので、まだ採られていない次の list を別の完全な list に差し替えても、HVS はどちらか一方の完全な list を採る（4.3 (2) の置き換えの前提。Pi で確かめる、10）。
- zedBSD の今の SRAM の使い方: 0〜31 firmware、32〜42 拡大縮小の filter（R0 で書いて常駐）、43〜51 console の list、64・80 primary だけの list の 2 slot、128・160 合成の list の 2 slot（各 32 word）。この設計では状態から作る list の 3 slot（各 256 word、word 256〜1023、register の窓の byte で `0x4400`〜`0x4fff`）に替える（4.3 (2)）。

### 1.2 format・tiling

- RGB の 32 bit（zedBSD は BGRA8888・RGBA8888）、linear。V3D 4.2 の UIF は読めない。plane に出す画像は linear で描く。[WS141 p015](../../ws141/phase015/phase.md)（GPU が書いて GPU が読む画像を UIF に）は、overlay の swapchain の画像を linear のままにする必要がある（この WS の制約、WS141 への伝達は Q1）。
- 1 行の pitch は 65535 byte まで、幅・高さは 4096 まで（今の driver の検査）。pointer は byte 単位、zedBSD は 4 byte の整列を求めている。

### 1.3 拡大縮小

- plane ごとに独立に拡大縮小できる。縦横それぞれ、同じ大きさなら無し、縮小が 2/3 より緩いか拡大なら PPF（多相の filter、SRAM の kernel を使う）、2/3 より強い縮小なら TPZ。
- 拡大縮小する plane は list の word が増える（unscaled の 8 word に対して最大で約 18 word）。正確な並びは p003b で出典の表（4.3 (8)）に記録する。
- 縦の拡大縮小は LBM（line buffer memory）を使う。HVS5 の LBM は 60K word。RGB の PPF で 1 plane「幅 × 16 byte を 128 に切り上げて 4 で割った word」、幅 2048 で 8192 word。
- HVS の処理量: unscaled は 1 clock に 4 pixel、拡大縮小する plane は 2 pixel。縦に縮小する plane は読みが「縮小の比の切り上げ」倍（2/3 より強い縮小では 2 を超えうる、review-1 M1）。

### 1.4 alpha

- plane ごとに per-pixel の alpha（premultiplied か否か）か固定の alpha。zedBSD の今の符号: frame は固定（不透明）、cursor は per-pixel premultiplied。overlay は cursor と同じ符号で足りる。
- VK_KHR_display では alpha の種類と `globalAlpha` は surface を作る時に決まり、present ごとには変えられない（fade の口は無い）。

### 1.5 list の更新と vblank（今）

- 新しい list を空いた slot に書き、「次の list」に載せ、選んだ pixelvalve の vblank の IRQ で「今の list」が一致したら採用（`bcm2711_display_flip_vblank_locked`）。pending の間は次の publish をしない。cursor の変更は待たずに publish し、pending の間の変更は採用の IRQ か submitter の終わりで次の list に入れる（`publish_cursor_locked`、IRQ の中で呼ばれる）。
- primary の present（`output_present`→`bcm2711_display_flip_compose`）は controller の mutex を持ったまま、settle（pending の list の採用を 10µs おきに最大 100ms 待つ、`display-flip.c:322-360`）→ `submit_frame`（pending・busy・uncertain なら EBUSY、`display-flip.c:585-588`）→ 採用を最大 100ms 待つ（`wait_adoption`）。cursor の SET・MOVE も同じ mutex を取る。
- 採用は「frame の始め」なので、list を publish してから画面に出るまでは次の vblank まで（最大 1 refresh）。

### 1.6 帯域・clock・memory

- 読みの帯域: Linux は全 plane の和を 1.5 GB/s までに制限する。ただしこれは BCM2835 の頃の load tracker の値で、BCM2711 に当てはまるかは確かめていない（review-1 M1）。この設計では予算を「未確認」とし、p003b の終わりの Pi の実測（`hvs-load`、8）で決める。HVS は隠れた plane も読む（重なりで省かない、と Linux の計算は扱っている。実測で確かめる）。
- 処理量（core clock）: zedBSD は合成の list で「全 plane の pixel×refresh÷4 の 60%」を core clock の下限にし、firmware の mailbox に頼む（`raise_clock`、眠る。下限 500 MHz、`display-flip.c:722-731`）。
- 走査が間に合わないと underrun（HVS の IRQ で報告、今の driver は採用で消して有効にするだけで数を出していない）。
- memory: HVS には MMU が無く、物理の最初の 1 GiB だけが届く。zedBSD の bcm2711 では Vulkan の native の allocation が全部「連続・1 GiB より下・uncached」（`vulkan-memory.c:417-418`）なので、swapchain の画像はこの条件を満たす。

### 1.7 今の driver でできている事・足りない事

| 項目 | 今 | 足りない事 |
| --- | --- | --- |
| plane の数 | console・frame・cursor の 3 つ固定（合成の list 32 word） | 状態から任意の数の plane を並べた list |
| 位置・大きさ | frame は全画面、cursor は任意の位置で unscaled | overlay の位置・clip、scaled（PPF・TPZ の word、LBM の区画） |
| 更新 | primary は mutex の下で settle と採用の待ち、cursor は待たない＋coalesce | primary も overlay も cursor も状態に入れて coalesce、待つのは primary の caller だけで lock を持たない |
| 借り | slot ごとに frame 1 つ（`holds[2]`）、物理アドレスで slot の間を移す（`display-device.c:1592-1628`）、解くのは mutex の下 | 状態の項目ごとの借り、thread の文脈での解放 |
| 待ち | WAIT は眠らず、同期の primary の数だけ（`display-device.c:947-977`） | IRQ が記録した採用まで眠る WAIT |
| gate | display の op は core の session の gate（`gpu.c:1412-1466`）と controller の mutex の両方を通る | 両方から外す |
| 帯域・clock | 2 plane の見積もり、submitter が上げ下げ | 全 plane の見積もり、thread の文脈だけで上げ下げ、予算を超える commit の拒否 |
| console | 合成の list の一番下に常に置く | opaque な全画面の primary の下では省く |
| underrun | 消して有効にするだけ | 数を flip の report に出す |

## 2. 今の経路（この設計に関係する所、source で確かめた事実）

- compositor の出力: `kwl_glass_draw`（shell.c）の順は、壁紙 → desktop の icon と窓（下から、`draw_window`＝影・body・title bar）→ 窓の popup → system bar（上端、高さ `KWL_GLASS_BAR` 44）→ 各種の popup・menu・通知・keyboard → cursor（plane が無ければ）。
- 窓: 影（柔らかさ 22・focused で 30、窓の外へ 2×soft、最大 60px）、半透明の body の下の glass、panel、sub-surface、client の画像（角の丸め）、title bar。glass は通常は blur した壁紙を**画面の座標で** sample する。
- drag（WS211 p006）: title の press で `window_raise`、移動は M が pointer から位置を更新（`shell.c:10118-10150`: 出力をまたぐと `kwl_window_set_output`、double click の 2 回目の press の slop の間は動かない（`kwl_title_tap_still`、BUG-265）、y の下限 `lowest`＝出力の上端＋`kwl_output_top`）、`drag_damage` で描き直す。Pi で約 30fps。
- dock・undock（WS211 p007）: アニメ 120ms、最初のコマで desktop の絵（`KWL_LAYER_DESKTOP`）と窓の絵（`KWL_LAYER_WINDOW`、出力の大きさ、影を除く）を焼く。
- cursor の plane（WS141 p014、WS213 p004 の段 C）: libvulkan は `GPU_DISPLAY_HAS_CURSOR` の出力で plane を 1 つ多く列挙。compositor は能力で plane を探し（`cursor-plane.c:471-476`）、C が受け箱から present する。
- libvulkan の present（`wsi-swapchain.c`）:
  - `VkDisplayPresentInfoKHR` は 1 回の present に 1 つで、全部の chain に同じ物を当てる（`:1866-1882`）。cursor の chain は `srcRect` が画像の全体でないと `VK_ERROR_INITIALIZATION_FAILED`（`:1906-1917`）。
  - 対象が 2 つ以上の present は ordinary の lane（`present_lane` `:3648-3651`）。cursor の lane・fast path は 1 つの cursor の chain だけ。
  - fast path は usage が TRANSFER だけの chain（`transfer_only` `:937-939`）だけ。他は why=usage で copy の経路（`:3697`）。
  - primary の present は worker が `GPU_DISPLAY_PRESENT_SYNC` を出し（`wsi-display.c:1103-1134`）、その間 `display_mutex` を持つ。前の画像の holds は「置き換えた時」に減らす（同期なので置き換え＝採用）。`display_image_available` は libvulkan の holds だけを見る（`:1173-1193`）。WAIT の ioctl の間も `display_mutex` を持つ（`:1256-1261`）。
- kernel の core（`gpu.c`）: PRESENT_SYNC は依存の fence の待ちを gate の外で行い（`:5925`）、その後 gate（busy）を取って backend の present を呼ぶ（`:5940-5953`）。backend の present は settle と採用の待ちで最大 2×100ms。その間、同じ open（libvulkan の connection は device と node ごとに 1 つの fd を共有）の他の ioctl は gate で待つ。lease は open をまたげないので、cursor 用に別の fd を開く逃げ道は無い。
- WS213: M（main）、R（描画）、C（cursor、自分専用の swapchain だけ、3.2）、I（入力）。park は R と C の両方を止める（4.8）。
- guardrail: compositor は GPU を libvulkan だけで扱い、GPU の UAPI を ioctl で直に呼ばない。

## 3. 方式（何を、いつ plane に載せるか）

| 案 | 内容 | 判断 |
| --- | --- | --- |
| A 常時 | 全部の窓をそれぞれの plane に | 採らない。帯域、窓ごとの絵の memory、stack の順が surface で固定（前後の入れ替えで写し直し）、glass が窓の場所に依る。 |
| B 操作の間だけ（採る） | 操作中の 1 枚だけを plane に。操作の始めに焼き、終わりに合成へ戻す | drag・dock の間は primary も窓の絵も変わらず GPU は 0（時計などの他の変化を除く）。帯域は 1 枚分＋帯。 |
| C client の画像を直に | client の buffer をそのまま plane に | 採らない。角の丸め・opacity・影・panel の glass が HVS で作れない。 |

B の plane の構成（下から）: primary（今の frame、操作中の窓と bar を除く）→ overlay 1 窓の plane → overlay 2 上の帯の plane（system bar と、dock のアニメの title bar）→ 一番上 cursor。console の plane は opaque な全画面の primary の下では省く。

## 4. 口（kernel・libvulkan・compositor のつなぎ方）

### 4.1 選択肢と原子性の規則

| 案 | 判断 |
| --- | --- |
| (a) libvulkan が VK_KHR_display の overlay の plane を列挙し、新しい UAPI の commit で kernel へ（採る） | compositor は標準の Vulkan だけ。位置・拡大縮小は `srcRect`・`dstRect`、順は plane の stack の順、alpha は `VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR`。present は plane ごとに 1 回（F1）。 |
| (b) compositor から kernel への private の ioctl | 採らない（guardrail）。 |
| (c) vendor の Vulkan 拡張 | 採らない（p014 のユーザーの決定）。 |
| (d) cursor と同じ形（overlay ごとに SET（kernel が写す）・MOVE） | 採らない。5〜10 MB の絵を写すのは重く、primary との切り替えを同じ refresh に揃えられない。 |

**原子性の規則**（libvulkan の実装の定め。仕様の外で、仕様は禁じない。p004 で `userland/desktop/libvulkan/README.md` に書く）:

1. 1 回の `vkQueuePresentKHR` には 1 つの plane の swapchain を渡す（compositor の約束）。複数を渡された時は libvulkan は chain ごとに順に扱い、揃えることを約束しない。
2. **切り替えの保留と、取り込む primary**（N1）: overlay の plane の present のうち、**見える・隠れるの切り替え**（隠れている plane を出す、出ている plane を隠す。判定は「kernel に記録済みの状態」に対して）の物は、kernel に出さずに display ごとの「保留」に入れる。primary の present は display ごとに `vkQueuePresentKHR` の呼び出しの順の **ticket** を持つ（swapchain_mutex の下で振る。ordinary の lane は ticket の順に kernel へ出す）。保留には「保留した時に発行済みの primary の ticket＋1」（`min_primary`、同じ lock の下で読む）を付ける。ticket が `min_primary` 以上の primary の job が kernel に出す時に、その display の保留を全部取り込み、1 つの commit（1 つの list）にする。保留より**前**に present された primary（前のコマ、lane で GPU を待っている物）は、kernel に出るのが保留より後でも取り込まない。
3. **保留がある plane への後続の present**（N2a）: 保留がある plane への present は、切り替えかどうかに関わらず保留に入り、保留を置き換える（`min_primary` は最初の保留の値を保ち、切り替えの判定も最初の物のまま）。置き換えられた保留の画像はすぐ空く（DROPPED と同じ、kernel に名指されていない）。fast path・overlay の lane で直に commit しない（直に出すと、取り込む primary の前に plane が出て窓が 2 重になる）。
4. 保留が無い plane への切り替えでない present（出ている plane の位置・画像の変更）は保留しない。画像の中身が前の commit から変わっていない（GPU の書き込みが無い）物は呼んだ thread で直に commit（位置だけの fast path）、新しい中身の物は overlay の lane で GPU の完了を待ってから commit。
5. よって R は「切り替えの overlay の present → primary の present」の順に出す。逆に出すと、切り替えは次の primary（次のコマ）まで遅れる（README に書く）。R は primary の描画の記録と submit を済ませてから、切り替えの present と primary の present を間に描画を挟まずに続けて呼ぶ（規則 6 の期限に掛からないように）。
6. **primary を伴わない flush と期限**（N2b・c）: 次の時、libvulkan は保留を primary 無しで単独の commit にし、log に `why=expired` か `why=flush` を出す。単独の flush は primary と同じ refresh を約束しない。
   - 期限: 保留の後 2 refresh（出力の mode の refresh から）の間に、ticket が `min_primary` 以上の primary が **present されない**（overlay の lane の worker が時計で見る）。present された後は、その primary の job が kernel に出るまで（GPU の完了まで、1080p で 61ms 程）待ち、期限は掛けない。その job が kernel に出る前に失敗した時は 4.4 (6)。
   - `vkQueueWaitIdle`・`vkDeviceWaitIdle`（`present_drain`）: その queue・device の display の保留を単独で commit し、採用を待ってから返る。
   - lease の release・display の surface の破棄の前。
   - `vkDestroySwapchainKHR`: その chain の保留は捨てる（画像は空く）。その chain の画像で plane が出ていれば、隠す状態を単独で commit して採用を待ってから画像を解く。
   - 退役した chain（OUT_OF_DATE・破棄）の保留は捨て、commit に入れない（壊れた chain の保留が後の primary の commit を ESTALE にして primary まで失敗させることが無い）。
   - compositor が規則 5 の順を守れば、WaitIdle・破棄・release は切り替えの present とその primary の間に来ない（R は 2 つを 1 つの thread で続けて出す）。来た時の見た目（1 コマ窓が 2 重か消える）は compositor の誤りとして log で見つける。

規則 2 を「primary の present が kernel に届く前に出た overlay の present を全部まとめる」（review-1 の例）にしない理由: Pi は今 1 core で R が M に preempt されうるので「primary が kernel に届く前」は時刻の競争になり決まらない。また C の位置だけの present（drag の移動）まで R の primary の GPU の完了（1080p で 61ms）を待たされる。「切り替えだけを、保留より後に present された primary に付ける」なら呼び出しの順序だけで決まり（ticket）、C の移動は保留が無ければ待たない。切り替えは必ず primary の描き直しと組になる（窓が primary と plane の間を移るので）ので、compositor の側の不便は無い。

入る時の流れの例（5.1）: R が窓の plane を出す present（保留 W、`min_primary`＝t＋1。lane には前のコマの primary t が GPU を待っている）→ 上の帯の present（保留 B）→ 窓を除いた primary の present（ticket t＋1）。primary t は保留を取り込まずに今の状態（窓入り）で出る。primary t＋1 の job が W・B を取り込み 1 つの commit になる。その間に C が持ち主になって窓を動かすと（5.8 の entered の後）、C の present は保留 W を置き換えるだけで、窓は primary t＋1 と同じ list から最新の位置に出る。

C の窓と cursor: 別の plane の別の present（窓は overlay の fast path、cursor は B-u の cursor の fast path）。kernel は「まだ採られていない次の list を差し替える」（4.3 (2)）ので、続けて出た 2 つは、間に frame の始めが挟まらなければ同じ refresh に出る。同じ refresh は**努力目標**とし、kernel の数（`pair`、8）で測る。

### 4.2 UAPI の差分の案（`include/uapi/gpu-display.h`、追加と WAIT の文の変更。**ユーザーの承認の前は適用しない**）

```c
/* gpu_display_info.flags: planes 1 to plane_count - 1 are overlays (GPU_DISPLAY_COMMIT, GPU_DISPLAY_OVERLAY_INFO). */
#define GPU_DISPLAY_HAS_OVERLAYS	2048U

/* The planes of one commit: the primary (0) and at most four overlays. */
#define GPU_DISPLAY_PLANE_PRIMARY	0U
#define GPU_DISPLAY_OVERLAY_MAX		4U

/* gpu_display_plane_state.flags (inputs). */
#define GPU_DISPLAY_PLANE_SHOWN		1U	/* in the list; clear: hidden, no image read or borrowed */
#define GPU_DISPLAY_PLANE_IMAGE		2U	/* handle to format name a new image; clear: keep the image */

/* gpu_display_plane_state.result (output). */
#define GPU_DISPLAY_PLANE_APPLIED	1U
#define GPU_DISPLAY_PLANE_DROPPED	2U	/* a later serial of the plane was applied before */

/* gpu_display_commit.flags (inputs). */
#define GPU_DISPLAY_COMMIT_WAIT		1U	/* return after adoption; needs the primary named */

/* gpu_display_overlay_info.flags (outputs). */
#define GPU_DISPLAY_OVERLAY_SCALE_UP	1U	/* dst may be larger than src */
#define GPU_DISPLAY_OVERLAY_SCALE_DOWN_X 2U	/* dst may be narrower than src */
#define GPU_DISPLAY_OVERLAY_SCALE_DOWN_Y 4U	/* dst may be lower than src */

#define GPU_DISPLAY_COMMIT		_IOWR('G', 44, struct gpu_display_commit)
#define GPU_DISPLAY_OVERLAY_INFO	_IOWR('G', 45, struct gpu_display_overlay_info)

/*
 * One plane's new state in a commit.  plane is 0 (the primary) or an
 * overlay.  The image is storage of this open, GPU_PIXEL_BGRA8888 or
 * RGBA8888; an overlay's has premultiplied alpha and must be uncached
 * native storage.  src (inside the image) is shown at dst (inside the
 * output); an overlay may scale within its GPU_DISPLAY_OVERLAY_INFO limits,
 * the primary is the whole output, unscaled.  SHOWN without IMAGE keeps the
 * plane's image (EINVAL when it has none).  serial increases for each
 * state of the plane; a state with a serial at or below the plane's last
 * recorded one is not recorded and reports DROPPED.  wait_fd (-1 for none)
 * with wait_generation is a fence the image's writes signal; the commit
 * waits for it before the state is recorded.  The image is borrowed until
 * a list without it is adopted.
 */
struct gpu_display_plane_state {
	uint32_t plane;
	uint32_t flags;
	uint64_t serial;
	uint64_t handle;
	uint64_t offset;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t format;
	uint32_t src_x;
	uint32_t src_y;
	uint32_t src_width;
	uint32_t src_height;
	uint32_t dst_x;
	uint32_t dst_y;
	uint32_t dst_width;
	uint32_t dst_height;
	int32_t wait_fd;
	uint32_t result;
	uint64_t wait_generation;
};

/*
 * Changes some planes of the output this open leases; planes not named keep
 * their state, and a plane named twice is EINVAL.  The call first waits for
 * every named wait_fd, then records the named states at once; every display
 * list made afterwards shows them together, from one refresh boundary.  A
 * state that reports DROPPED is not recorded while the others of the same
 * call are: the one exception to all or nothing.  sequence (output) is the
 * number the record got, from one count per output that never goes back and
 * that GPU_DISPLAY_PRESENT shares (the output's last number when nothing
 * was recorded); adopted (output) is the highest number that is adopted with
 * every lower one (shown by the adopted list, or replaced by a later state it
 * shows).  The call does not wait for the boundary unless COMMIT_WAIT is set
 * (allowed only when the primary is named): then it returns after a list
 * made from this record or a later one is adopted, and signals signal_fd
 * (-1 for none; EINVAL without COMMIT_WAIT) at that adoption.  ENOSPC: the
 * planes would exceed the output's bandwidth or the overlays' limits;
 * nothing changed.  ESTALE for an old generation, EBUSY when the lease is not
 * this open's (also when it ends while the call waits), ENXIO when
 * disconnected.  ETIMEDOUT when the adoption did not come: the output keeps
 * every borrowed image, the lease's later commits and presents fail with
 * EBUSY, and releasing the lease or closing the open returns the output to
 * the console and lets the images go.
 */
struct gpu_display_commit {
	uint32_t version;
	uint32_t size;
	uint64_t lease;
	uint64_t generation;
	uint32_t flags;
	uint32_t count;
	struct gpu_display_plane_state planes[1 + GPU_DISPLAY_OVERLAY_MAX];
	int32_t signal_fd;
	uint32_t reserved0;
	uint64_t signal_generation;
	uint64_t sequence;
	uint64_t adopted;
	uint64_t reserved;
};

/* One overlay plane's limits on the output's current mode (vkGetDisplayPlaneCapabilitiesKHR). */
struct gpu_display_overlay_info {
	uint32_t version;
	uint32_t size;
	uint32_t display_id;
	uint32_t plane;
	uint64_t generation;
	uint32_t max_src_width;
	uint32_t max_src_height;
	uint32_t max_dst_width;
	uint32_t max_dst_height;
	uint32_t flags;
	uint32_t formats;	/* GPU_DISPLAY_FORMAT_* */
	uint64_t reserved;
};
```

`struct gpu_display_wait` の文の変更（ABI の形は同じ、意味の変更）:

```c
/*
 * Waits until the commit or present numbered sequence (the output's count,
 * GPU_DISPLAY_COMMIT) has been adopted, at most timeout_ns (one call waits
 * at most a second; zero answers at once).  completed_sequence is the
 * highest number adopted with every lower one; images that only lower
 * records named and the adopted list no longer names are no longer borrowed
 * when it returns.  EAGAIN when timeout_ns is zero and the sequence is not
 * yet adopted, ETIMEDOUT when the time ran out, EINTR on a signal, EBUSY
 * when the lease is not this open's (also when it ends during the wait),
 * ENXIO when disconnected.
 */
```

- 同期の backend（i915・Venus）では番号は present が返った時に採用済みなので、WAIT の振る舞いは変わらない。WAIT が眠るのは、bcm2711 で COMMIT_WAIT なしに出した commit（overlay の変更）の番号を待つ時だけ。
- **番号の意味**（N3）: 番号は「状態を記録した時」に guard の下で振る（4.3 (2) の `state_serial`）。入口で振ると、fence を待っている前の commit より後の番号の fast path が先に記録・採用され、WAIT(前の番号) がすぐ返って scanout 中の画像を再利用させる。記録の時に振れば、番号の順と記録の順が同じなので「その番号以下が全部採用済み」が成り立つ。番号は **controller ごとに 1 本**（今の bcm2711 の `controller->completed` と同じく lease をまたいで増え、戻らない）。lease の始めの console の採用で、それより前の番号は全部採用済みになる。
- **p003a と p003b**（N9）: p003a は今の UAPI のまま（PRESENT・PRESENT_SYNC の返す番号は今の「成功した present の数」、WAIT は眠らない今の形）で、中で `state_serial` を使う。外に見せる番号を `state_serial` にし WAIT を眠る形にするのは、承認の後の p003b。

- 形の要点:
  - primary も `gpu_display_plane_state` の 1 つ（plane 0）。`gpu_display_present` を埋めないので lease・generation・sequence を二重に持たない（review-1 M2）。primary は BLOB（共有の allocation）だけ。copy の経路（ordinary の storage）は今の `GPU_DISPLAY_PRESENT` に残る。
  - fence: plane ごとに wait（画像の GPU の書き込みの完了）、commit に signal（COMMIT_WAIT の時、採用で）。今の PRESENT_SYNC の wait・signal と同じ役。
  - plane ごとの serial: libvulkan の fast path（呼んだ thread）と overlay の lane（worker）が同じ plane の commit を前後して出しても、古い物が新しい物を上書きしない（B-u の ticket と同じ役を kernel で）。DROPPED の画像は借りられない。
  - src は画像の中なので uint32。dst も出力の中だけを許す（端の clip は compositor が src で行う、cursor の負の位置と違う）ので uint32。
  - ENOSPC は「何も変えない」。libvulkan の扱いは 4.4 (6)。
  - cursor は commit に入れない: cursor の SET は kernel の cursor の画像（今 4 枚、p003a で 5 枚、4.3 (2)）に写す形（p014）で、画像を借りる overlay と借りの扱いが違う。C は窓と cursor を別の present で出す（F1）ので、1 つの commit に入れる呼び手が無い。同じ refresh は kernel の list の差し替え（4.3 (2)）で狙う。
  - 大きさ: `gpu_display_plane_state` 96 byte、`gpu_display_commit` 552 byte、`gpu_display_overlay_info` 56 byte（p003b で `_Static_assert` で確かめる）。番号 44・45、flag 2048 は今空いている（review-1 で確認済み）。
- `gpu_display_info.plane_count` の意味: 今 1、overlay ありで 1＋K（bcm2711 は K＝2: 窓と上の帯）。libvulkan の cursor の plane は今どおり最後に 1 つ足す。
- kernel の内部の口（`include/drivers/gpu/gpu-display.h` の `struct drv_gpu_display_ops`、今は flags の欄が無い）に次を足す。HAL（`include/hal/hal.h`・`src/hal/`）でも UAPI でもないので承認は要らない（p003a で足せる物は p003a で）。
  - `uint32_t flags` と `DRV_GPU_DISPLAY_UNGATED`（display の op が gate を通らない、p003a）。
  - `adoption_wait(opaque, session, sequence, deadline, flags)`: 記録の後、pin を放してから core が呼ぶ採用の待ち（4.3 (1)、p003a）。
  - `commit`・`overlay_info`（p003b）。
  - `gpu_ops_validate` の optional の組: `UNGATED` は `adoption_wait` があることを求める。`commit` と `overlay_info` は両方か無し、`commit` は `UNGATED` を求める。NULL の op は core が EOPNOTSUPP、i915・Venus は全部 NULL・flags 0 で今どおり。

### 4.3 kernel（core の `gpu.c`、bcm2711 の `display-flip.c`・`display-device.c`）

**(1) gate から外す（H1）**

- `DRV_GPU_DISPLAY_UNGATED` を宣言した backend では、`GPU_DISPLAY_PRESENT`・`PRESENT_SYNC`・`WAIT`・`CURSOR_SET`・`CURSOR_MOVE`・`CURSOR_HIDE`（p003a）と `GPU_DISPLAY_COMMIT`・`OVERLAY_INFO`（p003b）は `gpu_session_enter` を通らない。
- **表の lock**（N5）: session に `table_lock`（**spinlock**。waitq の条件の lock は spinlock、`waitq.h` の `waitq_sleep`）を足す。`session->resources` を歩く所・変える所は全部（作成・破棄・import・close、gate を通る op の `gpu_resource_lookup` も含む）この lock の下。resource に pin の数を足し、同じ lock の下で増減する。
- **順**: (i) core は fence（PRESENT_SYNC の wait、COMMIT の plane ごとの wait_fd）を lookup の**前**に、gate も lock も持たずに待つ（今の PRESENT_SYNC の `gpu.c:5925` と同じ）。(ii) `table_lock` の下で lookup と pin。(iii) lock を放して backend の記録の op（検査、backend の buffer の retain、clock の上げ、guard の下の記録）。(iv) pin を放す。(v) PRESENT・PRESENT_SYNC・COMMIT_WAIT なら backend の `adoption_wait`（pin も lock も持たずに眠る、4.3 (2)）。pin の期間は (iii) だけ（µs、clock を上げる時だけ mailbox の ms）で、採用の待ち（最大 100ms）には掛からない。借りは (iii) で backend が自分の buffer の参照で持つ。
- `GPU_RESOURCE_DESTROY`: `table_lock` の下で pin を見て、0 でなければ waitq（条件の lock は `table_lock`）で 0 まで待つ。DESTROY は gate を持ったまま待つが、pin の持ち手（gate を通らない op の (iii)）は gate も display_mutex も待たないので deadlock しない（libvulkan は DESTROY を display_mutex の下で呼ぶ、`wsi-display.c:1218-1222`。fast path の commit は display_mutex を取らない、4.4 (5)）。
- device の撤去・session の error の検査は op の始めに今の gate と同じ条件で行う。
- **backend の lock**（bcm2711）: controller の mutex（claim・release・mode・copy の経路）→ plane の mutex（cursor の SET の行の写し 16 KiB まで、記録の前の準備）→ `flip.guard`（spinlock: 状態、lease の写し、失効の印、`state_serial`・`adopted_serial`）の順。overlay・cursor・primary の記録は controller の mutex を取らない。lease の判定は、claim・release が controller の mutex の下で guard の下に写す「今の持ち主・lease・generation」の写しを使う（B-k2 の案と同じ）。
- **lease の失効**（N4）: release・close・fault は、controller の mutex を取る**前**に、guard の下の写しで自分の lease であることを確かめて「失効中」を立て、waitq を起こす（copy の経路は controller の mutex を持って採用を待つので、先に起こさないと release が最大 100ms 待つ）。記録は guard の下で lease・generation・失効・切断を確かめ直し、合わなければ何も記録せずに EBUSY（失効・持ち主が違う）・ESTALE・ENXIO。眠っている PRESENT・PRESENT_SYNC・COMMIT_WAIT の採用の待ちと WAIT は起きて、同じ判定で EBUSY か ENXIO で返る（借りは (6) で lease の終わりが解く）。release はその後 plane の mutex を取って進行中の cursor の SET の終わりを待ち、状態を戻す（(6)）。

**(2) 状態から作る list と差し替え（H2）**

- flip の状態に「primary（画像・serial）、overlay[K]（画像・src・dst・shown・serial）、cursor」と、状態の番号（`state_serial`）を持つ。どの変更も「guard の下で状態を書き、`state_serial` を 1 増やしてその記録の番号にし（N3）、`publish_locked`」。各 list は作った時の `state_serial` を持つ。
- `publish_locked`（guard の spinlock の下、IRQ からも呼ぶ）: 状態の全部から空いた slot に list を書いて「次の list」に載せる。slot は 3 つ（各 256 word、SRAM の word 256〜1023）。attach の時の HVS の register の窓の大きさの検査（`display-flip.c:89` の `0x42c4`）は `0x5000` 以上に直す（N7）。
  - まだ採られていない list が 1 つあり、その後に差し替えをしていなければ、その list を**差し替える**（3 つ目の slot に書き、「次の list」の register を書き直す）。HVS がその間に前の list を採っていれば、新しい list は次の frame で採られる。どちらでも完全な list しか採られない（1.1）。
  - 差し替えを既にした後（採られたかどうか分からない list が 2 つ）は書かずに dirty のままにし、採用の IRQ で書く（coalesce）。「今の list かもしれない」slot は常に 2 つ以下なので 3 slot で足りる。
  - 差し替えの書き込み（N7）: 今の `publish_list`（`display-flip.c:964-968`）は underrun の IRQ を止め、PV の status（`0x28`）に `0x80` を書いて vblank を消してから「次の list」を書く。差し替えでは PV の status を書かない（まだ処理していない vblank を消すと、前の list の採用を見逃す）。underrun は止めない（(9) で IRQ の中で W1C して数える。今は採用まで止めて採用の時に消すので、drag の間の underrun を数えられない）。背景の fill の要求も最初の publish の時だけ。
- `check_pipeline`（`display-flip.c:881-882`、今は next＝今の list を求める）は、next が「今の list か、採られていない list（差し替えの前後の 2 つまで）」でよいように直し、回復の時に許す list の位置も 3 slot に直す（N7）。
- cursor の画像（N7）: `DISPLAY_CURSOR_IMAGES`（`display-device.c:52`、今 4: shown・pending・recorded・書く 1 枚）を 5 にする（差し替えの後は 3 slot の list が全部「読まれているかもしれない」（今の list、差し替えられた list、差し替えた list）ので 3 枚、＋まだ list に入っていない記録済みの状態の 1 枚、＋書く 1 枚）。「どの list にも名指されていない画像」の数え（`cursor_named`）は 3 slot の list を全部見る。
- primary の状態が無い（console だけの list の）間の overlay の commit は EINVAL（overlay は primary の上にだけ出す、p003b）。
- 採用の IRQ（`bcm2711_display_flip_vblank_locked`）: 「今の list」の register から採られた slot を知り、それより古い list を退役にし、採られた list の `state_serial` を `adopted_serial` に記録し（list は状態の全部から順に作るので、その番号以下の記録は全部「出ているか、後の状態に置き換えられて出ている」＝採用済み）、waitq を起こし（`waitq_wake_all`、v3d の IRQ と同じ使い方 `v3d-hardware.c:812`）、dirty なら `publish_locked`。IRQ の中では借りを解かず、clock・cache も触らない。
- settle と EBUSY をやめる。primary の記録も状態を書いて `publish_locked` するだけ。採用の待ち（PRESENT・PRESENT_SYNC・COMMIT_WAIT、backend の `adoption_wait`）は pin も controller の mutex も持たずに眠る: `flip.guard` の下で `adopted_serial` と `waitq_sequence(&wq)` を読み、`adopted_serial` が自分の記録の番号より小さければ `waitq_sleep(&wq, &guard, observed, deadline, 0)`（上限 100ms、起きたら guard の下で読み直す）。その間も overlay・cursor の記録は進み、その list は今の primary を含む（「自分の primary の記録以上を含む list」の採用＝review-1 H2 の待ち）。primary の caller は 1 つの lease に 1 人（libvulkan の worker）なので、primary は 1 refresh に 1 回より速く変わらない（FIFO）。
- 採られない（100ms）: 待っていた caller か次の記録が気づき、今と同じく uncertain にして借りを全部持ったままにする。以後のその lease の記録は EBUSY（今の uncertain の `submit_frame` と同じ）、release・close が console の list に戻して借りを解く（今の `restore_console`）。待っていた caller は ETIMEDOUT。
- `GPU_DISPLAY_PRESENT`・`PRESENT_SYNC`（bcm2711）は「primary だけの記録＋採用の待ち」と同じ実装を通る（p003a。p003b の COMMIT_WAIT も同じ）。cursor の SET・MOVE・HIDE も状態の変更で、今の `publish_cursor_locked` と `base_words` の写しは不要になる。
- **copy の経路**（N11、Q1: p003a の範囲で安い方）: ordinary の storage の PRESENT は今の 2 枚の copy の buffer（`copies[2]`、`display-device.c:76`）のまま、controller の mutex を持って直列にする。写す前に guard の下で「写す先を名指す list が今かもしれない（今の list か採られていない list）」かを見て、そうなら `adoption_wait` と同じ形で採用を待ってから写す（最大 1 refresh、上限 100ms）。採るのは 3 枚にするより安いから: copy の経路は CPU で描いた画像の fallback で compositor（BLOB）は通らず、3 枚目は出力の大きさの連続の memory（1 GiB より下、1080p で 8.3 MB、4K で 33 MB）を常に取るが、待つ案は稀な場合に最大 1 refresh 待つだけで memory を使わない。PRESENT は同期（自分の記録の採用まで返らない）なので、普通は前の present の採用で他方の copy は空いていて待たない。待つのは、copy の present の直後に同じ lease の BLOB の present や cursor の差し替えが入った時だけ。

**(3) WS141 p016 B-k2 との関係**

- B-k2 の案（`plan/ws141/phase016/phase.md` 11 節）は cursor の op を controller の mutex から外すだけで、(a) core の session の gate（PRESENT_SYNC が busy の間 cursor の op は gate で待つ、H1）と、(b) settle・EBUSY（mutex の外の cursor の publish が採用の IRQ で次の list を出し続けると、primary の settle が 100ms で uncertain になり、`submit_frame` は pending で EBUSY、H2）が残る。B-k2 をそのまま実装すると、pointer を動かし続ける間 primary の present が ETIMEDOUT・EBUSY で失敗しうる。
- よって B-k2 は単独で実装せず、この WS の (1)・(2) に含める。(1)・(2) は今の UAPI の約束の中の直しなので承認に依らない **p003a** で行う（Q1、N9）。WS141 p016 の受け入れ B が B-k2 を要る時に p003a に依ることは Q1 が WS141 に記録する。

**(4) 借りと解放（H6、軽微）**

- 借り（`bcm2711_buffer_retain`）は slot ではなく**状態の項目**が持つ。commit が画像を名指すと、その thread の文脈で retain して状態の項目に付ける。状態の項目が置き換えられたら、前の項目の借りを「退役待ち」の list に、置き換えた記録の番号（`state_serial`）と一緒に移す。
- 退役待ちの借りは `adopted_serial` ≥ 置き換えた記録の番号 になったら解ける（その記録以後の状態から作った list が採られれば、前の画像を名指す list はもう今の list でも次の list でもない）。物理アドレスで slot の間を移す今の処理（`display-device.c:1611-1618`）は要らない。1 つの buffer に画像が複数あっても、借りは項目ごとの参照の数なので正しい。
- 解くのは thread の文脈だけ: 採用の待ち（PRESENT・PRESENT_SYNC・COMMIT_WAIT）の caller が起きた時、次の記録の始め、WAIT の caller が起きた時、release・fault・restore。IRQ は番号を記録して起こすだけ。
- WAIT（bcm2711、p003b）: lease の判定の後、(2) と同じ形（guard の下で `adopted_serial` と `waitq_sequence` を読み `waitq_sleep(&wq, &guard, observed, deadline, WAITQ_INTERRUPTIBLE)`）で `adopted_serial` ≥ 頼まれた番号 まで眠り（上限 timeout_ns、1 秒まで。signal で EINTR）、起きたら退役待ちを解いてから返す。番号は COMMIT と PRESENT で共有の controller ごとの 1 本（4.2 の番号の意味）。p003a の WAIT は今の形（眠らない、`completed` を答える）のまま。
- 採用の待ち（PRESENT・PRESENT_SYNC・COMMIT_WAIT）は `WAITQ_INTERRUPTIBLE` にしない（上限 100ms、今の PRESENT_SYNC と同じく signal で途中で返らない）。
- signal の fence: COMMIT_WAIT の caller が起きた時（thread の文脈）に signal する。採られなかった（ETIMEDOUT）時と失効（EBUSY・ENXIO）の時は error 付きで signal する（今の PRESENT_SYNC の失敗と同じ）。

**(5) 帯域・clock・cache（M1・M6）**

- commit の受け付け（thread の文脈）で、新しい状態の全 plane の読み（src の幅×高さ×4×refresh×縦の縮小の比の切り上げ）の和が予算を超えるか、overlay の能力を外れれば ENOSPC で何も変えない。
- 予算は p003b の終わりの Pi の実測（8 の `hvs-load`）で決める。実測の前の値は仮に Linux と同じ 1.5 GB/s とし、実測で置き換えるまで p005・p006 の Pi の受け入れに進めない。overlay の能力（`max_src`・`max_dst`・縦の縮小の可否）は「primary＋全 overlay を能力の最大で、console は省いた時」が予算に収まるように出力の mode ごとに決める。compositor が能力の中で頼む限り ENOSPC は起きない。
- core clock（N6、Q1: 今の方針を保つ）: 新しい状態の要る clock が今より高ければ、状態を記録する**前**に thread の文脈で mailbox で上げる（眠る、clock 用の mutex で直列）。記録は guard の下で「今の clock ≥ 要る clock」を確かめてから行い、足りなければ guard を放して上げ直す。**lease の間は下げない**（今の `display-flip.c:694-702` の「合成の frame は上げた下限を保ち、console に戻る時だけ下げる」と同じ）。下げるのは release・close・fault の console の list の採用の後（thread の文脈）だけ。よって記録と下げの競争（下げた後に、上げた値を前提に記録した状態が IRQ で再発行される）は起きない。IRQ の再発行（coalesce）は記録済みの状態だけを使うので、clock は既に足りている。
- cache: overlay の画像は uncached の native の storage だけを受け付ける（cached なら EINVAL）。primary の BLOB も uncached。primary の copy の経路は写した後に thread の文脈で clean。IRQ の再発行は clean しない。console の text の clean は commit の thread の文脈で行い、console は opaque な全画面の primary の下では list に入らないので、IRQ の再発行で古い text が出る心配は見た目だけ（その場合も console は primary の下）。

**(6) console の省略と lease の終わり（軽微）**

- primary が opaque（固定の alpha）で出力の全体を覆う時は console の plane を list に入れない。
- claim・release・fault・restore（`cursor_reset` に当たる所）で overlay の状態を全部隠した状態に戻し、plane ごとの serial を 0 にし、借りを退役待ちに移し、失効の印を消す（claim）。console の list の採用で全部を解き、`adopted_serial` を `state_serial` にする（それより前の番号は全部採用済み、WAIT はすぐ返る）。番号（`state_serial`）は 0 に戻さない。

**(7) scaled の plane**

- PPF・TPZ の選び方、係数の word、LBM の区画（overlay ごとに固定の区画、幅 2048 まで 8192 word を K 枚分）、filter は SRAM に常駐の物を指す。
- LBM の区画が幅 2048 固定なので、縦に拡大縮小する overlay の src の幅は 2048 まで。`GPU_DISPLAY_OVERLAY_INFO` の `max_src_width` は拡大縮小（SCALE_UP・SCALE_DOWN_Y）を許す時の値として 2048 以下を返す（4K の mode でも）。縦に拡大縮小しない（src と dst の高さが同じ）overlay は LBM を使わないので出力の幅まで。この区別は p003b で OVERLAY_INFO の flags と一緒に決め、libvulkan は `vkGetDisplayPlaneCapabilitiesKHR` の maxSrcExtent に小さい方を出す。値と名前は WS141 の規則（値は事実、名前は独自、配置・comment・構造を写さない）と (8) の表。

**(8) 値の出典の表（M4）**

- p003a・p003b は新しく使う HVS の値ごとに、次の表を p003b の Phase の directory の `hvs-values.md` に作って commit する（p003a で新しい値を使えば p003a が作り始める）（GPL の文と code は写さない。値・独自の名前・出典の場所・確かめ方だけ）。

| 列 | 中身 |
| --- | --- |
| 値 | 例: plane の control word の scaled の bit、PPF・TPZ の係数の word の並び、LBM の word の式、「次の list」の register の offset |
| zedBSD の名前 | 一括で付けた独自の名前（WS141 の規則 2） |
| 出典 | (a) zedBSD の既存の code（WS141 で監査済み）、(b) Broadcom・Raspberry Pi の公開の文書、(c) Pi の実測、(d) Linux 6.19 vc4 の読み（file と行、理解だけ） |
| 確かめ | host の試験の名前、Pi の実測の結果 |

- (d) だけが出典の値は、WS141 の 2026-10-04 のユーザーの決定（register の値は事実として使い、名前は独自に改名し、配置・comment・構造は写さない、`plan/ws141/rpi4-gpu-license-audit.md` の 5）の範囲で使う。当てはめは Q1。
- p003b の終わりに（p003a で新しい値を使えば p003a の終わりにも）、変えた display の file の GPL との類似の監査（字面・設計、WS141 p007 の道具と目視）を行い、結果を表と一緒に残す。WS の最後の p002 でもう一度行う。

**(9) 計数**

- v3d.perf=1 の flip の report に: 記録の数（primary・overlay・cursor 別）、coalesce の数、差し替えの数、underrun の数、`pair`、cadence に `BCM2711_FLIP_CADENCE_OVERLAY`（overlay の位置の変更を含む list の採用の間隔）を足す。
- underrun（N7）: 止めずに、HVS の IRQ で status の bit を W1C して数える（今は publish で止め採用で消すので、drag の間の underrun が数に出ない）。
- `pair`（軽微）: **採用の時に**数える。採られた list が前の採用の list から窓の plane の位置を変えた数を分母、そのうち cursor も変わった数を分子にする（作った list ではなく採られた list で数える。差し替えで捨てた list は数えない）。
- 採用の時刻: overlay の記録ごとに（plane、plane の serial、記録の時刻、採用の時刻）を固定の大きさの輪（例 256 件）に残して report に出す（時刻は 1 ms の scheduler の時計）。plane の serial で libvulkan の `WSI OVERLAY` の log（serial を出す）と C の log（入力の時刻）に突き合わせ、8 の「入力から採用までの遅れ」を出す。置き換えで採られなかった記録は採用の時刻が無い（DROPPED と区別して数える）。

### 4.4 libvulkan（`userland/desktop/libvulkan/wsi-display*.c`・`wsi-swapchain.c`）

1. **列挙**: `GPU_DISPLAY_HAS_OVERLAYS` の出力では plane を primary 0、overlay 1〜K、（cursor）の順に列挙し、stack の順も同じ。`vkGetDisplayPlaneCapabilitiesKHR`（overlay）: supportedAlpha は PER_PIXEL_PREMULTIPLIED、src・dst の範囲は `GPU_DISPLAY_OVERLAY_INFO`、minDstPosition は (0,0)、maxDstPosition は出力の大きさ − 1。cursor の plane の見分け（最大 64、負の位置）と重ならない。
2. **隠す（H3）**: overlay の chain の present で `srcRect` と `dstRect` がともに offset (0,0)・extent 1×1 なら、その plane を隠す（`SHOWN` を落とした状態）。画像は名指されず借りられない。swapchain は壊さない。README に約束として書く（1×1 の overlay を本当に出す用途は無いものとする）。
3. **present の道（F1）**: overlay の chain の present は 1 つずつ。4.1 の規則で 3 つに分ける。
   - **保留**（見える・隠れるの切り替え、と保留がある plane への全部の present、4.1 規則 2・3）: display ごとの保留の表（hold の lock の下）に plane の状態（画像・矩形・serial・wait の fence・`min_primary`・chain）を置き、present はすぐ返る。置き換えた保留の画像は holds を戻してすぐ空ける。ticket が `min_primary` 以上の primary の job（ordinary の lane）が、kernel に出す時に hold の lock の下でその display の保留を全部取り出して 1 回の `GPU_DISPLAY_COMMIT`（COMMIT_WAIT、plane ごとの wait の fence）にする。`min_primary` より小さい ticket の primary の job は保留に触れない。期限・WaitIdle・破棄・release の単独の flush は 4.1 規則 6（期限は overlay の lane の worker が `pthread_cond_timedwait` で見る）。
   - **fast path**（保留が無い plane で、出ている plane の位置・画像の変更で、画像が「書き込みの完了を libvulkan が前に確かめた時」から GPU に書かれていない）: 呼んだ thread で `GPU_DISPLAY_COMMIT`（wait なし、COMMIT_WAIT なし）だけ。worker・GPU・queue の mutex・display_mutex を通らない（B-u の cursor の fast path と同じ規則）。保留の有無は hold の lock（短い mutex、ioctl の間は持たない）の下で見て、保留があれば保留の道に回す。保留が無いと見てから ioctl までの間に同じ plane の切り替えの保留が入ることは無い（1 つの chain は外部同期で 1 つの thread だけが present し、切り替えの present はその chain から来る）。判定は B-u の `content_serial`（`transfer_only` の chain だけ、H4）。焼いた後に一度も present していない 2 枚目は、最初の 1 回だけ overlay の lane を通る（fence は既に済んでいるので待ちは短い）。
   - **overlay の lane**（出ている plane の新しい中身）: cursor の lane と同じく ordinary の lane の後ろに並ばない worker が、wait の fence を付けて commit。
4. **serial**: plane ごとの present の番号（libvulkan が plane ごとに持ち、swapchain を作り直しても増え続ける。kernel は lease の始めに 0 に戻す）を plane の状態の serial にする。DROPPED の present は B-u と同じく DROPPED の結果（画像はすぐ空き）。
5. **借りと acquire（H6）**: 画像の holds は「commit が名指した数」で、置き換えた commit の番号を画像に記録する。COMMIT が返す `adopted` 以下の番号の置き換えはその場で holds を減らす。acquire で空きが無ければ、置き換えの番号で `GPU_DISPLAY_WAIT`（timeout は acquire の timeout）を**display_mutex を持たずに**呼び、返ったら holds を減らす。primary の COMMIT_WAIT の ioctl の間も display_mutex を持たない（holds の更新の前後だけ持つ）。
6. **保留を含む commit の失敗（M2、N10）**:
   - ENOSPC か EINVAL（overlay の状態が原因。primary だけの形は今の present と同じ検査を通る）: 保留を全部捨て、**primary も出さない**（前の状態を保つ。Q1 が N10 の案を採った）。保留の overlay の chain には次の present・acquire で `VK_ERROR_OUT_OF_DATE_KHR` を返す。primary の画像は出さずに空け、primary の chain は作り直させない（その present の結果は成功のまま、`WSI OVERLAY ... why=nospace|invalid` を log）。compositor は overlay の chain の OUT_OF_DATE で `RETURNING` に入り、R が窓を含む primary を描く。
   - 理由: 窓を除いて描いた primary を overlay 無しで出し直すと（第 2 版の案）、R が窓入りの primary を描くまで（1080p で 61ms 以上、数コマ）窓が画面から消える。これは 5.7 が無いと約束した frame である。前の状態（入る前なら窓は primary に入り plane は隠れている、戻る前なら窓は plane に出ている）はそれだけで正しい画面で、1〜数コマ古いだけで欠けない。捨てた primary の画像は「描いたが出なかった」画像で、次に出す primary（`RETURNING`）は窓の矩形＋reach と bar を描き直し、捨てた画像と前の画面の差（窓と bar の所だけ）を覆うので、古い所は残らない（R の damage は画像ごとの中身で数えるので、出なかったことで狂わない。p005 で確かめる、10）。
   - ESTALE・ENXIO・EBUSY（commit 全体の理由）: 今の primary の present の失敗と同じ扱いを primary の chain と、取り込んだ保留の chain に返す。
   - primary の job が kernel に出す前に失敗（GPU の fault など）: 保留を同じく捨て、overlay の chain に OUT_OF_DATE。出ている plane が残る時は、compositor が OUT_OF_DATE の chain を破棄し、その破棄が 4.1 規則 6 の単独の隠す commit を出す。
   - 保留の無い overlay の lane・fast path の ENOSPC はその chain だけ OUT_OF_DATE。
   - 能力の中で頼む限り ENOSPC は起きない（4.3 (5)）ので、起きたら compositor の誤りか予算の誤り（log で見つける）。
7. **画像**: overlay の display surface の swapchain は primary の claim を借りる（cursor と同じ）。画像は native の共有の画像（linear、連続、1 GiB より下、uncached）。2 枚（FIFO）、overlay の surface の minImageCount は 1（2 枚を同時に acquire できる）。
8. **log**: `WSI OVERLAY present plane= serial= ticket= min_primary= path=held|replaced|fast|lane|commit|flush why= result= sequence= adopted= call_ns=`（why は B-u の `why=` と同じ語: usage・result・next・changed に、nospace・invalid・expired・flush を足す）。

### 4.5 HAL・UAPI の承認

- HAL の API・責務: 変更なし。
- UAPI（`include/uapi/gpu-display.h`）: 追加（COMMIT・OVERLAY_INFO・HAS_OVERLAYS と定数）と WAIT の意味の変更（眠る、番号を controller の 1 本に、借りの解放）。**ユーザーの承認が要る**（9 の 1）。承認の前は p003b 以降の実装に入らない。
- p003a は UAPI の文と struct を変えない。bcm2711 の PRESENT が採用の待ちの間に cursor の op が入っても EBUSY で失敗しなくなる（今の `struct gpu_display_present` の文は使用中の EBUSY を約束していない）ことは、今の約束の中の直しとして承認を待たずに行う（Q1、N9）。ユーザーへの 9 の 1 の説明にはこの変わり目も書いた。

## 5. compositor（`userland/desktop/wayland/`）

### 5.1 状態（窓 1 枚ごと、M が持つ）

`COMPOSED` → `BAKING`（R が焼く。画面は今どおり合成）→ `ON_PLANE`（primary は窓と bar を除く、窓の plane の持ち主は C）→ `RETURNING`（持ち主を R に戻し、R が窓を含む primary を描く）→ `COMPOSED`。dock のアニメの間は `ANIM`（持ち主は R）。どの状態からも条件を外れたら `RETURNING`。

- **焼き**（R の job、want の種類に「plane の絵」を足す。H4）: R は窓（影・title bar・body・panel・sub-surface）を専用の offscreen（p007 の `KWL_LAYER_WINDOW` の memory、出力の大きさ、COLOR_ATTACHMENT）に**画面と同じ座標で**描き（glass が正しい場所の blur を取る）、窓の矩形（影を含む、幅 w・高さ h）を窓の plane の swapchain の 2 枚の画像の **(0,0)** へ copy する（同じ job、2 回の `vkCmdCopyImage`）。swapchain は usage が TRANSFER_DST だけ（`transfer_only`、fast path に入る）。上の帯も同じく小さな offscreen に bar を描いて帯の swapchain（TRANSFER_DST だけ）の 2 枚へ copy。
- **矩形**（軽微）: `srcRect` は画像の中の座標なので (0,0,w,h)、`dstRect` は出力の中の窓の矩形 (x,y,w,h)。出力の端で clip する時は dstRect を出力の中に縮め、縮めた分だけ srcRect の offset と extent も動かす（左・上の端で切れたら srcRect の x・y が増える）。上の帯も同じ（帯の offscreen の帯の矩形を画像の (0,0) に copy）。
- **入る**（R）: 窓の plane の present（shown、上の矩形、切り替え→保留）→ 上の帯の present（shown、切り替え→保留）→ primary を「窓と bar を除いて」描いた present（damage は窓の矩形＋reach と bar の帯）。R は primary の描画を submit してから 3 つの present を続けて呼ぶ（4.1 規則 5）。libvulkan が 3 つを 1 つの commit にする（4.1）。primary の walk は窓を飛ばす（p007 の `layer_skip` の印）。R は 2 枚目の画像を acquire したまま持ち主の移し（5.8）で C に渡す。
- **戻る**（R、持ち主が R に戻った後）: 窓の plane の隠す present（1×1、切り替え→保留）→ 上の帯の隠す present → primary を窓と bar を含めて描いた present（damage は窓の今の矩形＋reach と bar）。隠す present には C から返された（C が acquire したままの）画像を使う（無ければ acquire、空きは採用の後、最大 1 refresh）。

### 5.2 drag

- 始め: 移動が始まった時（`server->drag` の窓が初めて動いた pass、slop を越えた後）に焼きを頼む。焼けるまでの数コマは今の合成の drag で、焼けた所で入る。
- 移動の計算（M3）: 今の M の計算（`shell.c:10118-10150`）を純粋な関数 `kwl_drag_place(const struct kwl_drag_rule *rule, int32_t pointer_x, int32_t pointer_y, struct kwl_drag_place *place)` に切り出し、M と C が同じ関数を使う。rule は M が書く写し: 掴んだ点からのずれ（`drag_dx/dy`）、y の下限（出力の上端＋`kwl_output_top`）、double click の tap の状態（`kwl_title_tap_still` に要る値の写し、BUG-265）、窓の出力の矩形。place は窓の左上と「動かない（slop の中）」「出力の外へ出た」の印。
- 移動: M は窓の位置（`surface->x/y`）を今どおり I の FIFO の event から同じ関数で更新するが、`drag_damage` を出さない（`ON_PLANE` の間）。C は受け箱の rule と I の位置から place を計算し、窓の plane の present（`dstRect`＝place の (x,y,w,h)、`srcRect`＝(0,0,w,h)、出力の端では 5.1 の矩形の規則で clip、位置だけの fast path）と cursor の present を続けて出す。present の間引き（C0、refresh − 2.5ms）はそのまま。M と C は同じ入力に同じ関数を使うので、入力が止まった後の C の最後の位置は M の位置と同じ。button を離した時は、M の drop の位置と C が最後に出した位置の差は、C0 の 1 回の間引きの間の pointer の移動まで（その差は `RETURNING` の primary が M の位置で描くので残らない）。
- **acquire と最後の位置**（N8）: C の窓の plane の acquire は今の cursor と同じく timeout 0（`cursor-thread.c:443`）。2 枚の FIFO では前の present の採用まで次の画像が空かないので、RETRY は普通に起きる。RETRY の時は期限を「前の present から 1 refresh」に立て（今の cursor の 1 ms の再試行ではなく）、空いたらその時の**最新の** place で出す。C は「窓の plane に最後に出した place」と「受け箱の rule と I の最後の位置からの place」が同じになるまで、入力が来なくても期限で起きて続ける（最後の位置に必ず着く）。保留がある間（5.8 の entered の直後）の present は保留を置き換えてすぐ返る（4.1 規則 3）。
- pointer が別の出力に移った（place の印）: C は従うのをやめ、M は `RETURNING`（M は今どおり `kwl_window_set_output` で窓を移す。出力が 1 つの条件 5.5 の下では起きないが、heads の出力の端で起きる）。
- 中身: drag の間はその窓の client の更新を見せない（frame callback を drop まで止める）。**ユーザーの判断が要る**（9 の 3）。受け入れない時の案: R が commit ごとに offscreen に焼き直して fence と版を受け箱に置き、C が次の present の前に自分の command pool で offscreen から acquire した画像へ copy する（持ち主の移しは要らない。GPU は窓 1 回分＋copy）。
- 終わり（button を離す）: `RETURNING`。bar への drop（dock）は 5.3 へ続く（窓は plane のまま、持ち主は R へ）。

### 5.3 dock・undock・pull back のアニメ

- アニメの間、R が窓の plane の `dstRect` をアニメの矩形にする（HVS が拡大縮小、overlay の lane か fast path）。dock・pull back（小→大）は始めの絵を拡大。
- dock の始めの焼き直し（軽微）: drag の絵は title bar を含むが dock の絵は含まないので、アニメの始めに offscreen に焼き直して **2 枚とも**に copy する（p007 の最初のコマと同じ費用）。出ている方の画像は HVS が読んでいるので直に書かない: 空いている方に copy して present（overlay の lane）し、その採用で空いたもう一方にも同じ offscreen から copy してから使う。アニメの最初の 2 コマが copy 付き、以後は 2 枚を交互に位置だけで出す。drag の焼き（`BAKING`）の時は plane が隠れていて 2 枚とも空いているので、1 つの job で 2 枚に copy できる。上の帯は R が毎回 offscreen から acquire した方へ copy する（帯 1 本、小さい）。
- undock（大→小）の形は p003b の帯域の実測の後に決める（9 の 5）。案 (i) 終わりの大きさで焼いた絵を拡大で出す（縮小をさせない、始めが少し柔らかい）、案 (ii) 実測の予算に収まる mode では縦の縮小を HVS にさせる、案 (iii) 収まらない mode は p007 の GPU の経路。
- title bar は窓の位置から bar の slot へ滑って fade する（`draw_window` の anim の部分）ので、窓の絵には入れず、上の帯の plane に bar と滑る title bar をコマごとに描く（R、小さな GPU の仕事）。上の帯の plane の矩形は bar の帯と title bar の通り道の外接。その高さが帯の能力（4.3 (5) で予算から決める）を超える時はそのアニメを p007 の GPU の経路にする。
- 影は窓の絵に入れて一緒に拡大縮小する。角の丸めも伸びる（p007 で受け入れた種類）。
- configure はアニメの終わり（p007 のまま）。p007 の絵の経路は、plane の無い出力と条件を外れた時の fallback として残す。

### 5.4 上の帯の plane（p005 から）

- 中身: system bar（p005）と、dock のアニメの title bar（p006）。premultiplied、透明な所は下が見える。
- p005 から入れる（review-1 軽微: 窓の影（最大 60px）は bar の帯（44px）とよく交わり、帯が無いと合成に戻る場面が多い）。p005 の帯の矩形は bar の帯（と bar の影）だけ。
- primary は帯が出ている間 bar を描かない（2 重に描くと影が濃くなる）。bar の中身の変化（時計・status）は帯の画像を描き直す（帯 1 本、overlay の lane）。
- bar の glass が下の窓に依らず合成と同じ画素になるか（bar の glass が blur した壁紙だけを sample し、alpha が 1 か）は p005 の始めに source で確かめる（10）。依るなら、帯が出ている間 bar の下に窓が入った時だけ帯を描き直す（R、帯 1 本）。

### 5.5 条件（`ON_PLANE` にしてよい時、毎 pass 確かめ、外れたら `RETURNING`）

- 出力に窓の plane と帯の plane（能力: PER_PIXEL_PREMULTIPLIED、src が窓の矩形の大きさ以上、dst が出力の中）がある。dock のアニメは拡大も要る。
- 出力が 1 つ（heads が無い）、窓がその出力の上で、始めの矩形（影を含む）が出力の中に全部入る。
- 窓が一番上、server の装飾の窓（CSD でない）、sheet・popup が開いていない、fullscreen でない。
- 見えている窓に backdrop を使う物（docked の中央の窓、`set_blur`、並べの glide）が無い。arrange の menu・keyboard の blur・App Home・Wiseview・desktop の slide・screen off・greeter・lock・dnd が無い。
- 窓の上に描く物（bar 以外: 窓の popup、menu、通知、status panel、volume、switcher、power dialog、keyboard、corner の hint）が見えていない。途中で出たら `RETURNING`。
- `--no-planes` の起動の選択が無い。

### 5.6 glass・影・角・blur・重なり・damage

| 物 | 扱い |
| --- | --- |
| 影 | 窓の絵に入れる（premultiplied）。HVS の premultiplied の over は GPU の blend と同じ式で、差は丸め（8 で比べる）。 |
| 角の丸め | 絵の alpha 0 の角。drag では変わらない、dock では伸びる。 |
| title bar・半透明の body・panel の glass | 始めの場所の blur のまま絵に焼かれ、窓と一緒に動く（**9 の 2**）。drop で正しい blur に戻る。 |
| backdrop の blur | 使う窓・画面がある時は plane にしない（5.5）。 |
| 重なり | 窓は一番上、下の窓は primary に今どおり。上に来る物は bar（帯）だけで、他は条件で除く。 |
| damage（WS211 p008） | `ON_PLANE` の間、窓の移動は primary の damage を出さない。入る・戻るの時だけ窓の矩形＋reach と bar の帯。他の物（時計は帯、他の窓の commit は primary）は今どおり R が描く。hover の記録は今の `drag_damage` と同じく無効。 |
| 窓の中身の commit | drag の間は見せない（5.2）。dock のアニメの間は始めの絵。 |

### 5.7 継ぎ目

- 入る時: 絵は合成と同じ draw で同じ座標に焼くので、画素は合成と同じ（丸めの差だけ）。切り替えの 3 つの present は 1 つの commit になるので、窓が 2 重・消える frame は無い（4.1）。
- 戻る時: 窓の glass が今の場所の blur に替わる。drag の間に止めた中身が最新に替わる。
- dock の終わり: title bar の glass が bar の中に消え、窓の影は合成の影に替わる。
- 間に合わない時（焼きが遅い）: 合成の drag のまま始まり、焼けた所で切り替わる。

### 5.8 WS213 の R・C との関係と持ち主の移し（H5）

- R: 焼き、入る・戻るの present、上の帯の描き直し、dock のアニメの窓の plane。want の種類を足す。
- C: `ON_PLANE` の drag の間、窓の plane と cursor を動かす。
- **持ち主の移し**（Vulkan の swapchain の外部同期: 同じ swapchain を同時に 2 つの thread が使わない）。受け箱に「窓の plane の持ち主（R・C）、移しの世代、rule（5.2）、C が持つ画像の index」を足す。
  - **R→C**（入るの後）: R は入るの 3 つの present が返った後、M へ「entered（世代、acquire したままの画像）」を R→M の通り道で知らせ、以後その swapchain を呼ばない。M は受け箱の持ち主を C にして C を起こす。C は次の周でそれを読んで使い始め、受け箱に「took（世代）」を書く。
  - **C→R**（戻る・dock の前）: M は受け箱の持ち主を「R へ返す（世代）」にして C を起こす。C は今の present を終えた（呼び出しが返った）所で、以後その swapchain を呼ばず、受け箱に「given（世代、C が acquire したままの画像）」を書いて pipe で M を起こす。M はそれを受けてから R に戻る・dock の job を頼む。止まるのは C の窓の plane の扱いだけで、C の cursor の present も R も止まらない（park は使わない）。
  - C の失敗（fence・acquire・present）の時は今の cursor と同じく failed を立てて窓の plane を呼ばずに止まり、M は `RETURNING`（持ち主は given の扱い）。
  - park: 今どおり R と C の両方を止める。plane（窓・帯）の swapchain の作成・破棄は park の中で M（cursor の plane と同じ）。park に入る時に `ON_PLANE` なら、R が動ける限り先に `RETURNING` を済ませる（切り替えが primary と 1 つの commit になる）。済ませられない時（R の失敗、出力が消えた）は park の中で M が両方の plane に隠す present を出し、`vkQueueWaitIdle` で保留を単独で flush する（4.1 規則 6。窓は次の primary まで画面から消えうるが、park の場面（mode の変更・screen off・出力の喪失）では見えないか許す）。swapchain の破棄でも同じく単独で隠される。
- **WS213 の不変条件の変更**（WS213 p001 の 3.2 の文。WS213 への伝達は Q1）: 「C は自分専用の object（plane 専用の command pool・command buffer・fence・semaphore・swapchain）と queue だけを使う」を「C は自分専用の object と queue、および M が受け箱で C に移した窓の plane の swapchain（移しの世代が今の物の間だけ）を使う。R は窓の plane の swapchain を、M が R に戻した後だけ使う。移しは 5.8 の entered・took・given の手順だけで行う」に変える。4.8 の park（R と C の両方）は変えない。
- libvulkan の安全: C と R が同じ VkQueue を同時に使うことは WS141 p016 B-u の契約に従う。overlay の fast path も queue の mutex を取らない。

### 5.9 前後の入れ替え

- 今の前後の入れ替え（click で raise、triple click で lower）にはアニメが無く、1 回の合成で済む。focus が変わると影と title bar が変わるので、plane でも焼き直しが要る。
- よってこの WS の段に入れない（効果が小さい）。ws.md の目標と完了の条件の「前後の入れ替え」を外してよいかは **9 の 4**（答えの後に Q1 が ws.md を直す）。

### 5.10 i915・Venus・Linux・FreeBSD

- overlay の plane は bcm2711 の kernel だけが見せる。i915・Venus の libvulkan は今どおり、Mesa（Linux・FreeBSD）の build は display の plane を開かない。compositor は窓の plane が見つからなければ今どおり合成する。core の gate の変更は `DRV_GPU_DISPLAY_UNGATED` を宣言した backend（bcm2711）だけ。

## 6. 効果と帯域の見積もり（Pi での確かめ前）

Pi の今の数字（Q1、2026-10-12）: submit→shown の p50 は 800x480 で 11ms、1024x600 で 26ms、1920x1080 で 61ms。drag は約 30fps。

| 場面 | 今 | plane の後（見込み） |
| --- | --- | --- |
| drag の移動 | 毎コマ窓の前と今の矩形を合成、約 30fps、窓は cursor に遅れる | primary の submit 0（時計の帯の描き直しを除く）。C の fast path（ioctl だけ）＋次の vblank。窓と cursor は多くの場合同じ refresh（努力目標、`pair` で測る） |
| drag の始め・終わり | — | 1 回ずつ窓の矩形の合成と焼き 1 回（＋copy 2 回） |
| dock・undock（120ms） | p007 の 2 quad の全画面のコマ | primary は止まり、拡大縮小は HVS。コマごとの GPU は帯（title bar と bar の pill）の描き直しだけ |
| 中身の変化 | 今どおり | 今どおり |

帯域（読み、60Hz、4 byte。全画面 1 枚: 800x480 で 92 MB/s、1024x600 で 147 MB/s、1920x1080 で 498 MB/s）。予算は未確認（1.6）、p003b の実測で決める。

| 組み合わせ | 1920x1080 | 1024x600 |
| --- | --- | --- |
| drag: primary＋窓（1080p は 1400x928 で 312 MB/s、1024x600 は 820x570 で 112 MB/s、影を含む）＋帯（bar、幅×64）＋cursor、console 省略 | 0.84 GB/s | 0.28 GB/s |
| 同じ、console あり（primary が opaque でない時） | 1.34 GB/s | 0.42 GB/s |
| 能力の最大の例: primary＋窓の plane（出力の大きさ、拡大だけ）＋帯（高さ 1/4）、console 省略 | 1.12 GB/s | 0.33 GB/s |
| 縦の縮小を許した最悪（窓の plane が出力の大きさで比の切り上げ 2〜3）＋帯（出力の大きさ）＋console | 2.5〜3.0 GB/s | 0.74〜0.88 GB/s |

- 能力（4.3 (5)）は実測の予算から mode ごとに決め、縦の縮小は予算に収まる mode だけ許す。
- 処理量: 1080p で primary 31M＋窓 20M＋帯 2M clock/s、core clock 500 MHz に十分収まる。
- memory: 窓の plane の画像 2 枚（出力の大きさ、1080p で 8.3 MB×2）、帯 2 枚（小さい）、offscreen は p007 の `KWL_LAYER_WINDOW`（8 MB）を流用、帯の offscreen（小さい）。plane は出力の open（park の中）で作り、close まで持つ。
- 隠れている plane は list に入らないので帯域 0（H3）。

## 7. 段（p003a 以降の Phase の案、Queue は Q1）

番号: p002 は ws.md の「規約の全文の見直し」のまま（最後に行う）。実装は p003 から。kernel の段は p003a（承認不要）と p003b（承認の後）に分ける（Q1、N9）。担当はどちらも同じ 1 人。

| Phase | 内容 | 依存（M7） | 試験 |
| --- | --- | --- | --- |
| p003a | kernel（core と bcm2711、**担当 1 人**、WS141 p016 B-k2 の範囲を含む。**UAPI の承認に依らない**、Q1 の N9）: display の op の gate 外し（`drv_gpu_display_ops` の flags と `DRV_GPU_DISPLAY_UNGATED`、spinlock の `table_lock` と pin、fence を lookup の前に、`adoption_wait`、DESTROY の pin の待ち）、状態から作る list と 3 slot の差し替え、primary も coalesce、settle・EBUSY の撤去、記録の時の `state_serial` と IRQ の `adopted_serial`・waitq、PRESENT・PRESENT_SYNC の採用の待ちを lock と pin の外で、cursor の op を状態の変更に（mutex を取らない、画像 5 枚、`cursor_named` を 3 slot で）、状態の項目の借りと thread の文脈の解放、lease の失効、copy の経路（2 枚のまま採用を待つ、N11）、clock（lease の間は下げない）、console の省略、`check_pipeline`・`publish_list`・attach の検査、underrun を止めずに数える、計数。外に見せる番号と WAIT は今のまま。新しい HVS の値を使えば 4.3 (8) の表に載せる | 無し（承認に依らない）。WS141 p016 B-k2 を単独で実装しないこと、WS141 p016 の受け入れ B が B-k2 を要る時に p003a に依ること（Q1 が WS141 に記録） | host: list の符号（primary・cursor・console の省略・順）、差し替えと coalesce（IRQ の模型で、採られる list が常に完全、3 slot が足りる、cursor の画像 5 枚が足りる）、`state_serial`・`adopted_serial` の単調と「以下が全部採用済み」、借りの解き方（1 つの buffer に画像 2 つ）、採用の待ちの眠りと起こし、lease の失効で待ちが EBUSY で返る、gate 外しと DESTROY の pin の待ち（deadlock しない）、cursor の op が primary の採用の待ちの間に進む（B-k2 の受け入れの量）、copy の経路が今かもしれない copy 先を待つ、ASan・UBSan。build warning 0。T1: i915・Venus の回帰（gate が変わらない）と Pi の今の desktop の回帰（primary・cursor の表示、EBUSY・ETIMEDOUT が出ない） |
| p003b | kernel（p003a と同じ担当、**UAPI の承認の後**）: UAPI（`GPU_DISPLAY_COMMIT`・`OVERLAY_INFO`・`HAS_OVERLAYS` と定数、WAIT を眠る形に、外に見せる番号を `state_serial` に）と core の検査（4.2 の EINVAL の場合を含む）、overlay（unscaled・scaled・LBM、幅 2048 の制限）、plane ごとの serial と DROPPED、wait・signal の fence、帯域の予算と ENOSPC、overlay の状態の lease の終わりの戻し、計数（`pair`、採用の時刻の輪）。終わりに値の出典の表と GPL の類似の監査（4.3 (8)）、Pi の帯域の実測（`hvs-load`） | p003a の clear（kernel が main に）、9 の 1（UAPI の承認） | host: overlay の list の符号（位置・clip・scaled の word・LBM・順）、予算と ENOSPC、COMMIT の原子性と一部 DROPPED、COMMIT_WAIT と WAIT の眠り・EINTR・失効、番号の意味（fence を待つ commit の後の fast path の番号が大きい）、UAPI の struct の大きさ、ASan・UBSan。build warning 0。T1: i915・Venus の回帰（WAIT が変わらない）。Pi（T1・Q1 経由）: `hvs-load` で plane の組み合わせごとの underrun（8） |
| p004 | libvulkan: overlay の plane の列挙と能力、overlay の surface と swapchain、隠す約束、保留・fast path・overlay の lane、plane ごとの serial、借りと acquire（WAIT を display_mutex の外で）、primary の COMMIT_WAIT、ENOSPC の扱い、log、README の規則 | p003b の clear（kernel が main に）、**WS141 p016 B-u の Pi の受け入れ**（fast path・ticket・lane が Pi で確かめられていること） | host: WSI の fixture（偽の display の transport）で、compositor と同じ usage（TRANSFER_DST だけ）の chain の fast path（why=）、保留と primary の 1 commit、**前のコマの primary が lane に残っている時の切り替え**（その primary は保留を取り込まない、N1）、保留がある plane への後続の present が保留を置き換え直に出ない、期限・WaitIdle・破棄の単独の flush、退役した chain の保留を捨てる、隠す、DROPPED、acquire の WAIT の間 display_mutex を持たない、保留を含む commit の ENOSPC で primary も出さず overlay の chain だけ OUT_OF_DATE（N10） |
| p005 | compositor: drag（焼き、入る・戻る、持ち主の移し、C の follow と `kwl_drag_place`、条件と fallback、中身の止め方、`--no-planes`、log）と上の帯の plane（bar だけ） | p004、**WS213 p004 の clear（whole-Phase）**、9 の 2・3 | host: 状態の遷移・持ち主の移し・条件（fake の Vulkan、TSan）、`kwl_drag_place` の単体（下限・slop・出力の外）。T1（i915・Venus で画が前と同じ）。Pi（8） |
| p006 | compositor: dock・undock・pull back の拡大縮小（帯に title bar の通り道）と undock の形 | p005、p003b の実測（9 の 5） | host・T1・Pi（8） |
| p002 | 規約の全文の見直し（WS が変えた C の全部）と、新しい HVS の code の GPL との類似の監査（2 回目） | p003a〜p006 | style-check、監査の記録 |

- 各段で build（rpi4・amd64・keiland-linux、warning 0）と変えた所の短い host 試験。QEMU と実機は T1 と Q1 経由（担当は QEMU を起動しない）。
- WS141 p015（UIF）への制約: overlay の swapchain の画像は linear のまま（1.2）。WS141 への伝達は Q1。

## 8. 試験

- host（各段、上の表）。kernel の list は `plan/ws141/tests/display-host-test.sh` と同じ形の試験を `plan/ws214/tests/` に置く。libvulkan の fixture は B-u の WSI の fixture と同じ形で、**compositor と同じ usage の chain**（TRANSFER_DST だけ）を使う（WS141 p016 u16 の教訓: usage の違う fixture では why=usage を見逃す）。回帰として残す物は WS の終わりに master.md の Tools に登録するか tests/ のシナリオへ（AGENTS.md の基準）。
- libvulkan の保留の fixture（p004、N1・N2）: 偽の display の transport で primary の job の GPU の完了を止められるようにし、(i) 前のコマの primary が lane に残っている時に切り替えを保留し、その primary が保留を取り込まず、次の primary が取り込む、(ii) 保留の後の同じ plane の present（C の fast path に当たる物）が保留を置き換え、kernel に直に出ない、置き換えられた画像がすぐ acquire できる、(iii) primary が来ない時に 2 refresh で単独の commit（`why=expired`）、(iv) WaitIdle・破棄・release の単独の flush、(v) 退役した chain の保留が commit に入らない、(vi) 保留を含む commit の ENOSPC で primary も出ない、を確かめる。
- **p003a の Pi の回帰**（T1・Q1 経由、UAPI に依らない）: 今の desktop で pointer を動かし続けながら窓の中身を描き続け（primary の present と cursor の op が重なる）、primary の present の EBUSY・ETIMEDOUT が 0、cursor の op が primary の採用の待ちの間に返る（B-k2 の受け入れの量、WS141 p016 の数え方）、underrun の数、差し替え・coalesce の数を flip の report で読む。i915・Venus は T1 の回帰（gate が変わらない）。
- **帯域の実測（p003b の終わり、Pi、T1・Q1 経由）**: 試験の道具 `hvs-load`（`plan/ws214/tests/`、試験だけが UAPI を直に呼ぶ）が display を claim し、primary（全画面）＋overlay の組み合わせ（大きさ、拡大・横の縮小・縦の縮小の比、console の有無）を 10 秒ずつ出し、flip の report の underrun の数を読む。800x480・1024x600・1920x1080 のうち Pi で選べる mode で。結果から予算と mode ごとの能力を決め、p003b に記録する。HVS が隠れた plane を読むかもここで確かめる（console の省略の効果）。
- **画素の比較（M5）**: 焼いた絵の正しさと premultiplied の影を確かめる。T1（i915・Venus でも焼きの経路は動かせる: plane が無くても `--plane-bake-test` で焼きと読み戻しだけを行う）と Pi で、同じ場面の (a) 合成の shot（`--no-planes`）と、(b) 窓を除いた primary の shot に読み戻した絵を CPU で premultiplied の over（out＝src＋dst×(1−a)）で重ねた物を比べ、差が各成分 ±2 以内。HVS の実際の出力は shot で取れないので、Pi では (b) の計算と、ユーザーの目視（入る・戻るの継ぎ目）で見る。
- T1（WS の終わりにまとめて）: i915（5330）・Venus（QEMU）で drag・dock・undock の shot が前と同じ、`KWL VULKAN_ERROR`・`KWL FAILED` なし、`KWL WPLANE` が出ない（plane が無い）。Linux の TSan の guest（WS213 の受け入れ 3 と同じ手順）。
- Pi（1 boot に場面をまとめる、v3d.perf=1、`--log-frames`）。受け入れは **kernel の採用**（`BCM2711_FLIP_CADENCE_OVERLAY`・`FLIP`、4.3 (9) の採用の時刻の輪）で判定する（compositor の present の間隔ではない）。採用の間隔は pointer の速さに依る（止まれば採用も無い）ので、判定は「入力の時刻から、その位置を含む list の採用までの遅れ」と「動いている区間（連続する 2 refresh に入力がある区間）だけの採用の間隔」で行う（軽微）:
  1. 窓（Settings・Files・Monitor）を 10 秒ずつ drag（今の mode と、可能ならもう 1 つ）。判定: 動いている区間の overlay の採用の間隔の中央値 ≤ 17ms・p90 ≤ 34ms、入力の時刻から採用までの遅れの p90 ≤ 34ms（2 refresh: C0 の間引き 1 回と次の frame の始め。p50 は記録）、drag の間の primary の submit 0（入る・戻るを除く。`KWL WPLANE` の計数で primary と帯の submit を分けて数える）、帯の submit は時計・status の変化の時だけ（1 秒に 1 回以下）、underrun 0、`pair` の割合（採用の時に数えた物、記録、努力目標）。
  2. bar へ drag して dock、double click で dock・undock、pull back。アニメの帯のコマ数（`KWL WPLANE anim`）が 120ms に 6 以上、underrun 0。
  3. drag の途中で通知を出す（条件を外れて戻る）、時計の分が変わる時刻をまたいで drag する（帯の描き直し）。
  4. 端で drag（下限・slop、`kwl_drag_place` と M の位置が一致）。判定（軽微）: (a) pointer を止めてから離した時、drop の後の窓の位置が C の最後の present の位置と同じ、(b) 動かしながら離した時、差が C0 の 1 回の間引きの間の pointer の移動以内（log の入力の時刻と位置で計算）、(c) pointer を止めた後、C の最後の present の位置が最後の入力の place と同じ（N8 の「最後の位置に必ず着く」）。
  5. ユーザーの目視: 窓と cursor が一緒に動くか、入る・戻るの継ぎ目（glass の色の替わり、中身の替わり）が気になるか。
- `--no-planes` で同じ場面を流し、今との比べにする。

## 9. 未決（ユーザー・Q1 の判断が要る物）

ユーザーに聞く物（Q1 がそのまま聞く。各項の後半がユーザー向けの短い説明）:

1. **UAPI の追加と WAIT の意味の変更の承認**（4.2）。
   - 説明: 「窓を画面の重ね合わせの板（HVS の plane）に載せるため、kernel に新しい命令を 2 つ（板の状態をまとめて変える commit、板の能力の問い合わせ）足し、既存の『表示の完了を待つ』命令（WAIT）を本当に待てる（眠る）形に変えます。使うのは libvulkan だけで、アプリや compositor は今どおり標準の Vulkan だけを使います。また bcm2711（Pi 4）では今の表示の命令の振る舞いも変わります（表示の命令が『使用中』（EBUSY）で失敗することが無くなり、WAIT が眠ります）。このうち EBUSY が無くなる所は今の命令の約束の範囲の直しなので、承認を待たずに先の段（p003a）で行います。承認いただけますか。」
2. **窓の glass のぼかしを操作の間は始めの場所のままにする**（5.6）。
   - 説明: 「窓をドラッグしている間、窓の半透明のガラスの部分（title bar など）は、ドラッグを始めた場所の背景のぼかしのまま一緒に動きます。ボタンを離すと今の場所の正しいぼかしに一瞬で替わります。背景の色が大きく違う所まで動かすと替わり目が見えます。これでよいですか。」（受け入れない時は title bar を別の板にして止まった時に焼き直す案を次の版で設計する）
3. **ドラッグの間、その窓の中身を止める**（5.2）。
   - 説明: 「窓をドラッグしている間、その窓の中身（動画や時計など）は止まり、離すと最新に戻ります。動画は離すまで止まり、離すと飛びます（音は続きます）。止めない案もありますが、中身が変わるたびに GPU が窓 1 枚分を描き直します。止める案でよいですか。」
4. **「窓の前後の入れ替え」をこの WS から外す**（5.9）。
   - 説明: 「窓をクリックして手前に出す操作はアニメが無く、今でも 1 回描くだけで軽いので、重ね合わせの板に載せても速くなりません。この WS の目標から外してよいですか。」
5. **undock（大きな窓を元の大きさに戻すアニメ）の形**（5.3）。
   - 説明: 「大きな窓を小さく戻すアニメで、板に縮小をさせると表示の帯域が大きく増えます。帯域の実測（kernel の段の終わり）の後に、(i) 始めの数コマが少しぼやける代わりに軽い形、(ii) 帯域に余裕のある画面の大きさでだけ縮小させる形、(iii) 今どおり GPU で描く形、から選んでいただきます。今は判断を保留でかまいません。」

Q1 の判断で決まった物・Q1 が行う物:

6. kernel の段は担当 1 人、WS141 p016 B-k2 の範囲を含む（Q1 の判断、2026-10-12）。review-2 の N9 で p003a（承認不要）と p003b（承認の後）に分けた（Q1、2026-10-12）。N6（clock は lease の間は下げない）、N10（失敗した保留は primary ごと捨てて前の状態を保つ）、N11（copy の経路は 2 枚のまま採用を待つ）も Q1 の判断。B-k2 の側の穴（4.3 (3)）の WS141 への伝達、WS213 の不変条件の変更（5.8）の WS213 への伝達、WS141 p015 の linear の制約（1.2）の伝達は Q1。
7. libvulkan の原子性の規則（4.1）は p004 で README に書く（実装の定め、仕様の外。Mesa には無い）。
8. 値の出典の表で Linux だけが出典の値の扱い（4.3 (8)）は Q1 が WS141 の決定を当てはめる。

## 10. 確かめていない事（この版の限り）

- Pi の実機・QEMU は何もしていない。帯域の予算、隠れた plane の読み、underrun は p003b の実測で確かめる。
- 「次の list」の register の差し替えで HVS が常に完全な list の一方を採ること（frame の始めの直前の書き込み）は、p003a の host の模型と Pi の差し替えの数・underrun・画面の乱れの無さで確かめる。確かめられなければ差し替えをやめて coalesce だけにする（窓と cursor が 1 refresh ずれることがある）。
- Vulkan の仕様の本文（VK_KHR_display_swapchain）で、`VkDisplayPresentInfoKHR` の適用の範囲と複数の swapchain の present の同時性の記述は reference page の範囲でしか確かめていない。4.1 の規則は zedBSD の実装の定めとして扱う。
- HVS5 の scaled の plane の word の正確な並び、LBM の式は p003b で出典の表（4.3 (8)）に記録して確かめる。
- bar の glass が下の窓に依らないこと（5.4）、窓の popup・sheet・sub-surface の条件（5.5）は p005 で source を読み直して確かめる。
- OUT_OF_DATE（ENOSPC）の時の compositor の戻り方の見た目、焼いた影の alpha の画素は p005 の試験（8 の画素の比較）で確かめる。
- p007（dock の絵）と p008（damage）の Pi の結果は未着。数字は Q1 の 2026-10-12 の値。
- review-2 の「未確認」（この版でも確かめていない）: HVS の「次の list」の register の 32 bit の書き込みの原子性と、vblank・frame の始めでの latch の時点（p003a の Pi の回帰で差し替えの数と画面の乱れの無さを見る）。C0 の間引きの実装と、I の位置と M の `pointer_x` の一致（p005 の始めに source で、`kwl_drag_place` を M と C が同じ入力で呼べるか）。i915・Venus の `output_wait` の返り値（4.2 で「WAIT の振る舞いは変わらない」と書いた前提、p003b の始めに source で）。Pi が今 1 core か（4.1 の理由の前提の 1 つ。複数 core でも規則 2 は呼び出しの順序だけで決まるので結論は変わらない）。Vulkan の仕様の本文（上の項）。
- N10 の「出なかった primary の画像で R の damage が狂わない」こと（R の damage が画像ごとの中身で数えているか）は p005 の始めに WS211 p008 の source で確かめる。狂うなら、捨てた時に R へ「全画面の damage」を知らせる。
- 保留の期限の 2 refresh が、R の「切り替えの present → primary の present」の間（同じ thread で続けて呼ぶ、通常は µs）に対して十分か、Pi の 1 core で R が M に preempt された時を含めて p004・p005 の log（`why=expired` が出ないこと）で確かめる。
- kernel の採用の時刻（1 ms の scheduler の時計）と compositor の入力の時刻（`kwl_microseconds`）が同じ時計の上か、ずれをどう合わせるかは p005 で決める（8 の遅れの判定の前提）。
