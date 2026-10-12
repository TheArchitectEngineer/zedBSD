<!-- awesome-plan project=zedbsd record=ws210-p013 -->

# ws210-p013: app の present を変わった所の copy に（GPU の canvas の前の中間）

Parent: [WS210](../ws.md)
Status: planning（P1 の設計 1.1、2026-10-12。[review-1](review-1.md) を反映、Q1 の確認待ち。実装の前に Pi の baseline）
Phase disposition: normal
Queue: [P1 の lane](../../agents/P1/queue.md)

## 由来と範囲

- 2026-10-12 ユーザー（Pi 4）: app の中の hover で一瞬引っかかる。Q1 の依頼で P1 が app の側を直した（Files 03a97b3c5、Settings 7ea4c7816、Photos と `kl_ui_hit_still` 07bce9305、Music・Calendar・Phone eed04412c）。hover の描画は部分だけになったが、present は今も毎 frame 窓全体を処理する。
- 2026-10-12 Q1:「present の側（(a)+(b): swapchain の image ごとの damage、renderArea を部分に、canvas を host-mapped でない image へ staging から部分 copy）の設計を plan/ws210 の新しい Phase に短く書いて」。
- 範囲: libkeiland の `ui/present.c`（`keiui_present_*`、`kl_window_present_part` の Vulkan の経路）だけ。公開の API は変えない。kernel（bcm2711、P3）・compositor・libvulkan の WSI は変えない。GPU の canvas（p003〜）の前の中間の直しで、p005 の window の frame（W）が入ったら置き換わる。

## 今の経路（main 68086b6be の source で確かめた）

1. app が CPU の pixels に描く（部分の時は clip の中だけ）。
2. `keiui_present_frame`: `part` の行を host-visible・LINEAR・**常に map した** canvas の image（B8G8R8A8）へ CPU で写す（`present.c:236-249`）。
3. acquire した swapchain の image（libvulkan の Wayland は R8G8B8A8 を先に出すので R8G8B8A8、`wsi-wayland.c:349-356`）に、canvas を sample する全面の quad を描く。renderArea は全体、loadOp は DONT_CARE、initialLayout は UNDEFINED（`present.c:574-605`・`1210-1278`）。
4. present。WSI は app の image を共有の image へ `vkCmdCopyImage` する。compositor が共有の image を取り込む。

Pi（bcm2711）で窓全体になる所:
- **(i) canvas の sample**: canvas の buffer は host_mapped なので snapshot を使えず（`native-storage.c:383`）、毎 pass、窓全体を raster→UIF に CPU で変換する（1040x680 で約 13ms、bug-analyzer の dmesg）。
- **(ii) quad の pass**: renderArea が全体なので、pass の出力の書き込みの記録も全体になる（`vulkan-native-execute.c:180-187`）。
- **(iii) WSI の copy**: TLB copy は source の変わった box だけに絞る（`vulkan-native-image.c:594-628`）。しかし (ii) の box が全体なので、全体を copy する。共有の image の書き込みも全体になる。
- **(iv) compositor の取り込み**: `snapshot_update` は書き込みの記録の box だけを変換する（`native-storage.c:657-750`）。しかし (iii) が全体なので全体を変換する（約 13ms）。

kernel の部分の変換（write ring、`BCM2711_BUFFER_WRITES` 8）と TLB copy の部分化は既に main にある。present が書き込みを部分にすれば (iii)・(iv) も部分になる。

## 版

| 版 | 内容 |
| --- | --- |
| 1 | 第 1 版（3fdaa73c6） |
| 1.1 | [review-1](review-1.md) の H1〜H4・M1・M2・M4・M6・L1・L2 を反映（Q1 2026-10-12）。E3（quad の部分化）を削った。H1 は WS211 のユーザーの判断を当て、方式は Pi の実測で選ぶ。末尾の表 |

## 判断の前提（H1）

