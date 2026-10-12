<!-- awesome-plan project=zedbsd record=ws214-p001 -->

# ws214-p001: 調べと設計（HVS の plane の能力と制約、口、compositor の載せ方、fallback、試験）

Parent: [WS214](../ws.md)
Status: planning（設計の第 2 版、2026-10-12 P2。[review-1](review-1.md) を反映、再 review の前。コードは書いていない）
Phase disposition: normal
Queue: P2（2026-10-12 Q1 の割り当て）

## review-1 への対応

[review-1](review-1.md)（design-reviewer、判定「p003 以降に進めない」）の指摘と、この版での直し。

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
2. **口**: compositor は今どおり標準の Vulkan だけ（guardrail）。libvulkan が **VK_KHR_display の overlay の plane** を列挙し、位置と拡大縮小は `VkDisplayPresentInfoKHR` の `srcRect`・`dstRect`、順は plane の stack の順。**present は plane ごとに 1 回**。primary と overlay を同じ refresh に揃えるのは libvulkan の規則（見える・隠れるの切り替えは次の primary の present と 1 つの commit に）と kernel の commit。隠すのは 1×1 の present（libvulkan の約束）。kernel には新しい UAPI（overlay の commit と能力の問い合わせ、WAIT の意味の変更）が要る → **ユーザーの承認が要る**（差分の案は 4.2、適用しない）。HAL は変えない。
3. **kernel（bcm2711）**: display list を「状態（primary・overlay・cursor）から作る」形にし、primary も含めて coalesce する。settle・EBUSY・mutex の下の採用の待ちをやめ、IRQ が採用を記録して waitq で起こす。display の op は core の session の gate からも controller の mutex からも外す（WS141 p016 B-k2 を含む）。scaled の plane（PPF・TPZ・LBM）、借りの持ち方、帯域と clock の予算、console の省略。1 つの Phase（p003）で担当 1 人。
4. **compositor**: R が絵を offscreen に焼いて overlay の swapchain（TRANSFER_DST だけ）へ copy し、切り替えの present を出す。drag の移動は窓の plane の持ち主を C に移して C が pointer の箱から動かす（位置だけの fast path、GPU 0）。条件を外れたら今どおりの合成（fallback）。i915・Venus・Linux・FreeBSD は overlay の plane が無いので今と同じ。
5. **見た目の妥協（ユーザーの判断が要る、9）**: 操作の間、窓の glass の blur は**始めの場所のまま**、窓の中身は**止まる**。drop で正しい blur・中身に戻る。undock の拡大縮小の形は帯域の実測の後に決める。

## 1. 調べ: bcm2711 の HVS（HVS5）の plane

出典: Linux 6.19 の vc4（`drivers/gpu/drm/vc4/vc4_plane.c`・`vc4_hvs.c`・`vc4_kms.c`・`vc4_regs.h`、`plan/ws141/temp/linux`、GPL のため理解だけに使い、文書と code に写さない＝WS141 のライセンスの規則）、zedBSD の `src/drivers/gpu/bcm2711/display-*.c`、Vulkan の公開の reference page（`VkDisplayPresentInfoKHR`）。Pi 実機での確かめはしていない。

### 1.1 plane の数と display list

- HVS には固定の数の plane の register は無い。plane は display list（SRAM、HVS の register の窓の後半 16 KiB＝4096 word、zedBSD は `0x4000` から）の中の「plane の word の列」で、数の上限は SRAM の大きさと、1 line の間に HVS が処理できる量（core clock）と、帯域で決まる。Linux は overlay を 16 枚＋primary＋cursor として見せている（`VC4_NUM_OVERLAY_PLANES`）。
- list は plane の列と終わりの word。順は list の中の順（後ろほど上）。channel ごとに「次の list」の register（channel 0 は `0x20`）に先頭の位置を書くと、HVS は次の frame の始めにそれを「今の list」（`0x30`）にする。plane の word の一部（context）は HVS が走査中に書くので、動いている list を書き換えずに新しい list を空いた場所に作る（zedBSD の今の方式）。「次の list」の register は 32 bit の 1 回の書き込みなので、まだ採られていない次の list を別の完全な list に差し替えても、HVS はどちらか一方の完全な list を採る（4.3 (2) の置き換えの前提。Pi で確かめる、10）。
- zedBSD の今の SRAM の使い方: 0〜31 firmware、32〜42 拡大縮小の filter（R0 で書いて常駐）、43〜51 console の list、64・80 primary だけの list の 2 slot、128・160 合成の list の 2 slot（各 32 word）。

### 1.2 format・tiling

- RGB の 32 bit（zedBSD は BGRA8888・RGBA8888）、linear。V3D 4.2 の UIF は読めない。plane に出す画像は linear で描く。[WS141 p015](../../ws141/phase015/phase.md)（GPU が書いて GPU が読む画像を UIF に）は、overlay の swapchain の画像を linear のままにする必要がある（この WS の制約、WS141 への伝達は Q1）。
- 1 行の pitch は 65535 byte まで、幅・高さは 4096 まで（今の driver の検査）。pointer は byte 単位、zedBSD は 4 byte の整列を求めている。

### 1.3 拡大縮小

- plane ごとに独立に拡大縮小できる。縦横それぞれ、同じ大きさなら無し、縮小が 2/3 より緩いか拡大なら PPF（多相の filter、SRAM の kernel を使う）、2/3 より強い縮小なら TPZ。
- 拡大縮小する plane は list の word が増える（unscaled の 8 word に対して最大で約 18 word）。正確な並びは p003 で出典の表（4.3 (8)）に記録する。
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

