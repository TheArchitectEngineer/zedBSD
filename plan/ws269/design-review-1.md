<!-- design-reviewer のレビュー 1（2026-10-19、ws269 design.md 版 1）。Q1 が報告の本文をそのまま置いた -->

## WS269 p001 設計のレビュー 1

対象: /home/awe/zedBSD-claude1/plan/ws269/design.md（版 1）と、同じ directory の ws.md・phase001/phase.md。file は変更していません。

確かめたこと:
- Linux mainline の `drivers/gpu/drm/imagination/` を `gh api` で読みました。
- Imagination の firmware の tree と kernel.org の linux-firmware は GitLab API で見ました。
- Mesa の文書と `src/imagination/` も読みました。
- StarFive の vendor kernel（branch `JH7110_VisionFive2_devel`）の `jh7110.dtsi` と `sf_7110/sysconfig.{c,h}` を読みました。
- firmware は header の 4 KiB と device info を読み取っただけです。逆アセンブルはしていません。

---

### Blocker

**B1. GPU を coherent と仮定している。vendor の driver は非 coherent として扱っている（§3.1 coherency、§3.3 resource_map、§6 の依存、§7 D1）**

根拠:
- vendor の `sf_7110/sysconfig.c` は次のようにしています。
  - `eCacheSnoopingMode = PVRSRV_DEVICE_SNOOP_EMULATED`（約 179–181 行）
  - `pfnHostCacheMaintenance` で `sifive_l2_flush64_range` を呼ぶ（99–103、186–222 行）
  - invalidate は、2 MiB の zero page を FLUSH64 して L2（2 MiB）を全部追い出す方法で行う（106–124 行）
  - `dma_set_mask(…, DMA_BIT_MASK(32))`（427 行）
  - つまり、JH7110 で BXE を動かしている唯一の driver は、GPU を「snoop が無い・DMA は 32 bit」として扱っています。
- zedBSD 側の現状は次のとおりです。
  - 既定は `VF2_DMA_DEFAULT_COHERENT 1`（src/kern/platform/visionfive2.c:86）。
  - 非 coherent の device の確保は、`kern_pmem_map_uncached` が無いと ENOTSUP になる（src/drivers/generic/dma.c:77-81・337-339）。
  - riscv64 の uncached の写像は UNSUPPORTED（src/hal/riscv64/page.c:418・433）。
  - user 空間の NOCACHE の写像も UNSUPPORTED（src/hal/riscv64/space.c:1779-1780。`DRV_GPU_MAPPING_UNCACHED_RAM` は src/kern/vm/device.c:166-168 で `HAL_SPACE_NOCACHE` になる）。
- WS256 側の状況は次のとおりです。
  - p023 で試すのは DMAC だけです。予備 (i)（uncached の別名の写像）は「DMAC が非 coherent なら作る」だけです。
  - design-peripherals.md §2.4 は「bus master ごとに短い確かめを流す」と決めています。
  - 設計の Phase の表には、GPU の確かめも予備 (i) への依存もありません。

起こる状況:
- p005 で KCCB・FWCCB・FW の init の構造を cacheable のまま書くと、MIPS の FW は古い DRAM を読みます。結果は「FW が起きない」に見えます。
- D1 (a)「2 回で起きなければ止める」が、host 側の不具合で WS を打ち切ることになります。
- libvulkan には HOST_VISIBLE|HOST_COHERENT の memory type が少なくとも 1 つ要ります（Vulkan の必須要件）。uncached の user の写像が無いと、これを出せません。

直し方:
1. GPU は**非 coherent を既定**として設計する。組み込みの記述には DT の node が無いので、`drv_fdt_dma_coherent` の既定には頼らない。
2. DMA の address を 32 bit（DDR の下 3 GiB）に限る。広げるのは確かめた後にする。
3. CPU と GPU が共有する memory を種類ごとに決める。
   - FW の ring・構造: uncached の別名（WS256 の予備 (i)）を使うか、kick の前の FLUSH64 と、読む前ごとの FLUSH64 にする。
   - user の blob: (a) riscv64 の HAL の実装で NOCACHE を別名の写像で出す、(b) submit の時に flush して HOST_COHERENT を出さない（非 conformant になる）、のどちらかを D として選ぶ。