- Guardrail「画像の処理は GPU で」（plan/guardrail.md）。部分の copy は、kernel が CPU で行を写すか、GPU の tile の経路で写すかのどちらか。
- WS211 のユーザーの判断（2026-10-12、master の決定の記録）を p013 にも当てる（Q1）:「一番高速な方法で。PIで少しでも動作にひっかかりがあるなら許容しない。ただしSMPで問題なくなれば許容する。それを総合的に評価し、実装は何回か作り替えていいので、最適化する。」
- → 方式は Pi の実測で選ぶ（E5）。小さく揃えた部分は kernel の CPU の行の copy、大きい部分と揃わない部分は GPU の tile の経路。TFU の on/off も比べる。引っかかりがあれば採らない。SMP の後に評価し直す。Guardrail の例外にするかは、この評価の結果で決める（ユーザー）。

## 設計（1.1）

### E1. staging から swapchain の image へ直接 copy する

- swapchain の形式: B8G8R8A8_UNORM を優先して選ぶ。無ければ R8G8B8A8_UNORM を選び、staging へ写す時に R と B を入れ替える（M1。CPU の行の写しの中で行い、追加の pass は無い）。
- usage に `TRANSFER_DST` を足す。surface が TRANSFER_DST を出さない時は今の quad の全体の経路を残す（M1。E3 の部分化は削った）。
- `clipped` は `VK_FALSE` にする（M2。clipped=TRUE だと隠れた画素の内容を規格が約束しない）。
- staging: host-visible・coherent・常に map した buffer。usage は TRANSFER_SRC（L2、kernel が検査する）。pitch は幅×4 を 64 byte に切り上げる。HOST→TRANSFER の barrier は不要（submit が host の書き込みを見せる、L2）。
- swapchain の image の枚数は先に問い合わせてから確保する（L1。今は 8 枚で切っている、present.c:631-635）。
- command（H3）:
  1. acquire の semaphore は `VK_PIPELINE_STAGE_TRANSFER_BIT` で待つ。
  2. barrier: srcStage=TRANSFER、srcAccess=0、dstStage=TRANSFER、dstAccess=TRANSFER_WRITE。oldLayout は `whole[i]` なら UNDEFINED、でなければ PRESENT_SRC_KHR（H4。kernel は FIFO の実際の layout と照合する、vulkan-barrier.c:158-161）。newLayout は TRANSFER_DST_OPTIMAL。
  3. `vkCmdCopyBufferToImage` に E2 の矩形の list を region として渡す（最大 4 つ、kernel の上限は 64）。
  4. barrier: srcStage=TRANSFER・srcAccess=TRANSFER_WRITE → dstStage=BOTTOM_OF_PIPE・dstAccess=0、PRESENT_SRC_KHR へ。
  - render pass・pipeline・descriptor・頂点の buffer は使わない。

### E2. 写す所: swapchain の image ごとの古い所（H4・M4・H2）

- `stale[i]` は矩形の list（最大 4 つ、M4）。`whole[i]` は「内容が分からない」の印。
- **staging に `part` を写した時点**で、全部の j について `stale[j] ∪= part` とする（H4。acquire・submit が失敗しても、part はどの image にも残る）。
- acquire した image が i の時: `draw` は `whole[i]` なら全体、でなければ `stale[i]` の list。
- `vkQueueSubmit` が成功した後でだけ、`stale[i]` を空にし `whole[i] = 0` にする。submit の後の失敗（present の失敗・device の失い）では、全部の `whole` を立てる。resize と OUT_OF_DATE の後も全部の `whole` を立てる。
- list への足し方:
  - 重なる矩形、または外接の箱が和の 1.5 倍以下になる矩形はまとめる。
  - 5 つ目は、外接の箱の増えが一番少ない組をまとめて入れる。
- **揃え（H2）**:
  - 各矩形の x を 16 px（64 byte）の倍数に切り下げ、右端を 16 px の倍数に切り上げる（窓の幅で切る）。staging の pitch は 64 の倍数なので、bufferOffset も 64 揃えになる。kernel の `kern_memcpy` は 8 byte 揃いの時だけ word で写す（src/kern/kcrt.c:114-145）。uncached の 1 byte の load・store を避けるため。
  - list の面積の和が窓の面積のしきい値（初めは 30%、E5 で Pi の実測で決める）を超えたら、全体を 1 region で写す。全体・64 揃いの 1 region は bcm2711 の GPU の tile の経路に乗る（vulkan-native-image.c:690-715）。
