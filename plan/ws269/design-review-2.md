<!-- design-reviewer の差分の確認（2026-10-19、ws269 design.md 改訂 2）。Q1 が報告の本文をそのまま置いた -->

## WS269 p001 改訂 2 の差分の確認

対象は `/home/awe/zedBSD-claude1/plan/ws269/design.md`（版 2、commit 044895607）です。`git diff 978a588d4 044895607 -- plan/ws269`、ws.md、phase001/phase.md（add4ed7d9 で Q1 が受け入れ 4 を直した後の版）と照らしました。
file は変更していません。build・試験・QEMU は実行していません。firmware は、レビュー 1 の scratchpad にある `parsefw.py` で、header の device info（データ部）だけを読み直しました。逆アセンブルはしていません。

### レビュー 1 の各項目の確認

| 指摘 | 判定 | 補足 |
| --- | --- | --- |
| B1 | おおむね直った | 非 coherent を既定、DMA 32 bit、memory の種類ごとの表、予備 (i) を p004 で作る、D1 の数え方はできています。ただし §3.2 の 4 の判定の仕方に新しい誤りがあります（下の Major 1）。invalidate の扱いも不足しています（Major 2）。 |
| M1 | 直った | display の node（gpu0）と render の node（gpu1）を分けました。Keiland の `SCANOUT_DEVICE "/dev/gpu0"`（scanout-zedbsd.c:45）とも合っています。present の道に問題が残ります（Major 2）。 |
| M2・M3・M4・M5・M6・M8 | 直った | — |
| M7 | 直った | user の NOCACHE を別名の PA で出す案は、Pi 4 の前例（`bcm2711/render-device.c:484`、`gpu.c:3271`）と同じ形で、hal.h を変えずに作れると判断します。PTE の PA を読み返す HAL の API は無く、`hal_space_prot` は PPN を保ちます（space.c の 700 行台）。 |
| M9 | おおむね直った | 細部は Minor 4。 |
| m1〜m6、m8〜m12、m14 | 直った | m6 の順に小さなずれがあります（Minor 4）。 |
| m7 | 対応はあるが、契約の読み違いがある | Major 3。 |
| m13 | ws.md は直った | §6 の表とずれています（Minor 6）。 |

---

### Blocker

なし。

### Major

**M1（新規）. §3.2 の 4 の coherency の確かめは、判定と陽性の対照が成り立っていない**

- 根拠:
  - 設計は「CPU が別名で読む値と cacheable で読む値を比べる」と書き、陽性の対照を「FLUSH64 を省いた時に古い値が見える事」としています（design.md:100）。
  - WS256 の同じ手順（plan/ws256/design-peripherals.md:151-156）は次の 3 点を決めています。
    1. 判定は cacheable の view を期待の模様と比べる。
    2. 別名の読みは分類だけに使う。coherent でも device の書き込みが L2 に留まれば、別名は古い DRAM を読む。
    3. 陽性の対照は、device と独立に CPU だけで stale を作れる事を示す 0b で行う。
  - 改訂 2 の「FLUSH64 を省いて古い値が見える」は、非 coherent の観測そのものです。対照になっていません。
- 起こる状況:
  - L2 の line が追い出されて、cacheable の読みがたまたま新しい値を返すと「coherent」と誤判定します。
  - §3.2 の 1 の「coherent と確かめられた時だけ切り替える」に従って既定を切り替えると、後の Phase で黙ってデータが壊れます。
  - 「FW の sync の書き込み」は 4 byte の UFO 程度です。WS256 の 64 KiB × 8 回に比べて検出力が弱いです。
  - GPU 側の SLC（16 KiB、S9c）に書き込みが残ると、別名の読みも古くなります。vendor は `RGXFWIF_KCCB_CMD_SLCFLUSHINVAL` を使っています（img-rogue/services/server/devices/rogue/rgxinit.c:3651、rgxcompute.c:1102）。設計は SLC に触れていません。
- 重大度: Major。誤判定は既定を coherent に倒し、後で破損として出ます。
- 修正案:
  - WS256 §2.4 の 0a・0b・判定の規則をそのまま参照する。判定は「cacheable の読み == 期待値」にする。
  - 書き込みの量は、FW が書ける範囲で最大にする。少なくとも数 KiB を複数回にする。
  - 読む前に SLC の flush の FW の命令を入れる（Value(s) referenced）。
  - 0b が INCONCLUSIVE の時は切り替えない、と明記する。