4. p005 の最初に、GPU の coherency の確かめ（§2.4 の陽性の対照の形）を足す。依存に WS256 p022・p023 と「予備 (i) を DMAC の結果に関係なく作る」を足す。
5. D1 の打ち切りは、coherency を確かめた後の試行だけを数える。

---

### Major

**M1. Keiland の表示の道が既存の形と合わない（§2、§3.3 display、§3.4、§5 C、D5）**

根拠:
- Keiland の scanout は `/dev/gpu0` を開いて、`GPU_DISPLAY_CLAIM`・`PRESENT`・`QUERY` を使います（userland/desktop/libkeiland-backend-zedbsd/scanout-zedbsd.c:45・161・236・322）。
  - つまり要るのは `drv_gpu_display_ops` です。`drv_gpu_ops.present` では足りません。
- role は `GPU_CAP_DISPLAY` と `scanout->query_device` から決まります（src/drivers/gpu/gpu.c:4484-4500）。libvulkan も role で node を選びます（wsi-display-nodes.c:88・592）。
- Pi 4 は display の node と render の node を分け、share・scanout の ops で buffer をやり取りします（bcm2711/display-device.c:1557-1559、render-device.c:875-877）。
- compositor は Vulkan だけで描きます（userland/desktop/wayland/compose.c:1341 `vkCreateInstance`、:4227 `vkQueueSubmit`）。CPU の道は見当たらず、CPU の Vulkan もありません。

起こる状況:
- powervr が `present` だけを持って `/dev/gpu0` に出ても、Keiland の claim は失敗します。
- D5 (a)「WS270 を GPU 無しで先に始める」の前提（Keiland が GPU 無しで描ける）は、ほぼ成り立ちません。

直し方:
- boot framebuffer の display の backend を作る Phase を足す。中身は `drv_gpu_display_ops` の query・mode・claim・release・present・wait、kernel の console からの引き継ぎ、render の node からの share・scanout の import。
- gpu0・gpu1 の順と role を決める。
- Wayland の buffer の受け渡しのために `share` の ops（export・import）を p006 の範囲に入れる。
- D5 の選択肢を書き直す。(a) WS270 は p008 まで画面なしの立ち上げだけ、(b) 待つ、(c) CPU の描画の backend（大きい）。WS270 と調整する。

**M2. Phase の依存が逆になっている（§6）**

- p006 の「transfer の job で present・色」について:
  - Rogue の transfer（TQ）は fragment の pipeline で USC・PDS の program を走らせるものなので、p007 に依存します（推測。Mesa の transfer は uscgen の TQ の shader を使います。今回は読み直していません）。
- p007 の「実機で compute の結果を読み返す」について:
  - Venus 経由にすると、compute pipeline・dispatch・memory の decode が要ります。これは p008 にあります。
  - Venus の decode は backend ごとに大きいです。bcm2711 の `vulkan-*.c` は 24,329 行、amdrdna は `vk/` が 11,385 行、compiler が 9,575 行です。

直し方:
- p006 の受け入れは、FW を通した null の kick か health の kick と fence、CPU の copy による present にする。
- p007 は kernel の中の試験の道を使うか、最小の Venus の decode（instance・device・memory・compute）を前の Phase に移す。
- 「Venus の decode の層」を独立の Phase にすることも検討する。

**M3. 実機の受け入れの判定の方法がプロジェクトの規則と合わない（§3.2 の末尾、§6 の「確かめ」）**

根拠:
- master.md:659 にある 2026-10-19 15:28 のユーザーの決定:
  - #15「画面が出るまで待つ（serial の対話の例外は作らない）」
  - #10「SD boot, framebufferで目視」
- AGENTS.md は serial の log で判定することを禁じています。
- 設計は「実機: boot log に BVNC と FW の応答」と書いていて、媒体が分かりません。`userland/tests/gpu-*` の結果を何で読むかも書いていません。