- 算法は present.c から独立した純関数の module（`ui/present-damage.c`）にし、host で試験する。p005 の W の部分の再描画でも使う（L4）。
- `VK_KHR_incremental_present` の region は今と同じく `part`。

### E3. swapchain の内容の保持（M2）

- zedBSD の libvulkan は app の image を読むだけで、書き換えない（wsi-swapchain.c:2524-2660・1254-1340）。Mesa の WSI も image を使い回す。ただし規格の明文は確認できなかった。
- libkeiland は OS の macro を使わない。常に保持に頼る。
- debug のための環境変数を 2 つ置く:
  - `KEILAND_PRESENT_WHOLE=1`: 毎 frame 全体を写す。T1 の PNG の比較の基準にする。
  - `KEILAND_PRESENT_CHECK=1`: present の後に swapchain の image を読み戻して staging と比べ、違えば `KEILAND PRESENT_STALE` を 1 回出す。
- どちらも production の挙動は変えない（coding-style 12 節の診断の口）。

### E4. 費用と memory

- staging は窓の大きさ × 4 byte。今の canvas の image と入れ替わるので、窓あたりは ±0。
- pipeline・descriptor・頂点の buffer が要らなくなる。
- Pi の見込み（review-1 の推測、hover 228x140、3 frame の和で 228x300、x を揃えた場合）:
  - staging への書き込み 0.1〜0.3ms。
  - kernel の行の copy 0.5〜1.5ms（揃えないと 3〜10ms）。
  - compositor の部分の変換 0.6〜1.5ms。
  - 合計 1.5〜4ms。
- 今の値（13ms は CPU の変換の時の値）は TFU を有効にした今の main とは違うので、E5 の baseline で取り直す（M3）。

### E5. 方式を決める Pi の実測（実装の前と後）

- **baseline**（実装の前）: P3 の perf の計測で取る。ユーザーが place9 の後に行う予定（Q1）。
  - 対象: Files・Settings の hover と scroll の 1 frame。
  - 項目: app の draw・present、kernel の copy・変換、compositor の frame、TFU の on/off。
- **比べ方**（実装の後）:
  - (α) 揃えた部分を kernel の CPU の行の copy で写す。
  - (β) しきい値を超えたら全体を GPU の tile の経路で写す（しきい値を 10・30・60% で比べる）。
  - (γ) P3 の GPU の部分の tile の経路（下の E7）ができたら、それ。
  - TFU は on と off の両方で測る。
- 判定: hover と scroll の間に「引っかかり」（1 frame が 16.7ms を超える、または入力から画面までの遅れが前より長い）が無い方式を採る。全部に引っかかりがあれば採らず、ユーザーに報告する。SMP の後に評価し直す。

### E6. 試験（M5）

- host: `present-damage.c` の純関数の試験（新規、`plan/ws210/tests/`）。
  - image の枚数 2〜4、acquire は任意の順で、同じ image が続く場合も含める。
  - acquire・submit・present の失敗を注入し、resize・OUT_OF_DATE も含める。
  - どの順でも、各 image が present の時に最新の staging と一致することを、画素の model で確かめる。
- build: rpi4・amd64 で warning 0。`exports.py --check`。OS 境界の check。
- T1（QEMU、Venus と llvmpipe）: Files・Settings で hover・scroll・resize の後の PNG を、`KEILAND_PRESENT_WHOLE=1` の PNG と比べる。`KEILAND_PRESENT_CHECK=1` で `PRESENT_STALE` が 0 であること。
- Pi（ユーザー、P3 の perf の行）: E5 の比べ方と、B8G8R8A8 の swapchain の見た目。switcher の縮小画像と screenshot も見る（L3）。
- i915 の揃っていない部分の copy は、E2 の揃えにより使わない。Linux・FreeBSD の guest の試験は WS210 では許可が要るので、行うならユーザーに聞く。

### E7. P3 への依頼（Q1 が回す）