**M2（新規）. present の CPU の copy が HAL の契約に反するか、実用に遅い。range の FLUSH64 での invalidate だけに頼っている**

- 根拠:
  - §3.5 は「present は CPU の copy（copy の前に render の buffer を FLUSH64）」と書いています（design.md:130）。これは direct map の cacheable で読む前提です。
  - 一方、非 coherent の device では `drv_dma_alloc_coherent` が uncached の写像を作ります（src/drivers/generic/dma.c:337-347）。
  - hal.h:653-662 は「uncached の写像がある間は、direct map の address で触ってはならない」と定めています。render の buffer をこの道で取れば、FLUSH64 + direct map の読みは契約違反です。uncached の写像で読むなら、1080p で 1 frame 8 MB を uncached で読むことになり、毎 frame かなり遅いと推測します（未計測）。WS256 p011 の (β) なら、framebuffer の書き込みにも FLUSH64 が足されます。
  - vendor は range の FLUSH64 での invalidate のほかに、2 MiB の zero page を書いて L2 を丸ごと追い出す `do_invalid_range` を、kernel の写像を作る時に呼んでいます（img-rogue/services/system/rogue/common/env/linux/dma_support.c:143、rgxlayer_impl.c:1021、定義は sf_7110/sysconfig.c:106-124）。
  - 設計の「invalidate は S9a と同じく FLUSH64 で代える」（design.md:96）は、この事実を落としています。range の FLUSH64 だけで、L1 や投機の読みが残らないかは未確認です。WS256 も「inclusive と推測」としています（design-peripherals.md:483）。
- 起こる状況: present の画が古くなるか、frame rate が非常に低くなります。HOST_CACHED の type の invalidate も同じ不確かさを持ちます。
- 重大度: Major。
- 修正案:
  1. render の target の memory の種類を決める。uncached（drv_dma）か、streaming（`drv_dma_map` + `drv_dma_sync_for_cpu`）か。それに合わせて copy の読み口を契約どおりにする。
  2. 当面の present は damage の矩形だけを copy する。p010 の後に、GPU の transfer で boot framebuffer へ直接書く案を p011 の候補に置く（framebuffer が GPU の 32 bit の範囲に入るか、DC8200 の coherency を条件に）。
  3. vendor の L2 の全体の追い出しを事実として §3.2 に書く。GPU → CPU の向きは uncached の別名を原則にする。cacheable の invalidate を使う所では、WS256 の 0b 型の確かめを付ける。

**M3（新規）. recovery の ops の当て方が `drv_gpu_recovery_ops` の契約と合わない（m7 の対応）**

- 根拠: include/drivers/gpu/gpu.h:59-88 の契約と、design.md:127 の当て方が次のように食い違います。
  - `stop_poll`: 契約では、その session の native access が止まり、全部の descriptor と callback が retire するまで EAGAIN を返し、止まった時だけ 0 を返します。設計の「poll = FW の hang・fault の通知と health の kick の応答」は、検出の仕組みであって、この意味ではありません。
  - `fault(void *, int)`: 契約では device 全体に対して冪等で、新しい publication を止め、destroy や close の道で「不確かな DMA の backing を保持する」ように仕掛けます。設計の「FW の fault の情報を context に結ぶ」は、device 全体である点と backing を保持する義務を落としています。
  - `isolate`: 契約では、その context の descriptor と allocation を、checked reset まで device の所有として残します。設計は「EIO を返す」だけです。
  - `reset`: 設計は「遅い段をもう一度」としています。これでは recovery の中で root を待って file を読むことになります。FW の image は memory に保持しておくべきです。
- 起こる状況: GPU がまだ DMA している buffer を、fault の後に解放することがあり得ます（use-after-free）。
- 重大度: Major（memory の寿命の安全性）。
- 修正案: 契約の文に沿って当て直す。
  - stop_begin: 該当の context の FW の context の停止（cleanup）を要求する。
  - stop_poll: FW の cleanup の完了と callback の retire。
  - fault: device 全体の publication を止め、backing を保持する。
  - isolate: Rogue の context ごとの reset の通知で、guilty な context の backing を保持する。
  - reset: 保持した FW の image で再起動する。

### Minor