直し方:
- 判定は framebuffer の console の行をユーザーが目で見る、と明記する。
- 試験の出力は画面で読める形に設計する。
- 依存に WS256 p011 の実機の確認を足す。
- LAN（p018）の SSH を使うなら、それが例外として許されるかを Q1 に確かめる（今の例外は Linux と FreeBSD の guest だけです）。

**M4. D4 が既存の Guardrail と食い違う（§7 D4、§0 S3、phase の受け入れ 4）**

根拠:
- plan/guardrail.md:112-121（2026-10-15、全部の WS に適用）は次のように決めています。
  - MIT と GPL の値・表は、block ごとの `Value(s) referenced:` で使ってよい。
  - code・comment・構造・symbol の名前は、MIT でも写さない。
  - file の上部に出典や notice を書かない。
- D4 (b)「MIT の file は notice を付けて header を取り込む」は、これを破ります。
- license の事実は既に確かめられます。
  - mainline の file は `GPL-2.0-only OR MIT` です（pvr_fw_mips.c、pvr_rogue_fwif.h、pvr_rogue_cr_defs.h、pvr_drv.c、pvr_device_info.c、pvr_power.c の 1 行目）。
  - Mesa の `pco` は MIT です。
  - vendor の sysconfig.c は「Dual MIT/GPLv2」です。

直し方:
- D4 は外す。代わりに「FWIF の struct の layout（offset・大きさ）は firmware との ABI の値として `Value(s) referenced:` で使い、名前は独自にする」と Guardrail に沿って書く。
- 例外が欲しい時だけユーザーに聞く。
- phase の受け入れ 4 の「GPL は手順の参照だけ」は Guardrail より狭いので、どちらに合わせるかを Q1 と揃える。

**M5. firmware の入手先・版・license の事実（§0 S6、§4、D3）**

確かめた事実:
- kernel.org の linux-firmware の `powervr/` には `rogue_33.15.11.3`・`36.52.104.182`・`36.53.104.796` しかありません（WHENCE は Version 1.1.OS@6976702）。**36.50.54.182 は入っていません。**
- 36.50.54.182 があるのは Imagination の fork の branch `powervr` だけです。
  - この branch は未 merge で保護も無く、書き換えられる可能性があります。
  - head は 8a58f818（2026-04-15、1.1.OS@6976702）。
  - S6 の commit efbebc90 は古い 1.0.OS@6645434 です。
  - 今回取得した file の sha256: 59127d3a02e48876310a520b7dc355f7430d3748d2940caaa33f39237d517abb
- LICENSE.powervr の文面を読みました。
  - 改変しない binary の再配布は可。
  - **reverse engineering・decompile・disassemble は禁止。**
  - Imagination が license した製品でだけ使う。
  - 特許の許諾は OSI の承認した OS と組み合わせる時だけ。

直し方:
- awemorris/zedBSD-firmware の mirror に SHA で固定する。
- FW の版と、FWIF の値を取る Linux の tag を対にして固定する（mainline は `PVR_FW_VERSION_MAJOR 1`、pvr_device.c:36）。
- D1 の調べ方は、FW を逆アセンブルせず、FW の trace と register だけを使う、と明記する。
- D3 (a) はこの文面のままで妥当です。

**M6. 危険の見積もりを変える事実が抜けている（§1、§8、D1、D6）**

firmware の header を読んだ結果:
- firmware processor は MIPS。
- BRN は 63553・71242。
- feature は BXM-4-64（36.52.104.182）と同じで、違うのは大きさだけ。
  - COMMON_STORE: 16384 と 19456
  - ISP の tile 数: 4 と 6
  - MAX_PARTITIONS: 10 と 12
  - ISP の IPP pipe: 4 と 6
  - 違いのもう一つは BRN44079 が無いこと。
- 36.52.104.182 は mainline で SUPPORTED（pvr_device.c:534）、Mesa で conformant です。
- mainline には pvr_fw_mips.c と pvr_vm_mips.c があります。
  - よって「mainline が MIPS を扱えるか未確認」は解決済みです。