次のどちらかで、部分の copy の kernel の CPU の費用を消すか減らす（H1 (b)・H2）。
- (a) kernel の uncached の copy の高速化: `kern_memcpy` を ldp/stp で 64 byte 単位にする（揃った時）。-mgeneral-regs-only の中で。
- (b) GPU の部分の tile の経路: `buffer_image_whole`（vulkan-native-image.c:653-722）を、region を覆う 64x64 の tile に限った tile pass に広げる。staging は常に frame 全体が正しいので、tile 単位で丸ごと書いて正しい。

どちらを取るかは P3 と Q1 が決める。p013 は (α)(β) で先に進み、(b) ができたら (γ) を比べる。

### E8. この Phase の外（依存と後の候補）

- 依存: WS141 p016（TFU）と、incremental の TLB copy・snapshot の部分の変換の実機の確かめ（M3、p001 review-1 M8）。
- compositor は client の damage を使わず window の本体全体を合成する（wayland/damage.c:116-166、P3・WS141）。
- 後の候補（記録だけ、この Phase の後に Q1 が選ぶ）:
  - Phone の timeline の item の高さを frame をまたいで cache する（store に変更の世代が要る）。
  - Music の検索中の一覧の cache（推測で未計測）。
  - Calendar の卓上カレンダーの 3D の cache（重なる部分の hover の時だけ、host で約 3ms）。
- pointer の motion の合流と、描き直しの予約の取り消しは [p014](../phase014/phase.md)。

## 完了の条件（M6）

1. host の試験（E6）が PASS。rpi4・amd64 の build が warning 0。
2. T1 の PNG が全体の写しと一致し、`PRESENT_STALE` が 0。
3. Pi の実測（E5）で、hover・scroll に引っかかりの無い方式を 1 つ選んで既定にした。または、全部に引っかかりがあることをユーザーに報告して判断を得た。

調べの上限: E5 の方式の比べ方は Pi の place 3 回まで。超えたら uncleared にして、Q1 に報告する。

## 1.1 で直した点

| 指摘 | 直し | 場所 |
| --- | --- | --- |
| H1 Guardrail | WS211 の判断を当て、方式は Pi の実測で選ぶ。GPU の部分の tile の経路を P3 へ依頼 | 判断の前提、E5、E7 |
| H2 kern_memcpy の 1 byte の uncached | x と幅を 16 px に揃える。しきい値を超えたら全体で GPU の tile の経路。P3 に copy の高速化か部分の tile pass を依頼 | E2、E7 |
| H3 wait の stage | TRANSFER で待つ。最初の barrier は srcStage=TRANSFER・srcAccess=0。後の barrier は TRANSFER→BOTTOM_OF_PIPE | E1 |
| H4 stale の順と oldLayout | 写した時点で全部の stale に足す。submit の成功の後で i を空にする。失敗で全部の whole を立てる。oldLayout は whole なら UNDEFINED、でなければ PRESENT_SRC | E1、E2 |
| M1 E3 は通らない | E3 を削った。R8G8B8A8 だけの surface は写す時に R/B を入れ替える。TRANSFER_DST の無い surface は今の quad の全体の経路 | E1 |
| M2 保持の前提 | clipped=VK_FALSE。常に保持に頼り、強制全体と自己検査の環境変数を置く | E1、E3 |
| M4 外接の 1 つ | 矩形の list（4 つ）と複数の region、和のしきい値 | E2 |
| M6 完了の条件 | 完了の条件と調べの上限を足した。ws.md の Resume point に p013 | 完了の条件 |
| L1 8 枚で切る | 枚数を先に問い合わせる | E1 |
| L2 usage と barrier | staging に TRANSFER_SRC。HOST→TRANSFER の barrier は不要 | E1 |
| M3・M5・L3・L4 | baseline を先に取る。試験の順と注入。B8G8R8A8 の Pi の確かめ。算法を純関数の module に | E4、E5、E6 |

## 見積

2 LW（present.c、present-damage.c と host の試験、E5 の比べ方の切り替え）＋ Pi の baseline と比べ方の待ち。