1. **BRN の食い違いは説明がつく（§0.3、S10、§9）。** 次の事実を §0.1 に書き、§3.4 の 2 の「BRN・feature は FW の file から取り、表は手で持たない」を補ってください。
   - FW の file の device info が表せる BRN は、mainline の enum の 12 個だけです（drivers/gpu/drm/imagination/pvr_rogue_fwif_dev_info.h:8-19。`gh api` で最新 mainline も同じことを確認）。**71317 はこの enum に無いので、FW の header には原理的に出ません。**
   - 71317 は DDK 1.19 で host 側の BRN です。「GPU の MMU の 1 MiB・2 MiB の page は fault する（最大 256 KiB）」（img-rogue/services/server/devices/rogue/rgxinit.c:4413-4421・4912-4920）。DDK 1.19 の 36.x の core は全部これを持っています（BXM-4-64 の rgxcore_km_36.52.104.182.h も 63553・71317）。
   - 71242 は DDK 1.19 の 36.x の core 定義にありません。後の errata のデータで足されたと推測します。mainline での効果は、`!gpu_multicore_support` の時だけ SLC_CTRL_MISC の LAZYWB を足す事です（pvr_fw_startstop.c:80）。BXE の FW の header は GPU_MULTICORE_SUPPORT を持つので、実際には適用されません（header の再解析で確認）。
   - よって、組は「FW の header の 63553・71242」と「host 側の 71317」の和として扱います。§3.5 の GPU の MMU は 4 KiB の page だけで、1 MiB・2 MiB を使わない、と明記してください（BRN71317、Value referenced）。
   - FW の header には、DDK 1.19 の config に無い XE_TPU2 もあります（rgxconfig_km_36.V.54.182.h に 0 件）。mainline は XE_TPU2 があると SOFT_RESET2 を書きます（pvr_fw_startstop.c:99-135）。「feature を DDK の hwdefs から取らない」ことと、reset の手順は mainline 側を参照することを §3.4 に書いてください。
2. **§3.2 の表と §3.2 の 6 が矛盾している。**
   - 表は「予備 (i) が無い間は FLUSH64」、FW の code・data と page table は FLUSH64 と書いています。6 は「p005 では FW の構造を全部 uncached の別名」と書いています。p004 で別名を作るので、p005 の道は別名に一本化し、表の fallback は「別名の 0a が MISMATCH の時」に限ってください。
   - GPU に渡す address は常に memory port の PA で、別名は CPU だけが使う、とも明記してください。
   - MISMATCH の時の代わりの道（WS256 design-peripherals.md:153 と同じく、改めて設計して止まる）も書いてください。
3. **DT の道で coherency が逆に倒れる（§3.1 D2、§3.2 の 1）。**
   - 「DT に node があれば優先」の道で `drv_fdt_dma_coherent(…, VF2_DMA_DEFAULT_COHERENT)`（今は 1、visionfive2.c:86）を使うと、`dma-noncoherent` の無い node が coherent になります。GPU は DT の道でも既定 0 を渡す、と明記してください。
   - 優先する compatible の一覧を書いてください。vendor の DT は `img-gpu` で status disabled です（jh7110.dtsi:1403-1422）。board の実際の DTB に vendor の node が入っているかは未確認です。
4. **clock の細部（§3.1）。**
   - vendor は axi を apb より先に入れています（sysconfig.c:227 → 228、u0_img_gpu_enable 317-324）。設計の順（axi を最後）は Procedure referenced の参照先とずれます。どちらにするかと、その理由を書いてください。
   - gpu_sys_clk の親の ISP_AXI は、ISP_2X（mux）の divider です（clk-starfive-jh7110-sys.c:72）。親の行に ISP_2X も要ります。
   - gpu_root の mux を PLL2 に固定する（396 = 1188 / 3）ことを書いてください。
   - vendor の branch の jh7110.dtsi:235 は `opp-1250000000` を常に持つので、この vendor kernel は実際にはどの板でも 396 MHz で動きます（sysconfig.c:302-303）。594 MHz に上げる事には vendor の前例がありません。p005 から外して Future Work にすることを勧めます。