vendor について:
- vendor の kernel driver（Dual MIT/GPL）は、まさにこの core を動かしています。
- 「closed」なのは userland だけで、§1 の表の書き方は不正確です。

直し方:
- §1・§8 を更新する。参照先を BXM-4-64 の道と vendor の kernel driver にする。
- D1 を具体的な上限（LW と、使う診断の一覧）で書き直す。
- D6 の A の資料に、Mesa の `pco/pco_isa.py`（MIT、encoding を機械で読める形）と `csbgen` の XML を加える。

**M7.「HAL の変更は要らない」の根拠が電源と clock だけ（§2、phase の受け入れ 3）**

- B1 の uncached の user の写像は、今の riscv64 の HAL では作れません（space.c:1779-1780）。
- hal.h:750-756 は「通常の user の写像は managed RAM」と決めています。別名の PA（0x4_0000_0000 + pa）は managed RAM の外です。そのため契約の文面の変更が要るかもしれません（推測）。

直し方:
- この点を分析して書く。要るなら差分の案を `plan/ws269/proposed/` に置く（適用しない）。
- 要らないなら、src/hal の実装の変更（承認不要）として範囲と Phase を書く。

**M8. firmware を読む時期（§3.2 の 2）**

根拠:
- VF2 の attach は `probe_soc` の中で、root を mount する前に走ります。
- 既存の driver は thread の中で root を上限付きで待ちます（amdrdna/firmware.c:39-57、i915/firmware.c:11-17）。

直し方:
- attach を 2 段に分ける。早い段は電源と BVNC、root の後の worker で FW の起動。
- `/dev/gpu` の登録の時期を決める（Keiland の起動時の probe との競合も含めて）。
- initrd が root の時の置き場所も書く。

**M9. clock の rate と clock の木（§3.1）**

根拠:
- vendor は `clk_bv` を 594 MHz（BIN2 の板は 396 MHz）に `clk_set_rate` し、その値を FW に `ui32CoreClockSpeed` として渡します（sysconfig.c:317-327、約 505 行）。
- mainline v6.12 の `clk-starfive-jh7110-sys.c` の定義（44-46・106-111 行）:
  - `gpu_root` は PLL2 と PLL1 の mux。
  - `gpu_core` は divider（最大 7）。
  - `gpu_sys_clk` の親は **ISP_AXI**。
  - `gpu_rtc_toggle` は OSC を元にした gate と divider（最大 12）。
  - `noc_bus_gpu_axi` の親は gpu_core。

直し方:
- vf2-crg の表に親の行（ISP_AXI・APB_BUS・OSC など）も足す。足さないと rate が計算できません。
- 実際の rate を `drv_vf2_clock_get_rate` で読んで FW の init に渡す。
- rtc の divider の値を決める。

---

### Minor

- **m1. IRQ の番号（§3.1、§8）**: 「推測 82」は確定できます。vendor の DT は `interrupts = <82>`、sysconfig.h:51 は「77 + 5」、design-peripherals.md:93 の n + 5 の規則とも合います。
- **m2. vendor の DT の値**: 2 つ目の reg は `0x130C000`（0x130C_0000 ではなく、書き間違いに見えます）。clock-names は `clk_bv/apb/rtc/core/sys/axi`。reset は apb → doma の順だけで、27 には触りません。p002 で記録する。
- **m3. address の幅（S2、§8）**: 「64 GB の仮想 address」は、FW の情報では PHYS_BUS_WIDTH 36（物理 64 GiB）、VIRTUAL_ADDRESS_SPACE_BITS 40 です。
- **m4. §3.2 の 3 の FW の memory**: MIPS の場合、mainline は kernel 用の GPU の VM context を作りません（pvr_device.c:700-705）。FW の memory は MIPS の page table（pvr_vm_mips.c、write-combine）で写し、boot_data に register の物理 address を入れます（pvr_fw_mips.c:77）。また、BRN・feature は FW の file の device info から取るので、parser の範囲に入れる。
- **m5. 電源の管理**:
  - mainline の binding は B-series に power-domain「a」「b」を要求します。JH7110 で b を FW に任せる前提とずれるので、未確認として書く。
  - FW の Active Power Management の要求にどう応えるか（または最初は無効にするか）を書く。
  - vendor は hw event の mask を全部 0 にしていますが、写さずに GPU の event だけを扱う。