- 読みの帯域: Linux は全 plane の和を 1.5 GB/s までに制限する。ただしこれは BCM2835 の頃の load tracker の値で、BCM2711 に当てはまるかは確かめていない（review-1 M1）。この設計では予算を「未確認」とし、p003 の終わりの Pi の実測（`hvs-load`、8）で決める。HVS は隠れた plane も読む（重なりで省かない、と Linux の計算は扱っている。実測で確かめる）。
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
2. overlay の plane の present のうち、**見える・隠れるの切り替え**（隠れている plane を出す、出ている plane を隠す）の物は、kernel に出さずに display ごとの「保留」に入れ、**同じ display の次の primary の present** の commit に入れる。primary の present の前に出た保留は全部、その primary と 1 つの commit（1 つの list）になり、同じ refresh から見える。同じ plane の保留は後の物が前の物を置き換える。
3. 切り替えでない overlay の present（出ている plane の位置・画像の変更）は保留しない。画像の中身が前の commit から変わっていない（GPU の書き込みが無い）物は呼んだ thread で直に commit（位置だけの fast path）、新しい中身の物は overlay の lane で GPU の完了を待ってから commit。
4. よって R は「切り替えの overlay の present → primary の present」の順に出す。逆に出すと、切り替えは次の primary（次のコマ）まで遅れる（README に書く）。

規則 2 を「primary の present が kernel に届く前に出た overlay の present を全部まとめる」（review-1 の例）にしない理由: Pi は今 1 core で R が M に preempt されうるので「primary が kernel に届く前」は時刻の競争になり決まらない。また C の位置だけの present（drag の移動）まで R の primary の GPU の完了（1080p で 61ms）を待たされる。「切り替えだけを次の primary に付ける」なら順序だけで決まり、C の移動は待たない。切り替えは必ず primary の描き直しと組になる（窓が primary と plane の間を移るので）ので、compositor の側の不便は無い。

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
 * the primary is the whole output, unscaled.  serial increases for each
 * state of the plane; a state with a serial at or below the plane's last
 * applied one changes nothing and reports DROPPED.  wait_fd (-1 for none)
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
 * their state.  All named states are recorded at once and every display list
 * made afterwards shows them together, from one refresh boundary.  The call
 * does not wait for the boundary unless COMMIT_WAIT is set (allowed only
 * when the primary is named): then it returns after a list made from this
 * commit or a later one is adopted, and signals signal_fd (-1 for none) at
 * that adoption.  sequence (output) numbers this commit in the lease;
 * adopted (output) is the highest sequence adopted when the call returned.
 * ENOSPC: the planes would exceed the output's bandwidth or the overlays'
 * limits; nothing changed.  ESTALE for an old generation, EBUSY when the
 * lease is not this open's, ENXIO when disconnected, ETIMEDOUT when the
 * adoption did not come (the output is then recovered as after a fault).
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
 * Waits until the commit or present numbered sequence in this lease has been
 * adopted, at most timeout_ns (one call waits at most a second; zero answers
 * at once).  completed_sequence is the highest adopted number; images that
 * only earlier commits named and the adopted list no longer names are no
 * longer borrowed when it returns.  EAGAIN when timeout_ns is zero and the
 * sequence is not yet adopted, ETIMEDOUT when the time ran out.
 */
