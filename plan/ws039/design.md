<!-- awesome-plan project=zedbsd record=ws039-design -->
# amdrdna の設計（ws039-p001）

作成: 2026-10-15、P3（q1202）。**改訂 2（2026-10-15 夜、P3、q1210）**: 15 節の判断の項目へのユーザーの回答（[ws.md](ws.md)「design.md §15 #9 の回答」）と、RX 5600 XT の到着（10.0.10.3 の i5-13400F の機械に挿した。素の起動で試す）を反映した版。状態: 設計の案（code は無い）。版の呼び方: **第 1 版** = commit `73a01c38b`（review の前）、**review 反映版** = `ef8235e60`（[design-review.md](design-review.md) の 33 件を反映。この文書で「第 1 版のとおり」と書いていた所は全部この版の内容を本文に戻した）、**改訂 2** = `0ccee4921` 以降。改訂 2 の review は [design-review-2.md](design-review-2.md)（blocker 3・major 13・minor 15）で、その反映はこの文書の本文と同 file の末尾の「対応」の表。

改訂 2 で変えた物（ユーザーの決定、既定の案から変わった項目）:

| # | 決定（2026-10-15 夕 ユーザー） | この文書での扱い |
| --- | --- | --- |
| 1 | firmware は `userland/firmware/` の package、取得は awemorris/zedBSD-firmware（無ければ linux-firmware の固定の commit）、LICENSE.amdgpu を同梱 | 4.1 |
| 2 | PSP の memory training を**走らせる**（GOP の画面の退避と戻し） | 3 節 A3t、4.3t |
| 3 | SMU は **fan・thermal の制御も最初から** | 3 節 A4、4.4（Tier F） |
| 4 | **compute の queue も作る** | 3 節 A5、7.2〜7.4（KIQ、MAP_QUEUES） |
| 5 | executor は nvrtx から写す（既定のまま） | 9.4 |
| 6 | **取り消しと復帰**（終わらない仕事はその session だけ DEVICE_LOST、queue の停止・作り直しで desktop は動き続ける、`isolate`、必要なら mode1 reset の実験） | 3 節 A5c、11 節 |
| 7 | **mode set も入れる**（別の解像度・hotplug まで。Guardrail の scanout の規則は守る） | 3 節 A10〜A13、8.6 |
| 8 | takeover は最初 opt-in（既定のまま） | 12 節 |
| 9 | 試験は RX 5600 XT を挿した 10.0.10.3（i5-13400F。ユーザー「RTX機 10.0.10.3 は RDNA機に変わりました。」）の素の起動 | 12.5 |
| 10 | **resizable BAR も使う** | 3 節 A0・A2、5.5。**§15 #7 の回答（2026-10-15 夜）「まず256MBで実装します。」で、実装は BAR0 の 256 MiB の窓（2 つの pool）だけ。Re-Size BAR で広げた BAR0 の利用と OS の中の resize は後**（A0 は capability を読んで log に出すだけ） |
| 11 | **VMID は動的に割り当てる** | 5.4 |
| 12 | renderer だけの登録を許す（既定のまま） | 3 節の登録の規則、10.1 |