5. **D1 の 16 LW の線が p005 の見積もり（16〜24 LW）の下限と同じです。** parser や page table の host 試験も含めて数えると、正常な進みでも線に届きます。「FW を初めて起動した後の debug の LW」と数え方を明記してください。
6. **依存の写しがずれている。** ws.md:18 は WS256 の p020・p023 を依存に挙げていますが、§6 の表のどの Phase の依存にもありません。予備 (i) を WS256 p023 と WS269 p004 の両方が作り得るので、同じ src/hal/riscv64 の code を 2 つの WS が書くことになります。どちらが持つかを Q1 と決めて、§6 と ws.md を揃えてください。
7. **§0.2 と §9 の Q1 への依頼（phase.md の受け入れ 4）は解決済みです。** add4ed7d9 で Q1 が Guardrail に揃えたので、design.md の文面を「解決済み」に直してください。
8. **D3 は既存の流儀でほぼ決まっています。** userland/firmware/README.md は「driver を on にすると menuconfig が firmware を選ぶ（config/drivers/firmware.list）、license は image の build の時に同意」と定めています。D3 は「powervr の driver を firmware.list に載せる」の確認に書き換えられます。
9. **D7 (b) の注記。** vendor の user の写像は `pgprot_writecombine`（img-rogue/services/server/env/linux/pmr_os.c:378・388）です。Svpbmt の無い U74 では、これは cacheable になると推測します（未確認）。vendor は実質 (b) に近い道で動いている可能性があります。(a) の推奨は Pi 4 の前例と合っていて妥当ですが、この事実と、uncached の CPU 読みの性能の危険を D7 に書いてください。
10. **attach の失敗の道。** 遅い段が失敗した時（ENOENT・ETIMEDOUT）に PD_GPUA と clock を落とすかが書かれていません。
11. **late register の EBUSY。** `drv_gpu_register` は他の登録や削除と重なると EBUSY を返します（gpu.c:604-606）。worker の thread からの遅い登録では、上限付きの再試行が要ります。
12. **細かい行番号。** MIPS は rgxconfig_km_36.V.54.182.h の 72 行です（設計は 71）。sysconfig.c の snoop は 174-183 行です。

### D の選択肢と推奨の妥当さ

- **D1 (a)**: 妥当です。Minor 5 の数え方を足す必要があります。
- **D2 (a)**: 妥当です。Minor 3 の、DT の道の coherency と compatible を足す必要があります。
- **D3 (a)**: 妥当です。既存の firmware.list の流儀に言い換えられます（Minor 8）。
- **D5 (a)**: 妥当です。Keiland は gpu0 を固定で開き、compositor は Vulkan だけで描くので、前提は正しいです。libvulkan の node の一覧は instance ごとに一度だけ作られます（wsi-display-nodes.c:75 付近・406-436）。そのため、render の node を待つ事（§3.3）は必須です。
- **D6 A**: 妥当です。B の欠点（Vulkan の実装が 2 つになる、DRM の UAPI が要る）の事実も正しいです。
- **D7 (a)**: 妥当です（Pi 4 と同じ形）。Minor 9 を足す必要があります。

### 確かめられなかった事（推測のまま）

- uncached の CPU の読みの実際の帯域（Major 2 の「遅い」は未計測）。
- vendor が L2 の全体の追い出しを足した理由（L1 か投機の読みか）。
- FW の UFO の書き込みが SLC を経由する時の振る舞い。
- 実機の DTB に GPU の node があるか。
- U74 の `pgprot_writecombine` の実際の属性。

### 読んだ資料

- `~/visionfive-linux` 4cecf169f38e: sf_7110/sysconfig.{c,h}、common/env/linux/dma_support.c、rogue/rgxlayer_impl.c・rgxinit.c・rgxcompute.c、hwdefs の cores と configs、jh7110.dtsi、clk-starfive-jh7110-sys.c、sifive_ccache.c、mainline の imagination/
- zedBSD: include/hal/hal.h、src/hal/riscv64/space.c、src/drivers/generic/dma.c、include/drivers/gpu/gpu.h、src/drivers/gpu/gpu.c、bcm2711/render-device.c・display-device.c、libvulkan の wsi-display-nodes.c、scanout-zedbsd.c、vf2-crg.h、plan/ws256/design-peripherals.md、userland/firmware/README.md

### 結論

**条件付きで受け入れてよい。** B1・M1〜M9・m1〜m14 は実質的に直っていて、レビュー 1 の Blocker は解消しています。

- p002〜p005（資料の固定、firmware の package、PMU・CRG・別名、FW の起動）の Phase を作って進めることに支障はありません。ただし Minor 1・2・4 は p002・p004 の phase.md に入れてください。
- 新しい Major 3 件（§3.2 の 4 の判定、§3.5 の present の道と invalidate、recovery の契約）は、p006・p007 の Phase を作る前に小さな改訂 3 で直してください。どれも本文の局所的な直しで済み、確認は差分だけで足ります。
- HAL の API（hal.h）の変更は、今回も要らないと判断します。
