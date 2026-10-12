<!-- awesome-plan project=zedbsd record=ws210-p013 -->

# ws210-p013: app の present を変わった所の copy に（GPU の canvas の前の中間）

Parent: [WS210](../ws.md)
Status: planning（P1 の設計 第 1 版、2026-10-12。design-reviewer の review 待ち）
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

## 設計

### E1. 主の経路: staging から swapchain の image へ直接 copy する（quad も canvas の sample も無し）

- swapchain の形式は **B8G8R8A8_UNORM を優先**して選ぶ（無ければ R8G8B8A8）。CPU の pixels（0xAARRGGBB の word、memory では B,G,R,A）と byte の順が同じなので、変換無しの copy でよい。
- swapchain の usage に `TRANSFER_DST` を足す。libvulkan の Wayland は既に対応（`wsi-wayland.c:310-312`）。Mesa の Wayland の WSI も対応。
- canvas の image を、host-visible・coherent・常に map した **staging の buffer** に替える。大きさは pitch × 高さで、pitch は幅×4 を 64 byte に切り上げる。CPU は今と同じく `part` の行の列の範囲だけを staging へ写す（`keiui_present_copy`、そのまま）。
- frame の command:
  1. swapchain の image の barrier: 初めて使う image は UNDEFINED、使ったことのある image は PRESENT_SRC_KHR から、TRANSFER_DST_OPTIMAL へ。
  2. `vkCmdCopyBufferToImage`。region は下の E2 の `draw`（1 つの矩形）。bufferRowLength は pitch/4、bufferOffset は y × pitch + x × 4。
  3. barrier で PRESENT_SRC_KHR へ。
  - render pass・pipeline・descriptor・sampler・頂点の buffer は使わない（作らない）。
- bcm2711 での動き: 全体で 64 byte に揃った 1 region なら GPU の tile の load・store（`vulkan-native-image.c:690-715`）。部分なら kernel が `draw` の行だけを CPU で写し、box を記録する（`vulkan-buffer-copy.c:262-296`）。UIF の変換は無い。
  - 結果として (i)・(ii) が消え、(iii)・(iv) は `draw` の box だけになる。
- 色は今と同じ。quad の shader は sample をそのまま出すだけ（`canvas.frag`）なので、copy と同じ画素になる。premultiplied・opaque の扱いも変わらない。

### E2. swapchain の image ごとの古い所（buffer age の代わり）

- `stale[i]`: swapchain の image i に、最後に書いた後の frame が変えた所の外接の矩形。`whole[i]`: 内容が分からない（作った直後・resize・OUT_OF_DATE の後）。
- frame で acquire した image が i の時:
  - `draw = whole[i] ? 全体 : stale[i] ∪ part`。
  - 書いた後に `stale[i] = 空`・`whole[i] = 0` にし、他の全ての j について `stale[j] ∪= part` とする。
  - `part` が無い frame（全体の再描画）は、全ての image の `whole` を立てる。
- staging は常に最新の frame 全体を持つ（CPU は `part` の行を写すが、それ以外の行も前の frame のまま正しい）。だから `draw` がどれだけ広くても、staging から写せば正しい。
- `VK_KHR_incremental_present` の region は今と同じく `part`（前の present からの変化）。
- **規格上の前提（review で確かめる点）**: acquire し直した presentable image の内容が、前に present した時のまま残ること。zedBSD の libvulkan は app の image を自分で持ち、書き換えない。Mesa の Wayland・X11 の WSI も image を使い回す。ただし規格が内容を約束するかは未確認。約束が無ければ、zedBSD 以外では `draw = 全体`（今と同じ copy の量、quad の代わりに copy）にする選択肢を残す。

### E3. 予備の経路（今の quad の改良、(a)+(b)）

主の経路が使えない時（形式に B8G8R8A8 が無い、`TRANSFER_DST` が無い）に使う。
- canvas は host-mapped でない image（GENERAL のまま、usage は SAMPLED と TRANSFER_DST）にする。staging から `part` を `vkCmdCopyBufferToImage` し、snapshot の部分の変換を効かせる。
- render pass は 2 つ持つ。全体用（loadOp DONT_CARE、initialLayout UNDEFINED）と部分用（loadOp LOAD、initialLayout PRESENT_SRC_KHR）。両者は互換。
- 部分用では renderArea を `draw`（E2）にする。bcm2711 は renderArea の tile を全部 load・store する（`vulkan-native-pass.c:449-457`）。そのため LOAD でないと tile の端の画素が壊れる。
- 部分の renderArea の CLEAR は kernel が CPU で塗るので使わない。scissor も `draw` にする。

### E4. 費用と memory

- staging は窓の大きさ × 4 byte（1040x680 で 2.8MB、1920x1036 で 7.9MB）。これが今の canvas の image（同じ大きさ）と入れ替わるので、窓あたりは ±0。
- 主の経路では pipeline・descriptor・頂点の buffer が要らなくなり、少し減る。
- Pi で hover の 1 frame あたりの見込み（推測、Pi で測る）:
  - CPU の行の copy: 2 回（app → staging、kernel が staging → swapchain image）。どちらも `draw` の大きさ。
  - TLB copy と compositor の部分の変換: どちらも `draw` の tile と utile。
  - 今は (i) 約 13ms と (iv) 約 13ms がある。これが部分の大きさに比例する量（hover の 228x140 なら全体の約 5%）に減る見込み。

### E5. OS と試験

- 標準の Vulkan だけを使う。Linux・FreeBSD も同じ source で、OS の macro は無い。
- host の試験（新規、`plan/ws210/tests/`）: lavapipe で offscreen の swapchain は作れないので、E2 の `draw` の算法を純関数に分けて試験する。
  - 3 枚と 4 枚の image の順の acquire、resize、OUT_OF_DATE、全体の再描画で、どの image も最新の staging と一致することを確かめる。
- T1（QEMU、Venus と llvmpipe）: Files・Settings で hover・scroll・resize の後の画面の PNG を、全体で描いた frame と比べる。
- Pi（ユーザー）: hover 中の dmesg の `snap`・`mapped_kib`・copy の行（P3 の計測）で、`mapped_kib` が app の分だけ 0 になり、変換が box の大きさになることを確かめる。

### E6. この Phase の外（依存と後の候補）

- compositor は GPU の buffer の client damage を使わず、window の本体全体を合成する（`wayland/damage.c:116-166`）。合成は GPU の仕事で CPU の変換ではないが、Pi の frame の時間には効く。持ち主は P3（WS141）。
- 後の候補（記録だけ、この Phase の後に Q1 が選ぶ）:
  - Phone の timeline の item の高さを frame をまたいで cache する（今は frame ごとに全 item を 1 回測る。store に変更の世代が要る）。
  - Music の検索中の一覧の cache（推測で未計測）。
  - Calendar の卓上カレンダーの 3D の cache（重なる部分の hover の時だけ、host で約 3ms）。

## 未確認（review と実装で）

- E2 の規格上の前提（acquire し直した presentable image の内容が残るか）。
- compositor（kl_gpu_buffer、kernel の import）が B8G8R8A8 の共有 image を R8G8B8A8 と同じに扱うか（swap の flag、`vulkan-native-draw.c:380-383`）。QEMU の Venus と Pi で確かめる。
- bcm2711 の kernel が、swapchain の image を TRANSFER_DST の layout で受けて部分の buffer→image を CPU で写す経路（layout の検査、`vulkan-buffer-copy.c:247-252`）。

## 見積

1.5 LW（present.c の主と予備の経路、E2 の算法と host の試験）＋T1 と Pi の確かめの待ち。