この文書は Linux の amdgpu・Mesa の RADV/ACO の code・comment・構造を写さない。手順は自分の言葉で書く。register の offset・bit・packet の形・firmware の表などの**値**は、出典（版・path・表か define か関数の名前）を付けて書く（[Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」の WS039 の項）。出典の識別子（register の mnemonic、関数の名前）は、この文書では出典を指す語と hardware の部品の語として現れる。**code にはそのまま写さない**（13 節の規則で独自の名前を付ける）。正本の版・hash・license は [amdrdna-license-audit.md](amdrdna-license-audit.md)。

| 作業の文書（`plan/ws039/temp/notes/`、commit しない、英語、file:line 付き） | 中身 |
| --- | --- |
| `a-chips-pci.md` | PCI の device id、BAR、IP の base、IP discovery の表、reset と shutdown、VBIOS の post の確かめ、Mesa の family |
| `b-psp-fw-smu.md` | firmware の一覧と container の header、PSP の bootloader・ring・TMR・LOAD_IP_FW の順、memory training、SMU の mailbox と最小の手順、Tier の案、teardown |
| `c-gmc-vm-ih.md` | FB・VRAM・HDP、GART（VMID 0）、4 段の VM と PTE・PDE、TLB の invalidate、VMID の割り当て、fault、IH の ring と割り込みの表、doorbell |
| `d-cp-sdma.md` | RLC・golden・CSB、GFX の ring、MEC の HQD と KIQ、fence・IB・flush の packet、PM4 の表、SDMA 5.0 の起動と packet、teardown |
| `e-radv-gfx10.md` | 描画に要る register の state、shader の bind、記述子、user SGPR、cache の flush、clear・copy の道 |
| `f-dcn20.md` | DCN 2.0 の GOP の readout、surface の address と形式、flip・割り込み・frame counter、cursor、触らない物、blank・release、full mode set の範囲の outline |

note は commit しないので、**実装の Phase で使う値はこの文書か各 Phase の phase.md に出典付きで移す**（p002 の最初に A0・A1 の register の表を design か phase.md へ写す）。どの文書も source を読んだだけで、**実機・QEMU での観測は無い**。「見込み」と書いた所は source に根拠が無い推論。改訂 2 で新しく読んだ正本の file（`amdgpu_ids.c`・`amdgpu_job.c`・`amdgpu_ring.c`・`amdgpu_bios.c`・`amdgpu_atomfirmware.c`・`atom.c`・`amdgpu_psp.h` の runtime DB・`smu_v11_0_pptable.h`・`irqsrcs_thm_11_0.h`・`amd_acpi.h`・`gfx_v10_0.c` の KIQ と queue reset・`display/dc/dml/dcn20/`・`display/dc/link/`・`display/dc/bios/`）は監査の表に p002 で足す（残課題、16 節）。

## 1. 最初の対象と構成

### 1.1 最初の対象: Navi10（RX 5600 XT、RDNA1、GFX10.1）

- RX 5600 XT は PCI の vendor 0x1002、device **0x731F**、revision **0xCA**（libdrm の `amdgpu.ids`。0x731F は 5700・5700 XT と共通で、revision で製品が分かれる）。
- chip の判定は device id ではなく **IP discovery の表の GC の版**で行う（Linux v6.19 は Navi10 を含む Vega 以降の全部を discovery で判定し、`navi10_reg_init.c` のような固定の表はもう無い）。Navi10 は GC 10.1.10、MP0（PSP）11.0.0、MP1（SMU）11.0.0、SDMA 5.0.0、OSSSYS（IH）5.0.0、NBIO 2.3.0、HDP 5.0.0、MMHUB 2.0.0、DCN 2.0.0（出典: Linux v6.19 `drivers/gpu/drm/amd/amdgpu/amdgpu_ucode.c` の IP の版から firmware の名前を引く表。discovery の表が実機で何を返すかは A0 で記録する）。
- device id の表は**安全の門**として持つ: A0 で discovery を読む前に、device id が RDNA1 の表（Navi10 0x7310・7312・7318・7319・731A・731B・731E・731F、Navi14 0x7340・7341・7347・734F、Navi12 0x7360・7362。出典: Linux v6.19 `amdgpu_drv.c` の `pciidlist`）に無ければ attach しない。RDNA2（Navi21〜24、同じ表）・RDNA3（Navi31〜33、Linux は class で照合するので pci.ids の表）は**認識だけ**して「未対応」と log に出し attach しない（ユーザーの決定 4）。
- harvest（RX 5600 XT は 36 CU の cut-down）: Navi1x では discovery の harvest の表は使わず、CU・RB の有効な mask を GRBM の index の register から読む（A5 の constants の段）。

### 1.2 構成

| 部品 | 役割 | 要点 |
| --- | --- | --- |
| BAR5（32 bit、512 KiB の見込み） | register の窓 | register は dword の index で指す（byte = index × 4）。BAR5 の外の register は NBIO の index/data の間接（PCIE_INDEX2/DATA2、NBIO の seg 0 の dword 0xE・0xF）。出典: Linux v6.19 `amdgpu_device.c`・`nbio_v2_3.c`・`nbio_2_3_offset.h` |
| BAR0（64 bit、prefetchable、**256 MiB**。実機の 10.0.10.3 は 256 MiB @0x4000000000、ReBAR の capability は 256 MiB〜1 GiB） | VRAM の CPU の窓 | **GSP のような切り替えは無い**。BAR0 の offset = VRAM の offset = MC の address − FB の base。UEFI の GOP の framebuffer はこの窓の中にそのまま残り、**console の surface として最後まで使う**（2 節の 4）。CPU が書いた物を GPU に読ませる前は HDP の flush、GPU が書いた物を CPU が読む前は HDP の invalidate（5.1）。**§15 #7 の回答で 256 MiB の窓（5.2 の 2 つの pool）だけを実装する**。BAR0 が広い時（Re-Size BAR）も先頭 256 MiB だけを使う。広い BAR0 の利用と OS の中の resize は後（5.5） |
| BAR2（64 bit、2 MiB の見込み） | doorbell | dword の index で書く。GFX・compute の queue・SDMA は 64 bit の書き込み（`kern_mmio_write64`、amd64 の HAL は 1 回の 64 bit の store。HAL の変更は要らない）、IH の rptr は 32 bit |
| PSP（MP0） | secure processor。firmware を検証して TMR に置き、GFX・SDMA・SMU の firmware を起動する。**cold boot では GDDR6 の長い memory training も PSP に頼む**（4.3t） | driver は bootloader の mailbox（C2PMSG_35/36）で training・KDB・SYS_DRV・SOS を渡し、SOS の後は ring（C2PMSG_64〜71）で command を送る |
| SMU（MP1） | 電源・clock・fan・thermal（PMFW） | firmware は PSP が load。最初から **Tier F**（fan・thermal・CTF の feature を有効にし、温度と fan の回転を読み、THM の割り込みを受ける）。DPM（clock の切り替え）は後（4.4） |
| VBIOS（AtomBIOS） | post 済み（GOP）。**data の表**（firmwareinfo・vram_usagebyfirmware・powerplayinfo・smc_dpm_info）を A1v で読み、**command の表**（SetPixelClock・DIG の encoder/transmitter・EnableCRTC …）は mode set（A10〜）で自前の interpreter が実行する | image は ACPI の VFCT から取る（4.5）。post（asic_init）は最初はしない（mode1 reset の実験の時だけ、11 節） |
| GMC（GFXHUB・MMHUB） | GPU の MMU | VMID 0 は flat の GART（system memory を GPU の address に）、VMID 1〜15 は 4 段の page table。**VMID は submission ごとに動的に割り当てる**（5.4） |
| IH（OSSSYS） | 割り込みの ring | 全 source が 1 本の ring 0 に 32 byte の entry で来る。MSI 1 本。THM・MP1・SMUIO の source も受ける（6 節） |
| RLC・CP（PFP・ME・CE・MEC1・MEC2）・SDMA0/1 | command の実行 | PSP の autoload で RLC・CP は起動済みになる。driver は **KIQ**（MEC1 の HQD 0）を register で立て、GFX の queue と compute の queue（4 本）を KIQ の MAP_QUEUES で map し、SDMA0 の queue を作る（7 節）。取り消しは KIQ 経由の queue の reset（11 節） |
| DCN 2.0（6 pipe、6 OTG、6 DIG） | display | takeover（A6〜A8）では GOP が点けた pipe の timing・link・clock は触らず surface と cursor だけを替える。mode set（A10〜A13）では GOP の出力先の connector に限って pipe を組み直す（8.6）。Guardrail の scanout の規則（2 節の 0）はどちらでも守る |

## 2. 方針の要点

0. **scanout の規則（Guardrail、2026-10-04 ユーザー）**: amdrdna は UEFI の GOP の出力先（GOP が点けた OTG・DIG・connector）以外に、自分の判断で scanout を始めない。出力先の種類（DP・HDMI・DVI）は DIG_BE_CNTL の DIG_MODE で読む。**mode set（8.6）も GOP の出力先の connector に限る**: 別の解像度・同じ connector の hotplug（抜き差し、別の monitor）は driver の判断で行ってよい（「GOP の出力先であればどの interface でも初期化を試みる」の補い）。別の connector は graphical session の Keiland の明示の指示（`display.mode` の op、libvulkan の Display の拡張の経路）があった時だけ。
1. **PSP の道だけ**。Navi10 の実機では GFX・SDMA・SMU の firmware を PSP 経由でしか load できない（Linux v6.19 `sdma_v5_0.c` は direct の load を「emulation だけ」と注記し、MP1 11.0 以降の PMFW を driver が直に load する道は無い）。したがって **firmware の package は必須**で、無ければ A3 で止まる（GOP の画面は残る）。
2. **SMU は Tier F から**（判断 3 の回答）: PSP が PMFW を load した後、生きている印を読み、版を問い合わせ、**VBIOS の powerplayinfo から取った pptable を SMU に渡し、FW_CTF・THERMAL・FAN_CONTROL の 3 つの feature だけを有効にする**。THM の割り込みで温度の上限を受け、温度（SMU の metrics の表）と fan の回転（metrics か THM の tach）を読む。clock の DPM は有効にしない（boot の clock のまま）。feature の有効化が拒まれた時の退路は **Tier S**（SMU に頼らず THM の register で fan を固定の duty で回し、監視の thread の閾値で device を fault）（4.4）。
3. **段ごとに失敗の後に残る物を決め、自動の再試行はしない**（3.1）。**A2 以降の段は attach では走らせず、SSH からの明示の指示（control の node、3.2）で進める**。危ない段の前に begin の行を出し、log を disk に同期してから進む（12 節）。
4. **GOP の framebuffer を console の surface のまま残す（nvrtx の scanout と同じ形）**: amdrdna は VRAM の allocator から GOP の framebuffer の範囲を除き、text console はそこへ書き続ける。display の takeover（A6）は client の surface と GOP の framebuffer の間の **flip だけ**で、copy も text console の付け替えも無い。release・fault・shutdown では GOP の framebuffer に flip で戻る。**memory training（A3t）は VRAM の下 32 MiB を壊すので、その間は text console を止め（`kern_text_suspend`、`src/kern/text-display.c`。review B3）、GOP の framebuffer を含む下 32 MiB を system memory に退避し、HUBP を blank にして、戻してから unblank し console を再開する**（4.3t）。
5. **GPU に読ませる memory の置き場**: Linux の bare metal と同じく、**PSP の buffer（fw_pri・ring・fence・command・ucode）、GFX・compute・SDMA の ring、MQD、writeback（rptr・wptr・fence の語）、IH の ring、SMU の driver の表は GART に map した system memory か VRAM**（表: 5.2）。CPU は system memory を普通の RAM として読み書きし、VRAM に置く物は HDP の規則（5.1）。**GMC（GART）と HDP の初期化は PSP より前、IH の有効化は memory training の後**（段の順: A2 = GMC・HDP（IH の ring は用意するが enable しない）、A3t = training、A3 = IH の enable と PSP。Linux の順（GMC の hw_init → training → IH の hw_init）と同じ。review B3）。fw_pri は GART の MC address が 1 MiB 整列になるよう GART の中の位置を選ぶ。**A3t が済むまで VRAM の下 32 MiB には何も置かない**（GART の表・page table・IH の物を含む。5.2）。
6. **DMA と VRAM の寿命**: GART の表・PSP の buffer・ring・MQD・writeback・page table は device が生きている間ずっと保つ。device の fault でも返さない（`drv_gpu_recovery_ops` の fault の契約）。session の資源を返すのは **通常の close（`stop_poll` が 0 = fence が全部完了）の道だけ**。取り消しで `isolate` した session の BO・page table・VMID は、gpu.h の契約どおり **checked reset まで device の持ち物**（quarantine の list。`reset` は NULL なので reboot まで返さない。review M5、11 節）。
7. **投入と fence の待ちは lock の外**（WS241 の教訓）: ring への書き込みと doorbell は node の lock の中で短く、完了の待ちは lock を持たずに spin（2 ms）＋sleep。present は vblank まで lock を持たない。
8. **lock と割り込みの文脈**:

| lock | 守る物 | 種類 | 持てる文脈 |
| --- | --- | --- | --- |
| `mmio_index` | MM_INDEX/MM_INDEX_HI/MM_DATA の 2 段の操作（256 MiB の窓の外の VRAM の間接の読み書き。A0 の discovery の表もこれで読む） | spinlock（IRQ を止める） | 全部 |
| `pcie_index` | PCIE_INDEX2/DATA2 の 2 段の操作（BAR5 の外の register、SMN） | spinlock（IRQ を止める） | 全部 |
| `grbm_select` | GRBM_GFX_INDEX（SE/SH の選択）と GRBM_GFX_CNTL（me/pipe/queue/vmid の選択）。選択を broadcast・0 に戻すまで持つ。**queue の reset の HQD の読み（11 節）もこの下** | mutex | thread だけ |
| `psp_command` | PSP の ring の投入から応答まで（直列） | mutex | thread だけ（sleep する） |
| `smu_mailbox` | SMU の message の送信から応答まで | mutex | thread だけ |
| `kiq` | KIQ の ring の投入（MAP/UNMAP/QUERY・register の write-then-wait）と その完了の待ち | mutex | thread だけ |
| `ring`（GFX・compute 4 本・SDMA ごと） | ring の wptr と doorbell、fence の番号、submission の record（7.7） | spinlock | thread と IH の処理 |
| `vmid` | VMID の pool（LRU、各 VMID の最後の fence・pasid・PD） | spinlock | thread と IH の処理 |
| `display` | HUBP・OTG・cursor の register の書き込み、lease、**mode set の全体（8.6）** | mutex（nvrtx と同じ controller の mutex） | thread。`events` は別の短い spinlock で sequence を読む |
| `node` | session の表、BO の表、session の状態（READY/LOST…、11 節） | mutex | thread |

IH の処理は **割り込みの handler では writeback の wptr を読んで IH の thread を起こすだけ**にし、dispatch（fence の完了 `drv_gpu_complete`、vblank・flip・HPD の sequence の更新、VM fault の読み、THM の温度の割り込み、CP の priv/bad opcode）は thread で行う。

**lock の順**（review m10）: 投入の道は `node` → `vmid` → `ring`（VMID の fence の待ちは `node` も `vmid` も持たずに行う）。取り消しの道（bring-up の thread）は `node`（session の状態の変更だけ、短く）→ `kiq` → `grbm_select` → `ring`。display は `display` だけ（`node` と同時に持つ時は `node` が先）。`psp_command`・`smu_mailbox`・`kiq` は互いに同時に持たない。bring-up の thread が長い段（A3t の最大 3 s、A11〜A13 の mode set）の最中は取り消しがその分遅れる（11.3 の 6）。

9. **shader は legacy の VS と wave32**（p007 の約束）。NGG は使わない。
10. **独自の名前**: register・field・packet・firmware の command・enum の名前は全部 amdrdna の語で付ける（13 節）。
11. **段は実機で 1 つずつ確かめてから次へ**（Guardrail「i915 の display の書き換えは 5330 で動く段ごとに」の考え方を amdrdna に当てる）: 実機（RX 5600 XT）で ok の行が取れた段までを main に入れ、その先は host 試験と build までにとどめる。段の順と手順は 12.5。

## 3. 段の一覧

段の名前は A0〜A13（A = AMD）。各段は始めに `amdrdna: Ax begin`、終わりに `amdrdna: Ax ok …`（値付き）か `amdrdna: Ax fail: …` の 1 行を `kern_logf` で出す。実機の合格の印は kernel.log のこの行。**attach で自動に走るのは A0・A1（読むだけ）まで**。A1v（VBIOS）は ACPI の table が要るので bring-up の thread の最初（ACPI の attach の後。review M9）。A2 以降は control の node（3.2）の指示か、boot の parameter `amdrdna.auto=<段>`（合格した段まで上げる）で進める。線形の段の順は **A0 → A1 → A1v → A2 → A3t → A3 → A4 → A5 → A5s → A6 → A7 → A8 → A9 → A5c → A10 → A11 → A12 → A13**（A5c は取り消しの試験で Vulkan の app が要るので A9 の後。review m14）。A3 と A4 は **1 つの advance で続けて走る**（A3 の後で止まると PMFW に fan・thermal の feature が無い状態が残るため。review M8）。

| 段 | すること（自分の言葉で） | 確かめる値（ok の行に出す） | Phase |
| --- | --- | --- | --- |
| A0 | 発見と ID: PCI の vendor/device/revision、BAR0・2・5 の大きさと種類、**resizable BAR の capability（生の dword、supported の size、今の BAR0 の size。firmware が広げたかの判定、5.5）**、root の bus の `_CRS` の窓（log）、BAR5 の map、MP0 の C2PMSG_33 の bit 31（IFWI の初期化の完了）、VRAM の大きさ（NBIO の MEMSIZE、MiB）、IP discovery の表（VRAM の上端 −64 KiB の 10 KiB を MM_INDEX/MM_INDEX_HI/MM_DATA で読む）の署名・checksum・IP の一覧と GC の版、GRBM_STATUS、BIOS_SCRATCH_7 の bit 9（post 済み）、C2PMSG_81（SOS が生きているか）、MP1 の FIRMWARE_FLAGS、**PSP の runtime DB の cookie**（4.3t）。**読むだけ**。 | `A0 ok gc=10.1.10 vram=6144MiB bar0=256MiB rebar=0x3f000 posted=1 sos=0 rtdb=0`。discovery の失敗・post 未・未対応の chip は attach しない。**C2PMSG_81 ≠ 0（前の driver が PSP を load 済み）なら `A0 fail: sos alive, cold power cycle needed` を出して A1v まで（読むだけ）で止まる** | p002 |
| A1 | GOP の readout（読むだけ）: 6 つの OTG の MASTER_EN、点いている OTG の timing、ODM → OPP → MPC → MPCC → DPP/HUBP の鎖、HUBP の surface の address・format・pitch・viewport、DIG の BE の enable・FE の select・DIG_MODE、BIOS_SCRATCH_6 の ACC_MODE、D1〜D6 の VGA_CONTROL（**legacy の VGA の mode なら attach しない**）、OTG_MASTER_UPDATE_LOCK。loader が渡す GOP の framebuffer（物理・幅・高さ・stride）と照らす。VRAM の配置（5.2）を決めて log に出す。firmware の package の有無を調べる | `A1 ok otg=0 hubp=0 dig=0 mode=DP 1920x1080 fb=0x... pitch=... vram: gop=... top=... win=256MiB` | p002 |
| A1v | **VBIOS の data の表（読むだけ、bring-up の thread の最初）**: ACPI の VFCT（ACPI の driver の新しい公開の口、4.5）から自分の bus/device/function の image を取り（無ければ ROM の BAR）、"ATOM" の署名、rom header の master data table と master command table の offset、firmwareinfo（format/content の版、firmware_capability の 2STAGE_BIST_TRAINING の bit、bootup の sclk/mclk、pplib_pptable_id、bios_scratch_reg_startaddr）、vram_usagebyfirmware（VBIOS の予約の範囲 → 5.2 に反映）、powerplayinfo の header（大きさ・版、software_shutdown_temp）、smc_dpm_info の版、command の表の offset の一覧（asic_init・setpixelclock・digxencodercontrol・dig1transmittercontrol・enablecrtc …）。image は device の記録に保つ（mode set と training が使う） | `A1v ok vbios=VFCT 64KiB fwinfo=3.3 caps=0x... train=1 pptid=0 ppt=0x... scratch=0x4c fwrsv=...` | p002 |
| A2 | BAR0 の 256 MiB の窓の map（§15 #7）→ GMC・HDP（firmware は要らない）: FB の base/offset、GART の表（VRAM の 32 MiB より上、flat、512 MiB 分）、GFXHUB → MMHUB の順に VMID 0 の設定、context 1〜15 の設定、HDP の初期化（MISC_CNTL）と flush、fault の既定、VMID 0 の TLB の invalidate（engine 17）、doorbell の aperture、IH の ring（GART の system memory、256 KiB）と wptr の writeback・MSI の**用意**（RB_ENABLE と source の enable は A3 の最初。review B3）。確かめ: register の読み戻し、GART の表の読み戻し、HUBP・OPTC の underflow の状態 | `A2 ok bar0=256MiB pool=2 fb=0x... gart=0x...+512MiB ih=ready underflow=0` | p003 |
| A3t | **PSP の memory training**（cold boot で SOS が居ない時だけ。4.3t）: VBIOS の training の bit と runtime DB の判定 → text console を止める → GOP の framebuffer を含む VRAM の下 32 MiB を system memory に写す → HUBP を blank → bootloader に long training（c2p の offset、最大 3 s）→ 32 MiB を書き戻し HDP の flush → unblank → console を再開 → p2c の training data（4 KiB）を system memory に保存 | `A3t ok train=long 1234ms restored=32MiB` / `A3t skip: reason` | p004 |
| A3 | IH の enable（RB_ENABLE・INTR・source）→ PSP: root の mount の後に firmware を読み container の header を検査 → GART の system memory に fw_pri（MC で 1 MiB 整列）・ring・fence・command・ucode の buffer を確保、TMR は VRAM → bootloader で KDB → SYS_DRV → SOS（C2PMSG_81 ≠ 0 を待つ）→ ring の作成 → LOAD_TOC（TMR の大きさ）→ TMR の確保 → LOAD_IP_FW（SMU）→ SETUP_TMR → LOAD_IP_FW を順に（SDMA0・SDMA1・CE・PFP・ME・MEC1・MEC2・RLC の 3 つの list・RLC_G）→ AUTOLOAD_RLC → LOAD_ASD。**ok の直後に止まらず A4 へ続く** | `A3 ok ih=ring0 msi=1 sos=0x... tmr=...KiB fw=12` | p004 |
| A4 | SMU（4.4）: FIRMWARE_FLAGS の bit 0、GetDriverIfVersion・GetSmuVersion（不一致は log だけ）、**Tier F**（p012）: NumOfDisplays(0)、driver の表の場所、pptable（VBIOS の powerplayinfo ＋ smc_dpm_info）の転送、RunBtc、allowed mask = {FW_CTF, FAN_CONTROL, THERMAL}、EnableAllSmuFeatures と読み戻し、THM の thermal の割り込み、温度と fan の回転の読み、監視の thread。拒まれたら **Tier S**（p004 の最小: THM の固定の duty の fan と温度の監視） | `A4 ok if=0x37 smu=42.64.0 features=0x3_80000000 temp=45C fan=900rpm mode=F` / `… mode=S duty=50%` | p004（Tier S）・p012（Tier F） |
| A5 | CP と SDMA（7 節）: golden の register、GRBM の timeout、RB・CU・TCC の mask の読み、tile steering、VMID ごとの SH_MEM、RLC の autoload の完了待ち、CSB と RLC の CSIB、SRM、tcp harvest、**KIQ**（MEC1 pipe 0 queue 0、register で直に、RLC_CP_SCHEDULERS）、MEC の unhalt、KIQ の ring test、SET_RESOURCES、**GFX の queue 0 を MAP_QUEUES（engine 4）で**、ME/PFP/CE の unhalt、preamble、GFX の ring test、**compute の queue 4 本（MEC1 の pipe 0〜3 の queue 1）を MAP_QUEUES で**、各 ring test、SDMA0 の gfx queue と ring test、fence（RELEASE_MEM・compute の RELEASE_MEM・SDMA の FENCE＋TRAP）と EOP・TRAP の割り込み、最初の copy（SDMA の COPY_LINEAR、GFX の DMA_DATA）、**最初の compute の dispatch（p007 の compiler の最小の program を compute の queue で）** | `A5 ok kiq gfx=q0 compute=4 sdma0 fence irq copy dispatch tcc=...` | p005 |
| A5s | session: pasid と動的な VMID（5.4）、4 段の page table、BO（VRAM・system）、VM の flush の投入、VM fault の session への対応、`drv_gpu_ops` の RESOURCE〜SHARE、停止の hook、**GPU device の登録（renderer の node、display の ops は無し）** | `A5s ok sessions pasid vmid=lru(15) gpu device registered (no display)` | p011 |
| A5c | **取り消しと復帰**（11 節。線形の順では A9 の後）: run の期限、session の LOST、GFX の queue の reset（CP_VMID_RESET を KIQ 経由）と MQD の写しからの戻し、compute の queue の reset（UNMAP_QUEUES RESET）、kernel の packet と fence の流し直し、`isolate`、試験の app `vkhang`（無限 loop の shader）で「その app だけ DEVICE_LOST、reset の後 desktop は続く」 | `A5c ok vkhang lost in 2012ms, gfx queue reset+remap 8ms, desktop alive` | p013 |
| A6 | display の引き継ぎ: client の surface（VRAM、format 8 = ARGB8888、linear、GOP と同じ pitch・大きさ・256 byte 整列）を用意し、HUBP の PRIMARY_SURFACE_ADDRESS（HIGH → LOW の順）を替える、flip の完了、HUBP の flip の割り込みと OTG の VSTARTUP の割り込み、frame counter、GOP の framebuffer への flip で戻る | `A6 ok flip pending=0 vblank=60/s back=1` | p006 |
| A7 | cursor: HUBP の cursor、DPP の CNVC の cursor の mode、position・hot spot、enable/disable | `A7 ok cursor 64x64` | p006 |
| A8 | GPU device の登録に display・scanout の ops を足す（登録は 1 回。`amdrdna.disp.own=1` の時は A5s の登録を A8 まで遅らせる）、present・wait・lease・cursor の ops、HPD の sequence | `A8 ok gpu device registered, display 1920x1080` | p006 |
| A9 | 最初の draw（p007 の compiler の program）、executor（Venus の stream → PM4）、renderer の node、compositor の描画 | `A9 ok draw`、vkquad の pixel の一致 | p008・p009 |
| A10 | **mode set の 1（読むだけ＋interpreter）**: AtomBIOS の command の表の interpreter（host 試験で A1v の image の表を dry run）、AUX の engine で GOP の connector の DPCD（能力・link の状態）と EDID を読む（HDMI なら DDC の I2C engine）、link の状態（training の完了・lane の数・rate）を GOP の register と照らす | `A10 ok dpcd=1.4 lanes=4 rate=0x14 edid=DELL 1920x1080@60 modes=12 atom=dry ok` | p014 |
| A11 | **re-light**: GOP の connector を、GOP と同じ mode で一度 blank・link を落とし、自前の手順（PHY・pixel clock は VBIOS の command、OTG・OPP・MPC・DPP・HUBP・DIG・link training は自前）で組み直す。DLG・TTU・watermark は GOP の値を保つ（同じ mode） | `A11 ok relit 1920x1080@60 timing match training ok underflow=0` | p015 |
| A12 | **別の mode**: EDID の別の mode（例 1280x1024）に替える。DLG・TTU・watermark を自前の計算（8.6 の DML の代わり）で出す。戻しも | `A12 ok 1280x1024@60 underflow=0 wm=...` | p015 |
| A13 | **hotplug**: GOP の connector の抜き差し（HPD の割り込み → 再検出 → 同じ mode で re-light）、DP の短い pulse（link の喪失 → 再 training）、別の monitor（今の mode が出せなければ EDID の推奨の mode、boot の clock の中で）。別の connector は Keiland の claim と present があった時だけ（Guardrail、8.6 の 10） | `A13 ok unplug/replug relit, short pulse retrained, new monitor 1680x1050` | p015 |

登録の規則: `drv_gpu_register` は 1 回だけで、ops は登録の前に組み立てる（nvrtx の bind と同じ順）。組み合わせ: renderer だけ（`disp.own=0`、A5s の終わり）、renderer＋display（`disp.own=1`、A8）、display だけ（`vk=0 disp.own=1`、A8）。どれも同じ ops の表の作り方で、無い側の callback と capability を落とす。

### 3.1 段ごとの失敗の後に残る物と回復（自動の再試行はしない）

| 失敗した段 | 残る物 | 回復 |
| --- | --- | --- |
| A0・A1・A1v | 何も（hardware に書いていない） | attach しない（A1v の失敗は A2 以降を進めるが training・Tier F・mode set は skip）。GOP の画面は続く |
| A2（GMC・HDP） | GART・HDP の設定、MMHUB の aperture | context 0 の enable を 0 にして止まる。GOP の scanout は MMHUB の aperture を通るので、A2 の失敗の後に画面が乱れる可能性がある（14 節） |
| A3t | VRAM の下 32 MiB の退避の途中・training の途中、text console は止まっている | timeout なら退避を書き戻して unblank・console を再開し、`A3t fail` で止まる（SOS は載せない）。training が走ったままなら書き戻した物がまた壊れる可能性（14 節）。panic の表示（`kern_text_reveal_fatal`）は止められないので、その時は壊れた画面に出る。次の起動は cold power cycle |
| A3（SOS の前） | 何も（bootloader の mailbox に書いただけ） | 止まる。**次の起動は cold power cycle が要る見込み** |
| A3（SOS の後）・A4 | SOS が生きている（C2PMSG_81 ≠ 0）。TMR、RLC・CP。A4 なら SMU の feature | 停止の手順（11 節）を 1 回だけ流す。**次の起動は cold power cycle** |
| A5 | KIQ・queue が動いている | queue を unmap して CP・SDMA を halt して止まる（11 節） |
| A5s・A5c・A9 | session の資源 | その session の fault か取り消し（11 節）。device は動き続ける |
| A6〜A8 | client の surface を scanout 中 | GOP の framebuffer の address に戻して（flip）から buffer を返す |
| A10 | 何も（読むだけ。interpreter は dry run） | 止まる |
| A11〜A13 | link を落として組み直しの途中。**GOP の画面は戻らない**（timing・link を自前で組み直すので、失敗すると画面が無い） | 同じ mode の組み直しをもう 1 回だけ試し、だめなら blank のまま `fail`。SSH の log で解析。**次の起動（warm reboot で GOP が post し直す）で画面は戻る**（見込み、14 節） |

### 3.2 段の前進の指示（control の node）

- attach は A0・A1 で止まる（読むだけ、hardware に書かない）。bring-up の thread が ACPI の後に A1v を走らせる。その先は **`/dev/amdrdna0`（cdev、session の `/dev/gpu` とは別）の ioctl** で進める: `AMDRDNA_ADVANCE`（目標の段）、`AMDRDNA_STATUS`（今の段、最後の結果、各段の値、C2PMSG_81 など）。userland の道具 `amdrdnactl`（`userland/tests/amdrdnactl/`、試験の app の置き場の規則）が `advance A5`・`status`・`vbios` を出す。
- **UAPI（`include/uapi/amdrdna.h`、この WS の所有。review m13）**: ioctl の group の文字は `'A'`（`include/uapi/` の既存の group と衝突しないことを p002 で確かめる）。全 struct は `version`・`size` で始まり、固定幅の整数だけ、予約の欄は 0。`AMDRDNA_STATUS`（`_IOWR`）: {version, size, stage（今）, target, result（errno）, flags（sos_alive・posted・rebar・training_done …）, 各段の主な値（gc の版、vram の MiB、bar0 の MiB、fb の offset、tmr の KiB、smu の版、features、temp、fan、mode の bit …）, reserved}。`AMDRDNA_ADVANCE`（`_IOW`）: {version, size, target, reserved}（root だけ。EBUSY は進行中、EINVAL は線形の順の外）。`AMDRDNA_VBIOS`（`_IOWR`）: {version, size, offset, length, user の buffer の address} で image を可変長に読む（誰でも）。`AMDRDNA_LOG`（任意）: 段の値の文字列。登録は `cdev` の普通の口（`src/drivers/gpu/amdrdna/control.c`）。
- **開発の間だけ（15 節 #11 の回答「開発の間だけ」）**: control の node は kernel の option `CONFIG_AMDRDNA_CONTROL`（`config/kernel-options.list` の bool、開発の間の既定 y）が y の時だけ作る。driver が完成したら既定を n にするか、node と UAPI を消す。n の時は `/dev/amdrdna0` が無く、段は boot の parameter `amdrdna.auto=<段>` だけで進み、`amdrdnactl` は ENOENT を言う。段の前進と状態は bring-up の thread が持つので、node の有無で driver の振る舞いは変わらない（node は外からの指示と読み出しの口だけ）。
- 理由: 実機は USB の image から起動し、kernel の差し替えと boot の設定は SSH で行う（12.5）。boot の parameter で危ない段を自動に走らせると、固まった時に次の起動も同じ所で固まり SSH が戻らない。control の node なら、固まっても電源の入れ直しの後は A1v までで上がり SSH が使える。
- `amdrdna.auto=<段>`（boot の parameter）は、合格した段までを起動で自動に走らせる（production の image では `A9`、display 付きなら `A8` まで。p009 の合格の後に既定を上げる）。`amdrdna.auto` が無い時の既定は `A1v`。
- 停止の hook との競合: 段の進行は 1 本の bring-up の thread が行い、`stage`（最後に ok した段）と `target` を持つ。advance は target を上げて thread を起こすだけ。停止の hook は thread を止めてから `stage` までの逆順を流す（11 節）。

## 4. firmware（BLOB）、PSP、SMU、VBIOS

### 4.1 package（判断 1 の回答）

`userland/firmware/README.md` の規約に合わせ、**既定 off の独立の firmware の package `userland/firmware/amd-navi/`（package `amd-navi-firmware`）**。base の system と kernel に同梱しない。取得は nvidia-gsp と同じ形: `zedbsd-firmware.mk` の `ZEDBSD_FIRMWARE_REPOSITORY_URL`（awemorris/zedBSD-firmware の固定の commit）の `amd-navi/` から先に取り、無ければ linux-firmware の tag `20260410`（commit `dc85ccedc9c973682fbcf4d628ca61174bcc3120`、`git.kernel.org/.../linux-firmware.git/plain/<path>?id=<commit>`）から取る。どちらでも size と SHA-256（[phase001/phase.md](phase001/phase.md) の表）を照合する。**zedBSD-firmware の repository へ file を置くのはユーザー（repository の所有者）**: p004 で置く file の一覧（path・size・SHA-256、`LICENSE.amdgpu`・`WHENCE` の該当の行）を Q1 に渡す。置かれるまでは linux-firmware からの取得で動く。

| file（`amdgpu/`） | install | 扱い |
| --- | --- | --- |
| `navi10_sos.bin`（KDB・SYS_DRV・SOS・TOC を含む container） | `/lib/firmware/amdgpu/` | 必須。A3 の bootloader と LOAD_TOC |
| `navi10_asd.bin` | 同 | 必須（Linux と同じく load し、失敗は A3 の fail） |
| `navi10_smc.bin` | 同 | 必須。PSP の LOAD_IP_FW（type 18）。中の soft pptable（3 つ）は VBIOS の firmwareinfo の pplib_pptable_id が 0 でない時に使う（4.4） |
| `navi10_pfp.bin`・`navi10_me.bin`・`navi10_ce.bin`・`navi10_mec.bin`・`navi10_mec2.bin`・`navi10_rlc.bin` | 同 | 必須。LOAD_IP_FW |
| `navi10_sdma.bin`・`navi10_sdma1.bin` | 同 | 必須。LOAD_IP_FW（type 9・10） |
| `navi10_ta.bin` | 入れない | HDCP・DTM だけ |
| `navi10_vcn.bin`・`navi10_gpu_info.bin` | 入れない | VCN は範囲の外（ユーザーの決定 3）。gpu_info は Linux v6.19 が Navi10 で使わない |
| `LICENSE.amdgpu`・`WHENCE` の該当の行・manifest・`amd-navi-firmware.notice` | package の決まりの場所 | license の要点: binary の形の再配布可、notice の再掲が条件、reverse engineering・decompile・disassemble の禁止（[監査](amdrdna-license-audit.md)）。**zedBSD-firmware への複製はこの条件（改変しない binary、notice の同梱）を満たす読み。法的な最終判断はユーザー**（ws.md の #1 の続き） |

- Navi10 には display の firmware（dmcu・dmcub）が無い。
- firmware は `/lib/firmware` から読むので、**A3 は root の mount の後**（attach の時は A0〜A1v。A2 以降は control の node の指示）。読み込みは nvrtx の `file.c` と同じ形を amdrdna に持つ（写して独自の名前）。
- **検査の場所**: SHA-256 の照合は package の install の時（manifest）。kernel は load の時に container の header の整合を**必ず**検査し、合わなければ A3 の fail。
- 実機の image では `plan/ws039/tests/config.mk` が package を on にし、T1 は `yes | make firmware-licenses` で license を受け入れてから build する。file は USB の root の `/lib/firmware/amdgpu/` に scp で置く（12.5）。

### 4.2 container の header（load に要る欄だけを読む。中身は解析しない）

全 file の先頭は 32 byte の共通の header（出典: Linux v6.19 `amdgpu_ucode.h` の `common_firmware_header`）: 0x00 全体の byte 数、0x04 header の byte 数、0x08/0x0A header の版、0x0C/0x0E IP の版、0x10 ucode の版、0x14 ucode の byte 数、0x18 ucode の file 内の offset、0x1C crc32（見ない）。

| file | header の版 | 続きの欄（出典: 同 header の各 struct） | PSP に渡す image |
| --- | --- | --- | --- |
| sos | 1.1 | 0x20 から 12 byte の記述子 ×3: sos（版・offset・大きさ）、toc、kdb。offset は ucode の先頭から | SYS_DRV = ucode の先頭から sos.offset byte、SOS = ucode + sos.offset から sos.size、TOC・KDB は各 offset から |
| asd | 1.1 | 使わない | ucode の全部 |
| smc | 2.1 | 0x20 start address、0x24 pptable の数、0x28 entry の offset。**entry は {id, offset, size}（出典: `amdgpu_ucode.h` の `smc_soft_pptable_entry`、p012 で欄の順を写す）** | ucode の全部。soft pptable は 4.4 |
| pfp・me・ce・mec・mec2 | 1.0 | 0x20 feature、0x24 jump table の offset（dword）、0x28 jump table の大きさ（dword） | CE・PFP・ME は ucode の全部。MEC は ucode から jump table を除いた前半 |
| rlc | 2.1 | 0x20 から 18 語（v2.0 の欄）、0x68 に 1 語（direct の register list の長さ）、0x6C から srlc・srlg・srls の各 {版・feature・大きさ・offset}（16 byte ずつ、0x6C・0x7C・0x8C。offset は file の先頭から）、header は全部で 156 byte | RLC_G = ucode の全部、RESTORE_LIST_CNTL = srlc、GPM_MEM = srlg、SRM_MEM = srls |
| sdma・sdma1 | 1.0 | 0x20 feature、0x24 change、0x28/0x2C jump table | ucode の全部（JT を含む） |

p001 で 20260410 の file の header を読み、各 file の全体の byte 数が file の大きさと一致し、sos の中の 4 つの部分が隙間なく並ぶ（100208 + 78640 = 178848 = TOC の offset、+1536 = 180384 = KDB の offset、+3872 = 184256 = ucode の大きさ）ことと、rlc の srlc（592 byte @ 25344）・srlg（1536 @ 25936）・srls（16480 @ 27472）が file の末尾（43952）まで隙間なく並ぶことを確かめた。host 試験 (a) の期待値はこれらの実測の値。

### 4.3 PSP の手順（A3、Procedure referenced の対象）

出典（手順と値）: Linux v6.19 `psp_v11_0.c`（bootloader・ring）、`amdgpu_psp.c`（command の投入、TMR、load の順）、`psp_gfx_if.h`（command の id・payload・frame）、`amdgpu_psp.h`（bootloader の command の値）、`mp_11_0_offset.h`（MP0 の register）。

1. **register**: MP0 の base は discovery の表から（navi10 の静的な header では dword 0x16000）。使う物: C2PMSG_33（IFWI の完了、bit 31）、35（bootloader の command/status）、36（引数 = address >> 20）、64（TOS の command/response）、67（ring の wptr、dword）、69/70（ring の address）、71（ring の byte 数）、81（SOS の生存）。register の offset は MP0 の base + 0x61・0x63・0x64・0x80・0x83・0x85・0x86・0x87・0x91。
2. **bootloader の待ち**: C2PMSG_35 & 0x8000FFFF == 0x80000000 を最大 20 回 × 100 ms 待つ。
3. **KDB → SYS_DRV → SOS**: 各 image を fw_pri（1 MiB、MC で 1 MiB 整列、GART の system memory）に zero fill の上に写し、C2PMSG_36 = fw_pri の MC address >> 20、C2PMSG_35 = command（KDB 0x80000、SYS_DRV 0x10000、SOS 0x20000）、2 の待ち。SOS だけは 20 ms 置いてから C2PMSG_81 が 0 でなくなるのを 100 ms 待つ。
4. **ring**: C2PMSG_64 & 0x8000FFFF == 0x80000000 を待ち、69/70 = ring の MC address、71 = 0x1000、64 = 0x20000、20 ms、同じ応答を待つ。ring は 4 KiB = 64 byte の frame × 64。
5. **command の投入**（`psp_command` の mutex の中）: 4 KiB の command buffer を zero にして 1024 byte の command を写す。fence の値 = 通し番号。C2PMSG_67 から wptr（dword）を読み、frame = wptr / 16。frame に command buffer の address・fence の address・fence の値を書き、memory barrier、wptr += 16（mod 1024）を C2PMSG_67 に書く。fence の語（system memory）が通し番号になるのを 10〜100 µs の sleep で最大 20000 回待つ。応答の status ≠ 0 は warning、timeout は error。
6. **command の id**（`psp_gfx_if.h`）: LOAD_TA 1、UNLOAD_TA 2、LOAD_ASD 4、SETUP_TMR 5、LOAD_IP_FW 6、DESTROY_TMR 7、LOAD_TOC 0x20、AUTOLOAD_RLC 0x21。command は command buffer の +8、payload は +28、応答は +864。
7. **順**: LOAD_TOC（応答の tmr_size が 0 なら A3 の fail）→ TMR を VRAM に確保（1 MiB 整列）→ LOAD_IP_FW SMU（type 18）→ SETUP_TMR → LOAD_IP_FW を SDMA0 9・SDMA1 10・CE 3・PFP 2・ME 1・MEC1 4・MEC2 4・RLC の RESTORE_LIST_CNTL 22・GPM_MEM 20・SRM_MEM 21・RLC_G 8 の順 → AUTOLOAD_RLC → LOAD_ASD。
8. **停止**（11 節）: UNLOAD_TA（ASD の session id）→ DESTROY_TMR → C2PMSG_64 = 0x30000（ring の破棄）→ 20 ms → 応答。
9. **TMR**: 実機の大きさは LOAD_TOC が返す。A3 の ok の行に出す。

### 4.3t PSP の memory training（A3t、判断 2 の回答、Procedure referenced の対象）

出典（手順と値）: Linux v6.19 `psp_v11_0.c`（`psp_v11_0_memory_training`・`psp_v11_0_memory_training_send_msg`）、`amdgpu_psp.c`（runtime DB の読み `psp_get_runtime_db_entry`、`psp_sw_init` の training の判定）、`amdgpu_psp.h`（`PSP_RUNTIME_DB_*`、`psp_runtime_data_header`・`psp_runtime_entry`・`psp_runtime_boot_cfg_entry`、`BOOT_CFG_FEATURE_TWO_STAGE_DRAM_TRAINING`、`BIST_MEM_TRAINING_ENCROACHED_SIZE`、`GDDR6_MEM_TRAINING_*`、`PSP_BL__DRAM_LONG_TRAIN`）、`amdgpu_ttm.c`（`amdgpu_ttm_training_data_block_init`・`amdgpu_ttm_reserve_tmr`）、`amdgpu_atomfirmware.c`（`mem_training_supported`・`get_fw_reserved_fb_size`）、`atomfirmware.h`（`ATOM_FIRMWARE_CAP_ENABLE_2STAGE_BIST_TRAINING`）。

- **何のため**: GDDR6 の 2 段の training の 2 段目（長い方）。VBIOS/IFWI は GOP が動く程度に training 済みで、Linux は cold boot の度にこの長い training を PSP に頼む。省くと高い UCLK での margin が減る見込み（note b の 3.6）。ユーザーの決定で走らせる。
- **走らせる条件**（全部満たす時。どれか欠ければ `A3t skip: <理由>`）: (1) C2PMSG_81 == 0（SOS が居ない = cold boot。SOS が居れば training は PSP が受け付けない）。(2) VBIOS の firmwareinfo の firmware_capability に 2STAGE_BIST_TRAINING（0x00000400）が立つ。(3) PSP の runtime DB（VRAM の上端 − 1 MiB、header = {cookie u16 0x0ED5, version u16 0x0100}。その後の directory = {entry_count u16, 2 byte の詰め物, entry の配列（+4 から、各 {type u32, offset u16, size u16}）}。entry_count は 0x40 未満、BOOT_CONFIG（type 5）の entry は size ≥ 8 で、その offset（header から）の {boot_cfg_bitmask u32, reserved u32}。review m3）が無い、または bitmask の bit 0x2（TWO_STAGE_DRAM_TRAINING）が立つ。(4) BAR0 が VRAM の下 32 MiB を覆う（256 MiB の窓でも満たす）。(5) IH はまだ enable していない（A2 の順）。
- **配置**: reserve_size = VBIOS の firmwareinfo の fw_reserved_size_in_kb（content の版が 4・5 の時）、無ければ 64 KiB（discovery の表の offset）。c2p の offset = ALIGN(VRAM − reserve_size − 1 MiB, 1 MiB)（6144 MiB・64 KiB なら VRAM − 1 MiB）、p2c の offset = VRAM − 32 KiB、data の大きさ = 4 KiB。この 2 つは 5.2 の「上端 1 MiB」の予約の中。
- **手順**: (0) `kern_text_suspend()` で text console を止める（以後の log は disk と serial だけ。Linux が efifb・simpledrm を外してから training するのに当たる。review B3）。`A3t begin` の行はこの前に出し disk に同期する。(1) VRAM の先頭 32 MiB（`BIST_MEM_TRAINING_ENCROACHED_SIZE` 0x2000000）を BAR0 越しに system memory へ写す（HDP の invalidate を先に）。GOP の framebuffer はこの中にある見込み（A1 の address で確かめ、外なら退避は不要だが同じ手順でよい）。(2) GOP の HUBP を blank（DCHUBP_CNTL の HUBP_BLANK_EN。8.3 の power の op と同じ 1 bit）。画面は黒になる。(3) C2PMSG_36 = c2p >> 20、C2PMSG_35 = 0x100000（long train）。C2PMSG_35 の bit 31 が立つまで 100 ms × 最大 30 回（計 3 s、`MEM_TRAIN_SEND_MSG_TIMEOUT_US`）。**この間 CPU は BAR0 に触らない**（他の thread の console の書き込みは (0) で止めてある）。(4) 32 MiB を書き戻し、HDP の flush、unblank。(5) `kern_text_resume()`（保持した画面を描き直す）。(6) p2c の 4 KiB を system memory に保存（Linux は suspend/resume の短い training に使う。amdrdna は保存だけ。将来の S3 のため）。(7) 成功・失敗と時間を log。失敗（timeout）は A3t の fail で A3 に進まない（3.1）。
- **blank の是非**: blank しないと training の約 1〜3 秒、GOP の画面に training の壊れた内容が出る。blank は HUBP の 1 bit の書き込みで、A6 の power の op と同じ。既定は blank する（boot の parameter `amdrdna.train.blank=0` で切れる）。判断の項目（15 節 #3）。
- **危険**: training の最中に PSP が 32 MiB を超えて書かないこと（Linux の定数を信じる）、退避の 32 MiB の system memory（起動の早い時期に確保）、timeout の後に PSP が書き続ける可能性、panic の表示（`kern_text_reveal_fatal`）が training の最中に VRAM に書く可能性（止められない。14 節）。

### 4.4 SMU: mailbox、Tier F（fan・thermal）、Tier S（A4・停止、Procedure referenced の対象）

出典（手順と値）: Linux v6.19 `pm/swsmu/smu_cmn.c`（mailbox の手順と応答の code、`smu_cmn_write_pptable`）、`pm/swsmu/smu11/smu_v11_0.c`（register、版の確認、FIRMWARE_FLAGS、`set_driver_table_location`、`setup_pptable`・`set_pptable_v2_1`、`set_allowed_mask`、`set_irq_state`（THM の閾値）、`get_fan_speed_rpm`（tach）、`set_fan_speed_pwm`・`set_fan_static_mode`（FDO の register）、`get_fan_control_mode`・`auto_fan_control`、`irq_process`（THM・MP1・SMUIO の source）、`get_vbios_bootup_values`）、`pm/swsmu/smu11/navi10_ppt.c`（message の番号の表、`setup_pptable`・`store_powerplay_table`・`append_powerplay_table`・`check_powerplay_table`、`get_thermal_temperature_range`、`get_fan_speed_rpm`、`get_fan_parameters`、metrics の読み、allowed の feature の表）、`pm/swsmu/inc/smu_v11_0.h`・`pm/swsmu/inc/smu_v11_0_pptable.h`（`smu_11_0_powerplay_table`）、`pm/swsmu/inc/pmfw_if/smu_v11_0_ppsmc.h`・`pm/swsmu/inc/pmfw_if/smu11_driver_if_navi10.h`（`PPTable_t`・`SmuMetrics_t`・表の id・feature の bit）、`mp_11_0_offset.h`・`mp_11_0_sh_mask.h`、`thm_11_0_2_offset.h`・`thm_11_0_2_sh_mask.h`、`irqsrcs_thm_11_0.h`（smu_v11_0.c の中の define）、`atomfirmware.h`（`atom_smc_dpm_info_v4_5`）。

- **register**（MP1 の base は discovery から、静的な header では dword 0x16000）: message = base + 0x282（C2PMSG_66）、引数 = base + 0x292（C2PMSG_82）、応答 = base + 0x29A（C2PMSG_90）。生存の印 = SMN の 0x03B10024（MP1 の FIRMWARE_FLAGS）の bit 0。
- **手順**（`smu_mailbox` の mutex の中）: (1) 最初の message 以外は応答の register が 0 でなくなるまで待つ（最大 2 s）。(2) 応答 = 0、引数 = param、message = 番号 を書く。(3) 応答が 0 でなくなるまで待つ（最大 2 s）。(4) 0x01 = OK、0xFF = 失敗、0xFE = 未知の command、0xFD = 前提の不足、0xFC = busy（10 µs 置いて再送）、0xFB = debug。それ以外か timeout は SMU の hang と記録し、以後の message を送らない。(5) 出力の引数は引数の register から読む。
- **message の番号**（`smu_v11_0_ppsmc.h`）: GetSmuVersion 0x02、GetDriverIfVersion 0x03、SetAllowedFeaturesMaskLow/High 0x04/0x05、EnableAllSmuFeatures 0x06、DisableAllSmuFeatures 0x07、EnableSmuFeaturesLow/High 0x08/0x09、DisableSmuFeaturesLow/High 0x0A/0x0B、GetEnabledSmuFeaturesLow/High 0x0C/0x0D、SetDriverDramAddrHigh/Low 0x0E/0x0F、SetToolsDramAddrHigh/Low 0x10/0x11、TransferTableSmu2Dram 0x12、TransferTableDram2Smu 0x13、SetSystemVirtualDramAddrHigh/Low 0x16/0x17、SetMinDeepSleepDcefclk 0x22、PrepareMp1ForUnload 0x32、NumOfDisplays 0x40。表の id（`smu11_driver_if_navi10.h`）: PPTABLE 0、WATERMARKS 1、SMU_METRICS 6。feature の bit（同、Navi10 の allowed の表 `navi10_ppt.c`）: FW_CTF 31、FAN_CONTROL 32、THERMAL 33。
- **Tier F の順**（Linux の `smu_smc_hw_setup`（`amdgpu_smu.c`）の順に寄せ、DPM だけに要る物（PCIe の parameter、DPM の表の読み、SetMinDeepSleepDcefclk、display の clock）を除いた物。review M7。拒まれる可能性は 14 節）:
  1. FIRMWARE_FLAGS の bit 0 == 1（PMFW が動いた）、GetDriverIfVersion（0x37 の見込み）、GetSmuVersion。**版の不一致は log だけ**（Linux も info で済ませる。firmware は後方互換）。
  2. NumOfDisplays（0x40、引数 0）。Linux が hw_setup の最初に送る。
  3. **pptable を作る**: VBIOS の powerplayinfo の表（`smu_11_0_powerplay_table`、header の後の `table_revision`・`table_size`・`platform_caps`・`thermal_controller_type`・`software_shutdown_temp` …、末尾に `PPTable_t smc_pptable`）から `PPTable_t` を写す。firmwareinfo の pplib_pptable_id が 0 でなければ代わりに smc.bin の soft pptable（id が一致する entry）を使う（Linux と同じ選び方）。次に smc_dpm_info（`atom_smc_dpm_info_v4_5`、format 4 / content 5 が Navi10）の `I2cControllers` から `BoardReserved` の手前までを `PPTable_t` の同じ名前の欄に上書きする（board の固有の値。欄の offset と大きさは p012 で struct から出典付きで写し、`offsetof` の static assert で確かめる）。
  4. **driver の表の場所**: VRAM に表の最大の大きさ（`PPTable_t` と `SmuMetrics_t` の大きい方、4 KiB 整列）の buffer を取り、SetDriverDramAddrHigh/Low（0x0E/0x0F）で MC address を知らせる（Linux は表ごとに同じ buffer を使い回す）。SetToolsDramAddr は送らない（PMSTATUSLOG の表を持たない。Linux も表が無ければ送らない）。SetSystemVirtualDramAddr は memory pool の大きさが 0 なら Linux も送らないので送らない。
  5. **pptable の転送**: driver の表に `PPTable_t` を書き（HDP の flush）、TransferTableDram2Smu（0x13、引数 = 表の id PPTABLE 0）。
  6. **RunBtc**（0x3A、boot time calibration）。Linux の Navi10 は無条件に送る。
  7. **allowed mask**: SetAllowedFeaturesMaskHigh（0x05、上位 32 bit = (1 << (32−32)) | (1 << (33−32)) = 0x3）→ Low（0x04、下位 = 1 << 31 = 0x80000000）。
  8. **有効化**: **EnableAllSmuFeatures（0x06）**（Linux と同じ message。allowed mask が範囲を絞る）。GetEnabledSmuFeaturesLow/High（0x0C/0x0D）で読み戻し、3 bit が全部立てば Tier F。0x06 が拒まれた（0xFD・0xFF）か bit が立たない時は、第 2 の試しとして EnableSmuFeaturesLow（0x08、0x80000000）→ High（0x09、0x3）を送り、それも立たなければ DisableAllSmuFeatures（0x07）を送って Tier S へ。**allowed mask を 3 bit だけにして EnableAll を送った前例は Linux に無い**（14 節の 5）。
  9. **thermal の閾値と割り込み**（Procedure referenced: `smu_v11_0_set_irq_state`・`smu_v11_0_enable_thermal_alert`、`navi10_get_thermal_temperature_range`）: 範囲は pptable の TedgeLimit（max、℃）・ThotspotLimit・TmemLimit と powerplay の software_shutdown_temp。THM の THM_THERMAL_INT_CTRL（THM の base 0x16600 + 0xB）に MAX_IH_CREDIT 5（bit 31:29）、THERM_IH_HW_ENA（bit 28）、INTH/INTL の mask を 0（bit 24・25）、**DIG_THERM_INTH = min(255, software_shutdown_temp)**（bit 7:0、℃）、**DIG_THERM_INTL = max(0, range の min)**（bit 15:8、℃）、TRIGGER の mask（bit 26）を落とす（review m9）。THM_THERMAL_INT_ENA（base + 0xA）に INTH_CLR（bit 3）・INTL_CLR（bit 4）・TRIGGER_CLR（bit 5）。MP1 の sw の割り込み: MP1_SMN_IH_SW_INT に ID 0xFE・VALID 0、MP1_SMN_IH_SW_INT_CTRL の INT_MASK = 0 で有効（offset は p012 で `mp_11_0_offset.h` から写す）。IH の source: client THM 0x0F の src 0（DIG_THERM_L2H = 温度が上限を超えた）・1（H2L）、client ROM_SMUIO **0x16** の src **83**（GPIO19）= hardware CTF、client MP1 0x1F の src 0xFE = SMU の sw の割り込み（ack は MP1_SMN_IH_SW_INT_CTRL の INT_ACK）。Linux は L2H で「software CTF: 数秒後に system を shutdown」、hardware CTF で即 shutdown する。amdrdna の方針は 11 節（device の fault、system は止めない。判断の項目 15 節 #4）。監視の thread の閾値（既定 = TedgeLimit）と THM の INTH（software_shutdown_temp）は別の値: thread は早めに投入を止め、INTH は hardware の最後の網。
  8. **読み**: 温度は SMU の metrics（TransferTableSmu2Dram 0x12、id 6、HDP の invalidate の後に `SmuMetrics_t` の TemperatureEdge（u16、℃）・CurrFanSpeed（u16、rpm）。struct の版（legacy・NV12・NV1X）は SMU の版で決め、p012 で欄の offset を写す）。fan は FAN_CONTROL が有効なら metrics の CurrFanSpeed、無効（Tier S）なら THM の CG_TACH_STATUS（base + 0x6B）から rpm = 2500 × 60 × 10000 / tach（Linux の式、crystal 25 MHz）。THM の CG_MULT_THERMAL_STATUS（base + 0x5F）の ASIC_MAX_TEMP（bit 8:0）・CTF_TEMP（bit 17:9）も読んで log に並べる（単位の確認、A4 で metrics と比べる）。
  9. **監視の thread**: 1 秒ごとに温度と fan を読み、edge が閾値（既定 = pptable の TedgeLimit、boot の parameter `amdrdna.temp.max=` で下げられる）を超えたら、または THM の L2H の割り込みが来たら、新しい投入を止めて device の fault（11 節）。SMU が答えなくなった（hang）時も fault。
- **Tier S（退路。p004 の最小として A3 の直後に必ず持つ）**: SMU の feature 無しで、THM の register で fan を固定の duty で回す: CG_FDO_CTRL1（base + 0x68）の FMAX_DUTY100（bit 7:0）を読み、CG_FDO_CTRL0（base + 0x67）の FDO_STATIC_DUTY（bit 7:0）= duty × FMAX_DUTY100 / 255、CG_FDO_CTRL2（base + 0x69）の TMIN（bit 7:0、`CG_FDO_CTRL2__TMIN`）= 0 と FDO_PWM_MODE（bit 13:11）= static（値は `FDO_PWM_MODE_STATIC`、p004 で写す）。既定の duty は 50 %（`amdrdna.fan.duty=`）。温度の読みは THM の **CG_MULT_THERMAL_STATUS（base + 0x5F）の ASIC_MAX_TEMP（bit 8:0）**（Linux の Navi10 はこれを温度に使わないので単位・意味は未確認。review m15、14 節の 5）、fan は CG_TACH_STATUS。監視の thread の閾値は 95 ℃（pptable が読めていればその TedgeLimit）。**p004 の受け入れ: Tier S で温度と fan の回転が読め、fan が回る。p012 の受け入れ: Tier F で metrics の温度と THM の値を比べ単位を確かめる**。
- **停止**: Tier F なら DisableAllSmuFeatures（0x07）→ PrepareMp1ForUnload（0x32）。Tier S なら PrepareMp1ForUnload だけ（fan の static の設定は残す。VBIOS の次の post が戻す見込み）。
- **DPM（Tier 1）は範囲の外**（別の WS の候補）: clock の DPM の feature・BTC・SetMinDeepSleepDcefclk・NumOfDisplays・watermark の表（DCN の p-state の切り替えに要る）。

### 4.5 VBIOS の image と AtomBIOS の data の表（A1v、Procedure referenced の対象）

出典（手順と値）: Linux v6.19 `amdgpu_bios.c`（`amdgpu_acpi_vfct_bios`、`check_atom_bios`、`amdgpu_read_bios` の順）、`include/atombios.h`（`UEFI_ACPI_VFCT`・`VFCT_IMAGE_HEADER`・`GOP_VBIOS_CONTENT`。review m2: `amd_acpi.h` ではない）、`atomfirmware.h`（`atom_rom_header_v2_2`、`atom_master_data_table_v2_1`・`atom_master_list_of_data_tables_v2_1`、`atom_master_list_of_command_functions_v2_1`、`atom_common_table_header`、`atom_firmware_info_v3_1/3_3/3_4`、`vram_usagebyfirmware_v2_1/2_2`）、`amdgpu_atomfirmware.c`（`amdgpu_atomfirmware_scratch_regs_init`・`allocate_fb_scratch`・`get_fw_reserved_fb_size`・`mem_training_supported`）、`atom.c`（`amdgpu_atom_parse_data_header`・`amdgpu_atom_parse_cmd_header`）。

- **image の取得**: ACPI の VFCT の表。**zedBSD の ACPI の driver には生の table を署名で引く公開の口が無い**（`drv_acpi_firmware_find` は `src/drivers/acpi/acpi-tables.h` の内部の関数。review M9）ので、`include/drivers/acpi/acpi.h` に「署名で生の table を借りる」口（例 `drv_acpi_table_lookup(signature, &address, &length)`、ACPI の driver の所有なので差分の案 `plan/ws039/proposed/acpi-table-lookup.diff` を p002 で Q1 に出す。**計画に無い依存**）を足す。PCI の attach は `drv_acpi_attach` より前（`src/kern/platform/pcat.c`）なので、A1v は attach でなく bring-up の thread の最初（ACPI の後）に走る。表の header の後に VBIOSImageOffset、そこから {VFCT_IMAGE_HEADER（PCIBus・PCIDevice・PCIFunction・ImageLength は 4 byte、VendorID・DeviceID は 2 byte …）, 中身} が並ぶ。ImageLength が 0 の entry は飛ばし、header と image が table の長さを越えないことを確かめ（Linux と同じ検査）、自分の bus/device/function と vendor/device が一致する image を取り、先頭 2 byte が 0x55AA、"ATOM" の署名（rom header の `atom_bios_string`、header の位置は image の 0x48 の 16 bit の offset）を確かめる。欄の offset は p002 で `atombios.h`・`atomfirmware.h` から出典付きで写す。VFCT が無い時は Linux の順で ROM の BAR（expansion ROM、PCI の層の口は p002 で確かめる）から読む。どちらも無ければ `A1v fail: no vbios`（A2 以降は進めるが training・Tier F・mode set は skip）。
- **data の表**: rom header の `masterdatatable_offset` → `atom_master_data_table_v2_1`（common header の後に 35 個の u16 の offset）。index（struct の順から数えた。p002 で確かめる）: firmwareinfo 4、smc_dpm_info 2、vram_usagebyfirmware 11、gfx_info 14、powerplayinfo 15、displayobjectinfo 22、dce_info 27、vram_info 28。各表の先頭は common header（structuresize u16、format_revision u8、content_revision u8）。
- **読む物と使い道**: firmwareinfo v3.x（`firmware_capability`: training の bit 0x400、`bootup_sclk_in10khz`・`bootup_mclk_in10khz`（log）、`pplib_pptable_id`（v3.3〜、pptable の選択）、`bios_scratch_reg_startaddr`（BIOS_SCRATCH の base。A0 の post の判定の 0x4C を確かめる）、`fw_reserved_size_in_kb`（v3.4〜））、vram_usagebyfirmware v2.1/2.2（`start_address_in_kb`・`used_by_firmware_in_kb`・`used_by_driver_in_kb`。VBIOS の予約の範囲 → 5.2 の allocator から除く。第 1 版の「余裕で避ける」をこれで置き換える）、powerplayinfo（4.4）、smc_dpm_info（4.4）。displayobjectinfo・dce_info は mode set（8.6）で connector と encoder の対応を読む候補（p014）。
- **command の表**: rom header の `masterhwfunction_offset` → `atom_master_list_of_command_functions_v2_1` の u16 の offset（asic_init・digxencodercontrol・dig1transmittercontrol・setpixelclock・enabledisppowergating・enablecrtc・setdceclock …。index は p014 で写す）。A1v では offset と各 function の `atom_function_attribute`（ws・ps の大きさ）を log に出すだけ。実行は 8.6 の interpreter。
- **image の保持**: device の記録に image（64 KiB の見込み）を保つ。試験では `amdrdnactl vbios > navi10.rom` で取り出し（control の node の ioctl）、host の interpreter の試験（8.6）の入力にする。BLOB は repository に入れない。

## 5. GMC（memory の制御と VM）

出典（値）: Linux v6.19 `gfxhub_v2_0.c`・`mmhub_v2_0.c`・`gmc_v10_0.c`・`amdgpu_gmc.c`・`amdgpu_gart.c`・`amdgpu_vm.h`・`amdgpu_ids.c`・`amdgpu_ids.h`・`hdp_v5_0.c`・`amdgpu_hdp.c`・`amdgpu_ib.c`・`gc_10_1_0_offset.h`・`gc_10_1_0_sh_mask.h`・`gc_10_1_0_default.h`・`mmhub_2_0_0_offset.h`・`hdp_5_0_0_offset.h`・`hdp_5_0_0_sh_mask.h`・`nbio_2_3_offset.h`・`navi10_enum.h`・`osssys_5_0_0_offset.h`（IH_VMID_0_LUT）。

### 5.1 HDP の規則

HDP は BAR0 越しの VRAM の読み書きの cache。規則:

- (a) CPU が BAR0 で VRAM に書いた後、GPU・PSP・DCN に読ませる前に **flush**: NBIO の HDP_MEM_COHERENCY_FLUSH_CNTL（NBIO の seg 2 の dword 0xF7）に 0 を書き、NBIO の MEMSIZE を読んで posting。memory barrier を先に。
- (b) GPU が書いた VRAM を CPU が BAR0 で読む前に **invalidate**: HDP の READ_CACHE_INVALIDATE（HDP の base 0xF20 + 0xD1）に 1 を書いて読み戻す。
- (c) ring の上: IB の前に WAIT_REG_MEM（operation 1 = 書いて待つ、register space、NBIO の HDP_FLUSH_REQ に engine の bit を書き FLUSH_DONE の同じ bit を待つ）、IB の後に READ_CACHE_INVALIDATE への register の書き込み。SDMA は POLL_REGMEM の HDP_FLUSH の形。**engine の bit**（出典: `nbio_v2_3.c` の `nbio_v2_3_hdp_flush_reg`、`nbio_2_3_sh_mask.h` の `BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP0_MASK` …、`gfx_v10_0.c` の `ring_emit_hdp_flush`。review M6）: GFX の ring 0 = CP0 = 0x1、MEC1 の pipe p の compute の queue = CP2 << p（pipe 0〜3 で 0x4・0x8・0x10・0x20）、MEC2 の pipe p = CP6 << p、SDMA0 = 0x400、SDMA1 = 0x800。
- (d) 初期化: A2 で HDP の MISC_CNTL（base + 0xD3）の FLUSH_INVALIDATE_CACHE（bit 0）を立て、1 回 flush。
- 適用: GART の表・page table・CSB・MQD・IB・shader の code・記述子・SMU の driver の表を CPU で書いた後は (a)。`resource_write` は (a)、`resource_read` は (b)。client の surface を CPU で書く経路は present の時に (a) を 1 回行う。

### 5.2 VRAM の配置

| 領域 | 中身 | 整列 |
| --- | --- | --- |
| 予約: GOP の framebuffer | A1 で読んだ HUBP の address − FB の base から、pitch × 高さ。console の surface のまま | — |
| 予約: VBIOS の予約（A1v の vram_usagebyfirmware の firmware と driver の範囲） | 触らない | — |
| 予約: 上端 1 MiB | IP discovery の表（上端 −64 KiB の 10 KiB）、PSP の runtime DB（上端 −1 MiB）、memory training の c2p（上端 −1 MiB の見込み）と p2c（上端 −32 KiB） | — |
| **A3t まで予約: 下 32 MiB**（GOP の framebuffer と VBIOS の予約を除いた残りも） | memory training が壊す範囲。A3t が済む（か skip が決まる）まで allocator から出さない（GART の表・page table・IH の物・PSP の buffer の VRAM の部分を含む。review B3）。A3t の後は普通の pool に戻す | — |
| （後、§15 #7）BAR0 が VRAM 全体を覆う時（firmware の Re-Size BAR）: 全域が 1 つの pool | CPU が書く物（GART の表、page table、CSB、MQD 6 本、IB、shader の code、記述子、SMU の driver の表、client の surface、cursor、`resource_map` される BO）も GPU だけが触る物も同じ pool。TMR は 1 MiB 整列 | 用途ごと |
| 256 MiB の窓の時: 窓の中（先頭 256 MiB、予約を除く） | CPU が書く物（上と同じ） | 用途ごと |
| 256 MiB の窓の時: 窓の外 | TMR（PSP だけが触る）、GPU だけが触る BO（`resource_map` 不可、`resource_read/write` は MM_INDEX の間接で 1 語ずつ） | — |

allocator は「CPU が触るか」の flag を持ち、窓の時は 2 つの pool にする（§15 #7 の回答で今は常にこの形。BAR0 が全域の時の 1 つの pool は後）。A1 の ok の行に配置を出す。

### 5.3 FB と GART（VMID 0）

- **FB の位置**: VRAM の MC address の先頭 = GCMC_VM_FB_LOCATION_BASE の下 24 bit << 24。物理の offset = GCMC_VM_FB_OFFSET << 24（dGPU では 0 の見込み）。VRAM の大きさ = NBIO の MEMSIZE。GOP が設定済みで、driver は読むだけ。
- **GART（VMID 0）**: flat の表（8 byte の entry × 512 MiB / 4 KiB = 1 MiB、VRAM、page 整列）。GART の MC の範囲は FB の外に 4 GiB 整列で置く。entry = (物理 & 0x0000FFFFFFFFF000) | VALID(bit 0) | SYSTEM(bit 1) | SNOOPED(bit 2) | READABLE(bit 5) | WRITEABLE(bit 6) | EXECUTABLE(bit 4) | MTYPE UC(3 << 48)。表を書いた後: memory barrier → HDP flush → 両 hub の VMID 0 の TLB invalidate。
- **両 hub に同じ VMID 0 を設定**（GFXHUB は GFX・compute・SDMA、MMHUB は display・IH・HDP）。順: GFXHUB → MMHUB の各 (a) context 0 の page table の base（物理 | VALID）と START/END（4 KiB の page 番号、32/4 bit に分ける）、(b) system aperture（AGP は無効 = BOT 0xFFFFFF・TOP 0、aperture の LOW/HIGH = FB の範囲 >> 18、default の address = scratch の page、fault の default = dummy page、CNTL2 の read retry）、(c) L1 TLB の CNTL（enable、system access mode 3、advanced driver model、MTYPE UC）、(d) L2 の CNTL 1〜5（既定の値に cache enable・fragment・識別の mode を足す。CNTL3 の既定 0x80100007、CNTL4 0xC1、CNTL5 0x3FE0）、(e) context 0 の enable（depth 0）、(f) identity aperture の無効化、(g) context 1〜15 の設定（5.4）、(h) 18 本の invalidate engine の範囲 = 全域。その後 HDP の初期化（5.1 の (d)）、fault の既定（redirect to default page）、MMHUB → GFXHUB の順に VMID 0 の invalidate。MMHUB の aperture の書き換えは GOP の scanout の経路に当たりうるので、A2 の ok の行に HUBP・OPTC の underflow を出す。
- **invalidate の REQ の語**: (1 << vmid) | 0x00F80000（flush type 0、L2 PTE・PDE0〜2・L1 PTE の invalidate）。ACK の bit = 1 << vmid。MMHUB では SEM の register で acquire/release（power gating の跨ぎで ACK が消える対策）。CPU からの invalidate は engine 17 で REQ を書き、GFXHUB では dummy read を 1 回挟んでから ACK の bit を待つ。
- **invalidate の engine の割り当て**（GFXHUB の 18 本。Linux は engine 2・3 を firmware、17 を CPU に取り、ring には残りを順に配る（`amdgpu_gmc.c` の bitmap 0x1FFF3）。review M6）: GFX の ring 0 = 0、KIQ = 1、MEC1 の pipe 0〜3 の compute の queue = 4〜7、SDMA0 = 8、CPU = 17。ring ごとに別の engine なので、同時の flush が REQ/ACK を奪い合わない。
- **Navi1x の SDMA の errata**: VMID 0 の invalidate を VA の変換と同時に行うと memory が壊れうる。SDMA が動いた後の GART の更新は、SDMA の ring から invalidate を出す（A5 以降）。A2〜A4 では SDMA がまだ無いので CPU から行う。

### 5.4 session の VM: pasid と動的な VMID（判断 11 の回答、Procedure referenced の対象）

出典: Linux v6.19 `amdgpu_ids.c`（`amdgpu_vmid_grab`・`grab_idle`・`grab_used`、LRU、`last_flush`・`active` の fence）、`amdgpu_vm.c`（`amdgpu_vm_flush`: pipeline sync → VM flush → pasid mapping）、`gmc_v10_0.c`（`emit_flush_gpu_tlb`・`emit_pasid_mapping`、VMID の範囲 1〜7 と 8〜15 の分け）、`gfx_v10_0.c`（`ring_emit_vm_flush` の後の PFP_SYNC_ME）、`osssys_5_0_0_offset.h`（IH_VMID_0_LUT 0x0000、IH_VMID_0_LUT_MM 0x0010）。

- context 1〜15: enable、depth 3（4 段）、block size 0（9 bit）、fault の既定 1、**retry 0（no-retry、fault の嵐を防ぐ。Linux の Navi10 の既定は retry 1 だが、回復可能な fault を作らないので no-retry）**、START 0、END = 2^36 − 1（page 番号）。
- **4 段の page table**（PDB2 → PDB1 → PDB0 → PTB、各 512 entry × 8 byte = 4 KiB、VRAM、CPU で書いて HDP flush）: VA の bit [47:39] PDB2、[38:30] PDB1、[29:21] PDB0、[20:12] PTB。PDE = 物理（64 byte 整列）| VALID（system memory の表なら SYSTEM・SNOOPED も）。PTE = 物理 | VALID | SYSTEM（system memory）| SNOOPED | READ | WRITE | EXE | MTYPE。huge page は PDE の bit 54（P）で 2 MiB（PDB0）。fragment の field（bit 11:7）は最初 0。
- **session の持ち物**: root の PD（VRAM）、**pasid**（1〜65535、session ごとに固定。open で取り、close で返す）、今 持っている VMID（無ければ 0）、`tlb_seq`（PD を書き換えるたびに進む）、ring ごとの最後の submission の fence。
- **VMID の pool**（GFXHUB の 1〜15。MMHUB の VMID は使わない: display・IH は VMID 0 の物理）: 各 VMID は {今の session（pasid）, その PD の address, **ring ごと（GFX・compute 4 本・SDMA0）の最後にこの VMID で投入した fence**, 最後の flush を積んだ ring とその fence, `flushed_seq`, LRU の位置} を持つ（review M6: 「最後の fence」が 1 つだと ring をまたぐ使用の完了を取り違える）。
- **submission の時の割り当て**（`vmid` の spinlock の中、Procedure referenced: `amdgpu_vmid_grab`・`grab_idle`・`grab_used`）: (1) session が VMID を持ち、その VMID の記録がまだこの session なら、そのまま使う。ただし **flush が要る**のは: VMID の `flushed_seq` が session の `tlb_seq` より古い（PD を書き換えた）、または**最後の flush を積んだ ring がこの ring と違いその flush の fence が未完了**（別の ring の flush が実行される前にこの ring が走りうる。Linux の `last_flush` の判定）。要れば submission の先頭に VM の flush を積む。(2) 無ければ LRU の末尾から「**全 ring の最後の fence が完了した** VMID」を探す（この ring だけで使われていた VMID は、ring の順で先に実行されるので fence の完了を待たずに取り上げてよい: submission の先頭に pipeline sync を積む。Linux の `amdgpu_sync_peek_fence` が同じ ring の fence を除くのと同じ）。見つかればその session から取り上げて（その session の `vmid` を 0 に）この session に与え、submission の先頭に VM の flush を積む。(3) 完了した VMID が無ければ、LRU の末尾の VMID の fence を待つ（lock の外、2 ms の spin ＋ sleep、WS241 の規則）。16 以上の app が同時に GPU を使う時だけ起きる。
- **VM の flush（ring の上、GFX・compute は PM4、SDMA は SDMA の packet）**: pipeline の sync（ACQUIRE_MEM / 前の IB の完了を待つ）→ WRITE_DATA で context の PTB の base LO/HI（ctx0 + 2 × vmid の register、PD の物理 | VALID）→ WAIT_REG_MEM の write-then-wait で invalidate の REQ（engine は 5.3 の表の ring ごとの割り当て）と ACK → **WRITE_DATA で IH_VMID_0_LUT + vmid = pasid**（fault の帰属）→ GFX は PFP_SYNC_ME。CP の firmware の write-then-wait の条件（ME ≥ 0x46・PFP ≥ 0x68・MEC ≥ 0x5B）は 20260410 の firmware で満たす。
- **fault の帰属**: VM fault の IV の dw3（pasid）で session を引く（VMID は再割り当てされうるので pasid が正）。pasid が 0（割り当て前）なら VMID の記録から引く。
- **取り上げと隔離**: 取り消し（11 節）で LOST にした session の VMID は、**その session の全 ring の record が完了か取り消しになり、queue の reset が成功するまで** pool に返さない（その VMID の wave が居るかもしれない）。reset の成功の後に TLB を invalidate して返す。STUCK なら quarantine（返さない）。**queue の reset の後は全 VMID の `flushed_seq` を 0 にして次の submission で flush し直す**（Linux の `gpu_reset_counter` と同じ考え。review M3）。
- **費用**: session が 15 以下で順に動く desktop では flush は open の直後の 1 回だけ。16 以上の app が動くと switch ごとに flush（Linux と同じ）。
- **選ばなかった案**: 静的な VMID（第 1 版。15 session で ENOSPC。Keiland の desktop は app ごとに session を持つので足りなくなる）。pasid 無し（VMID の再割り当ての後の fault を取り違える）。

### 5.5 resizable BAR（判断 10 の回答、A0・A2）

**§15 #7 の回答（2026-10-15 夜 ユーザー）「まず256MBで実装します。」**: p002 以降の実装は BAR0 の 256 MiB の窓（5.2 の 2 つの pool）だけ。A0 は capability の生の dword・supported の size・今の BAR0 の size を読んで log に出すだけで、BAR0 が広くても先頭 256 MiB だけを使う。下の Re-Size BAR の利用（1 つの pool、8 GiB の窓の map）は後の Phase（Future Work）。実機の事実（2026-10-16、10.0.10.3 の Debian）: capability は BAR0 が 256 MiB・512 MiB・1 GiB、firmware の既定は 256 MiB @0x4000000000、Linux の amdgpu は root port と switch の窓を 0x4100000000〜0x43ffffffff に置き直して BAR0 を 8 GiB にした。

出典: PCI Express Base Specification の Resizable BAR Extended Capability（capability id 0x0015。Capability の register: cap + 4、supported の size の bit 23:4（bit n → 2^(n − 4) MiB、bit 4 = 1 MiB、bit 12 = 256 MiB、bit 17 = 8 GiB、bit 23 = 512 GiB（review m1）。より大きい size は Control の bit 31:16）。Control の register: cap + 8、BAR の index bit 2:0、BAR の数 bit 7:5、BAR の size bit 13:8 = log2(size) − 20）、Linux v6.19 `amdgpu_device.c`（`amdgpu_device_resize_fb_bar`: 今の BAR0 が VRAM 以上なら何もしない、root の bus に 4 GiB より上の窓が要る）、Linux の `drivers/pci/pci.c` の `pci_rebar_get_possible_sizes`（**Q1 の記憶: Sapphire の RX 5600 XT（0x731F、BAR 0）の capability の dword が 0x7000（256 MiB〜1 GiB）と読める不具合を 0x3F000（〜8 GiB）に直す quirk がある。取得範囲の外なので p002 で正本を取って確かめる**）。

- **何のため**: 6 GiB の VRAM を全部 CPU から見る。allocator が 1 つの pool になり、窓の外の BO の `resource_map` の ENOMEM と MM_INDEX の 1 語ずつの転送が無くなる。TMR・shader・記述子の置き場に制約が無くなる。
- **形（review B2 で改めた）: UEFI の Re-Size BAR に任せ、OS の中では BAR を動かさない**。理由: (1) BAR0 を 8 GiB にして置き直すには GPU の上の全部の bridge（CPU の root port と、Navi10 の board の上の PCIe switch の upstream・downstream。実機の topology は p002 で読む）の prefetchable の窓を広げて 4 GiB より上に置き直す必要があり、zedBSD の PCI の層は root の bus の未割り当ての BAR を host bridge の窓に置くだけで bridge の窓を書く口が無い（`pci-pcat-assign.c`）。(2) GOP の framebuffer は bootloader が固定の VA（`ZBL6_FRAMEBUFFER_VIRTUAL_BASE`）に GOP の物理 address で map し、HAL の早い console と pcat の text console がそれを使う。BAR0 を動かすとこの VA は古い物理を指し続け、text console の書き込みが宙に浮く。張り替えは HAL の責務の変更 = ユーザーの事前承認の対象になりうる。(3) UEFI の設定（"Above 4G Decoding" と "Re-Size BAR Support"）が on なら firmware が BAR0 と bridge の窓を広げて置き、GOP もその address で map するので、OS は何もせずに全 VRAM が見える。
- **手順**: A0 で capability の生の dword・supported の size・今の BAR0 の size を読み log に出す。A2 で BAR0 を map: **BAR0 の size ≥ VRAM なら 1 つの pool（5.2）、未満なら 256 MiB の窓の 2 つの pool**。UEFI の Re-Size BAR はユーザーが on にする（12.5。0x731F の quirk の形で firmware が 1 GiB までしか広げない可能性があり、その時は 1 GiB の窓として同じ 2 つの pool の形で使う）。
- **map の大きさ**: pcat の `map_bar` は `drv_pci_bar` の記述のとおりに map する（既存の窓の部分を借りる道もある）。8 GiB の device の map が HAL の device の VA の窓に収まるかを p003 で確かめ、収まらなければ driver が小さい記述（同じ bus_address、1 GiB の size）を作って窓ごとに map する。
- **OS の中の resize は Future Work**（別の WS: bridge の窓の再割り当て、BAR の size の測り直し、boot の framebuffer の VA の張り替えの口と HAL の承認の要否）。**ユーザーの決定 10「resizable BAR も使う」は UEFI の Re-Size BAR で満たす形になる**（15 節 #7 でユーザーに示す）。
- **選ばなかった案**: driver が PCI の config を直に書く（PCI の層の BAR の記録と食い違う。bridge の窓も同じ）。改訂 2 の最初の案（PCI の層に `drv_pci_device_resize_bar` を足す）は上の (1)(2) で動かない。

## 6. IH（割り込み）

出典（値）: Linux v6.19 `navi10_ih.c`・`amdgpu_ih.c`・`amdgpu_ih.h`・`amdgpu_irq.c`・`osssys_5_0_0_offset.h`・`osssys_5_0_0_sh_mask.h`・`nbio_v2_3.c`・`soc15_ih_clientid.h`・`ivsrcid/`、`display/amdgpu_dm/amdgpu_dm.c`（DCN の割り込みの登録）、`dcn_2_0_0_sh_mask.h`（HPD の register）、`smu_v11_0.c`（THM・SMUIO・MP1 の source）。

- ring 0 だけ（256 KiB、2 の冪、GART の system memory、256 byte 整列）、wptr の writeback の語も system memory。MSI 1 本（MSI-X なら vector 0）。
- 設定の順（A2 で用意、enable は A3 の最初。review B3）: RB_CNTL の enable を 0（GPU の timestamp は 1、INTR 0）→ RPTR・WPTR = 0 → NBIO の INTERRUPT_CNTL2 = dummy page >> 8、INTERRUPT_CNTL の dummy read override 0・nonsnoop 0 → RB_BASE = address >> 8、BASE_HI = (address >> 40) & 0xFF → RB_CNTL: MC_SPACE 4（GPUVM）、WPTR overflow の enable と clear、RB_SIZE = log2(byte / 4)（256 KiB で 16）、writeback の enable、MC_SNOOP、RPTR_REARM = MSI → WPTR_ADDR LO/HI → WPTR・RPTR = 0 → DOORBELL_RPTR = index | ENABLE → NBIO の IH の doorbell の範囲（offset = index、size 2）→ bus master → （A3 の最初に）RB_CNTL の enable・INTR を 1。
- RB_CNTL の field: RB_ENABLE bit 0、RB_SIZE 5:1、GPU_TS 7、WPTR_WRITEBACK 8、WPTR_OVERFLOW_ENABLE 16、ENABLE_INTR 17、MC_SNOOP 20、RPTR_REARM 21、MC_VMID 27:24、MC_SPACE 30:28、WPTR_OVERFLOW_CLEAR 31。
- 処理（IH の thread）: writeback の wptr を読み、rptr ≠ wptr の間 32 byte の entry を dispatch、rptr を書いて doorbell、**その後 wptr を読み直して動いていればもう一度回す**。entry: dw0 = client 7:0・src 15:8・ring 23:16・vmid 27:24・bit 31 = MMHUB、dw1/2 = timestamp、dw3 = pasid（15:0）、dw4〜7 = src_data[0..3]。
- 使う source（client、src）: GFX の EOP = GRBM_CP 0x14、181（ring_id から me・pipe・queue。**compute の queue の EOP も同じ src で me = 1**）。SDMA0 の trap = 0x08、224。VM fault = 0x12（MMHUB）/0x1B（GFXHUB）、0。CP の bad opcode 183・priv reg 184・priv inst 185（**IV の vmid で session を引いて取り消し**、11 節）。DCN = DCE 0x04: OTG の VSTARTUP 0x3C〜0x41、HUBP の flip 0x4F〜0x54、HPD 9（ctx id 0〜5、RX は 6〜11）。**THM 0x0F の src 0（L2H）・1（H2L）、ROM_SMUIO（client の値は p012）= hardware CTF、MP1 0x1F の src 0xFE**（4.4）。
- 各 source の enable は IP の側: CP_INT_CNTL_RING0 の TIME_STAMP_INT_ENABLE（bit 26）、compute の queue は MQD/HQD の側の EOP の割り込み、SDMA0_CNTL の TRAP_ENABLE（bit 0）、OTG_GLOBAL_SYNC_STATUS の VSTARTUP_INT_EN（0x1）と EVENT_CLEAR（0x10）、HUBPREQ の SURFACE_FLIP_INTERRUPT の INT_MASK（0x1）と CLEAR（0x100）、HPD: HPDn の DC_HPD_INT_CONTROL（HPD0 は DCN の seg 2 の 0x1F14 の組、stride 8。p006 で offset を出典付きで写す）の DC_HPD_INT_EN（0x10000）と DC_HPD_INT_ACK（0x1）、DC_HPD_RX_INT_EN（0x1000000）、status の DC_HPD_SENSE（0x2）。context の CNTL の fault の interrupt bit。THM・MP1 は 4.4 の 9。
- doorbell の index（64 bit の slot の番号を << 1 して dword）: **KIQ = 0x000、MEC の ring 0〜 = 0x003〜（compute の queue 4 本は 0x003〜0x006）**、GFX の ring 0 = 0x08B、SDMA0 = 0x100、SDMA1 = 0x10A、IH = 0x178。NBIO の doorbell の aperture の enable を A2 で立てる。MEC の doorbell の範囲（CP_MEC_DOORBELL_RANGE_LOWER/UPPER、0x1DFC/0x1DFD）は KIQ と compute の queue を覆うように A5 で書く。

## 7. CP と SDMA

出典（値）: Linux v6.19 `gfx_v10_0.c`（golden、constants、RLC、KIQ の `kiq_init_register`・`kiq_setting`、`gfx_mqd_init`・`compute_mqd_init`、`kiq_set_resources`・`kiq_map_queues`・`kiq_unmap_queues`・`kiq_query_status`、`cp_gfx_set_doorbell`、`reset_kgq`・`reset_kcq`・`kiq_reset_hw_queue`、`set_safe_mode`）、`amdgpu_gfx.c`（`kiq_acquire`、`enable_kgq`・`enable_kcq`）、`amdgpu_rlc.c`・`amdgpu_ring.c`（`reset_helper_begin/end`、backup と re-emit）・`amdgpu_fence.c`・`nvd.h`（PM4 の opcode と MAP/UNMAP/SET_RESOURCES/QUERY_STATUS の field）・`amdgpu_gfx.h`（UNMAP の action の enum）・`clearstate_gfx10.h`・`sdma_v5_0.c`・`navi10_sdma_pkt_open.h`・`v10_structs.h`（`v10_gfx_mqd`・`v10_compute_mqd`）・`gc_10_1_0_*.h`、Mesa 25.3.6 `src/amd/common/sid.h`。

### 7.1 RLC・golden・constants（A5 の前半）

golden の表（GC 40 項目、SDMA 24＋2 項目、値は出典付きのデータ）、GRBM_CNTL の READ_TIMEOUT、GB_ADDR_CONFIG の読み、RB・CU・TCC の mask、tile steering、VMID 0〜15 ごとの SH_MEM_CONFIG = 0xC00C と SH_MEM_BASES、RLC は PSP の autoload で動いているので halt・load・start はしない（CP_STAT == 0 と RLC_RLCS_BOOTLOAD_STATUS の bit 31 を待つ）、CSB（949 dword）を VRAM に置き RLC_CSIB に、SRM の enable、tcp harvest。CAM remap は入れない。

### 7.2 KIQ（判断 4 の回答。Linux の既定と同じ道）

- **何のため**: (1) GFX と compute の queue を firmware（MEC）の scheduler に map する正規の道（MAP_QUEUES）。(2) 取り消し（11 節）に要る **UNMAP_QUEUES（RESET・PREEMPT）、CP_VMID_RESET の write-then-wait、QUERY_STATUS** を firmware の順で出せる。(3) GFXOFF や SR-IOV の register の access（今は使わない）。第 1 版の「CP_RB0 の register で直に」は KIQ の失敗の時の退路（`amdrdna.kiq=0`）として残す。
- **場所**: Linux の Navi10 と同じ **MEC2（me = 2）の pipe 1 queue 0**（`amdgpu_gfx_kiq_acquire` は queue 0 で MEC1 の pipe 2・3 を避け、結果 MEC2 の pipe 1 になる。review M2。MEC1 の pipe 0 queue 0 は Linux では compute の ring 0 の場所）。doorbell 0x000（<< 1）。MQD（`v10_compute_mqd`、512 dword、VRAM か GART の system memory）、EOP の buffer（2 KiB）、ring（4 KiB、GART の system memory）、rptr の writeback。
- **立て方**（Procedure referenced: `gfx_v10_0_kiq_init_queue`・`kiq_init_register`・`kiq_setting`、`grbm_select` の mutex の下）: RLC_CP_SCHEDULERS（0x4CAA）を読み、下位 byte を (me << 5) | (pipe << 3) | queue に置き換え、**| 0x80 を付けて 1 回書く**（v6.19 の形。review M2） → MQD を 0 で埋め、GRBM_GFX_CNTL で me/pipe/queue を選び、MQD の欄（7.4 の表）を埋める → **CP_PQ_WPTR_POLL_CNTL（0x1E23）の EN = 0** → HQD が active なら DEQUEUE_REQUEST = 1 で待つ → DOORBELL_CONTROL = 0 → EOP の base/control → MQD の base/control → PQ の base → PQ_CONTROL → RPTR_REPORT_ADDR → WPTR_POLL_ADDR → MEC の doorbell の範囲 → DOORBELL_CONTROL（offset | EN）→ WPTR_LO/HI → VMID → PERSISTENT_STATE → HQD_ACTIVE = 1 → **CP_PQ_STATUS（0x1E58）の DOORBELL_ENABLE = 1** → GRBM の選択を戻す → CP_MEC_CNTL（0xE2D）= 0（MEC1・MEC2 の unhalt。halt の bit は ME1 30・ME2 28）。
- **KIQ の ring test**: WRITE_DATA で writeback の語に既知の値（Linux の `kiq` の fence は WRITE_DATA DST_SEL 5 | WR_CONFIRM）。これが**最初に実行される PM4**なので A5 の最初の確かめ。
- **SET_RESOURCES**（0xA0、8 dword）: dw1 = VMID_MASK 0 | QUEUE_TYPE 0（KIQ）、dw2/3 = queue の mask = **map する compute の queue 4 本の bit だけ**（Linux の `queue_bitmap` と同じ意味。review M2。bit の番号 = pipe × 8 + queue の見込み、p005 で `amdgpu_gfx.c` の `amdgpu_queue_mask_bit_to_set_resource_bit` から写す）、dw4/5 = cleaner shader の address（0。Linux は cleaner shader を持つが amdrdna は最初 0）、dw6 = 0（oac）、dw7 = 0（gds）。
- **KIQ の投入**（`kiq` の mutex の中）: ring に packet を積み、doorbell、完了は ring test と同じ WRITE_DATA の fence の語を spin（最大 100 ms）。KIQ 自体が答えなければ device の fault（11 節）。

### 7.3 GFX の queue 0（KIQ の MAP_QUEUES、engine 4）

- Navi10 の GFX は me 1 本・pipe 1 本・queue 8 本（`gfx_v10_0_sw_init`）。Linux は 1 本（`GFX10_NUM_GFX_RINGS_NV1X` 1）だけ map する。amdrdna も **1 本**（queue 0）。理由: 複数の GFX の HQD の time slice（CP_GFX_HQD_QUANTUM）が Navi10 の firmware で安全かは Linux に例が無い（Sienna Cichlid の 2 本だけ）。session ごとの GFX の queue は Future Work（11.1）。
- **MQD**（`v10_gfx_mqd`、Procedure referenced: `gfx_v10_0_gfx_mqd_init`）: wptr/wptr_hi 0、MQD の base（& ~3）と hi、MQD_CONTROL（VMID 0、PRIV_STATE 1、CACHE_POLICY 0）、HQD_VMID 0、queue の priority、HQD_QUANTUM の QUANTUM_EN 1、HQD の base（ring の MC address >> 8）と hi、rptr の address（writeback、& ~3）と hi（& 0xFFFF）、wptr poll の address と hi、HQD_CNTL = RB_BUFSZ(log2(byte / 4) − 1) | RB_BLKSZ(RB_BUFSZ − 2)（**BUF_SWAP は 0**: Linux は big endian の時だけ立てる。review M1）、doorbell の control（offset = 0x8B << 1、EN）、rptr の現在値、**HQD_ACTIVE 1**。欄の dword の位置は p005 で `v10_structs.h` から出典付きに写し、`offsetof` の static assert で確かめる。**init の時の MQD の写し**を system memory に保ち、queue の reset（11 節）ではそれを書き戻す。
- **map**: ring（GART の system memory、256 byte 整列）と MQD を用意 → GRBM_GFX_CNTL で me 0 pipe 0 queue 0 を選んで MQD の欄を埋める（register は書かない）→ GFX の doorbell の範囲（CP_RB_DOORBELL_RANGE_LOWER/UPPER、`cp_gfx_set_doorbell`）→ 選択を戻す → **KIQ に MAP_QUEUES（0xA2、7 dword）**: dw1 = QUEUE_SEL 0 | VMID 0 | QUEUE 0 (<< 13) | PIPE 0 (<< 16) | **ME 1 (<< 18。Linux は `me == 1 ? 0 : 1` なので GFX（me 0）は 1、MEC1 は 0、MEC2 は 1。review M2）** | QUEUE_TYPE 0 (<< 21) | ALLOC_FORMAT 0 (<< 24) | **ENGINE_SEL 4 (<< 26)** | NUM_QUEUES 1 (<< 29)、dw2 = DOORBELL_OFFSET (<< 2)、dw3/4 = MQD の address、dw5/6 = wptr の address → KIQ の ring test で完了を確かめる → ME/PFP/CE の unhalt（CP_ME_CNTL の halt の bit 24・26・28 を落とし CP_STAT == 0）→ preamble（CSB と同じ列 ＋ SET_BASE の CE partition 0x8000/0x8000）→ 256 dword に NOP で揃えて wptr を進め doorbell → GFX の ring test（SCRATCH_REG0（uconfig の offset 0x40）を MMIO で既知の値にし、SET_UCONFIG_REG で別の値を書き、MMIO で読み戻す）。
- **退路**（`amdrdna.kiq=0`）: CP_RB0 の register の道（review 反映版の 7.2）: CP_RB_WPTR_DELAY = 0 → CP_RB_VMID = 0 → pipe 0 → CP_RB0_CNTL = RB_BUFSZ(log2(byte/8)) | RB_BLKSZ(同 − 2)（BUF_SWAP 0）→ WPTR・WPTR_HI = 0 → RPTR_ADDR LO/HI（HI は & 0xFFFF）→ WPTR_POLL_ADDR LO/HI → 1 ms → CNTL を再び → RB0_BASE = MC address >> 8、BASE_HI = (address >> 8) >> 32 → CP_RB_ACTIVE = 1 → doorbell（CP_RB_DOORBELL_CONTROL の OFFSET = index・EN、RANGE_LOWER = index、RANGE_UPPER = 0x0FFFFFFC）→ CP_MAX_CONTEXT = 7、CP_DEVICE_ID = 1 → unhalt → preamble → ring test。この道では queue の reset（11 節）は使えず、取り消しは device の fault まで。

### 7.4 compute の queue（4 本、judgment 4 の回答）

- **数と場所**: MEC1 の pipe 0〜3 の queue 1（pipe ごとに 1 本、KIQ の pipe 0 queue 0 を避ける）。4 本にする理由: pipe ごとに 1 本なら MEC の pipe の並列が使え、MQD・EOP・ring・doorbell の資源が小さい（Linux の既定の 8 本は pipe × queue の格子）。doorbell 0x003〜0x006（<< 1）。
- **MQD**（`v10_compute_mqd`、Procedure referenced: `gfx_v10_0_compute_mqd_init`、値は note d の 4.2 の表、dword の位置は p005 で `v10_structs.h` から）: header 0xC0310800（review m7）、pipelinestat_enable 1、static_thread_mgmt_se0..3 = 0xFFFFFFFF、misc_reserved 3、EOP の base（>> 8）と EOP_CONTROL（EOP_SIZE 8 = 2 KiB）、PQ_DOORBELL_CONTROL（offset、EN）、DEQUEUE_REQUEST・PQ_RPTR・PQ_WPTR 0、MQD の base と control（VMID 0）、PQ の base（>> 8）、PQ_CONTROL = QUEUE_SIZE log2(byte / 4) − 1 | RPTR_BLOCK_SIZE 9 | UNORD_DISPATCH | PRIV_STATE | KMD_QUEUE、RPTR_REPORT_ADDR、WPTR_POLL_ADDR、HQD_VMID 0、PERSISTENT_STATE の PRELOAD_SIZE 0x53、IB_CONTROL の MIN_IB_AVAIL_SIZE 3、priority、**HQD_ACTIVE 0**（MAP_QUEUES が立てる）。
- **map**: MQD を埋める（GRBM の選択の下、register は書かない。rptr は今の register から読む。init の時の写しを保つ）→ KIQ に MAP_QUEUES（ENGINE_SEL 0、ME 0 = me 1、PIPE p、QUEUE 1）→ KIQ の ring test → compute の ring test（WRITE_DATA で writeback の語）。
- **用途**: (1) A5 の最初の dispatch の試験（p007 の compiler の最小の compute の program を DISPATCH_DIRECT で、結果を storage の buffer で確かめる）。(2) kernel の自分の仕事（将来の clear・copy の shader、cleaner shader）。(3) **Vulkan の compute 専用の queue family**（executor が 2 つ目の family を出す）: desktop の app は使わないので p008 の必須にはせず、A5c の取り消しの試験（12.5 の vkhang の compute の場合）で compute の queue の reset を確かめるために p013 で最小の経路（compute の pipeline の dispatch を compute の queue に投入する family）を作る。universal の family（graphics ＋ compute）の dispatch は順序を守るため GFX の ring のまま（engine をまたぐ順序の同期は作らない。第 1 版の SDMA と同じ理由）。
- **fence**: compute の queue の fence も RELEASE_MEM（EOP の event、compute の shader type の bit を header に）。EOP の割り込みの ring_id から me 1 の pipe を引く。
- **選ばなかった案**: KIQ 無しの直接の HQD（note d の 4.5。firmware の preempt・unmap が無く、取り消しが register の DEQUEUE だけになる）。session ごとの compute の queue（MEC1 の 31 本を配る。Keiland の app の数を超えると配れない。将来の案）。

### 7.5 SDMA0 の gfx queue

golden の後、SEM_WAIT_FAIL_TIMER = 0 → RB_CNTL の RB_SIZE = log2(byte/4) → RPTR・WPTR = 0 → WPTR_POLL の address と F32_POLL_ENABLE → RPTR_ADDR → RB_BASE = MC address >> 8、BASE_HI = address >> 40 → MINOR_PTR_UPDATE = 1 → WPTR = 0 → doorbell（GFX_DOORBELL の ENABLE、OFFSET = index）と NBIO の SDMA0 の doorbell の範囲（size 20）→ MINOR_PTR_UPDATE = 0 → SDMA0_CNTL の UTC_L1_ENABLE・MIDCMD_PREEMPT、AUTO_CTXSW → UTCL1_CNTL の RESP_MODE 3・REDO_DELAY 9、UTCL1_PAGE の policy → F32_CNTL の HALT = 0 → RB_CNTL に RB_ENABLE と RPTR_WRITEBACK → IB_CNTL の IB_ENABLE。ring test: WRITE（untiled）で writeback の語に既知の値。wptr は byte（dword << 2）で 64 bit の doorbell。SDMA の用途は **kernel の copy だけ**（A5 の copy の試験、GART の invalidate の errata の回避、将来の kernel の blit）。Vulkan の queue の中の copy・clear は**全部 GFX の ring**（engine をまたぐ順序の同期を作らない）。SDMA1 は最初は使わない。

### 7.6 packet の形（executor と driver が組み立てる物）

- **PM4 の type 3 の header**: bit 31:30 = 3、29:16 = body の dword 数 − 1、15:8 = opcode、bit 1 = compute（shader type）、bit 0 = predicate。1 dword の NOP は count 0x3FFF。
- **SET_*_REG の base**（dword）: CONFIG 0x2000（opcode 0x68）、CONTEXT 0xA000（0x69）、SH 0x2C00（0x76）、UCONFIG 0xC000（0x79）。
- **使う opcode**: NOP 0x10、SET_BASE 0x11、CLEAR_STATE 0x12、DISPATCH_DIRECT 0x15、DRAW_INDEX_2 0x27、CONTEXT_CONTROL 0x28、DRAW_INDEX_AUTO 0x2D、NUM_INSTANCES 0x2F、WRITE_DATA 0x37、WAIT_REG_MEM 0x3C、INDIRECT_BUFFER 0x3F、COPY_DATA 0x40、PFP_SYNC_ME 0x42、EVENT_WRITE 0x46、RELEASE_MEM 0x49、PREAMBLE_CNTL 0x4A、DMA_DATA 0x50、ACQUIRE_MEM 0x58、**SET_RESOURCES 0xA0、MAP_QUEUES 0xA2、UNMAP_QUEUES 0xA3、QUERY_STATUS 0xA4**（KIQ だけ）。
- **UNMAP_QUEUES**（0xA3、6 dword）: dw1 = ACTION（0 PREEMPT、1 RESET、2 DISABLE_PROCESS、3 PREEMPT_NO_UNMAP）| QUEUE_SEL 0 (<< 4) | ENGINE_SEL (<< 26、GFX 4・compute 0) | NUM_QUEUES 1 (<< 29)、dw2 = DOORBELL_OFFSET0 (<< 2)、dw3〜5 = 0（PREEMPT_NO_UNMAP では trail fence の address と seq）。
- **fence（GFX・compute）**: RELEASE_MEM（8 dword）: dw1 = event CACHE_FLUSH_AND_INV_TS（0x14）| index 5 << 8 | GCR の GLM_WB(12) GLM_INV(13) GL2_WB(21) SEQ(22) | cache policy 3 << 25、dw2 = DATA_SEL 1 << 29 | INT_SEL 2 << 24、dw3/4 = address、dw5/6 = 値、dw7 = 0。**fence の値だけで buffer の解放を決めない**（10.2）。
- **fence（SDMA）**: FENCE（op 5、MTYPE 3）＋ TRAP（op 6）。
- **IB**: INDIRECT_BUFFER: dw1/2 = address、dw3 = 大きさ（dword）| VMID << 24。前に CONTEXT_CONTROL と HDP の flush、後に invalidate。
- **cache**: ACQUIRE_MEM（8 dword、GCR_CNTL 0xC3B1）を IB の前に。PS/CS の partial flush は EVENT_WRITE。
- **VM の flush**: 5.4。
- **copy**: SDMA の COPY_LINEAR・CONSTANT_FILL は kernel の copy。CP の DMA_DATA は queue の中の copy・fill。

### 7.7 submission の record（取り消しの流し直しのため）

各 ring（GFX・compute 4 本）は submission ごとに {session, pasid, vmid, ring の始まりと終わりの dword, **fence の packet の始まりと終わり**, fence の値, IB の一覧, 流し直した回数} の record を ring の大きさ分の環で持つ（`ring` の spinlock）。取り消し（11 節）で queue を戻す時（Procedure referenced: `amdgpu_ring_backup_unprocessed_commands`・`amdgpu_ring_reset_helper_end`、`amdgpu_fence_driver_guilty_force_completion`。review M3）: (1) guilty の record 以降の全 record を見て、guilty の session の record は **command は捨てるが fence の packet だけ backup に入れる**（fence の通し番号を GPU に進めさせる）、他の session の record は command と fence の両方を backup に入れる。(2) guilty の session の全 record の fence は error で完了（guilty の record は ETIME、同じ session の後の record は ECANCELED）。(3) queue を MQD の写しから戻し（ring は NOP で埋め、rptr・wptr・writeback を 0）、remap、ring test。(4) backup を ring に書いて doorbell。**流し直した record の回数を数え、2 回目の reset で同じ record がまた guilty になったら、backup に入れずに全部を取り消す**（同じ仕事で reset を繰り返さない）。(5) 今の executor（nvrtx の写し、1 worker で submission ごとに同期で待つ）では ring の上の他 session の未完了の record は実際にはほとんど無く、流し直しの対象は主に kernel 自身の packet（VM の flush・fence・preamble）と compute の family の record。

## 8. DCN 2.0（display）

出典（値）: Linux v6.19 `display/dc/`（`dcn20_hubp.c`・`dcn10_hubp.c`・`dcn10_optc.c`・`dcn20_optc.c`・`dcn10_mpc.c`・`dcn20_dpp.c`・`dcn10_dpp.c`・`dcn10_link_encoder.c`・`dcn10_stream_encoder.c`・`dcn20_hwseq.c`・`dcn10_hwseq.c`・`irq_service_dcn20.c`・`dce_clock_source.c`・`bios_parser_helper.c`・`dc_hw_types.h`・`inc/hw/hubp.h`）、`dcn_2_0_0_offset.h`・`dcn_2_0_0_sh_mask.h`、`navi10_ip_offset.h`、`display/amdgpu_dm/amdgpu_dm.c`・`amdgpu_dm_plane.c`・`amdgpu_dm_crtc.c`、`gmc_v10_0.c`。mode set（8.6）の出典は 8.6 に。

### 8.0 Linux との違い（重要）

Linux の DC は desktop の DP・HDMI では **GOP の画面を保たない**（seamless boot は eDP だけ）。初期化で全 OTG を blank し plane を消し、最初の commit で full mode set をする。amdrdna の takeover（A6〜A8）は「GOP の timing・link を残して surface だけ替える」Linux に無い道で、**topology は register から自分で読む**。mode set（A10〜A13）は Linux の DC の道に近いが、GOP の出力先の connector に限り、DML は単一 pipe の形に絞る（8.6）。

### 8.1 register の addressing

DCN の base は discovery の表から（静的な header では seg 0 = 0x12、seg 1 = 0xC0、seg 2 = 0x34C0、seg 3 = 0x9000）。pipe の register の大半は seg 2。instance の stride: HUBP/HUBPREQ/HUBPRET/CURSOR 0xDC、OTG 0x80、MPCC 0x1B、MPC_OUT の mux 0x4、ODM 0x10、OPP/DPG 0x5A、DIG/DP 0x100、DP_AUX 0x1C、HPD 0x8。**DPP は 0〜3 が 0x16B の stride で 4・5 は別の位置（0x35D0・0x373B）**なので表で持つ。p002 で A1 に要る register の offset を phase.md か header に出典付きで写す。

### 8.2 GOP の readout（A1、読むだけ）

1. OTG 0〜5 の OTG_CONTROL の bit 0（MASTER_EN）。点いている物の timing: H_TOTAL・V_TOTAL（+1 が total）、H/V_BLANK_START_END（active の幅 = BLANK_START − BLANK_END）、H/V_SYNC_A と POL。
2. OTG k → OPP: ODMk_OPTC_DATA_SOURCE_SELECT の SEG0_SRC_SEL（bit 11:8）が OPP の id（NUM_OF_INPUT_SEGMENT が 1 なら ODM combine で 2 つ、SEG1 = 0xF は 1 つ）。
3. OPP j → MPCC: MPC_OUTj_MUX の bit 3:0（0xF は無し）。MPCC m: TOP_SEL（DPP の id、0xF は無し）、BOT_SEL（次の MPCC）、OPP_ID、MPCC_CONTROL の mode・alpha、STATUS。
4. DPP d = HUBP d。HUBPd の DCHUBP_CNTL の VTG_SEL（bit 7:4）が k に等しいことを確かめる。
5. HUBP の surface: SURFACE_CONFIG（PIXEL_FORMAT bit 6:0、期待 8 = ARGB8888、ROTATION 9:8 = 0）、TILING_CONFIG の SW_MODE（bit 4:0、期待 0 = linear）、SURFACE_PITCH（bit 13:0 = pitch − 1、画素）、PRIMARY_SURFACE_ADDRESS と HIGH（16 bit）、PRI_VIEWPORT の START・DIMENSION、SURFACE_CONTROL の DCC（期待 0）、HUBPRET_CONTROL の crossbar（ABGR の判定）、HUBP_CLK_CNTL と DPP_CONTROL の clock。
6. 出力: DIGn_DIG_BE_EN_CNTL の bit 0（n = PHY の index）、DIG_BE_CNTL の FE_SOURCE_SELECT（bit 14:8、one-hot A〜G）と DIG_MODE（bit 18:16: 1 DP、2 DVI、3 HDMI、5 DP MST）、DIGf_DIG_FE_CNTL の SOURCE_SELECT（bit 2:0 = OTG）。DP なら DP_VID_STREAM_CNTL の enable と status、DP_LINK_CNTL の training complete。
7. BIOS_SCRATCH_6 の bit 4（accelerated mode、GOP の後は 0 の見込み）。D1〜D6 の VGA_CONTROL（DCN の seg 1 の 0xC から）の bit 0: **立っていれば legacy の VGA の mode で HUBP の surface は使われていないので attach しない**。OTG_MASTER_UPDATE_LOCK が 0 であること（立っていると flip が latch しない）。
8. surface の address − FB の base（5.3）= VRAM の offset。これが loader の GOP の framebuffer の物理 − BAR0 の base と一致するのが A1 の合格。
9. **clock の読み**（mode set のため、review M11）: DENTIST の分周（DISPCLK・DPPCLK）、DCCG の DPREFCLK・DP の DTO、DCFCLK の今の値を読んで log に出す（register は p014 で `dcn20_clk_mgr.c`・`dccg` の header から写す）。これが mode set で出せる pixel rate の上限になる（8.6 の clock）。

### 8.3 surface の替え（A6）と flip

- client の surface: VRAM、256 byte 整列、format 8（32 bpp）、linear、**pitch・幅・高さは GOP と同じ**（DML の watermark・DLG・TTU を GOP の値のまま使うため。大きさや format を変えると再計算が要る）。GOP の framebuffer は console の surface のままで、display の release・fault・shutdown では flip で戻る（2 節の 4）。
- flip の順（Procedure referenced）: DCSURF_FLIP_CONTROL の FLIP_TYPE（bit 1、0 = VUPDATE で latch）→ VMID_SETTINGS_0 = 0 → SURFACE_CONTROL の TMZ 0 → **ADDRESS_HIGH → ADDRESS（LOW）の順**（LOW の書き込みで全部が latch する）。CPU が surface を書いた後なら先に HDP の flush。
- 完了: FLIP_CONTROL の FLIP_PENDING（bit 8）が 0 かつ EARLIEST_INUSE（LOW/HIGH）が要求した address。HUBP の flip の割り込み（INT_MASK bit 0、CLEAR bit 8、OCCURRED bit 16、STATUS bit 17）で present の完了を `drv_gpu_complete`。
- vblank: OTG_GLOBAL_SYNC_STATUS の VSTARTUP_INT_EN（bit 0）と EVENT_CLEAR（bit 4）。frame counter = OTG_STATUS_FRAME_COUNT の 24 bit。位置 = OTG_STATUS_POSITION。
- **触らない物（takeover の間）**: OTG の timing と VSTARTUP/VUPDATE/VREADY、ODM、pixel clock・PLL・DTO、DIG・PHY・link、OPP の FMT、MPC の木、DPP の scaler・CM、HUBP の DLG/TTU/RQ、HUBBUB の watermark、DENTIST の分周、power gating（**HUBP5 の domain は触ると chip が reset しうる**）。
- 危険: SMU が UCLK を変えると GOP の watermark が p-state の切り替えを覆わず underflow しうる。Tier F では UCLK DPM が無いので起きない。HUBP_UNDERFLOW_STATUS（DCHUBP_CNTL bit 30:28）と OPTC の UNDERFLOW_OCCURRED（bit 10）を A2・A6 の ok の行に出す。

### 8.4 cursor（A7）

HUBP: CURSOR_SURFACE_ADDRESS_HIGH → ADDRESS → CURSOR_SIZE（HEIGHT bit 8:0、WIDTH 24:16）→ CURSOR_CONTROL（MODE bit 10:8 = 2 premultiplied、PITCH 17:16: 0 = 64・1 = 128・2 = 256 画素、LINES_PER_CHUNK 28:24: 64 幅で 3）→ HUBPREQ の CURSOR_SETTINGS（DST_Y_OFFSET 0、CHUNK_HDL_ADJUST 3）。DPP: CNVC_CUR の CURSOR0_CONTROL の MODE（bit 6:4）と ENABLE。位置: CURSOR_POSITION（Y 13:0、X 29:16）、HOT_SPOT、DST_OFFSET（timing の offset = (x − hotspot) × dchub の refclk / pixel clock。pixel clock は DP なら DP_DTO の PHASE から。HDMI では読めないので 0 のまま試す）。負の位置は 0 に clamp して hotspot に足す。最大 256×256。cursor の buffer は VRAM の 4 KiB 整列（amdgpu_dm は BO の address をそのまま渡し、整列の検査は無い。実機で確かめる）。amdgpu_dm は mode 2（premultiplied）、pitch = buffer の幅。`cursor_set` は image を kernel の cursor の buffer に copy（HDP flush）してから address を書く。

### 8.5 release と shutdown

takeover の時に GOP の HUBP の state（address・CONFIG・PITCH・TILING・FLIP_CONTROL・CURSOR_CONTROL・flip の INT_MASK）を保存し、release（display の lease の終わり、device の fault、shutdown）では cursor を HUBP・DPP で無効にし、割り込みを止め、GOP の address に vsync の flip で戻し pending が消えるのを待ってから client の buffer を返す。timing・link を触っていないので GOP の framebuffer の絵（text console が書き続けている）がそのまま出る。warm reboot の前に BIOS_SCRATCH_6 の bit 4 を 0 にする（Linux の `dc_disable_accelerated_mode` と同じ意図）。**mode set をした後の release**: 8.6 の「戻し」（GOP の mode に組み直す）。shutdown では組み直さない（次の起動で GOP が post し直す。見込み、14 節）。

### 8.6 mode set（A10〜A13、判断 7 の回答）

出典（手順と値。p014・p015 で file ごとに監査の表に足す）: Linux v6.19 `amdgpu/atom.c`・`atom.h`（AtomBIOS の bytecode の interpreter: opcode の表、operand の encode、workspace・parameter の空間、`atom_execute_table`）、`include/atombios.h`・`atomfirmware.h`（opcode の値、command の表の parameter の struct: `set_pixel_clock_parameters_v7`、`dig_encoder_control_parameters_v1_5`、`dig_transmitter_control_parameters_v1_6`、`enable_crtc_parameters`、`enable_disp_power_gating_parameters_v2_1` …）、`display/dc/bios/command_table2.c`・`bios_parser2.c`（どの表をどの parameter で呼ぶか）、`display/dc/dce/dce_aux.c`（AUX の engine）・`dce_i2c_hw.c`（DDC の I2C）、`display/dc/link/protocols/link_dp_training.c`・`link_dp_training_8b_10b.c`・`link_dp_capability.c`・`link_dpcd.c`（DPCD の読み書きと training の順）、`display/dc/link/link_detection.c`（EDID と detect）、`display/dc/hwss/dcn20/dcn20_hwseq.c`（`enable_stream_timing`・`update_dchubp_dpp`・`blank_pixel_data`・`enable_plane`）、`display/dc/resource/dcn20/dcn20_resource.c`、`dcn10_optc.c`（`program_timing`）、`dcn20_optc.c`（ODM の bypass）、`dcn10_stream_encoder.c`（DP の MSA・VID stream、HDMI の設定）、`dcn10_link_encoder.c`（DIG の enable、transmitter control の呼び出し）、`dcn20_hubbub.c`（watermark の register）、`display/dc/dml/dcn20/display_rq_dlg_calc_20.c`・`dcn20_fpu.c`・`display_mode_vba_20.c`（DLG・TTU・watermark の式）、VESA の DisplayPort 1.4 と E-EDID の規格（公開の規格。training の手順と DPCD の address、EDID の形）。

**範囲**: GOP の出力先の connector（A1 の DIG と DIG_MODE）に対して、(a) 同じ mode の組み直し（A11）、(b) EDID の別の mode（A12）、(c) hotplug（同じ connector の抜き差し、短い pulse、別の monitor、A13）。**別の connector への出力は Keiland が その output を claim して present した時だけ**（Guardrail。UAPI の `display.mode` は列挙と検証だけで mode を設定する op ではない。review M10、下の 10）。DP を先に（RX 5600 XT の monitor は DP の予定）、HDMI は p015 の後半。DP MST・eDP・DVI・HDR・ODM（4K 以上の 2 pipe）・scaling・DCC・tiling・**boot の clock を越える mode**（下の clock）は範囲の外。

**部品**:

1. **AtomBIOS の command の表の interpreter**（p014）: VBIOS の bytecode を実行する。opcode（約 120 個: 転送・論理・算術・比較・分岐・遅延・呼び出し・register の block の選択・FB の base・switch …）と operand の種類（register・parameter・workspace・frame buffer・immediate・PLL・MC の各空間、byte/word/dword の幅、swizzle）は `atom.c` の表と `atombios.h` の enum から**データ**として取り、実行の手順は自分の言葉（Procedure referenced）。**opcode の名前は振る舞いから付けた独自の語**（例 `AMDRDNA_BYTECODE_COPY`。Linux の `atom_op_*` の名前は写さない。review M13）。register の空間は MMIO の dword の index で、BAR5 の外の index は PCIE の間接（5.3 の `pcie_index`）。**PLL・MC の空間は Linux の amdgpu では空の callback（読みは 0、書きは何もしない）なので同じにする**（review m4）。IIO の table（`atom_iio`）は VBIOS の中の別の bytecode で、register の間接の読み書きに使う。**host 試験**: A1v で取り出した実機の VBIOS の image で、SetPixelClock などの表を「register の読みは記録した値・書きは log」の dry run で走らせ、register の書き込みの列を **Linux の `atom.c`（MIT）を oracle として host で走らせた列**と比べる。oracle の source は **tree に入れず**、test の script が Linux の tarball（版と SHA-256 を固定）を `build/` に取得して host で build する（AGENTS.md の外部の source の規則と Guardrail の「code を写さない」に触れない形。扱いは 15 節 #8 の判断）。
2. **VBIOS の command の呼び出し**（p015、Procedure referenced: `command_table2.c`）: EnableDispPowerGating（pipe の power on）、SetPixelClock（pixel clock と PLL/DTO: DP は DP の DTO、HDMI は PHY の PLL）、DIGxEncoderControl（encoder の setup・enable/disable、lane の数・rate）、DIG1TransmitterControl（PHY の init・enable/disable・link の rate）、EnableCRTC（OTG の有効化）。parameter の struct は `atomfirmware.h` の値。
3. **AUX と DDC**（p014）: DP_AUXn の register（0x1F50 + 0x1C × n の組、`dce_aux.c` の順: ARB の request → data の FIFO に command と address → control で送信 → status の待ち → reply の FIFO）。DPCD の read/write（native AUX）、EDID（I2C-over-AUX、address 0x50、128 byte × block）。HDMI: DC_I2C_DDCn の hardware I2C engine（0x1EA3 + 2 × (n − 1) の組、`dce_i2c_hw.c`）。GPIO の pin は VBIOS の gpio_pin_lut（4.5）で connector に対応づける（p014 で読む）。
4. **EDID の解釈**: 128 byte の base block の detailed timing（DTD）と standard timing、拡張 block の CEA の short video descriptor は最初は DTD だけ。mode の一覧 → `display.query` の mode の列（UAPI の `gpu_display_mode` の index/count）。
5. **DP の link training**（p015、DP 1.4 の 8b/10b。Procedure referenced: `link_dp_training_8b_10b.c`）: DPCD の能力（0x000〜）を読み、rate と lane を選び（EDID の mode の帯域から）、DIG1TransmitterControl で PHY の rate を設定、DPCD の LINK_BW_SET・LANE_COUNT_SET、training pattern 1（clock recovery、voltage swing の調整、CR_DONE）、pattern 2/3（channel equalization、EQ_DONE・SYMBOL_LOCKED・INTERLANE_ALIGN）、pattern off。失敗なら rate か lane を落として再試行。
6. **pipe の組み立て**（p015、Procedure referenced: `dcn20_hwseq.c` の `enable_stream_timing`・`update_dchubp_dpp`。順は Linux の HW sequencer のとおり、文は自分の言葉）: OTG の lock → power gating off（VBIOS）→ ODM bypass → OPTC の clock → SetPixelClock → OTG の timing（H/V の total・blank・sync・polarity、VSTARTUP/VUPDATE/VREADY の global sync）→ OPP の pipe clock と FMT（8 bpc RGB、dither 無し）→ DPG で blank → EnableCRTC → DPP の clock と DTO → HUBP の VTG_SEL → HUBP の DLG/TTU/RQ（8）→ DPP の format（ARGB8888、CSC bypass、scaler bypass）→ MPC の木（1 層、alpha 無し、OPP の mux）→ viewport → cursor → HUBP の surface（8.3）→ HUBBUB の watermark（8）→ DIG の FE（stream encoder の DP の MSA・VID の timing・color、HDMI は infoframe と TMDS）→ DIG の BE（encoder control、link training）→ DP の VID stream の enable → unblank → OTG の unlock。
7. **blank と戻し**: 組み直しの前に DPG で blank し DP の VID stream を止める（`blank_pixel_data`）。GOP の mode への戻しは同じ手順を GOP の timing で。
8. **DLG・TTU・HUBBUB の watermark（DML の代わり）**: Linux の DML（`display_mode_vba_20.c` 5,115 行、`display_rq_dlg_calc_20.c` 1,665 行、`dcn20_fpu.c` 2,559 行、浮動小数）の移植は範囲に合わない。amdrdna は**単一 pipe・linear ARGB8888・scaling 無し・ODM 無し・DISPCLK/DCFCLK/UCLK は boot の値（DPM 無し）**の形に絞って、要る出力（HUBP の RQ の chunk/meta の設定、DLG の refcyc/vratio の値、TTU の QoS の値、HUBBUB の urgent・p-state・self-refresh の watermark）を `display_rq_dlg_calc_20.c` と `dcn20_fpu.c` の式（Procedure referenced）から 64 bit の固定小数で計算する。p-state の切り替えが無い（UCLK DPM 無し）ので p-state の watermark は最大、self-refresh は無効。**A11（同じ mode）は GOP の値をそのまま保つ**（A1 で読んで保存）ので DML の代わりは A12 からでよい。確かめは実機の underflow（HUBP_UNDERFLOW_STATUS・OPTC の UNDERFLOW_OCCURRED）と、A12 の値を GOP の値（同じ mode の時）と比べる。
9. **hotplug**（A13）: HPD の割り込み（6 節）→ 100 ms の debounce → DC_HPD_SENSE で接続を読む → 外れたなら blank して link を落とし `display.events` の sequence を進める（Keiland が出力 off を知る）→ 繋がったなら DPCD・EDID を読み、**今の mode が出せる monitor なら同じ mode で re-light**（compositor の present の大きさと pitch を変えない）、出せない時だけ EDID の推奨の mode（DTD の最初、boot の clock の中）で mode set し events を進める（その後の古い大きさの present は EINVAL。Keiland は events の後の query で新しい mode を知り present を作り直す）。**DP の短い pulse**（HPD_RX、src 6〜11。monitor の省電力からの復帰など。review m11）: DPCD の link の状態（0x202〜0x204）を読み、落ちていれば同じ mode で再 training（takeover の間（A6〜A8）も同じ扱いにする。p015）。GOP の connector 以外の HPD は events を進めるだけ（Keiland が claim して present するまで点けない）。
10. **DISPLAY の ops との対応**（review M10）: `display.query` は output（connector）ごとの今の mode と状態、`display.mode` は **ENUMERATE（EDID の mode の一覧。boot の clock で出せる物だけ）と VALIDATE（custom の mode の検証。出せなければ ERANGE）だけ**で、mode を設定しない。**mode set の引き金**は、lease の持ち主（compositor）の **present・commit の width・height・refresh_millihz が今の mode と違う時**（GOP の output では driver が EDID の一致する mode に組み直す）と、**別の output の claim と present**（Keiland の明示の指示 = Guardrail の例外の道）。`display.events` は HPD で進む。mode set の間の present は EAGAIN。i915（WS051・WS113）が present の大きさの変更をどう扱っているかを p014 の最初に読み、同じ形にする。
11. **clock**（review M11）: Linux の DCN 2.0 の clock manager は DISPCLK・DPPCLK を上げる前に SMU に電圧を頼み（`dcn20_clk_mgr.c` の `set_voltage_by_freq`・`set_hard_min_dcfclk_by_freq`・display の数）、DENTIST の分周を書く。amdrdna は DPM を有効にしないので電圧を上げられない。**出せる mode は boot の DISPCLK・DPPCLK・DCFCLK（A1 の 9 で読む）の中に pixel rate と DPP の要求が収まる物だけ**にし、ENUMERATE から外し VALIDATE は ERANGE。DENTIST・DCCG は読むだけ（DP の DTO は SetPixelClock の command が書く）。boot の値を越える mode は DPM（別の WS）の後。

**危険**（14 節）: PHY と PLL は VBIOS の command に頼るので interpreter の誤りは画面が点かない形で出る、DP の training の失敗、watermark の誤りによる underflow、HDMI の scrambling（340 MHz 以上）、boot の clock の上限が低いと別の mode がほとんど出せない。A11 の失敗は GOP の画面が戻らない（warm reboot で戻る見込み）。

**選ばなかった案**: PHY・PLL を自前で書く（Linux の DC も VBIOS に頼る。register の意味が公開されていない）。DML の全部の移植（規模と浮動小数。kernel に浮動小数の規則が無い）。Keiland 側で EDID を読む（AUX は kernel の register）。driver が hotplug で勝手に mode を替える（compositor の present と合わなくなる）。

## 9. executor と compiler の口

### 9.1 zedBSD の Vulkan の形

nvrtx の 8.0 節と同じ: libvulkan は Venus の client、kernel の中に Venus の executor と SPIR-V → GFX10 の compiler（p007、P5）を持つ。user は PM4 を直接書かない。

### 9.2 compiler との約束（p007 の 3 節に合わせる、P3 の側の確認）

p007 の 3 節の program の形をそのまま受ける（2026-10-15 の P5 の記述を読んで合わせた。差分は無い）:

- 全 stage wave32（driver が DISPATCH_INITIATOR の CS_W32_EN（bit 15）、VGT_SHADER_STAGES_EN の VS_W32_EN（bit 23）、SPI_PS_IN_CONTROL の PS_W32_EN（bit 15）を立てる）。legacy VS、scratch 無し。
- user SGPR: s[0:1] = 資源の表の GPU の仮想 address（64 bit）。CS は user SGPR 2 個（TGID_X/Y/Z_EN で s2〜s4 が workgroup の id）、VS は 5 個（s2 base vertex、s3 first instance、s4 instance rate の mask）、PS は 2 個（s2 は hardware が置く prim mask）。driver は SPI_SHADER_USER_DATA_VS_0（SH 0xB130 → dword 0x2C4C）・PS_0（0xB030 → 0x2C0C）・COMPUTE_USER_DATA_0（0xB900 → 0x2E40）から順に SET_SH_REG で書く（Mesa 25.3.6 `radv_shader_info.c` の stage ごとの base）。
- VS の入力 VGPR: v0 vertex id、v3 instance id（VGPR_COMP_CNT 3）。PS は SPI_PS_INPUT_ENA の bit の順。CS は v0〜v2（TIDIG_COMP_CNT 2）。
- 資源の表（push → SSBO の V# → UBO の V# → T#＋S# → VS の頂点の V#）は compiler が program ごとに配置を決めて結果の記述に offset を入れ、driver が draw・dispatch ごとに書く。
- 結果の記述から driver が書く register: PGM_LO/HI（address >> 8、HI は >> 40）、RSRC1/2、SPI_PS_INPUT_ENA/ADDR、SPI_SHADER_COL_FORMAT・Z_FORMAT、SPI_VS_OUT_CONFIG、SPI_SHADER_POS_FORMAT、PA_CL_VS_OUT_CNTL、SPI_PS_INPUT_CNTL_0..31、SPI_PS_IN_CONTROL（NUM_INTERP）、COMPUTE_NUM_THREAD_X/Y/Z、LDS。`amdrdna_gfx10_program_registers()` が作る（p007）。
- shader の code: VRAM、256 byte 整列、末尾に 3 cache line の詰め物（prefetch）、CPU で書いた後 HDP flush。PGM_HI は program ごとに書く（RADV の 32 bit の窓の約束は使わない）。p007 の出典は Mesa **25.0.7**（P5 の表）で、driver の側は Mesa 25.3.6。block の comment にはそれぞれの版を書く（監査の表も版ごと）。

p007 の 7.8 の再開点 3（`struct amdrdna_gfx10_program`・`struct amdrdna_gfx10_registers` の field、T#・S# は driver が作る、SPI_PS_INPUT_ADDR = ENA）は p008 の最初に P5 とすり合わせる。**A5 の compute の dispatch の試験**は p007 の host 試験の最小の compute の program（`shaders/ids.comp` 相当）の語をそのまま使う（p005 が p007 の段 A の成果に依存する）。

### 9.3 draw に要る state（A9、作業の文書 e の 1〜4・9 節）

- preamble（queue ごとに 1 回、IB の先頭で毎回でもよい。**queue の reset の後は kernel が出し直す**（11.3）、review M3）: CONTEXT_CONTROL、CLEAR_STATE、gfx10 の graphics の初期 state（RSRC3/4 の CU_EN、REQ_CTRL、DB の cache control、CB の cache control、binner の CNTL_1、NGG の mode CNTL、VGT の reuse、GE の uconfig 群、VERTEX_REUSE_BLOCK 14、PA_SU/CL の VTE・VTX・point size・line stipple、SPI_INTERP_CONTROL_0）と compute の初期 state（COHER_START_DELAY 0x20、STATIC_THREAD_MGMT の CU_EN、USER_ACCUM、DISPATCH_TUNNEL 0、START_X/Y/Z 0）。値は Mesa 25.3.6 `ac_cmdbuf.c`・`radv_queue.c` から出典付きで取る。
- render target（linear の 32 bpp）: CB_COLOR0_BASE（>> 8）、VIEW、INFO（FORMAT 10 = 8_8_8_8、NUMBER_TYPE 0 unorm か 6 srgb、COMP_SWAP 0 std/1 alt、BLEND_CLAMP、SIMPLE_FLOAT）、ATTRIB、ATTRIB2（MIP0 の幅・高さ − 1）、ATTRIB3（RESOURCE_TYPE 1、RESOURCE_LEVEL 1、SW_MODE 0、CMASK_PIPE_ALIGNED）、BASE_EXT。**gfx10.1 では linear の pitch は幅から決まる（256 byte に切り上げ）**: scanout の buffer を描画の対象にするには stride = align(幅 × 4, 256) が要る（client の surface は GOP と同じ pitch なので、GOP の pitch がこれを満たすかを A1 で確かめる。満たさなければ描画は別の buffer に行い GFX の ring の copy（DMA_DATA）で写す）。
- blend: CB_BLEND0_CONTROL（ONE・ONE_MINUS_SRC_ALPHA で 0x40000501）、SX_MRT0_BLEND_OPT、CB_TARGET_MASK、CB_COLOR_CONTROL（ROP3 0xCC、MODE 1）、CB_SHADER_MASK。depth 無し: DB_Z_INFO・STENCIL_INFO の format invalid、DB_DEPTH_CONTROL 0、DB_SHADER_CONTROL（Z_ORDER early、DUAL_QUAD_DISABLE 1）。
- viewport・scissor・raster: PA_SC_WINDOW_SCISSOR、SCREEN_SCISSOR_BR、VPORT_SCISSOR_0、PA_SC_MODE_CNTL_0（VPORT_SCISSOR_ENABLE）、PA_CL_VPORT_XSCALE から 6 語、VPORT_ZMIN/ZMAX、guardband、PA_SU_SC_MODE_CNTL 0、PA_CL_CLIP_CNTL（DX_CLIP_SPACE_DEF、DX_LINEAR_ATTR_CLIP）、PA_SC_AA_CONFIG 0、AA_MASK、DB_EQAA、PA_SC_MODE_CNTL_1、PA_SC_BINNER_CNTL_0（binning 無効 = 2）。
- draw: VGT_PRIMITIVE_TYPE（uconfig index 1、trilist 4）、VGT_SHADER_STAGES_EN（legacy VS、MAX_PRIMGRP_IN_WAVE 2、VS_W32_EN）、GE_CNTL（PRIM_GRP 128、VERT_GRP 256）、VGT_GS_MODE 0、NUM_INSTANCES、DRAW_INDEX_AUTO（count、AUTO_INDEX）か DRAW_INDEX_2。
- dispatch: COMPUTE_PGM_LO/HI、RSRC1/2/3、RESOURCE_LIMITS、NUM_THREAD_X/Y/Z、DISPATCH_DIRECT（groups、initiator = SHADER_EN | ORDER_MODE | TUNNEL | CS_W32_EN | FORCE_START_AT_000）。universal の family では GFX の ring で出す（7.4）。
- 記述子: T#（8 語、gfx10 の layout、RESOURCE_LEVEL 1、TYPE 9 = 2D、format 56 = 8_8_8_8 unorm・130 srgb、DST_SEL、SW_MODE 0）、S#（4 語）、V#（4 語、format 22 = 32_FLOAT の raw、OOB_SELECT 3）。値は Mesa 25.3.6 `ac_descriptors.c`・`gfx10-rsrc.json`・`gfx10_format_table.py` から。
- cache: submission の終わりは RELEASE_MEM（7.6）で CB/DB を flush して L2 を writeback してから fence。sampled read の前に GLV・GL1 の invalidate、CB の書き込みの後に PS_PARTIAL_FLUSH と CB の flush。RX 5600 XT は 192 bit bus で TCC が 12（2 の冪でない）の可能性があり、その場合 CB の書き込みの後に GL2 の writeback が要る（Mesa の `tcc_rb_non_coherent` の規則）。A5 で TCC の有効な mask を読んで判定する。
- clear・copy（全部 GFX の ring）: linear の image の clear は CP の DMA_DATA（fill）か draw、buffer ↔ linear image は pitch が同じなら DMA_DATA の copy、違えば行ごと、scale 付き blit は textured draw（compositor 自身の draw と同じ）。

### 9.4 executor の作り（判断 5 の回答: 写す）

nvrtx の executor のうち hardware に依らない部分（`vk-object.c`・`vk-session.c`・`vk-stream.c`・`vk-dispatch.c`・`vk-device.c`・`vk-memory.c`・`vk-resource.c`・`vk-queue.c`・`vk-reply.c`・`vk-target.c`・`vk-record.c`・`vk-command.c`・`vk-layout.c`、計 5,993 行。`vk-resource.c` は nvrtx の hardware の語を一部含むので切り分けが要る）を amdrdna に写して独自の名前にする。理由: WS037 の Future Work「executor の共有の層」は未着手で、共有の層を切り出す作業は WS241 が触る file と重なる。hardware の部分（`vk-execute.c`・`vk-pipeline.c`・`draw-push.c`・`exec-push.c`・`compute-launch.c`・`image.c` に当たる物）は PM4・GFX10 の記述子・linear の image で書き直す。p008 の最初に写す file の一覧と切り分けを記録し、共有の層への寄せ直しは Future Work。**executor の模型は写した物のまま: renderer の worker は node に 1 本、submission（run）ごとに GPU の完了を同期で待つ**（review M4）。したがって GFX の ring の上に同時にある user の run は最大 1 つで、**run の期限（`CONFIG_AMDRDNA_RUN_MS`、既定 2000）の起点は run を ring に積んだ時 = その run が ring の先頭に来た時**（先の run は完了済み）。期限で session を LOST にする（11 節）。worker を session ごとにして GPU の待ちを lock の外に出す形（WS250 の p005 の a と同じ）は Future Work。

## 10. 我々の interface への対応表

`struct drv_gpu_ops`（`include/drivers/gpu/gpu.h`、`DRV_GPU_INTERFACE_VERSION` 9）、`drv_gpu_display_ops`（`gpu-display.h`）、`drv_gpu_scanout_ops`（`gpu-scanout.h`）。手本は nvrtx の `runtime.c`・`present.c`・`share.c`。登録の検査は `src/drivers/gpu/gpu.c`。

### 10.1 capability と callback

最初の集合は nvrtx と同じ: `GPU_CAP_RESOURCE | GPU_CAP_TRANSFER | GPU_CAP_COMMAND | GPU_CAP_NOTIFICATION | GPU_CAP_JOB | GPU_CAP_JOB_CAPACITY | GPU_CAP_CAPSET | GPU_CAP_BLOB | GPU_CAP_MAPPING | GPU_CAP_SHARE`。display を持つ時は `GPU_CAP_DISPLAY | GPU_CAP_DISPLAY_EVENTS | GPU_CAP_DISPLAY_CONTROL` と display・scanout の ops。登録は 1 回。

| capability / 表 | callback | amdrdna での中身 | Phase |
| --- | --- | --- | --- |
| （常に） | `get_info`、`open`・`close` | session = executor の context ＋ pasid ＋ 4 段の page table（root は VRAM）＋ 状態（READY/LOST/…、11.3）。VMID は submission の時に動的（5.4）。close は共通の層の stop（`stop_begin` → `stop_poll` が 0）の後に来る。隔離（`isolate` が 0）された session の close は hardware に触らず資源を quarantine に残す | p011 |
| RESOURCE・BLOB | `resource_create`・`blob_create`・`resource_destroy` | buffer object: VRAM か system memory。session の page table に map。destroy は最後の fence の完了と TLB の invalidate の完了を確かめてから返す。隔離された session の物は返さない（quarantine） | p011 |
| BLOB（任意） | `blob_create_placed` | VRAM の連続・256 byte 整列・CPU から見える範囲（scanout） | p011 |
| TRANSFER | `resource_read`・`resource_write` | VRAM は BAR0（HDP の規則）、resizable BAR 無しの窓の外は MM_INDEX の間接、system memory は直接 | p011 |
| MAPPING | `resource_map` | VRAM は BAR0 を通す DEVICE の map。system memory は RAM の map | p011 |
| CAPSET | `get_capset` | executor の capset。chip・VRAM・wave size・queue family の数 | p011（中身は p008） |
| COMMAND | `command` | Venus の stream を executor の命令として decode | p008 |
| NOTIFICATION | `commands.submit`・`commands.drain` | executor が組み立てた IB を **session の VMID を割り当てて** GFX の ring（compute 専用の family なら compute の queue）に INDIRECT_BUFFER で積み、RELEASE_MEM の fence、doorbell。完了は EOP の割り込み。LOST の session は EIO | p011・p008 |
| JOB | `jobs.reserve`・`commit`・`cancel` | 予約の領域（ring の dword と fence の slot） | p011 |
| JOB_CAPACITY | `jobs.capacity` | ring の残り | p011 |
| SHARE | `share.*` | 同じ node の open の間の BO の共有 | p011 |
| DISPLAY | `display.query`・`mode`・`claim`・`release`・`present`・`wait` | query: A1 の GOP の mode（A10 以降は EDID の mode の一覧）、connector の種類、`GPU_DISPLAY_HAS_CURSOR`。mode: A12 以降（GOP の connector は driver の判断、他は Keiland の指示）。present: HUBP の flip。wait: VSTARTUP。release と fault では GOP の surface に戻す | p006（mode は p015） |
| DISPLAY_EVENTS | `display.events` | display の構成（topology）の通し番号（HPD で進む） | p006 |
| DISPLAY_CONTROL | `display.power`・`display.refresh` | power: HUBP の blank の解除/設定。refresh: VSTARTUP の割り込みの待ち | p006 |
| cursor | `display.cursor_set`・`cursor_move`・`cursor_hide` | 8.4 | p006 |
| scanout の表 | `scanout.query_device`・`scanout.constraints`、`import_image` = NULL | constraints: VRAM の連続・CPU から見える範囲、offset 256 byte、pitch = 今の mode の pitch、形式は ARGB8888/ABGR8888 の linear | p006 |
| recovery の表 | `fault`（必須）、`stop_begin`・`stop_poll`（nvrtx と同じ: 投入を止めて完了を待つ）、**`isolate`（実装、11 節: queue の reset で GPU の上の停止を確かめた時だけ 0、資源は quarantine）**、`reset` = NULL | 11 節 | p011・p013 |

### 10.2 安全の模型

nvrtx の 8.2 と同じ: user は PM4 を書かない。kernel の構造を user の page table に map しない。buffer の解放は ring の順と TLB の invalidate の完了で決める。VM の fault は pasid で session を特定し、その session だけを失う。

## 11. 回復・取り消し・停止・reset（判断 6 の回答、WS250 の形）

出典（手順）: Linux v6.19 `amdgpu_job.c`（`amdgpu_job_timedout`: ring ごとの reset → だめなら GPU の reset）、`amdgpu_ring.c`（`reset_helper_begin/end`）、`gfx_v10_0.c`（`reset_kgq`: KIQ 経由で CP_VMID_RESET を書き MQD の active を待つ → MQD の init → MAP_QUEUES、`reset_kcq`: UNMAP_QUEUES RESET → HQD_ACTIVE の待ち → init → MAP、`kiq_reset_hw_queue`、`set_safe_mode`）、`gc_10_1_0_offset.h`（CP_VMID_RESET 0x1E53、CP_GFX_HQD_ACTIVE 0x1E80、SPI_COMPUTE_QUEUE_RESET 0x1F7B、RLC_SAFE_MODE 0x4C05）。[WS250 の設計](../ws250/design.md)（nvrtx の同じ目的の形: session の状態機械、期限、kill の手順、`isolate` の基準）。

### 11.1 目的（ユーザーの決定）と WS250 との違い

終わらない shader（または GPU の fault）を出した app は `VK_ERROR_DEVICE_LOST` で止まってよい。**他の app と compositor は動き続ける**。GPU の上で仕事を取り消し、queue を戻して GPU を使える状態にする。

**WS250 との違い**（review M4。ユーザーに示す点、15 節 #13）: WS250（nvrtx）は session ごとの channel（案 A）を採り、GPU の側で詰まった仕事の後ろで待たないようにした。amdrdna の最初の版は **GFX の queue が 1 本で全 session が共有**（7.3）なので、WS250 が退けた「共有の queue を止めて戻す」（案 B）の形になる: **期限（既定 2 s）＋ reset（ms 台）の間は compositor を含む全 session の GFX の仕事が止まり、その後 desktop は動き続ける**。session ごとの GFX の queue（Navi10 の 1 pipe に 8 本の HQD を time slice で回す。Linux に Navi10 の例が無い）は Future Work。compute 専用の family の仕事は自分の queue の reset で済み、GFX は止まらない。

### 11.2 検出

- **run の期限**: executor の run（submission）の fence の待ちは `CONFIG_AMDRDNA_RUN_MS`（既定 2000。`config/kernel-options.list` に 1 行、choice 1000/2000/5000/10000）。起点は run を ring に積んだ時（9.4: 同期の 1 worker なので先の run は完了済みで、ring の先頭に来た時と同じ）。越えたら**その session の喪失**。他の session とは混ぜない。desktop の run は ms 台なので 2 桁の余裕（WS241 の実測）。
- **CP の例外の割り込み**（priv reg 184・priv inst 185・bad opcode 183）: IV の vmid → その時の VMID の記録 → session。期限を待たず喪失にし、queue の reset へ（Linux も scheduler の fault = ring の reset として扱う）。
- **VM fault**: pasid → session。no-retry で既定の page へ逃げるので GPU は止まらないことが多い。session を喪失にし、以後の submission は投入しない。**fault した run にも run の期限を当て**、期限までに fence が完了すれば reset 無しで FREED、完了しなければ reset の道へ（review M4 の 3: 無限 loop と fault の両方の場合）。
- **共通の層の期限**（supervised job の 60 s）は最後の網（WS250 の 4.2 と同じ）。

### 11.3 取り消しの手順（session の状態機械）

```
READY ──正常の close（stop_begin(ENODEV)）──▶ STOPPING（投入を止め fence の完了を待つ）──完了──▶ FINISHED ──close──▶ 資源を返す
READY ──run の期限 / CP の例外 / VM fault──▶ LOST ──fence が期限までに完了──▶ FREED（reset 無し）
LOST ──完了しない──bring-up の thread が queue を reset──▶ RESETTING ──reset と remap が全部 ok──▶ FREED
RESETTING ──どれか失敗──▶ STUCK ──▶ isolate は EIO → device の fault（全 session EIO、電源の切断まで）
FREED ──isolate(0)──▶ ISOLATED ──close──▶ 資源は quarantine（返さない）
```

1. LOST: `session->lost = reason`、`drv_gpu_report_session_error(EIO)`。以後の submit・command・reserve は EIO。libvulkan は EIO を `VK_ERROR_DEVICE_LOST` にする（WS250 の 1 節の事実）。
2. **GFX の queue の reset**（guilty の session の run が GFX の queue に居る時。`kiq` の mutex の下、**KIQ の packet だけで MMIO は触らないので RLC の safe mode も GRBM の選択も要らない**。Procedure referenced: `gfx_v10_0_reset_kgq`。review M3）: (a) `ring` の spinlock の下で record を backup（7.7 の (1)(2)）、投入を止める。(b) KIQ に WRITE_DATA で CP_VMID_RESET（0x1E53）= RESET_REQUEST(1 << vmid) | PIPE0_QUEUES(1 << 0)、WAIT_REG_MEM（memory）で MQD の `cp_gfx_hqd_active` の語が 0 になるのを待つ、WAIT_REG_MEM（register）で CP_VMID_RESET == 0、KIQ の ring test。(c) GFX の MQD を **init の時の写しから書き戻し**、ring を NOP で埋め、rptr・wptr・writeback を 0、MAP_QUEUES、KIQ の ring test、GFX の ring test。(d) **preamble（9.3）を kernel が出し直す**（CP_VMID_RESET が context の state を戻すかは未確認なので、常に出し直す）。(e) backup を ring に書き（7.7 の (4)）、doorbell。(f) 全部 ok なら FREED: その session の全 ring の record が完了か取り消しになってから VMID の TLB を invalidate して pool に返し、**全 VMID の `flushed_seq` を 0** にする（5.4）。どれか失敗なら STUCK。
3. **compute の queue の reset**（guilty の run が compute の queue に居る時。Procedure referenced: `gfx_v10_0_reset_kcq`）: (a) backup。(b) KIQ に UNMAP_QUEUES（ACTION RESET、ENGINE 0、その queue の doorbell）、KIQ の ring test。(c) `grbm_select` の mutex の下で me/pipe/queue を選び CP_HQD_ACTIVE の bit 0 が 0 になるのを待つ（100 ms）。Linux はここを RLC の safe mode で囲むが、それは clock gating（CGCG・MGCG）が有効な時だけで、**amdrdna は clock gating を設定しないので safe mode に入らない**（CG を入れる時はこの (c) を safe mode で囲む）。(d) MQD を写しから戻し MAP_QUEUES、ring test。(e) backup を書く。(f) FREED / STUCK。
4. **KIQ が答えない**（ring test の timeout）: device の fault。
5. STUCK・ISOLATED の session の資源（BO・page table・VMID）は返さない。FINISHED（正常の close）と、FREED のまま isolate が呼ばれずに close に至った session（共通の層が `stop_poll` の 0 を見て FINISHED にした時）は通常の道で返す。
6. **時間**: KIQ の往復は ms 台の見込み。reset の間、GFX の queue の他の session の frame は止まる（期限の 2 s ＋ reset の時間、1 回）。その後 desktop は動く。bring-up の thread が長い段の最中なら reset はその後（2 節の lock の順）。

### 11.4 共通の層の ops

| op | 実装 |
| --- | --- |
| `stop_begin(session, error)` | **投入を止めるだけ**（nvrtx と同じ。review B1: 共通の層は正常の close でも GPU に仕事を出した session に `stop_begin(ENODEV)` を呼ぶので、ここで reset を始めると app の終了のたびに共有の queue が止まる）。READY なら STOPPING。LOST なら何もしない（取り消しは期限の道が進める） |
| `stop_poll(session)` | 全 ring の record の fence が完了していれば 0（FINISHED か FREED）。STOPPING・LOST・RESETTING なら EAGAIN。**STUCK なら EIO**（すぐ escalate。review M4） |
| `isolate(session)` | **FREED なら 0**（queue の reset と remap が成功し、VMID の wave が居ないと確かめた）→ ISOLATED。それ以外（LOST・RESETTING・STUCK）は EIO → 共通の層は device の fault へ。isolate が 0 の後の `resource_destroy`・`close` は hardware に触らず、資源は **`quarantined` の list に積んで返さない**（gpu.h の契約「checked reset まで device の持ち物」。`reset` は NULL なので reboot まで。review M5）。呼ばれるのは共通の層の stop の期限（10 s）を越えた時か `stop_poll` が EIO を返した時だけなので、普通の取り消しは FREED → FINISHED の道で資源が返る |
| `fault(error)` | 冪等。新しい投入を止め、全ての destroy・close が不確かな DMA を保持する。driver が自分で見つけた異常（PSP の timeout、KIQ の timeout、GRBM の hang、温度の閾値、SMU の hang、hardware CTF）は隔離を構えてから `drv_gpu_report_error` |
| `reset()` | **NULL**（最初の版）。mode1 reset の実験は 11.6 |

### 11.5 温度と CTF（判断 3 の回答の続き）

- 監視の thread（4.4 の 9）: edge が閾値を超えた、THM の L2H の割り込みが来た、SMU が答えなくなった → device の fault（新しい投入を止める。GPU の clock は boot の値のまま。PMFW の FW_CTF が hardware の上限で GPU 自身を止める）。**system は止めない**（Linux は software CTF で system を shutdown する。zedBSD は device の fault にとどめ、log に出す。判断の項目 15 節 #4）。
- hardware CTF（SMUIO の割り込み）: device の fault、log に emergency。

### 11.6 mode1 reset の実験（判断 6 の回答「必要なら」）

- 第 1 版の B2 のとおり、mode1 reset（C2PMSG_64 = 0x70000、500 ms、PSP の bootloader からやり直し）は display（GOP の timing）と post の印（BIOS_SCRATCH_7、未確認）を失う見込み。**A10 の AtomBIOS の interpreter が入れば asic_init（post）を自前で走らせられ、A11 の re-light で画面も戻せる**ので、`reset`（device の作り直し: 停止の手順 → mode1 → asic_init → A2〜A5s の再実行 → A11）は p015 の後の選択の Phase（p016、16 節）にする。それまでは STUCK と device の fault は電源の切断まで。
- 同じ道が warm reboot の後の `sos alive`（C2PMSG_81 ≠ 0）の cold power cycle を無くす: A0 で SOS が居れば mode1 reset → asic_init → 通常の起動。これも p016。

### 11.7 停止（shutdown・reboot の hook、`stage` までの逆順）

(1) 投入を止め、fence を待つ（timeout）。(2) EOP・TRAP・THM の割り込みを止める。(3) GFX の queue を KIQ の UNMAP_QUEUES（PREEMPT）、compute の queue を UNMAP_QUEUES（RESET。Linux の teardown と同じ。止まった compute の PREEMPT は終わらない可能性がある。review m8）で外し、CP_ME_CNTL で CE/PFP/ME を halt、CP_MEC_CNTL で MEC を halt。(4) SDMA0: RB・IB の enable を 0、F32 の HALT。RLC は止めない。(5) display を GOP の surface に戻す（8.5）。mode set 済みなら GOP の mode に戻す（8.6 の 7）。BIOS_SCRATCH_6 の bit 4 を 0。(6) SMU: Tier F なら DisableAllSmuFeatures、PrepareMp1ForUnload。(7) PSP: UNLOAD_TA（ASD）、DESTROY_TMR、ring の破棄。(8) IH を止め、bus master を切る。SOS は生きたままになる。

## 12. 段の印・boot の parameter・試験の道

- **印**: 3 節の `kern_logf` の行。試験の image は `kmsg=console`（logo 無し）。印は GOP の framebuffer の text console に出る（client の surface を出している間と、A3t の blank の間を除く）。
- **boot の parameter**（nvrtx の `stage.c` と同じく生の command line を語で照らす）: `amdrdna.off=1`（attach しない）、`amdrdna.auto=A5`（起動で自動に進める段の上限。既定 `A1v`）、`amdrdna.disp.own=1`（A6〜A8 を走らせ display 付きで登録する。p006 の実機の合格の後に既定 on。判断 8）、`amdrdna.disp.test=1`（A6 で色の帯を数秒出して戻す）、`amdrdna.vk=1`（A9）、`amdrdna.pause=1`（危ない段の前に数秒待つ）、`amdrdna.temp.max=`、`amdrdna.fan.duty=`（Tier S）、`amdrdna.train=0`（A3t を省く）、`amdrdna.train.blank=0`、`amdrdna.kiq=0`（CP_RB0 の道）、`amdrdna.modeset=0`（A10 以降を走らせない）。
- **試験の道は素の起動だけ**（ユーザーの決定 2、判断 9 の回答: RTX 機と入れ替え）。VFIO の passthrough は使わない。QEMU の試験は無い（QEMU に Navi10 は無い）。
- **A3 以降の試行の間は cold power cycle が要る**（11.6 の p016 まで）: 停止の手順の後も SOS は生きていて、platform の warm reboot が GPU を reset しなければ次の起動の A0 で `sos alive` で止まる。最初の試行で warm reboot の後の C2PMSG_81 を見て確かめる。
- **固まった時の証拠**: 危ない段（A2・A3t・A3・A5・A5c・A6・A11）の begin の行の後に log を disk に同期してから hardware に書く。次の起動で読める印（BIOS_SCRATCH の未使用の register に段の番号を書く案）は p002 で register の空きを確かめてから決める。
- **host 試験**（実機の前、各 Phase に入れる。`plan/ws039/tests/`）: (a) firmware の container の parser。(b) IP discovery の parser。(c) PSP の frame・command buffer の layout の static assert。(d) PTE・PDE・GART の entry、4 段の index の分解、**VMID の pool の割り当て（LRU・取り上げ・fence の待ち）**。(e) PM4・SDMA の packet（header・RELEASE_MEM・WAIT_REG_MEM・COPY_LINEAR・DMA_DATA・**MAP_QUEUES・UNMAP_QUEUES・SET_RESOURCES**）、**MQD の欄の offset の static assert**。(f) DCN の register の address の表。(g) 13.3 の監査の script。(h) p007 の compiler の試験は P5。(i) **VBIOS の parser**（合成の rom header と data の表、実機の image を取ってからはその image）。(j) **AtomBIOS の interpreter の dry run**（8.6 の 1）。(k) **session の状態機械と取り消し**（record の backup と re-emit、FREED/STUCK の判定。KIQ は stub）。(l) **SMU の pptable の組み立て**（powerplayinfo ＋ smc_dpm_info → PPTable_t の欄の offset）。(m) **DLG・TTU・watermark の固定小数の計算**（Linux の DML を host で浮動小数で走らせた参照の値と比べる。参照の道具は test の中だけ、15 節 #8）。
- **負荷・耐久**は別枠（ユーザーに確かめてから）。

### 12.5 実機の段ごとの手順（RX 5600 XT を挿した 10.0.10.3）

ユーザー（2026-10-15 夜）「RTX機 10.0.10.3 は RDNA機に変わりました。」（改訂 2 の最初の版で 10.0.30.5 と書いたのは誤り）。

| 項目 | 値・手順 |
| --- | --- |
| 機械 | 10.0.10.3（Core i5-13400F の desktop、iGPU 無し）。RX 5600 XT: `lspci` で Navi 10 `1002:731f` rev ca、subsystem Sapphire `1da2:e411`。**普段は Debian 13 の Linux が動く**（`ssh awe@10.0.10.3`、鍵、`sudo -n` 可）。Linux の amdgpu は載っていない（`Kernel modules: amdgpu` の表示だけ）。UEFI の boot の entry は debian と Windows だけ |
| zedBSD の起動 | USB の zedBSD の image（RAk の形、`plan/ws039/tests/config.mk` で build）を挿し、**Linux から `efibootmgr --bootnext <USB の entry>` で 1 回だけ USB から起動**（5330 と同じ手順。Q1 の見込み、p002 の最初に確かめる）。zedBSD が固まって電源を入れ直すと **既定の Debian が立つ**ので、SSH は必ず戻る（BootNext は 1 回限り）。zedBSD の側の SSH（RAk の image の利用者、IP は DHCP で同じ NIC なので 10.0.10.3 の見込み。p002 で確かめる）で `amdrdnactl` と `kernel.log` を読む。zedBSD から Debian に戻るのは `sudo reboot`（warm）か電源の入れ直し |
| kernel・設定の差し替え | USB の ESP の `/boot/boot0/vmunix`・`zedbsd.cfg` と root の `/lib/firmware/amdgpu/`・binary を、**Debian から USB を mount して書く**（scp で Debian へ → mount → cp。zedBSD が固まっていても書ける）か、zedBSD が上がっていれば zedBSD の SSH で書く。元の `vmunix.rak`・`zedbsd.cfg.bak` は残す。zedbsd.cfg: `kmsg=console`、logo 無し、`login=text`（A9 まで。A9 以降 `login=graphical amdrdna.vk=1`）、`amdrdna.auto=A1v` |
| image の中身 | `plan/ws039/tests/config.mk`（amdrdna=y、`amd-navi-firmware`=y、`amdrdnactl`・`vkhang`・`vkquad`・`keiland-shot`）。T1 は `yes \| make firmware-licenses` の後に `make -j16 BUILD=build/t1/amdrdna ZEDBSD_CONFIG=plan/ws039/tests/config.mk build/t1/amdrdna/vmunix`（warning 0、vmunix check PASS、cksum を記録）。firmware の file は USB の root の `/lib/firmware/amdgpu/`（package の install と同じ path・同じ byte） |
| UEFI の設定（ユーザー） | CSM off（legacy の VGA の mode なら attach しない）。"Re-Size BAR Support" は今は要らない（§15 #7 の回答で 256 MiB の窓だけを使う）。monitor は RX の **DP**（HDMI は p015 の後半） |
| **Linux の amdgpu での照合**（p002 の前に 1 回、以後は必要な時） | 読むだけの物（機械を変えない。許可は要らない）: `lspci -vvv -s <bdf>`（BAR の大きさ・ReBAR の capability・bridge の窓 = 5.5 と A0 の期待値）、`lspci -t`（GPU の上の bridge と switch の topology）、`/sys/firmware/acpi/tables/VFCT`（VBIOS の image。4.5 の parser の host 試験の入力）、`dmesg`（起動の時の PCI の割り当て）。**機械を変える物（amdgpu の load、設定の変更）は Q1 の許可を取ってから**: `sudo modprobe amdgpu` の後の `dmesg`（PSP の SOS・TA の版、`memory training`、TMR の大きさ、discovery の GC の版、SMU の版と driver IF の版、fan・thermal の feature の有効化、GART・VRAM の配置）、`/sys/kernel/debug/dri/<n>/amdgpu_firmware_info`（各 firmware の版 = 4.2 の期待値）、`amdgpu_vbios`（VBIOS）、`amdgpu_regs*`（register の dump: A0・A1 の読み戻しの照合）、`amdgpu_pm_info`（温度・fan・clock = A4 の照合）、`/sys/class/hwmon`。**amdgpu を load すると SOS が居るので、その後の warm reboot で zedBSD の A0 が `sos alive` を出す**（14 節の 2 の「warm reboot で platform が GPU を reset するか」を確かめる機会でもある）。dump の後は `rmmod amdgpu` ではなく電源の入れ直し（ユーザー）で zedBSD の cold boot に戻す |
| 1 回の試行 | (1) Debian で BootNext → `sudo reboot` → (2) zedBSD の SSH が戻る（5 分以内）→ (3) `amdrdnactl status`（A0・A1・A1v の値、`sos alive` か）→ (4) `amdrdnactl advance <段>`（例 A2）→ (5) `amdrdnactl status` と `dmesg \| grep amdrdna`（`Ax ok` の行）→ (6) 合格の段の値を記録 → (7) `sudo reboot` で Debian に戻る。画面の確認が要る段（A3t の blank と戻し、A6・A7・A11〜A13）は**ユーザーの目視か写真**（T1 は「確認してほしい項目」を一覧にし、ユーザーが都合の良い時に見る。「今すぐ試験を」とは頼まない）。機械で確かめられる物は register で（OTG の CRC（register は p014 で出典を写す）で同じ絵の前後の一致、HUBP・OPTC の underflow。review m12） |
| 段の進め方 | 1 回の起動で合格済みの段まで `advance` で一気に進め、新しい段を 1 つだけ試す。合格したら次の試行で `amdrdna.auto=` をその段まで上げてよい（起動で自動）。**実機で ok の行が取れた段の code だけを main に merge する**（2 節の 11） |
| 固まった時 | zedBSD の SSH が 5 分で戻らない → 固まったと判定 → **ユーザーの一覧に「10.0.10.3 の電源の入れ直し」を載せる**（T1 は Q1 に報告、Q1 がユーザーの一覧を更新）。入れ直しの後は Debian が立つので SSH は戻り、USB の log（`kernel.log` の同期済みの部分）を Debian から読める |
| cold power cycle | A3 以降を試した後の warm reboot（Debian 経由でも）で A0 が `sos alive` を出したら、以後その試行の系列は電源の入れ直しが要る（p016 まで）。1 回の起動で A2〜A5s を一気に進める形にして、入れ直しの回数を段の数でなく試行の数にする。Debian の S5（`poweroff`）＋ Wake-on-LAN で GPU の電源が切れるなら、ユーザー無しで入れ直せる可能性がある（S5 で slot の電源が切れるか、UEFI の WOL の設定、Debian の NIC の WOL は未確認。review m12。最初の試行で確かめる項目） |
| 証拠 | `kernel.log` の `amdrdna:` の行、`amdrdnactl status` の出力、Linux の dump との照合の表、T1 の結果の file（`plan/agents/T1/results-*.md` の形）、写真（ユーザー）。実機の証拠と host 試験の証拠を分けて書く |
| 画面が戻らない時（A11〜A13） | `sudo reboot` で GOP が post し直して画面が戻る見込み（14 節）。戻らなければ電源の入れ直し |
| lock | 10.0.10.3 の lock を 1 つ（`/tmp/amd-hw.lock` の考え、T1 の運用）。T1・T2・T3 の並行は別の機械の時だけ |
| 残り | kernel が attach で panic する不具合（A0・A1 の code の誤り）でも、**BootNext が 1 回限りなので次の起動は Debian に戻る**（改訂 2 の最初の版で心配した「毎回の起動で固まる」は無い）。bootloader の fallback の kernel の提案（15 節 #9）は優先度を下げる。A0・A1 の code は host 試験（合成の表と Linux の dump）で先に確かめる |

## 13. 出典の規則の実装 Phase での守り方

Guardrail「MIT・GPL の出典のデータの引用」（WS039 の項）を code でこう守る。

### 13.1 名前の規則（code・symbol の名前を写さない）

- 全ての識別子は `amdrdna_`（関数・型・変数）か `AMDRDNA_`（macro・enum）で始め、**Linux・Mesa の symbol の名前（`mm*`・`*__SHIFT`・`*_MASK`・`PACKET3_*`・`GFX_CMD_ID_*`・`SOC15_*`・`PPSMC_MSG_*`・`V_*`・`S_*`・`R_*`・`SQ_*` など）を含めない**。AMD の register の mnemonic（`GRBM_STATUS`・`CP_RB0_BASE`・`RELEASE_MEM`・`HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS` など、この文書が出典の指示に使っている語）もそのまま使わず、役割を自分の語で書く。
- 例（左が出典の名前、右が amdrdna の名前。左は comment の「referenced」の中にだけ現れる）:

| 出典の名前（comment の中だけ） | amdrdna の名前（code） |
| --- | --- |
| `mmGRBM_STATUS` | `AMDRDNA_GC_REG_BLOCK_STATUS` |
| `mmRCC_DEV0_EPF0_RCC_CONFIG_MEMSIZE` | `AMDRDNA_NBIO_REG_VRAM_SIZE_MIB` |
| `mmMM_INDEX` / `mmMM_INDEX_HI` / `mmMM_DATA` | `AMDRDNA_NBIO_REG_VRAM_WINDOW_LOW` / `_HIGH` / `_DATA` |
| `mmBIOS_SCRATCH_7`、`ATOM_S7_ASIC_INIT_COMPLETE_MASK` | `AMDRDNA_NBIO_REG_BIOS_SCRATCH_7`、`AMDRDNA_BIOS_SCRATCH_7_POSTED` |
| `mmMP0_SMN_C2PMSG_35` | `AMDRDNA_PSP_REG_BOOT_COMMAND`（C2PMSG_36 は `_BOOT_ARGUMENT`、81 は `_SECURE_OS_ALIVE`、33 は `_PLATFORM_INIT_DONE`） |
| `mmMP1_SMN_C2PMSG_66` / `_82` / `_90` | `AMDRDNA_SMU_REG_MESSAGE` / `_ARGUMENT` / `_RESPONSE` |
| `mmOTG0_OTG_CONTROL`、`OTG_MASTER_EN` | `AMDRDNA_DCN_REG_TIMING_CONTROL`（instance は引数）、`AMDRDNA_DCN_TIMING_CONTROL_ENABLED` |
| `mmHUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS` | `AMDRDNA_DCN_REG_PLANE_SURFACE_ADDRESS_LOW` |
| `mmCP_RB0_BASE` / `CP_RB0_CNTL` | `AMDRDNA_GC_REG_GFX_RING_BASE` / `AMDRDNA_GC_REG_GFX_RING_CONTROL` |
| `PACKET3_RELEASE_MEM` (0x49) | `AMDRDNA_PM4_OP_EVENT_FENCE` |
| `PACKET3_WAIT_REG_MEM` (0x3C) | `AMDRDNA_PM4_OP_WAIT_VALUE` |
| `GFX_CMD_ID_LOAD_IP_FW` (6) | `AMDRDNA_PSP_CMD_LOAD_ENGINE_FIRMWARE` |
| `PPSMC_MSG_GetSmuVersion` (2) | `AMDRDNA_SMU_MSG_QUERY_FIRMWARE_VERSION` |
| `AMDGPU_PTE_VALID` | `AMDRDNA_PTE_PRESENT` |
| `struct psp_gfx_rb_frame` | `struct amdrdna_psp_ring_slot` |

- 名前の語の選び方: block（GC・NBIO・PSP・SMU・GMC・IH・SDMA・DCN）＋ 役割（英語の普通の語）。出典の略語（RB・HQD・MQD・OTG・HUBP）は hardware の部品の名前で AMD の文書の語なので、**部品の名前としては使ってよい**が、出典の symbol を丸ごと（`CP_HQD_PQ_CONTROL` のように）写さない。迷ったら出典の綴りと一致しない語を選ぶ。
- struct の field の名前・順も自分で決める（PSP の command buffer のように hardware の layout が固定の物は offset で書き、`offsetof` の static assert で確かめる）。

### 13.2 出典の comment の書き方

値（register の offset・bit・enum・packet の形・firmware の表・golden の表）は、その値を定義する block の直前に複数行の comment で出典を書く。file の先頭には書かない。

```c
/*
 * The PSP boot mailbox: the command register and its argument register
 * (the firmware buffer address shifted right by 20), and the word the
 * secure OS writes once it runs.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/asic_reg/mp/mp_11_0_offset.h
 * (mmMP0_SMN_C2PMSG_35, mmMP0_SMN_C2PMSG_36, mmMP0_SMN_C2PMSG_81).
 */
#define AMDRDNA_PSP_REG_BOOT_COMMAND		0x0063U
#define AMDRDNA_PSP_REG_BOOT_ARGUMENT		0x0064U
#define AMDRDNA_PSP_REG_SECURE_OS_ALIVE		0x0091U
```

自分で計算した値（base ＋ offset、log2、bit の組み合わせ）は、同じ複数行の comment の中に「derived:」の行で書く（1 行の `/* … */` は作らない。review m2）:

```c
/*
 * The absolute register index of the GC block status word on Navi10.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/navi10_ip_offset.h (GC_BASE, segment 0),
 * drivers/gpu/drm/amd/include/asic_reg/gc/gc_10_1_0_offset.h (mmGRBM_STATUS).
 * derived: 0x1260 + 0x0DA4 = 0x2004 (dword index), 0x8010 bytes.
 */
```

hardware の操作の手順を参考にした関数は、関数の直前（か手順の block の直前）に同じ形で書く:

```c
/*
 * Hands one boot image to the PSP boot loader: wait until the mailbox is
 * ready, publish the buffer address, write the command, wait again.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/amdgpu/psp_v11_0.c (psp_v11_0_bootloader_load_component).
 */
```

- 版は「Linux v6.19」「Mesa 25.3.6」「Mesa 25.0.7」（p007）「linux-firmware 20260410」の形（commit の hash は監査の表にだけ）。複数の出典は行を分ける。
- copyright・MIT・GPL の notice は付けない。

### 13.3 監査（p010 と各 Phase の終わり）

1. **symbol の衝突の検査**（script、`plan/ws039/tests/audit-symbols.sh`、p002 で作る）: 出典の header（Linux の `include/asic_reg/**/*.h`・`amdgpu/nvd.h`・`amdgpu/psp_gfx_if.h`・`amdgpu_psp.h`・`amdgpu_vm.h`・`soc15_ih_clientid.h`・`ivsrcid/**`・`navi10_sdma_pkt_open.h`・`smu_v11_0_ppsmc.h`・`smu11_driver_if_navi10.h`・`navi10_enum.h`・`discovery.h`、Mesa の `sid.h` と **`src/amd/registers/gfx10*.json`・`pkt3.json` の名前**（`amdgfxregs.h` は build の時に JSON から作られ tree に無い。review m3））から識別子（`[A-Za-z_][A-Za-z0-9_]{3,}`）を集め、`src/drivers/gpu/amdrdna/` の code（comment を除く）の識別子と交差させる。許すのは C の keyword・libc の名前・一般語の短い allow list（`enable`・`status`・`base`…、file で管理）だけ。交差が空でなければ FAIL。
2. **出典の comment の検査**（同じ script）: `0x` の数値を持つ `#define AMDRDNA_` と `static const` の表の各 block について、**block の直前の comment**（空行で区切られた直前の `/* … */`）に `Value(s) referenced:` か `Values referenced:` があること（`derived:` だけの comment は不可）。無ければ FAIL。file の先頭 20 行に `referenced` があれば FAIL。
3. **人の review**: p010 で code の構造（関数の分け方・順・comment の文）が出典の関数と「同じ形」になっていないかを読む。手順が同じでも、文は自分の言葉、変数と関数の切り方は zedBSD の規約（`plan/coding-style.md`）。
4. 監査の表（[amdrdna-license-audit.md](amdrdna-license-audit.md)）に、参照した file ごとの path・SHA-256・license・取ったデータの種類を p010 までに揃える（p007 の Mesa 25.0.7 の file は P5 の `amdrdna-compiler-license-audit.md`）。

### 13.4 改訂 2 で足す名前の例

| 出典の名前（comment の中だけ） | amdrdna の名前（code） |
| --- | --- |
| `PACKET3_MAP_QUEUES` (0xA2) / `PACKET3_UNMAP_QUEUES` (0xA3) | `AMDRDNA_PM4_OP_QUEUE_ATTACH` / `AMDRDNA_PM4_OP_QUEUE_DETACH` |
| `mmCP_VMID_RESET` | `AMDRDNA_GC_REG_SPACE_QUEUE_RESET` |
| `mmRLC_CP_SCHEDULERS` | `AMDRDNA_GC_REG_SCHEDULER_QUEUE_SELECT` |
| `struct v10_gfx_mqd` / `v10_compute_mqd` | `struct amdrdna_graphics_queue_image` / `amdrdna_compute_queue_image`（欄は offset、static assert） |
| `PPSMC_MSG_SetAllowedFeaturesMaskLow` (0x04) | `AMDRDNA_SMU_MSG_PERMIT_FEATURES_LOW` |
| `FEATURE_FAN_CONTROL_BIT` (32) | `AMDRDNA_SMU_FEATURE_FAN` |
| `mmCG_FDO_CTRL0` | `AMDRDNA_THM_REG_FAN_DUTY` |
| `mmTHM_THERMAL_INT_CTRL` | `AMDRDNA_THM_REG_TEMPERATURE_ALERT` |
| `PSP_BL__DRAM_LONG_TRAIN` (0x100000) | `AMDRDNA_PSP_BOOT_CMD_TRAIN_MEMORY_LONG` |
| `IH_VMID_0_LUT` | `AMDRDNA_IH_REG_SPACE_TO_PROCESS` |
| `atom_master_list_of_data_tables_v2_1.firmwareinfo` | `AMDRDNA_VBIOS_DATA_FIRMWARE_INFO`（index 4） |
| `atom_op_move` / `ATOM_OP_MOVE`（AtomBIOS の interpreter の opcode。Linux の symbol） | `AMDRDNA_BYTECODE_COPY`（振る舞いから付けた独自の語。値 = opcode の番号はデータとして出典付き。**AtomBIOS の opcode の名前は AMD の公開の ISA の文書の命令名ではないので、mnemonic の例外は当てない**。review M13） |

## 14. 危険と未知（実機で観測していない）

1. IP discovery の表の中身（GC の版、base の address、header の版）。
2. C2PMSG_81 の cold boot の値と、warm reboot で platform が GPU を reset するか（しなければ A3 以降の試行ごとに cold power cycle、p016 まで。Linux の amdgpu を load した後の warm reboot で先に観測できる、12.5）。
3. TMR の大きさ。ASD を省けるか（省かない）。
4. **memory training**: VBIOS の training の bit と runtime DB の値、training の時間、32 MiB の範囲が正しいか（Linux の定数を信じる）、timeout の後に PSP が書き続けるか、blank の最中の HUBP の振る舞い、training の最中に CPU が BAR0 に書いた時の結果（console は止めるが panic の表示は止められない）。
5. **SMU の Tier F**: allowed mask を FW_CTF・FAN_CONTROL・THERMAL の 3 bit だけにして EnableAllSmuFeatures を送った前例が Linux に無い（Linux は常に DPM の feature と一緒に有効にする。拒まれたら per-feature、それもだめなら Tier S）、RunBtc が DPM 無しで害が無いか、metrics の表が転送されるか、THM の CG_MULT_THERMAL_STATUS の温度の単位と意味（Tier S の唯一の源。Tier F の metrics と比べて確かめる）、fan が実際に回るか（ユーザーの目視・音）、smc_dpm_info の content の版（5 の見込み）、A3 の直後から A4 までの短い間の fan の状態。
6. RLC と SMU の handshake: DPM の feature が無い SMU で RLC が event を待たないか。
7. **KIQ**: Navi10 の firmware が MAP_QUEUES（engine 4）で GFX を map する（Linux の既定なので見込みは高い）。KIQ の ring test が最初の PM4。CP_PQ_STATUS の DOORBELL_ENABLE の既定値。
8. **queue の reset**: CP_VMID_RESET の後に MQD の active が落ちるか、CP_VMID_RESET が context の state（preamble）を戻すか（戻さない前提で出し直す）、remap の後の流し直しが正しく動くか、compute の RESET_QUEUES の後の HQD_ACTIVE。Linux 6.11 以降の道で、Navi10 での実績は Linux の test に依る。
9. **動的な VMID**: switch ごとの flush の費用、pasid の LUT の書き込みが IH の fault の pasid に反映されるか、ring をまたぐ flush の順（5.4）。
10. **resizable BAR**（§15 #7 で後回し）: 実機の capability は BAR0 が 256 MiB〜1 GiB（2026-10-16 の lspci）、Linux の amdgpu は bridge の窓を置き直して 8 GiB に広げた（quirk の形）。UEFI の Re-Size BAR が何 GiB にするか、8 GiB の device の map が HAL の窓に収まるかは、広い BAR0 を使う時に確かめる。10.0.10.3 の topology: root port 00:01.0 → Navi10 の switch の upstream 01:00.0 → downstream 02:00.0 → GPU 03:00.0。
11. GOP の pitch が 256 byte の整列を満たすか。GOP の framebuffer の VRAM の offset と HUBP の address の一致。GOP の framebuffer が VRAM の下 32 MiB にあるか（training の退避の前提ではないが画面の戻りの前提）。
12. VBIOS の予約の範囲（A1v で読む。読めなければ review 反映版の余裕）。VFCT が無い機械。ACPI の table の公開の口の差分が入るか（M9）。
13. HUBP の cursor の address の整列。HDMI の時の DST_OFFSET。
14. TCC の数（12 なら GL2 の writeback が要る）。
15. A2 の MMHUB の aperture の書き換えが GOP の scanout に影響するか。
16. **mode set**: AtomBIOS の interpreter の誤り（dry run の oracle で減らす）、PHY の設定の VBIOS の表の parameter の版、DP の training の失敗、DLG・TTU・watermark の固定小数の誤りによる underflow、A11 の失敗で GOP の画面が戻らない（warm reboot の GOP の post で戻る見込み。戻らなければ電源）、HDMI の scrambling、boot の DISPCLK・DPPCLK の上限が低くて別の mode がほとんど出せない可能性（M11）、p014・p015 の LW が上限に近いか越える見込み（interpreter・DP の training・pipe の組み立て・固定小数の DML の代わりを合わせた規模）。
17. 素の起動だけなので、GPU が固まった時の回復は電源の切断（BootNext が 1 回限りなので Debian には戻る）。
18. amd64 の IOMMU（今は DMA の物理 = bus の address）。
19. **共有の GFX の queue**: 取り消しの間（期限 2 s ＋ reset）は desktop も止まる（11.1）。

## 15. 判断の項目

**決定済み（2026-10-15 ユーザー）**: 参考の source の扱い（値は出典付き、名前・構造・comment は写さない、GPL も同じ、AMD の ISA の mnemonic は可、Mesa の symbol は不可）、試験は bare metal、到達点は「desktop が描ける」まで（VCN は後）、まず RDNA1、driver と compiler の 2 担当、firmware は zedBSD-firmware、memory training を走らせる、SMU は fan・thermal も最初から、compute の queue も作る、executor は写す、取り消しと復帰（WS250 の目的と同じ。形の違いは 11.1）、mode set も入れる、takeover は opt-in から、RX 5600 XT は 10.0.10.3（元の RTX 機）に挿した、resizable BAR、動的な VMID、renderer だけの登録を許す。

**新しく聞く点**（既定の案と理由。作業の内容で書く）:

| # | 問い | 既定の案と理由 |
| --- | --- | --- |
| 1 | **mode set の順**: desktop（A9）の後に mode set（A10〜A13）を置く。先に mode set をすると desktop が遅れる | 後（p014・p015 は p009 の後）。takeover（GOP の mode のまま）で desktop は描けるので、mode set は desktop の後の改善として進める |
| 2 | **mode set の範囲**: DP を先、HDMI は後半、DP MST・eDP・4K の ODM・scaling は外。別の connector は Keiland の指示だけ（Guardrail） | そうする。monitor は DP で用意してほしい |
| 3 | **memory training の間の画面**: HUBP を blank して黒にする（既定）か、blank せず 1〜3 秒の壊れた画面を許すか | blank する（HUBP の 1 bit で、A6 の power の op と同じ）。`amdrdna.train.blank=0` で切れる |
| 4 | **温度の上限と CTF の時の振る舞い**: device の fault にとどめ system は止めない（Linux は system を shutdown する） | 止めない。PMFW の FW_CTF が hardware の上限で GPU を守る。system の shutdown は policy なので driver が決めない |
| 5 | **fan の退路（Tier S）の duty**: SMU が feature を拒んだ時、THM で固定の duty 50 % | 50 %（`amdrdna.fan.duty=` で変えられる）。音が気になれば下げる。full の DPM（Tier 1）は別の WS |
| 6 | **compute の queue の使い方**: 4 本を kernel の dispatch の試験と取り消しの試験に使い、Vulkan の compute 専用の queue family は p013 で最小の経路だけ（desktop は使わない） | そうする。universal の family の dispatch は順序のため GFX の ring のまま |
| 7 | **（回答 2026-10-15 夜「まず256MBで実装します。」: BAR0 の 256 MiB の窓で実装し、Re-Size BAR は後）** **resizable BAR は UEFI の Re-Size BAR で**: OS の中で BAR を動かす案（改訂 2 の最初の案）は、GPU の上の bridge の窓の再割り当てと boot の framebuffer の固定の VA の張り替え（HAL の承認の対象になりうる）が要り動かない（review B2、5.5）。ユーザーの決定 10「resizable BAR も使う」を、UEFI の "Above 4G Decoding"・"Re-Size BAR Support" を on にして firmware が広げた BAR0 を使う形で満たす | そうする（同じ効果: 6 GiB 全部が CPU から見える）。UEFI の設定はユーザー。OS の中の resize は Future Work（別の WS） |
| 8 | **試験の oracle に Linux の MIT の code を使う**: AtomBIOS の interpreter の dry run の照合に `atom.c`（MIT）を host で走らせる。**source は tree に入れず**、test の script が Linux の tarball（版と SHA-256 を固定）を `build/` に取得して host で build する（AGENTS.md の外部の source の規則の形）。DML の oracle は header 群と kernel の FPU の macro に依り手間が大きいので使わず、DLG・watermark は実機の underflow と GOP の値（同じ mode）との比較だけ（review M13） | 使う（tarball の取得の形。監査の表に「test の oracle、tree に無い」と書く）。だめなら照合は実機の register の値だけ |
| 9 | **ACPI の table の公開の口**（WS039 の外、ACPI の driver の差分 `plan/ws039/proposed/acpi-table-lookup.diff`、Q1 の判断）: VBIOS を VFCT から読むのに要る（review M9）。無ければ ROM の BAR から読む道だけ | 入れる（小さい差分: 署名で生の table を借りる関数 1 つ）。bootloader の fallback の kernel の提案は、BootNext が 1 回限りで Debian に戻るので優先度を下げた（12.5） |
| 10 | **`reset`（mode1 reset ＋ asic_init ＋ re-light）**: p016 として p015 の後に置く（cold power cycle も無くなる） | 後（p016 は選択）。それまで STUCK と `sos alive` は電源の切断 |
| 11 | **control の node の UAPI**（`/dev/amdrdna0`、`include/uapi/amdrdna.h`、`amdrdnactl`）を production の image にも残す（段の状態の読み、VBIOS の取り出し） | **回答（2026-10-15 夜）「開発の間だけ」**: 完成したら消すか config で off。`CONFIG_AMDRDNA_CONTROL`（開発の間の既定 y）で作る（3.2） |
| 12 | **実機で確かめた段だけを main に入れる**（2 節の 11）。その先は branch に置く | そうする（i915 の display の規則の考え方）。Q1 の merge の判断の基準 |
| 13 | **取り消しの間は desktop も止まる**（11.1）: GFX の queue が 1 本で全 session が共有なので、期限（2 s）＋ reset（ms 台）の間は compositor の仕事も止まり、その後動き続ける。WS250（nvrtx）の session ごとの channel とは違う。session ごとの GFX の queue（Navi10 の 8 本の HQD、Linux に例が無い）は Future Work | この形で進める（ユーザーの決定 6「desktop は動き続ける」を「reset の後」と読む）。短くするなら期限を 1 s に（config） |
| 14 | **Linux の amdgpu を 10.0.10.3 で load して dump を取る**（12.5）: 機械の状態を変える（SOS が載る → 次の zedBSD の起動は cold power cycle）ので Q1 の許可の後に | p002 の最初に 1 回（register・VBIOS・firmware の版・PCI の topology・ReBAR の結果の照合）。その後は電源の入れ直し |

## 16. Phase への引き継ぎと LW

| Phase | 段 | 内容 | 依存 | 見込み |
| --- | --- | --- | --- | --- |
| [p001](phase001/phase.md) | — | この文書（改訂 2）、license の監査、review | — | 済の見込み（4〜6 LW） |
| p002 | A0・A1・A1v | Linux の amdgpu の dump との照合（12.5、Q1 の許可）、骨格（PCI の attach、BAR の map、`CONFIG_DRIVER_PCI_AMDRDNA` は済）、RDNA1〜3 の device id の表、IP discovery の parser、resizable BAR の capability と BAR0 の大きさの読み（log だけ、§15 #7）、GOP の readout と clock の読み、VBIOS（VFCT か ROM の BAR）と data の表の parser、PSP の runtime DB の parser、VRAM の配置、段の印、control の node（`/dev/amdrdna0`、UAPI、`amdrdnactl`）、boot の parameter、独自の名前の register の表、監査の script、host 試験 (b)(f)(g)(i)。**実機: A0・A1・A1v の ok** | 実機（RX 5600 XT を挿した 10.0.10.3）、ACPI の table の公開の口の差分（`plan/ws039/proposed/acpi-table-lookup.diff`、Q1 の判断。無ければ A1v は ROM の BAR か skip） | 5〜6 LW |
| p003 | A2 | BAR0 の 256 MiB の窓の map（2 つの pool、§15 #7）、GMC（FB、allocator（下 32 MiB の予約を含む）、GART、両 hub の VMID 0、context 1〜15、HDP）、IH（ring・MSI の用意、thread、dispatch。enable は A3）、VM fault の受け、host 試験 (d) の前半。**実機: A2 の ok、underflow 0** | p002 | 3〜5 LW |
| p004 | A3t・A3・A4（Tier S） | firmware の package（zedBSD-firmware への file の一覧を Q1 へ）、container の parser、root の mount 後の load、memory training（console の停止・退避・blank・training・戻し）、IH の enable、PSP の bootloader・ring・TOC・TMR・LOAD_IP_FW・AUTOLOAD・ASD、**SMU の mailbox と版の読みと Tier S（THM の固定の duty の fan、THM の温度の読み、監視の thread）**、停止の PSP・SMU の部分、host 試験 (a)(c)。**実機: A3t・A3 の ok と A4 の `mode=S`（温度と fan の回転が読め fan が回る。cold power cycle の往復を含む）** | p003 | 6〜8 LW |
| p012 | A4（Tier F） | SMU の Tier F: NumOfDisplays、pptable（VBIOS ＋ smc_dpm_info、soft pptable の選択）、driver の表、RunBtc、allowed mask と EnableAllSmuFeatures（per-feature の第 2 の試し）、THM の割り込みと閾値、metrics の温度と fan の読み（THM の値との比較）、host 試験 (l)。**実機: A4 の `mode=F`、features の 3 bit、温度の 2 つの源の一致** | p004 | 4〜6 LW |
| p005 | A5 | golden・constants・RLC の autoload 待ち・CSB、KIQ、GFX の queue（MAP_QUEUES）、compute の queue 4 本、SDMA0、fence と割り込み、最初の copy、最初の dispatch（p007 の最小の program）、CP_RB0 の退路、host 試験 (e)。**実機: A5 の ok** | p004、p007 の段 A（済） | 4〜6 LW |
| p011 | A5s | session: pasid・動的な VMID・page table・BO・VM の flush、`drv_gpu_ops` の open〜SHARE・recovery（fault・stop）、停止の hook、GPU device の登録（renderer だけ）、capset の骨、host 試験 (d) の後半。**実機: A5s の ok、`/dev/gpu0` が出る** | p005 | 5〜7 LW |
| p013 | A5c | 取り消しと復帰: run の期限（config）、session の状態機械（11.3）、GFX の queue の reset（MQD の写しからの戻し、preamble の出し直し）、compute の queue の reset、record の backup と流し直し（fence の packet、2 回目の取り消し）、`isolate` と quarantine、compute 専用の family の最小の経路、`vkhang`（`userland/tests/vkhang/`、無限 loop の fragment と compute）、host 試験 (k)。**実機: vkhang の app だけ DEVICE_LOST、reset の後 desktop 継続、vkquad ok、compute の queue の reset** | p011、p008（vkhang は Vulkan の app なので A9 の後に実機） | 6〜8 LW |
| p006 | A6〜A8 | client の surface と flip、vblank・flip・HPD の割り込み、cursor、display・scanout の ops と DISPLAY_CONTROL、display 付きの登録、release で GOP に戻す。**実機: A6〜A8 の ok（画面はユーザーの目視）** | p003（display 自体は PSP に依らない）、p011（登録） | 5〜8 LW |
| [p007](phase007/phase.md) | — | compiler（P5）。driver との口は 9.2。段 A〜F は済、段 D の続きと P3 とのすり合わせ | なし（host） | 残り 2〜4 LW（p007 の記録） |
| p008 | A9 | executor（nvrtx の写しと PM4 の hardware の部分）、記述子、draw・dispatch、renderer の node、run の期限、vkquad。**実機: A9 の ok、vkquad の pixel の一致** | p011・p007 | 8〜12 LW |
| p009 | — | compositor が amdrdna で描く、`amdrdna.auto` の既定を上げる、reboot の後片付け。**実機: desktop** | p006・p008 | 2〜4 LW |
| p014 | A10 | i915 の present の大きさの変更の扱いの確認、AtomBIOS の command の表の interpreter（dry run と oracle の host 試験 (j)、oracle は tarball の取得）、AUX・DDC の engine、DPCD・EDID の読み、link の状態の readout、`display.mode` の ENUMERATE・VALIDATE（boot の clock の中の mode だけ）。**実機: A10 の ok（読むだけ）** | p006、p002（VBIOS） | 7〜10 LW |
| p015 | A11〜A13 | VBIOS の command の呼び出し（power gating・pixel clock・encoder・transmitter・CRTC）、OTG・OPP・MPC・DPP・HUBP・DIG の組み立て、DP の link training、同じ mode の re-light、別の mode（present の大きさの変更が引き金、DLG・TTU・watermark の固定小数、host 試験 (m)）、hotplug（同じ connector、短い pulse の再 training）、戻し、HDMI（後半）。**実機: A11・A12・A13 の ok、underflow 0** | p014 | 16〜24 LW |
| p016（選択） | — | `reset`: mode1 reset ＋ asic_init（interpreter）＋ A2〜A5s の再実行 ＋ re-light。`sos alive` の時の自動の reset | p015、p013 | 6〜10 LW |
| p010 | — | 規約の全文の見直し、出典の監査（13.3）、license の監査の表の完成（改訂 2 で読んだ file を含む） | p002〜p009・p011〜p015 | 2〜3 LW |

必須（p002〜p015 と p007 の残り、p016 を除く。表の数を足した物: 5+3+6+4+4+5+6+5+2+8+2+7+16+2 = 75 〜 6+5+8+6+6+7+8+8+4+12+4+10+24+3 = 111）: 約 **75〜111 LW**（review 反映版の 43〜70 LW から、training・Tier F・KIQ と compute・取り消し・動的な VMID・VBIOS・mode set の分が増えた。8 LW ≈ 2〜3 時間）。p016 は別に 6〜10 LW。順は実機の段の順（p002 → p003 → p004 → p012 → p005 → p011 → p008（p007 の済の成果）→ p006 → p009 → p013 → p014 → p015 → p010）。p006 は p003 の後に実機で先に確かめてもよい（display は PSP に依らない）。

**残課題（p001 の clearance の後、p002 の最初に）**: 改訂 2 で読んだ正本の file を [amdrdna-license-audit.md](amdrdna-license-audit.md) に足す（`amdgpu_ids.c`・`amdgpu_job.c`・`amdgpu_ring.c`・`amdgpu_fence.c`・`amdgpu_bios.c`・`amdgpu_atomfirmware.c`・`atom.c`・`atom.h`・`atombios.h`（VFCT）・`pm/swsmu/amdgpu_smu.c`・`display/dc/clk_mgr/dcn20/dcn20_clk_mgr.c`・`smu_v11_0_pptable.h`・`smu_v11_0.c` の fan/thermal の関数・`gfx_v10_0.c` の KIQ と reset の関数・`v10_structs.h`・`display/dc/dml/dcn20/*`・`display/dc/link/*`・`display/dc/bios/*`・`display/dc/dce/dce_aux.c`・`dce_i2c_hw.c`）。