- **m6. 組み込みの記述と CRG の API**: DT の node が無くても `struct drv_vf2_clock`/`drv_vf2_reset {crg,index}` を直接作れます（vf2-crg.h）。これを明記する。reset の解除は status を待つので、PD → clock → reset の順が必須であることも書く。
- **m7. 未設計の点**: recovery を `drv_gpu_recovery_ops` の契約（stop_begin・poll・fault・reset・isolate、gpu.h:59-88）にどう当てるか。IRQ の上半分・下半分と lock の順。
- **m8. D1 (a) の「2 回の試行」**: 何を 1 回と数えるかが決まっていません（Queue の試行か、起動の回数か、LW か）。
- **m9. §5 B の「toolchain の依存」**: Mesa は userland の package で、AGENTS.md の定義する toolchain ではありません。B の本当の欠点は、DRM の UAPI と winsys が要ることです。
- **m10. §2 の proposed/**: 「`plan/ws269/proposed/` は空」とありますが、directory はそもそもありません。
- **m11. LW の見積もり**: p007・p008 の LW を、bcm2711（52,772 行）や amdrdna の規模と比べて見積もり直す。
- **m12. Mesa の資料**: Mesa には `common/device_info/bxe-4-32.h` があり、compiler は今は `pco` です。p002 の取得の一覧に入れる。
- **m13. p004 の依存**: WS256 の p022（CRG の実機の確かめ）と p011 の実機の確認も足す。ws.md の依存の表とも揃える。
- **m14. 圧縮**: FBCDC（TFBC）の feature があります。最初は圧縮を使わない、と明記する。

---

### 確かめられなかった事（推測のまま）

- GPU が本当に非 coherent か。根拠は vendor の扱いだけです。
- Rogue の TQ が必ず USC を要するか。Mesa の構造からの推測で、今回 source は読み直していません。
- hal.h の契約の文面の変更が要るか。
- vendor の DT の 0x130C000 の意味。
- FW の SOC_TIMER と GPU timer（0x130C_0000）の関係。
- Mesa の csbgen の XML の license の行。

### 作業の記録

- 読んだ・取得した物は scratchpad だけに置きました。
  - /tmp/claude-1000/-home-awe-zedBSD-claude1/70c47280-c9e2-5e3f-b5d9-072cd0539dfc/scratchpad/pvr/（mainline の file の写し）
  - /tmp/claude-1000/-home-awe-zedBSD-claude1/70c47280-c9e2-5e3f-b5d9-072cd0539dfc/scratchpad/fwdl/bxe.fw と bxm.fw（header の解析だけに使用）
  - /tmp/claude-1000/-home-awe-zedBSD-claude1/70c47280-c9e2-5e3f-b5d9-072cd0539dfc/scratchpad/parsefw.py
  - /tmp/claude-1000/-home-awe-zedBSD-claude1/70c47280-c9e2-5e3f-b5d9-072cd0539dfc/scratchpad/vendor-sysconfig.c
- build・試験・QEMU は実行していません。

### 結論

**このままでは受け入れられません。改訂 2 が要ります。** Blocker は 1 件（B1）、Major は 9 件（M1〜M9）、Minor は 14 件です。

D の扱い:
- D2 の (a) と D3 の (a) は妥当です。
- D4 は Guardrail で既に決まっているので外します。
- D1・D5・D6 は B1・M1・M2・M6 の事実を入れて選択肢を作り直す必要があります。

明るい材料もあります。BXE-4-32 は、mainline でサポートされ Mesa で conformant の BXM-4-64（36.52.104.182）とほぼ同じで、同じ MIPS の firmware processor を使います。そのため、§1 が書くほど「研究的」ではありません。改訂 2 では、この最も近い参照を軸にし、coherency・表示・Phase の順を直すのがよいと考えます。