```

- 同期の backend（i915・Venus）では番号は present が返った時に採用済みなので、WAIT の振る舞いは変わらない。WAIT が眠るのは、bcm2711 で COMMIT_WAIT なしに出した commit（overlay の変更）の番号を待つ時だけ。

- 形の要点:
  - primary も `gpu_display_plane_state` の 1 つ（plane 0）。`gpu_display_present` を埋めないので lease・generation・sequence を二重に持たない（review-1 M2）。primary は BLOB（共有の allocation）だけ。copy の経路（ordinary の storage）は今の `GPU_DISPLAY_PRESENT` に残る。
  - fence: plane ごとに wait（画像の GPU の書き込みの完了）、commit に signal（COMMIT_WAIT の時、採用で）。今の PRESENT_SYNC の wait・signal と同じ役。
  - plane ごとの serial: libvulkan の fast path（呼んだ thread）と overlay の lane（worker）が同じ plane の commit を前後して出しても、古い物が新しい物を上書きしない（B-u の ticket と同じ役を kernel で）。DROPPED の画像は借りられない。
  - src は画像の中なので uint32。dst も出力の中だけを許す（端の clip は compositor が src で行う、cursor の負の位置と違う）ので uint32。
  - ENOSPC は「何も変えない」。libvulkan の扱いは 4.4 (6)。
  - cursor は commit に入れない: cursor の SET は kernel の 4 枚の画像に写す形（p014）で、画像を借りる overlay と借りの扱いが違う。C は窓と cursor を別の present で出す（F1）ので、1 つの commit に入れる呼び手が無い。同じ refresh は kernel の list の差し替え（4.3 (2)）で狙う。
  - 大きさ: `gpu_display_plane_state` 96 byte、`gpu_display_commit` 552 byte、`gpu_display_overlay_info` 56 byte（p003 で `_Static_assert` で確かめる）。番号 44・45、flag 2048 は今空いている（review-1 で確認済み）。
- `gpu_display_info.plane_count` の意味: 今 1、overlay ありで 1＋K（bcm2711 は K＝2: 窓と上の帯）。libvulkan の cursor の plane は今どおり最後に 1 つ足す。
- kernel の内部の口（`include/drivers/gpu/gpu-display.h` の `struct drv_gpu_display_ops`）に任意の `commit`・`overlay_info` と、gate を通らないことの宣言 `DRV_GPU_DISPLAY_UNGATED`（ops の flags）を足す。NULL なら core が EOPNOTSUPP、i915・Venus は NULL で今どおり。HAL（`include/hal/hal.h`・`src/hal/`）は変えない。

### 4.3 kernel（core の `gpu.c`、bcm2711 の `display-flip.c`・`display-device.c`）

**(1) gate から外す（H1）**

- `DRV_GPU_DISPLAY_UNGATED` を宣言した backend では、`GPU_DISPLAY_COMMIT`・`PRESENT`・`PRESENT_SYNC`・`WAIT`・`CURSOR_SET`・`CURSOR_MOVE`・`CURSOR_HIDE`・`OVERLAY_INFO` は `gpu_session_enter` を通らない。
- 代わりに session に短い lock（`table_lock`、mutex）を足し、handle の表の変更（作成・破棄・import）と、これらの op の handle の引きを守る。引いた resource には pin（数）を付けて lock を放し、backend の呼び出しの間だけ持つ。`GPU_RESOURCE_DESTROY` は pin が 0 になるまで waitq で待つ（backend の呼び出しは µs〜ms で、借りは backend が自分の buffer の参照で持つので、呼び出しの後は pin が要らない）。
- device の撤去・session の error の検査は op の始めに今の gate と同じ条件で行う。
- backend の側（bcm2711）: overlay・cursor・primary の commit は controller の mutex を取らない。lease の判定（`controller->owner`・`session->lease`）は、claim・release が controller の mutex → plane の lock（下）の順で写す「今の持ち主と lease」の写しを plane の lock の下で読む（B-k2 の案と同じ）。cursor の SET の行の写し（16 KiB まで）も plane の lock（mutex）の下。

**(2) 状態から作る list と差し替え（H2）**

- flip の状態に「primary（画像・serial）、overlay[K]（画像・src・dst・shown・serial）、cursor」と、状態の版（`state_serial`、commit ごとに増える）を持つ。どの変更も「状態を書き、`publish_locked`」。
- `publish_locked`（guard の spinlock の下、IRQ からも呼ぶ）: 状態の全部から空いた slot に list を書いて「次の list」に載せる。slot は 3 つ（各 256 word、SRAM の 256 から）。
  - まだ採られていない list が 1 つあり、その後に差し替えをしていなければ、その list を**差し替える**（3 つ目の slot に書き、「次の list」の register を書き直す）。HVS がその間に前の list を採っていれば、新しい list は次の frame で採られる。どちらでも完全な list しか採られない（1.1）。
  - 差し替えを既にした後（採られたかどうか分からない list が 2 つ）は書かずに dirty のままにし、採用の IRQ で書く（coalesce）。「今の list かもしれない」slot は常に 2 つ以下なので 3 slot で足りる。
- primary の状態が無い（console だけの list の）間の overlay の commit は EINVAL（overlay は primary の上にだけ出す）。`check_pipeline` の回復の時に許す list の位置も 3 slot に直す。
- 採用の IRQ（`bcm2711_display_flip_vblank_locked`）: 「今の list」の register から採られた slot を知り、それより古い list を退役にし、採られた list の `state_serial` を `adopted_serial` に記録し、waitq を起こし（`waitq_wake_all`、v3d の IRQ と同じ使い方 `v3d-hardware.c:812`）、dirty なら `publish_locked`。IRQ の中では借りを解かず、clock・cache も触らない。
- settle と EBUSY をやめる。primary の commit も状態を書いて `publish_locked` するだけ。COMMIT_WAIT の caller は lock を持たずに waitq で「`adopted_serial` ≥ 自分の commit の版」まで眠る（上限 100ms）。その間も overlay・cursor の commit は進み、その list は今の primary を含む（「自分の primary の世代以上を含む list」の採用＝review-1 H2 の待ち）。primary の caller は 1 つの lease に 1 人（libvulkan の worker）なので、primary は 1 refresh に 1 回より速く変わらない（FIFO）。
- 採られない（100ms）: 待っていた caller か次の commit が気づき、今と同じく uncertain にして借りを全部持ったままにする。その lease の commit は ETIMEDOUT、回復は今の fault の経路（console の list に戻す）。
- `GPU_DISPLAY_PRESENT`（bcm2711）は「primary だけの COMMIT_WAIT」と同じ実装を通る（copy の経路は今の copy の buffer に写してから primary の状態に）。cursor の SET・MOVE・HIDE も状態の変更で、今の `publish_cursor_locked` と `base_words` の写しは不要になる。

**(3) WS141 p016 B-k2 との関係**

- B-k2 の案（`plan/ws141/phase016/phase.md` 11 節）は cursor の op を controller の mutex から外すだけで、(a) core の session の gate（PRESENT_SYNC が busy の間 cursor の op は gate で待つ、H1）と、(b) settle・EBUSY（mutex の外の cursor の publish が採用の IRQ で次の list を出し続けると、primary の settle が 100ms で uncertain になり、`submit_frame` は pending で EBUSY、H2）が残る。B-k2 をそのまま実装すると、pointer を動かし続ける間 primary の present が ETIMEDOUT・EBUSY で失敗しうる。
- よって B-k2 は単独で実装せず、この p003 の (1)・(2) に含める（Q1 の判断: kernel の段は 1 つの Phase、担当 1 人）。WS141 p016 への伝達は Q1。

**(4) 借りと解放（H6、軽微）**

- 借り（`bcm2711_buffer_retain`）は slot ではなく**状態の項目**が持つ。commit が画像を名指すと、その thread の文脈で retain して状態の項目に付ける。状態の項目が置き換えられたら、前の項目の借りを「退役待ち」の list に、置き換えた版の番号と一緒に移す。
- 退役待ちの借りは `adopted_serial` ≥ 置き換えた版 になったら解ける（その版より後の状態から作った list が採られれば、前の画像を名指す list はもう今の list でない）。物理アドレスで slot の間を移す今の処理（`display-device.c:1611-1618`）は要らない。1 つの buffer に画像が複数あっても、借りは項目ごとの参照の数なので正しい。
- 解くのは thread の文脈だけ: COMMIT_WAIT の caller が起きた時、次の commit の始め、WAIT の caller が起きた時、release・fault・restore。IRQ は数を記録して起こすだけ。
- WAIT（bcm2711）: lease の判定の後、`adopted_serial` ≥ 頼まれた番号 まで waitq で眠り（上限 timeout_ns、1 秒まで）、起きたら退役待ちを解いてから返す。COMMIT と PRESENT の番号は lease の中で 1 本の数。
- signal の fence: COMMIT_WAIT の caller が起きた時（thread の文脈）に signal する。採られなかった（ETIMEDOUT）時は error 付きで signal する（今の PRESENT_SYNC の失敗と同じ）。

**(5) 帯域・clock・cache（M1・M6）**

- commit の受け付け（thread の文脈）で、新しい状態の全 plane の読み（src の幅×高さ×4×refresh×縦の縮小の比の切り上げ）の和が予算を超えるか、overlay の能力を外れれば ENOSPC で何も変えない。
- 予算は p003 の終わりの Pi の実測（8 の `hvs-load`）で決める。実測の前の値は仮に Linux と同じ 1.5 GB/s とし、実測で置き換えるまで p005・p006 の Pi の受け入れに進めない。overlay の能力（`max_src`・`max_dst`・縦の縮小の可否）は「primary＋全 overlay を能力の最大で、console は省いた時」が予算に収まるように出力の mode ごとに決める。compositor が能力の中で頼む限り ENOSPC は起きない。
- core clock: 新しい状態の要る clock が今より高ければ、状態を記録する**前**に thread の文脈で mailbox で上げる（眠る、clock 用の mutex で直列）。記録は guard の下で「今の clock ≥ 要る clock」を確かめてから行い、足りなければ上げ直す。下げるのは thread の文脈（COMMIT_WAIT の caller が起きた後、release）だけで、採られた状態と記録された状態の両方が要らなくなった時。IRQ の再発行（coalesce）は記録済みの状態だけを使うので、clock は既に足りている。
- cache: overlay の画像は uncached の native の storage だけを受け付ける（cached なら EINVAL）。primary の BLOB も uncached。primary の copy の経路は写した後に thread の文脈で clean。IRQ の再発行は clean しない。console の text の clean は commit の thread の文脈で行い、console は opaque な全画面の primary の下では list に入らないので、IRQ の再発行で古い text が出る心配は見た目だけ（その場合も console は primary の下）。

**(6) console の省略と lease の終わり（軽微）**

- primary が opaque（固定の alpha）で出力の全体を覆う時は console の plane を list に入れない。
- claim・release・fault・restore（`cursor_reset` に当たる所）で overlay の状態を全部隠した状態に戻し、serial を 0 にし、借りを退役待ちに移す。console の list の採用で全部を解く（今と同じ）。

**(7) scaled の plane**

- PPF・TPZ の選び方、係数の word、LBM の区画（overlay ごとに固定の区画、幅 2048 まで 8192 word を K 枚分）、filter は SRAM に常駐の物を指す。値と名前は WS141 の規則（値は事実、名前は独自、配置・comment・構造を写さない）と (8) の表。

**(8) 値の出典の表（M4）**

- p003 は新しく使う HVS の値ごとに、次の表を `plan/ws214/phase003/hvs-values.md` に作って commit する（GPL の文と code は写さない。値・独自の名前・出典の場所・確かめ方だけ）。

| 列 | 中身 |
| --- | --- |
| 値 | 例: plane の control word の scaled の bit、PPF・TPZ の係数の word の並び、LBM の word の式、「次の list」の register の offset |
| zedBSD の名前 | 一括で付けた独自の名前（WS141 の規則 2） |
| 出典 | (a) zedBSD の既存の code（WS141 で監査済み）、(b) Broadcom・Raspberry Pi の公開の文書、(c) Pi の実測、(d) Linux 6.19 vc4 の読み（file と行、理解だけ） |
| 確かめ | host の試験の名前、Pi の実測の結果 |

- (d) だけが出典の値は、WS141 の 2026-10-04 のユーザーの決定（register の値は事実として使い、名前は独自に改名し、配置・comment・構造は写さない、`plan/ws141/rpi4-gpu-license-audit.md` の 5）の範囲で使う。当てはめは Q1。
- p003 の終わりに、変えた display の file の GPL との類似の監査（字面・設計、WS141 p007 の道具と目視）を行い、結果を表と一緒に残す。WS の最後の p002 でもう一度行う。

**(9) 計数**

- v3d.perf=1 の flip の report に: commit の数（primary・overlay・cursor 別）、coalesce の数、差し替えの数、underrun の数、`pair`（同じ list で窓の plane と cursor が一緒に変わった数／窓の plane の移動の数）、cadence に `BCM2711_FLIP_CADENCE_OVERLAY`（overlay の位置の変更を含む list の採用の間隔）を足す。

### 4.4 libvulkan（`userland/desktop/libvulkan/wsi-display*.c`・`wsi-swapchain.c`）

1. **列挙**: `GPU_DISPLAY_HAS_OVERLAYS` の出力では plane を primary 0、overlay 1〜K、（cursor）の順に列挙し、stack の順も同じ。`vkGetDisplayPlaneCapabilitiesKHR`（overlay）: supportedAlpha は PER_PIXEL_PREMULTIPLIED、src・dst の範囲は `GPU_DISPLAY_OVERLAY_INFO`、minDstPosition は (0,0)、maxDstPosition は出力の大きさ − 1。cursor の plane の見分け（最大 64、負の位置）と重ならない。
2. **隠す（H3）**: overlay の chain の present で `srcRect` と `dstRect` がともに offset (0,0)・extent 1×1 なら、その plane を隠す（`SHOWN` を落とした状態）。画像は名指されず借りられない。swapchain は壊さない。README に約束として書く（1×1 の overlay を本当に出す用途は無いものとする）。
3. **present の道（F1）**: overlay の chain の present は 1 つずつ。4.1 の規則で 3 つに分ける。
   - **保留**（見える・隠れるの切り替え）: display ごとの保留の表に plane の状態（画像・矩形・serial・wait の fence）を置き、present はすぐ返る。次の primary の present の job（ordinary の lane）が、kernel に出す時に保留を全部取り込んで 1 回の `GPU_DISPLAY_COMMIT`（COMMIT_WAIT、plane ごとの wait の fence）にする。
   - **fast path**（出ている plane の位置・画像の変更で、画像が「書き込みの完了を libvulkan が前に確かめた時」から GPU に書かれていない）: 呼んだ thread で `GPU_DISPLAY_COMMIT`（wait なし、COMMIT_WAIT なし）だけ。worker・GPU・queue の mutex・display_mutex を通らない（B-u の cursor の fast path と同じ規則）。判定は B-u の `content_serial`（`transfer_only` の chain だけ、H4）。焼いた後に一度も present していない 2 枚目は、最初の 1 回だけ overlay の lane を通る（fence は既に済んでいるので待ちは短い）。
   - **overlay の lane**（出ている plane の新しい中身）: cursor の lane と同じく ordinary の lane の後ろに並ばない worker が、wait の fence を付けて commit。
4. **serial**: plane ごとの present の番号（libvulkan が plane ごとに持ち、swapchain を作り直しても増え続ける。kernel は lease の始めに 0 に戻す）を plane の状態の serial にする。DROPPED の present は B-u と同じく DROPPED の結果（画像はすぐ空き）。
5. **借りと acquire（H6）**: 画像の holds は「commit が名指した数」で、置き換えた commit の番号を画像に記録する。COMMIT が返す `adopted` 以下の番号の置き換えはその場で holds を減らす。acquire で空きが無ければ、置き換えの番号で `GPU_DISPLAY_WAIT`（timeout は acquire の timeout）を**display_mutex を持たずに**呼び、返ったら holds を減らす。primary の COMMIT_WAIT の ioctl の間も display_mutex を持たない（holds の更新の前後だけ持つ）。
6. **ENOSPC（M2）**: 保留を含む commit が ENOSPC なら、保留の overlay の状態を外して primary だけで commit し直し、外した overlay の chain にだけ `VK_ERROR_OUT_OF_DATE_KHR` を返す（次の present・acquire で）。primary の chain は作り直させない。overlay の lane・fast path の ENOSPC はその chain だけ OUT_OF_DATE。primary の commit が他の理由（ESTALE など）で失敗した時は、取り込んだ保留の overlay の chain にも同じ結果を返す。能力の中で頼む限り起きない（4.3 (5)）ので、起きたら `WSI OVERLAY ... why=nospace` を log に出す（compositor の誤りか予算の誤り）。
7. **画像**: overlay の display surface の swapchain は primary の claim を借りる（cursor と同じ）。画像は native の共有の画像（linear、連続、1 GiB より下、uncached）。2 枚（FIFO）、overlay の surface の minImageCount は 1（2 枚を同時に acquire できる）。
8. **log**: `WSI OVERLAY present plane= ticket= path=held|fast|lane|commit why= result= adopted= call_ns=`（why は B-u の `why=` と同じ語: usage・result・next・changed・nospace）。

### 4.5 HAL・UAPI の承認

- HAL の API・責務: 変更なし。
- UAPI（`include/uapi/gpu-display.h`）: 追加（COMMIT・OVERLAY_INFO・HAS_OVERLAYS と定数）と WAIT の意味の変更（眠る、借りの解放）。**ユーザーの承認が要る**（9 の 1）。承認の前は p003 以降の実装に入らない。

## 5. compositor（`userland/desktop/wayland/`）

### 5.1 状態（窓 1 枚ごと、M が持つ）

`COMPOSED` → `BAKING`（R が焼く。画面は今どおり合成）→ `ON_PLANE`（primary は窓と bar を除く、窓の plane の持ち主は C）→ `RETURNING`（持ち主を R に戻し、R が窓を含む primary を描く）→ `COMPOSED`。dock のアニメの間は `ANIM`（持ち主は R）。どの状態からも条件を外れたら `RETURNING`。

- **焼き**（R の job、want の種類に「plane の絵」を足す。H4）: R は窓（影・title bar・body・panel・sub-surface）を専用の offscreen（p007 の `KWL_LAYER_WINDOW` の memory、出力の大きさ、COLOR_ATTACHMENT）に**画面と同じ座標で**描き（glass が正しい場所の blur を取る）、窓の矩形（影を含む）を窓の plane の swapchain の 2 枚の画像へ copy する（同じ job、2 回の `vkCmdCopyImage`）。swapchain は usage が TRANSFER_DST だけ（`transfer_only`、fast path に入る）。上の帯も同じく小さな offscreen に bar を描いて帯の swapchain（TRANSFER_DST だけ）の 2 枚へ copy。
- **入る**（R）: 窓の plane の present（shown、`srcRect`＝`dstRect`＝窓の矩形、切り替え→保留）→ 上の帯の present（shown、切り替え→保留）→ primary を「窓と bar を除いて」描いた present（damage は窓の矩形＋reach と bar の帯）。libvulkan が 3 つを 1 つの commit にする（4.1）。primary の walk は窓を飛ばす（p007 の `layer_skip` の印）。R は 2 枚目の画像を acquire したまま持ち主の移し（5.8）で C に渡す。
- **戻る**（R、持ち主が R に戻った後）: 窓の plane の隠す present（1×1、切り替え→保留）→ 上の帯の隠す present → primary を窓と bar を含めて描いた present（damage は窓の今の矩形＋reach と bar）。隠す present には C から返された（C が acquire したままの）画像を使う（無ければ acquire、空きは採用の後、最大 1 refresh）。

### 5.2 drag

- 始め: 移動が始まった時（`server->drag` の窓が初めて動いた pass、slop を越えた後）に焼きを頼む。焼けるまでの数コマは今の合成の drag で、焼けた所で入る。
- 移動の計算（M3）: 今の M の計算（`shell.c:10118-10150`）を純粋な関数 `kwl_drag_place(const struct kwl_drag_rule *rule, int32_t pointer_x, int32_t pointer_y, struct kwl_drag_place *place)` に切り出し、M と C が同じ関数を使う。rule は M が書く写し: 掴んだ点からのずれ（`drag_dx/dy`）、y の下限（出力の上端＋`kwl_output_top`）、double click の tap の状態（`kwl_title_tap_still` に要る値の写し、BUG-265）、窓の出力の矩形。place は窓の左上と「動かない（slop の中）」「出力の外へ出た」の印。
- 移動: M は窓の位置（`surface->x/y`）を今どおり I の FIFO の event から同じ関数で更新するが、`drag_damage` を出さない（`ON_PLANE` の間）。C は受け箱の rule と I の位置から place を計算し、窓の plane の present（`dstRect`＝place、出力の端で clip した `srcRect`、位置だけの fast path）と cursor の present を続けて出す。present の間引き（C0、refresh − 2.5ms）はそのまま。M と C は同じ入力に同じ関数を使うので、drop の時の M の位置は C の最後の位置と同じ。
- pointer が別の出力に移った（place の印）: C は従うのをやめ、M は `RETURNING`（M は今どおり `kwl_window_set_output` で窓を移す。出力が 1 つの条件 5.5 の下では起きないが、heads の出力の端で起きる）。
- 中身: drag の間はその窓の client の更新を見せない（frame callback を drop まで止める）。**ユーザーの判断が要る**（9 の 3）。受け入れない時の案: R が commit ごとに offscreen に焼き直して fence と版を受け箱に置き、C が次の present の前に自分の command pool で offscreen から acquire した画像へ copy する（持ち主の移しは要らない。GPU は窓 1 回分＋copy）。
- 終わり（button を離す）: `RETURNING`。bar への drop（dock）は 5.3 へ続く（窓は plane のまま、持ち主は R へ）。

### 5.3 dock・undock・pull back のアニメ

- アニメの間、R が窓の plane の `dstRect` をアニメの矩形にする（HVS が拡大縮小、overlay の lane か fast path）。dock・pull back（小→大）は始めの絵を拡大。
- dock の始めの焼き直し（軽微）: drag の絵は title bar を含むが dock の絵は含まないので、アニメの始めに offscreen に焼き直して **2 枚とも**に copy する（p007 の最初のコマと同じ費用）。出ている方の画像は HVS が読んでいるので直に書かない: 空いている方に copy して present（overlay の lane）し、その採用で空いたもう一方にも同じ offscreen から copy してから使う。アニメの最初の 2 コマが copy 付き、以後は 2 枚を交互に位置だけで出す。drag の焼き（`BAKING`）の時は plane が隠れていて 2 枚とも空いているので、1 つの job で 2 枚に copy できる。上の帯は R が毎回 offscreen から acquire した方へ copy する（帯 1 本、小さい）。
- undock（大→小）の形は p003 の帯域の実測の後に決める（9 の 5）。案 (i) 終わりの大きさで焼いた絵を拡大で出す（縮小をさせない、始めが少し柔らかい）、案 (ii) 実測の予算に収まる mode では縦の縮小を HVS にさせる、案 (iii) 収まらない mode は p007 の GPU の経路。
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
  - park: 今どおり R と C の両方を止める。plane（窓・帯）の swapchain の作成・破棄は park の中で M（cursor の plane と同じ）。park に入る時に `ON_PLANE` なら先に `RETURNING` を済ませるか、park の中で M が両方の plane を隠す。
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

帯域（読み、60Hz、4 byte。全画面 1 枚: 800x480 で 92 MB/s、1024x600 で 147 MB/s、1920x1080 で 498 MB/s）。予算は未確認（1.6）、p003 の実測で決める。

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

## 7. 段（p003 以降の Phase の案、Queue は Q1）

番号: p002 は ws.md の「規約の全文の見直し」のまま（最後に行う）。実装は p003 から。

| Phase | 内容 | 依存（M7） | 試験 |
| --- | --- | --- | --- |
| p003 | kernel（core と bcm2711、**担当 1 人**、WS141 p016 B-k2 の範囲を含む）: display の op の gate 外し（table_lock と pin、`DRV_GPU_DISPLAY_UNGATED`）、状態から作る list と 3 slot の差し替え、primary も coalesce、settle・EBUSY の撤去、IRQ の採用の記録と waitq、COMMIT_WAIT、眠る WAIT、状態の項目の借りと thread の文脈の解放、overlay（unscaled・scaled・LBM）、帯域・clock の予算と ENOSPC、console の省略、lease の終わりの戻し、UAPI と core の検査、計数。終わりに値の出典の表と GPL の類似の監査（4.3 (8)）、Pi の帯域の実測（`hvs-load`） | 9 の 1（UAPI の承認）。WS141 p016 B-k2 を単独で実装しないこと（Q1 が WS141 に伝える） | host: list の符号（overlay の位置・clip・scaled の word・LBM・順・console の省略）、差し替えと coalesce（IRQ の模型で、採られる list が常に完全）、借りの解き方（1 つの buffer に画像 2 つ）、予算と ENOSPC、COMMIT_WAIT と WAIT の眠りと起こし、gate 外しと destroy の pin の待ち、cursor の op が primary の待ちの間に進む（B-k2 の受け入れの量を含む）、UAPI の struct の大きさ、ASan・UBSan。build warning 0。T1: i915・Venus の回帰（gate が変わらない）。Pi（T1・Q1 経由）: `hvs-load` で plane の組み合わせごとの underrun（8） |
| p004 | libvulkan: overlay の plane の列挙と能力、overlay の surface と swapchain、隠す約束、保留・fast path・overlay の lane、plane ごとの serial、借りと acquire（WAIT を display_mutex の外で）、primary の COMMIT_WAIT、ENOSPC の扱い、log、README の規則 | p003 の clear（kernel が main に）、**WS141 p016 B-u の Pi の受け入れ**（fast path・ticket・lane が Pi で確かめられていること） | host: WSI の fixture（偽の display の transport）で、compositor と同じ usage（TRANSFER_DST だけ）の chain の fast path（why=）、保留と primary の 1 commit、隠す、DROPPED、acquire の WAIT の間 display_mutex を持たない、ENOSPC で overlay の chain だけ OUT_OF_DATE |
| p005 | compositor: drag（焼き、入る・戻る、持ち主の移し、C の follow と `kwl_drag_place`、条件と fallback、中身の止め方、`--no-planes`、log）と上の帯の plane（bar だけ） | p004、**WS213 p004 の clear（whole-Phase）**、9 の 2・3 | host: 状態の遷移・持ち主の移し・条件（fake の Vulkan、TSan）、`kwl_drag_place` の単体（下限・slop・出力の外）。T1（i915・Venus で画が前と同じ）。Pi（8） |
| p006 | compositor: dock・undock・pull back の拡大縮小（帯に title bar の通り道）と undock の形 | p005、p003 の実測（9 の 5） | host・T1・Pi（8） |
| p002 | 規約の全文の見直し（WS が変えた C の全部）と、新しい HVS の code の GPL との類似の監査（2 回目） | p003〜p006 | style-check、監査の記録 |

- 各段で build（rpi4・amd64・keiland-linux、warning 0）と変えた所の短い host 試験。QEMU と実機は T1 と Q1 経由（担当は QEMU を起動しない）。
- WS141 p015（UIF）への制約: overlay の swapchain の画像は linear のまま（1.2）。WS141 への伝達は Q1。

## 8. 試験

- host（各段、上の表）。kernel の list は `plan/ws141/tests/display-host-test.sh` と同じ形の試験を `plan/ws214/tests/` に置く。libvulkan の fixture は B-u の WSI の fixture と同じ形で、**compositor と同じ usage の chain**（TRANSFER_DST だけ）を使う（WS141 p016 u16 の教訓: usage の違う fixture では why=usage を見逃す）。回帰として残す物は WS の終わりに master.md の Tools に登録するか tests/ のシナリオへ（AGENTS.md の基準）。
- **帯域の実測（p003 の終わり、Pi、T1・Q1 経由）**: 試験の道具 `hvs-load`（`plan/ws214/tests/`、試験だけが UAPI を直に呼ぶ）が display を claim し、primary（全画面）＋overlay の組み合わせ（大きさ、拡大・横の縮小・縦の縮小の比、console の有無）を 10 秒ずつ出し、flip の report の underrun の数を読む。800x480・1024x600・1920x1080 のうち Pi で選べる mode で。結果から予算と mode ごとの能力を決め、p003 に記録する。HVS が隠れた plane を読むかもここで確かめる（console の省略の効果）。
- **画素の比較（M5）**: 焼いた絵の正しさと premultiplied の影を確かめる。T1（i915・Venus でも焼きの経路は動かせる: plane が無くても `--plane-bake-test` で焼きと読み戻しだけを行う）と Pi で、同じ場面の (a) 合成の shot（`--no-planes`）と、(b) 窓を除いた primary の shot に読み戻した絵を CPU で premultiplied の over（out＝src＋dst×(1−a)）で重ねた物を比べ、差が各成分 ±2 以内。HVS の実際の出力は shot で取れないので、Pi では (b) の計算と、ユーザーの目視（入る・戻るの継ぎ目）で見る。
- T1（WS の終わりにまとめて）: i915（5330）・Venus（QEMU）で drag・dock・undock の shot が前と同じ、`KWL VULKAN_ERROR`・`KWL FAILED` なし、`KWL WPLANE` が出ない（plane が無い）。Linux の TSan の guest（WS213 の受け入れ 3 と同じ手順）。
- Pi（1 boot に場面をまとめる、v3d.perf=1、`--log-frames`）。受け入れは **kernel の採用の cadence**（`BCM2711_FLIP_CADENCE_OVERLAY`・`FLIP`）で判定する（compositor の present の間隔ではない）:
  1. 窓（Settings・Files・Monitor）を 10 秒ずつ drag（今の mode と、可能ならもう 1 つ）。判定: overlay の cadence の間隔の中央値 ≤ 17ms・p90 ≤ 34ms、drag の間の primary の submit 0（入る・戻るを除く。`KWL WPLANE` の計数で primary と帯の submit を分けて数える）、帯の submit は時計・status の変化の時だけ（1 秒に 1 回以下）、underrun 0、`pair` の割合（記録、努力目標）。
  2. bar へ drag して dock、double click で dock・undock、pull back。アニメの帯のコマ数（`KWL WPLANE anim`）が 120ms に 6 以上、underrun 0。
  3. drag の途中で通知を出す（条件を外れて戻る）、時計の分が変わる時刻をまたいで drag する（帯の描き直し）。
  4. 端で drag（下限・slop、`kwl_drag_place` と M の位置が一致: drop の後の窓の位置が C の最後の present の位置と同じ、log で比べる）。
  5. ユーザーの目視: 窓と cursor が一緒に動くか、入る・戻るの継ぎ目（glass の色の替わり、中身の替わり）が気になるか。
- `--no-planes` で同じ場面を流し、今との比べにする。

## 9. 未決（ユーザー・Q1 の判断が要る物）

ユーザーに聞く物（Q1 がそのまま聞く。各項の後半がユーザー向けの短い説明）:

1. **UAPI の追加と WAIT の意味の変更の承認**（4.2）。
   - 説明: 「窓を画面の重ね合わせの板（HVS の plane）に載せるため、kernel に新しい命令を 2 つ（板の状態をまとめて変える commit、板の能力の問い合わせ）足し、既存の『表示の完了を待つ』命令（WAIT）を本当に待てる（眠る）形に変えます。使うのは libvulkan だけで、アプリや compositor は今どおり標準の Vulkan だけを使います。cursor の板のときに承認いただいたのと同じ種類の追加です。承認いただけますか。」
2. **窓の glass のぼかしを操作の間は始めの場所のままにする**（5.6）。
   - 説明: 「窓をドラッグしている間、窓の半透明のガラスの部分（title bar など）は、ドラッグを始めた場所の背景のぼかしのまま一緒に動きます。ボタンを離すと今の場所の正しいぼかしに一瞬で替わります。背景の色が大きく違う所まで動かすと替わり目が見えます。これでよいですか。」（受け入れない時は title bar を別の板にして止まった時に焼き直す案を次の版で設計する）
3. **ドラッグの間、その窓の中身を止める**（5.2）。
   - 説明: 「窓をドラッグしている間、その窓の中身（動画や時計など）は止まり、離すと最新に戻ります。止めない案もありますが、中身が変わるたびに GPU が窓 1 枚分を描き直します。止める案でよいですか。」
4. **「窓の前後の入れ替え」をこの WS から外す**（5.9）。
   - 説明: 「窓をクリックして手前に出す操作はアニメが無く、今でも 1 回描くだけで軽いので、重ね合わせの板に載せても速くなりません。この WS の目標から外してよいですか。」
5. **undock（大きな窓を元の大きさに戻すアニメ）の形**（5.3）。
   - 説明: 「大きな窓を小さく戻すアニメで、板に縮小をさせると表示の帯域が大きく増えます。帯域の実測（kernel の段の終わり）の後に、(i) 始めの数コマが少しぼやける代わりに軽い形、(ii) 帯域に余裕のある画面の大きさでだけ縮小させる形、(iii) 今どおり GPU で描く形、から選んでいただきます。今は判断を保留でかまいません。」

Q1 の判断で決まった物・Q1 が行う物:

6. kernel の段は 1 つの Phase（p003）にまとめ担当 1 人、WS141 p016 B-k2 の範囲を含む（Q1 の判断、2026-10-12）。B-k2 の側の穴（4.3 (3)）の WS141 への伝達、WS213 の不変条件の変更（5.8）の WS213 への伝達、WS141 p015 の linear の制約（1.2）の伝達は Q1。
7. libvulkan の原子性の規則（4.1）は p004 で README に書く（実装の定め、仕様の外。Mesa には無い）。
8. 値の出典の表で Linux だけが出典の値の扱い（4.3 (8)）は Q1 が WS141 の決定を当てはめる。

## 10. 確かめていない事（この版の限り）

- Pi の実機・QEMU は何もしていない。帯域の予算、隠れた plane の読み、underrun は p003 の実測で確かめる。
- 「次の list」の register の差し替えで HVS が常に完全な list の一方を採ること（frame の始めの直前の書き込み）は、p003 の host の模型と Pi の差し替えの数・underrun・画面の乱れの無さで確かめる。確かめられなければ差し替えをやめて coalesce だけにする（窓と cursor が 1 refresh ずれることがある）。
- Vulkan の仕様の本文（VK_KHR_display_swapchain）で、`VkDisplayPresentInfoKHR` の適用の範囲と複数の swapchain の present の同時性の記述は reference page の範囲でしか確かめていない。4.1 の規則は zedBSD の実装の定めとして扱う。
- HVS5 の scaled の plane の word の正確な並び、LBM の式は p003 で出典の表（4.3 (8)）に記録して確かめる。
- bar の glass が下の窓に依らないこと（5.4）、窓の popup・sheet・sub-surface の条件（5.5）は p005 で source を読み直して確かめる。
- OUT_OF_DATE（ENOSPC）の時の compositor の戻り方の見た目、焼いた影の alpha の画素は p005 の試験（8 の画素の比較）で確かめる。
- p007（dock の絵）と p008（damage）の Pi の結果は未着。数字は Q1 の 2026-10-12 の値。
