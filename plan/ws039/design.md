<!-- awesome-plan project=zedbsd record=ws039-design -->
# amdrdna の設計（ws039-p001）

作成: 2026-10-15、P3（q1202）。状態: 設計の案（code は無い）。design-reviewer の review（2026-10-15、blocker 3・major 10・minor 20、[design-review.md](design-review.md)）を反映した版。各指摘の扱いは design-review.md の末尾の「対応」の表。

この文書は Linux の amdgpu・Mesa の RADV/ACO の code・comment・構造を写さない。手順は自分の言葉で書く。register の offset・bit・packet の形・firmware の表などの**値**は、出典（版・path・表か define か関数の名前）を付けて書く（[Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」の WS039 の項）。出典の識別子（register の mnemonic、関数の名前）は、この文書では出典を指す語と hardware の部品の語として現れる。**code にはそのまま写さない**（13 節の規則で独自の名前を付ける）。正本の版・hash・license は [amdrdna-license-audit.md](amdrdna-license-audit.md)。

| 作業の文書（`plan/ws039/temp/notes/`、commit しない、英語、file:line 付き） | 中身 |
| --- | --- |
| `a-chips-pci.md` | PCI の device id、BAR、IP の base、IP discovery の表、reset と shutdown、VBIOS の post の確かめ、Mesa の family |
| `b-psp-fw-smu.md` | firmware の一覧と container の header、PSP の bootloader・ring・TMR・LOAD_IP_FW の順、SMU の mailbox と最小の手順、teardown |
| `c-gmc-vm-ih.md` | FB・VRAM・HDP、GART（VMID 0）、4 段の VM と PTE・PDE、TLB の invalidate、fault、IH の ring と割り込みの表、doorbell |
| `d-cp-sdma.md` | RLC・golden・CSB、GFX の ring、MEC の HQD と KIQ、fence・IB・flush の packet、PM4 の表、SDMA 5.0 の起動と packet、teardown |
| `e-radv-gfx10.md` | 描画に要る register の state、shader の bind、記述子、user SGPR、cache の flush、clear・copy の道 |
| `f-dcn20.md` | DCN 2.0 の GOP の readout、surface の address と形式、flip・割り込み・frame counter、cursor、触らない物、blank・release |

note は commit しないので、**実装の Phase で使う値はこの文書か各 Phase の phase.md に出典付きで移す**（p002 の最初に A0・A1 の register の表を design か phase.md へ写す。review M8）。どの文書も source を読んだだけで、**実機・QEMU での観測は無い**。「見込み」と書いた所は source に根拠が無い推論。

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
| BAR0（64 bit、prefetchable、256 MiB か resizable BAR で全 VRAM） | VRAM の CPU の窓 | **GSP のような切り替えは無い**。BAR0 の offset = VRAM の offset = MC の address − FB の base。UEFI の GOP の framebuffer はこの窓の中にそのまま残り、**console の surface として最後まで使う**（2 節の 4）。CPU が書いた物を GPU に読ませる前は HDP の flush、GPU が書いた物を CPU が読む前は HDP の invalidate（5.1） |
| BAR2（64 bit、2 MiB の見込み） | doorbell | dword の index で書く。GFX の ring・SDMA は 64 bit の書き込み（`kern_mmio_write64`、amd64 の HAL は 1 回の 64 bit の store。review m7、HAL の変更は要らない）、IH の rptr は 32 bit |
| PSP（MP0） | secure processor。firmware を検証して TMR に置き、GFX・SDMA・SMU の firmware を起動する | driver は bootloader の mailbox（C2PMSG_35/36）で KDB・SYS_DRV・SOS を渡し、SOS の後は ring（C2PMSG_64〜71）で command を送る |
| SMU（MP1） | 電源・clock（PMFW） | firmware は PSP が load。最初は **Tier 0**（生きているかの確認と温度の読みだけ、boot の clock のまま）（2 節の 2） |
| GMC（GFXHUB・MMHUB） | GPU の MMU | VMID 0 は flat の GART（system memory を GPU の address に）、VMID 1〜15 は 4 段の page table |
| IH（OSSSYS） | 割り込みの ring | 全 source が 1 本の ring 0 に 32 byte の entry で来る。MSI 1 本 |
| RLC・CP（PFP・ME・CE・MEC1・MEC2）・SDMA0/1 | command の実行 | PSP の autoload で RLC・CP は起動済みになる。driver は GFX の ring 0 と SDMA0 の queue を作る（compute の queue は作らない、7.3） |
| DCN 2.0（6 pipe、6 OTG、6 DIG） | display | GOP が点けた pipe の timing・link・clock は触らず、surface の address と cursor だけを替える |
| VBIOS（AtomBIOS） | post、mode set の command table | GOP が post 済み（BIOS_SCRATCH_7 の bit 9）。amdrdna は AtomBIOS の interpreter を持たない（post も mode set もしない） |

## 2. 方針の要点

0. **scanout の規則（Guardrail、2026-10-04 ユーザー）**: amdrdna は UEFI の GOP の出力先（GOP が点けた OTG・DIG・connector）以外に scanout を始めない。出力先の種類（DP・HDMI・DVI）は DIG_BE_CNTL の DIG_MODE で読み、どの種類でも同じ surface の替え方で済む（DCN の surface は OTG より手前の HUBP にあるので、link の種類に依らない）。mode set・link training・別の connector は範囲の外。
1. **PSP の道だけ**。Navi10 の実機では GFX・SDMA・SMU の firmware を PSP 経由でしか load できない（Linux v6.19 `sdma_v5_0.c` は direct の load を「emulation だけ」と注記し、MP1 11.0 以降の PMFW を driver が直に load する道は無い）。したがって **firmware の package は必須**で、無ければ A3 で止まる（GOP の画面は残る）。
2. **SMU は Tier 0 から**: PSP が PMFW を load した後、生きている印（MP1 の FIRMWARE_FLAGS の bit 0）を読み、版の問い合わせ（GetDriverIfVersion・GetSmuVersion）と温度の読み（4.4）だけを行う。feature の有効化・DPM・pptable の転送はしない（Linux の `amdgpu.dpm=0` と同じ）。**危険**: fan・thermal の制御（FAN_CONTROL・THERMAL・FW_CTF の feature）が無く、clock は boot の値のまま。対策: (a) A4 以降、監視の thread が 1 秒ごとに温度を読み、edge が閾値（既定 95 ℃、boot の parameter で変えられる）を超えたら新しい投入を止めて device の fault にする（11 節）。(b) p004 の受け入れに「温度が少なくとも 1 つの方法で読める」を入れ、読めなければ Tier 0.5（THERMAL・FAN_CONTROL・FW_CTF だけ有効）を A5 の前の必須の段にする（判断の項目 3）。
3. **段ごとに失敗の後に残る物を決め、自動の再試行はしない**（3.1）。危ない段（A3 の PSP、A6 の surface の替え）の前に begin の行を出し、log を disk に同期してから進む（12 節）。
4. **GOP の framebuffer を console の surface のまま残す（nvrtx の scanout と同じ形、review M7）**: amdrdna は VRAM の allocator から GOP の framebuffer の範囲を除き、text console はそこへ書き続ける。display の takeover（A6）は client の surface と GOP の framebuffer の間の **flip だけ**で、copy も text console の付け替えも無い（pcat の text に新しい口は要らない）。release・fault・shutdown では GOP の framebuffer に flip で戻る。段の印は A8 まで画面に出る（client の surface を出している間は出ない）。PSP の **長い memory training（VRAM の下 32 MiB を壊す）は走らせない**（判断の項目 2）。
5. **GPU に読ませる memory の置き場（review B1）**: Linux の bare metal と同じく、**PSP の buffer（fw_pri・ring・fence・command・ucode）、GFX・SDMA の ring、writeback（rptr・wptr・fence の語）、IH の ring は GART に map した system memory**に置く。CPU は普通の RAM として読み書きし、HDP の flush・invalidate は要らない。VRAM に置くのは TMR（PSP だけが触る）、GART の表、page table、CSB、MQD、IB、shader の code、記述子、client の surface・cursor・BO で、CPU が BAR0 で書いた後は HDP の flush、GPU が書いた物を BAR0 で読む前は HDP の invalidate（5.1 の規則）。したがって **GMC（GART）と HDP の初期化は PSP より前**（段の順: A2 = GMC・HDP・IH、A3 = PSP。Linux の block の順と同じ）。fw_pri は GART の MC address が 1 MiB 整列になるよう GART の中の位置を選ぶ（物理の連続は要らず、zedBSD の DMA の vector の 64 KiB の上限にも当たらない）。
6. **DMA と VRAM の寿命**: GART の表・PSP の buffer・ring・writeback・page table は device が生きている間ずっと保つ。device の fault でも返さない（`drv_gpu_recovery_ops` の fault の契約）。
7. **投入と fence の待ちは lock の外**（WS241 の教訓）: ring への書き込みと doorbell は node の lock の中で短く、完了の待ちは lock を持たずに spin（2 ms）＋sleep。present は vblank まで lock を持たない。
8. **lock と割り込みの文脈（review M3）**:

| lock | 守る物 | 種類 | 持てる文脈 |
| --- | --- | --- | --- |
| `mmio_index` | MM_INDEX/MM_INDEX_HI/MM_DATA の 2 段の操作（VRAM の間接の読み書き） | spinlock（IRQ を止める） | 全部（1 語ずつ。窓の外の VRAM の転送は遅い。判断の項目 10） |
| `pcie_index` | PCIE_INDEX2/DATA2 の 2 段の操作（BAR5 の外の register、SMN） | spinlock（IRQ を止める） | 全部 |
| `grbm_select` | GRBM_GFX_INDEX（SE/SH の選択）と GRBM_GFX_CNTL（me/pipe/queue/vmid の選択）。選択を broadcast・0 に戻すまで持つ | mutex | thread だけ（割り込みから GRBM の選択はしない） |
| `psp_command` | PSP の ring の投入から応答まで（直列） | mutex | thread だけ（sleep する） |
| `smu_mailbox` | SMU の message の送信から応答まで | mutex | thread だけ |
| `ring`（GFX・SDMA ごと） | ring の wptr と doorbell、fence の番号 | spinlock | thread と IH の処理 |
| `display` | HUBP・OTG・cursor の register の書き込み、lease | mutex（nvrtx と同じ controller の mutex） | thread。`events` は別の短い spinlock で sequence を読む |
| `node` | session の表、VMID の割り当て、BO の表 | mutex | thread |

IH の処理は **割り込みの handler では writeback の wptr を読んで IH の thread を起こすだけ**にし、dispatch（fence の完了 `drv_gpu_complete`、vblank・flip・HPD の sequence の更新、VM fault の読み）は thread で行う。`drv_gpu_complete`・`drv_gpu_report_session_error` は IRQ-safe なので handler から呼んでもよいが、fault の STATUS の読み（GRBM の dummy read を含む）と display の sequence の更新を一つの場所に揃えるため thread にする。

9. **shader は legacy の VS と wave32**（p007 の約束）。NGG は使わない。Navi10 は NGG と legacy の切り替えで VGT_FLUSH が要る不具合があるので、全 pipeline で同じ mode にする（Mesa 25.3.6 `ac_gpu_info.c` の bug flag の表）。
10. **独自の名前**: register・field・packet・firmware の command・enum の名前は全部 amdrdna の語で付ける（13 節）。

## 3. 段の一覧

段の名前は A0〜A9（nvrtx の N・P に当たる。A = AMD）。各段は始めに `amdrdna: Ax begin`、終わりに `amdrdna: Ax ok …`（値付き）か `amdrdna: Ax fail: …` の 1 行を `kern_logf` で出す（1 行は console の桁に収める）。実機の合格の印は kernel.log のこの行。**A2 以降の順は Linux の IP block の順（GMC → IH → PSP → SMU → GFX → SDMA → display）に合わせた**（review B1）。

| 段 | すること（自分の言葉で） | 確かめる値（ok の行に出す） | Phase |
| --- | --- | --- | --- |
| A0 | 発見と ID: PCI の vendor/device/revision、BAR0・2・5 の大きさと種類、BAR5 の map、MP0 の C2PMSG_33 の bit 31（IFWI の初期化の完了）、VRAM の大きさ（NBIO の MEMSIZE、MiB）、IP discovery の表（VRAM の上端 −64 KiB の 10 KiB を MM_INDEX/MM_INDEX_HI/MM_DATA で読む）の署名・checksum・IP の一覧と GC の版、GRBM_STATUS、BIOS_SCRATCH_7 の bit 9（post 済み）、C2PMSG_81（SOS が生きているか）、MP1 の FIRMWARE_FLAGS。**読むだけ**。 | `A0 ok gc=10.1.10 vram=6144MiB bar0=256MiB posted=1 sos=0`。discovery の失敗・post 未・未対応の chip は attach しない。**C2PMSG_81 ≠ 0（前の driver が PSP を load 済み）なら `A0 fail: sos alive, cold power cycle needed` を出して A1 まで（読むだけ）で止まる**（review B2、判断の項目 6） | p002 |
| A1 | GOP の readout（読むだけ）: 6 つの OTG の MASTER_EN、点いている OTG の timing、ODM → OPP → MPC → MPCC → DPP/HUBP の鎖、HUBP の surface の address・format・pitch・viewport、DIG の BE の enable・FE の select・DIG_MODE、BIOS_SCRATCH_6 の ACC_MODE、D1〜D6 の VGA_CONTROL（**legacy の VGA の mode なら attach しない**、review m12）、OTG_MASTER_UPDATE_LOCK。loader が渡す GOP の framebuffer（物理・幅・高さ・stride）と照らす。VRAM の配置（5.2）を決めて log に出す。firmware の package の有無を調べる（無ければ A1 で止まり、GOP の画面のまま） | `A1 ok otg=0 hubp=0 dig=0 mode=DP 1920x1080 fb=0x... pitch=... vram: gop=... top=... win=256MiB`、GOP の framebuffer の物理 − BAR0 の base が HUBP の address − FB の base と一致 | p002 |
| A2 | GMC・HDP・IH（firmware は要らない）: FB の base/offset、GART の表（VRAM、flat、512 MiB 分）、GFXHUB → MMHUB の順に VMID 0 の設定、context 1〜15 の設定、HDP の初期化（MISC_CNTL）と flush、fault の既定、VMID 0 の TLB の invalidate（engine 17）、doorbell の aperture、IH の ring（GART の system memory、256 KiB）と wptr の writeback、MSI、IH の enable。確かめ: register の読み戻し、GART の表の読み戻し、HUBP・OPTC の underflow の状態（MMHUB の aperture の書き換えが GOP の scanout を乱していないか、review m17） | `A2 ok fb=0x... gart=0x...+512MiB ih=ring0 msi=1 underflow=0` | p003 |
| A3 | PSP: root の mount の後に firmware を読み container の header を検査 → GART の system memory に fw_pri（MC で 1 MiB 整列）・ring・fence・command・ucode の buffer を確保、TMR は VRAM → bootloader で KDB → SYS_DRV → SOS（C2PMSG_81 ≠ 0 を待つ）→ ring の作成 → LOAD_TOC（TMR の大きさ）→ TMR の確保 → LOAD_IP_FW（SMU）→ SETUP_TMR → LOAD_IP_FW を順に（SDMA0・SDMA1・CE・PFP・ME・MEC1・MEC2・RLC の 3 つの list・RLC_G）→ AUTOLOAD_RLC → LOAD_ASD | `A3 ok sos=0x... tmr=...KiB fw=12`。各 command の status と fence の値 | p004 |
| A4 | SMU の Tier 0: FIRMWARE_FLAGS の bit 0、GetDriverIfVersion（0x37 の見込み）・GetSmuVersion、温度（THM の register と SMU の metrics の表の両方を試す、4.4）、監視の thread を始める | `A4 ok if=0x37 smu=42.64.0 temp_thm=... temp_smu=...` | p004 |
| A5 | CP と SDMA: golden の register（GC 40 項目、SDMA 24＋2 項目）、GRBM の timeout、RB・CU・TCC の mask の読み、tile steering、VMID ごとの SH_MEM、RLC の autoload の完了待ち（CP_STAT = 0 と BOOTLOAD_COMPLETE）、CSB と RLC の CSIB、SRM、tcp harvest、GFX の ring 0（CP_RB0 の register で直に）、ME/PFP/CE の unhalt、preamble（clear state）、SCRATCH の ring test、SDMA0 の gfx queue、WRITE の ring test、fence（RELEASE_MEM と FENCE＋TRAP）と EOP・TRAP の割り込み、最初の copy（SDMA の COPY_LINEAR で VRAM → VRAM、GFX の DMA_DATA で同じ） | `A5 ok gfx=rb0 sdma0 fence irq copy tcc=...` | p005 |
| A5s | session: VMID の割り当て、4 段の page table、BO（VRAM・system）、VM の flush の投入、VM fault の session への対応、`drv_gpu_ops` の RESOURCE〜SHARE・recovery、停止の hook、**GPU device の登録（renderer の node、display の ops は無し）** | `A5s ok sessions vmid=1..15 gpu device registered (no display)` | p011 |
| A6 | display の引き継ぎ: client の surface（VRAM、format 8 = ARGB8888、linear、GOP と同じ pitch・大きさ・256 byte 整列）を用意し、HUBP の PRIMARY_SURFACE_ADDRESS（HIGH → LOW の順）を替える（VUPDATE で latch する flip）、flip の完了（FLIP_PENDING と EARLIEST_INUSE）、HUBP の flip の割り込みと OTG の VSTARTUP の割り込み、frame counter、GOP の framebuffer への flip で戻る。試験では `amdrdna.disp.test=1` で色の帯の surface を数秒出して戻す | `A6 ok flip pending=0 vblank=60/s back=1` | p006 |
| A7 | cursor: HUBP の cursor（address・size・mode 2 = premultiplied、pitch）、DPP の CNVC の cursor の mode、position・hot spot、enable/disable | `A7 ok cursor 64x64` | p006 |
| A8 | GPU device の登録に display・scanout の ops を足す（登録は 1 回。`amdrdna.disp.own=1` の時は A5s の登録を A8 まで遅らせ、display と renderer の ops を揃えて登録する。review B3）、present・wait・lease・cursor の ops の動作、HPD の sequence | `A8 ok gpu device registered, display 1920x1080` | p006 |
| A9 | 最初の compute の dispatch と draw（p007 の compiler の program）、executor（Venus の stream → PM4）、renderer の node、compositor の描画 | `A9 ok dispatch draw`、vkquad の pixel の一致 | p008・p009 |

登録の規則（review B3）: `drv_gpu_register` は 1 回だけで、ops は登録の前に組み立てる（nvrtx の bind と同じ順）。組み合わせ: renderer だけ（`disp.own=0`、A5s の終わり）、renderer＋display（`disp.own=1`、A8）、display だけ（`vk=0 disp.own=1`、A8）。どれも同じ ops の表の作り方で、無い側の callback と capability を落とす。

### 3.1 段ごとの失敗の後に残る物と回復（自動の再試行はしない）

| 失敗した段 | 残る物 | 回復 |
| --- | --- | --- |
| A0・A1 | 何も（hardware に書いていない） | attach しない。GOP の画面は続く |
| A2 | GART・IH・HDP の設定、MMHUB の aperture | IH を止め、context 0 の enable を 0 にして止まる。GOP の scanout は MMHUB の aperture を通るので、A2 の失敗の後に画面が乱れる可能性がある（14 節） |
| A3（SOS の前） | 何も（bootloader の mailbox に書いただけ。KDB・SYS_DRV の load が途中で止まった時の PSP の状態は正本に無い） | 止まる。firmware の buffer は保つ。**次の起動は cold power cycle が要る見込み**（C2PMSG_81 が立ったままなら A0 で止まる） |
| A3（SOS の後）・A4 | SOS が生きている（C2PMSG_81 ≠ 0）。TMR が張られていれば TMR。RLC・CP が動いていれば動いたまま | 停止の手順（11 節）を 1 回だけ流す。SOS 自体は止められない（Linux も止めない）。**次の起動は cold power cycle**（判断の項目 6） |
| A5 | ring・queue が動いている | CP・SDMA を halt して止まる（11 節） |
| A5s | session の資源 | その session の fault。device は動き続ける |
| A6〜A8 | client の surface を scanout 中 | GOP の framebuffer の address に戻して（flip）から buffer を返す。戻せなければ buffer を reset まで保持 |
| A9 | session の資源 | その session の fault。device は動き続ける |

## 4. firmware（BLOB）と PSP・SMU

### 4.1 package

`userland/firmware/README.md` の規約に合わせ、**既定 off の独立の firmware の package**（案: `userland/firmware/amd-navi/`、package の名前は p004 で決める。判断の項目 1）。base の system と kernel に同梱しない。取得元は linux-firmware の tag `20260410`（commit `dc85ccedc9c973682fbcf4d628ca61174bcc3120`、AX211・i915・nvidia-gsp と同じ）。**BLOB は 11 file**（下の表、`navi10_mec2.bin` は `navi10_mec.bin` と同じ byte）に `LICENSE.amdgpu`・`WHENCE` の該当の行・manifest を添える。file の大きさと SHA-256 は [phase001/phase.md](phase001/phase.md) の表（p001 で取得して確かめた）。

| file（`amdgpu/`） | install | 扱い |
| --- | --- | --- |
| `navi10_sos.bin`（KDB・SYS_DRV・SOS・TOC を含む container） | `/lib/firmware/amdgpu/` | 必須。A3 の bootloader と LOAD_TOC |
| `navi10_asd.bin` | 同 | 必須（Linux と同じく load し、失敗は A3 の fail）。HDCP・DTM の TA は使わないが、ASD が無い bare metal の例が正本に無い |
| `navi10_smc.bin` | 同 | 必須。PSP の LOAD_IP_FW（type 18）。中の soft pptable（3 つ）は Tier 0 では使わない |
| `navi10_pfp.bin`・`navi10_me.bin`・`navi10_ce.bin`・`navi10_mec.bin`・`navi10_mec2.bin`・`navi10_rlc.bin` | 同 | 必須。LOAD_IP_FW |
| `navi10_sdma.bin`・`navi10_sdma1.bin` | 同 | 必須。LOAD_IP_FW（type 9・10） |
| `navi10_ta.bin` | 入れない | HDCP・DTM だけで display・GFX・SDMA に要らない。Linux では file の有無を検査するが、amdrdna は要求しない |
| `navi10_vcn.bin`・`navi10_gpu_info.bin` | 入れない | VCN は範囲の外（ユーザーの決定 3）。gpu_info は Linux v6.19 が Navi10 で使わない |
| `LICENSE.amdgpu`・`WHENCE` の該当の行・manifest | package の決まりの場所 | license の要点: binary の形の再配布可、notice の再掲が条件、reverse engineering・decompile・disassemble の禁止（[監査](amdrdna-license-audit.md)） |

- Navi10 には display の firmware（dmcu・dmcub）が無い（linux-firmware の directory に file が無く、Linux v6.19 の DCN 2.0 は要求しない）。
- firmware は `/lib/firmware` から読むので、**A3 は root の mount の後に非同期**で行う（attach の時は A0〜A2）。読み込みは nvrtx の `file.c`（root の mount を待ち、大きい file を読む）と同じ形を amdrdna に持つ（写して独自の名前。判断の項目 5 と同じ扱い）。
- **検査の場所（review m16）**: SHA-256 の照合は package の install の時（manifest）。kernel は load の時に container の header の整合（全体の byte 数 = file の大きさ、ucode の offset + 大きさ ≤ file、各記述子の範囲が file の中、sos の 4 つの部分が重ならない）を**必ず**検査し、合わなければ A3 の fail。
- **非同期の bring-up と停止の競合（review m13）**: 段の進み具合を device の状態（`stage` = 最後に ok した段）として持ち、停止の hook（11 節）は bring-up の thread を止めてから（段の途中なら、その段の begin まで待つか timeout）、`stage` までの逆順だけを流す。

### 4.2 container の header（load に要る欄だけを読む。中身は解析しない）

全 file の先頭は 32 byte の共通の header（出典: Linux v6.19 `amdgpu_ucode.h` の `common_firmware_header`）: 0x00 全体の byte 数、0x04 header の byte 数、0x08/0x0A header の版、0x0C/0x0E IP の版、0x10 ucode の版、0x14 ucode の byte 数、0x18 ucode の file 内の offset、0x1C crc32（Linux は検査しない。amdrdna も見ない）。

| file | header の版 | 続きの欄（出典: 同 header の各 struct） | PSP に渡す image |
| --- | --- | --- | --- |
| sos | 1.1 | 0x20 から 12 byte の記述子 ×3: sos（版・offset・大きさ）、toc、kdb。offset は ucode の先頭から | SYS_DRV = ucode の先頭から sos.offset byte、SOS = ucode + sos.offset から sos.size、TOC・KDB は各 offset から |
| asd | 1.1 | 使わない（記述子は 0） | ucode の全部 |
| smc | 2.1 | 0x20 start address、0x24 pptable の数、0x28 entry の offset | ucode の全部（pptable は使わない） |
| pfp・me・ce・mec・mec2 | 1.0 | 0x20 feature、0x24 jump table の offset（dword）、0x28 jump table の大きさ（dword） | CE・PFP・ME は ucode の全部。MEC は ucode から jump table を除いた前半（JT は autoload では送らない） |
| rlc | 2.1 | 0x20 から 18 語（v2.0 の欄: feature、jump table、save/restore・list の offset と大きさ）、**0x68 に 1 語（direct の register list の長さ）、0x6C から srlc・srlg・srls の各 {版・feature・大きさ・offset}（16 byte ずつ、0x6C・0x7C・0x8C。offset は file の先頭から）、header は全部で 156 byte**（review M4 で直した） | RLC_G = ucode の全部、RESTORE_LIST_CNTL = srlc、GPM_MEM = srlg、SRM_MEM = srls |
| sdma・sdma1 | 1.0 | 0x20 feature、0x24 change、0x28/0x2C jump table | ucode の全部（JT を含む） |

p001 で 20260410 の file の header を読み、各 file の全体の byte 数が file の大きさと一致し、sos の中の 4 つの部分が隙間なく並ぶ（100208 + 78640 = 178848 = TOC の offset、+1536 = 180384 = KDB の offset、+3872 = 184256 = ucode の大きさ）ことと、rlc の srlc（592 byte @ 25344）・srlg（1536 @ 25936）・srls（16480 @ 27472）が file の末尾（43952）まで隙間なく並ぶことを確かめた（作業の文書 b の 2.8・2.9）。host 試験 (a) の期待値はこれらの実測の値。

### 4.3 PSP の手順（A3、Procedure referenced の対象）

出典（手順と値）: Linux v6.19 `psp_v11_0.c`（bootloader・ring）、`amdgpu_psp.c`（command の投入、TMR、load の順）、`psp_gfx_if.h`（command の id・payload・frame）、`amdgpu_psp.h`（bootloader の command の値）、`mp_11_0_offset.h`（MP0 の register）。

1. **register**: MP0 の base は discovery の表から（navi10 の静的な header では dword 0x16000）。使う物: C2PMSG_33（IFWI の完了、bit 31）、35（bootloader の command/status）、36（引数 = address >> 20）、64（TOS の command/response）、67（ring の wptr、dword）、69/70（ring の address）、71（ring の byte 数）、81（SOS の生存）。register の offset は MP0 の base + 0x61・0x63・0x64・0x80・0x83・0x85・0x86・0x87・0x91（`mp_11_0_offset.h`）。
2. **bootloader の待ち**: C2PMSG_35 & 0x8000FFFF == 0x80000000（ready かつ status 0）を最大 20 回 × 100 ms 待つ。
3. **KDB → SYS_DRV → SOS**: 各 image を fw_pri（1 MiB、MC で 1 MiB 整列、GART の system memory）に zero fill の上に写し（system memory なので HDP の flush は要らない。memory barrier だけ）、C2PMSG_36 = fw_pri の MC address >> 20、C2PMSG_35 = command（KDB 0x80000、SYS_DRV 0x10000、SOS 0x20000）、2 の待ち。SOS だけは 20 ms 置いてから C2PMSG_81 が 0 でなくなるのを 100 ms 待つ。
4. **ring**: C2PMSG_64 & 0x8000FFFF == 0x80000000（TOS ready）を待ち、69/70 = ring の MC address、71 = 0x1000、64 = 0x20000（GPCOM ring の作成）、20 ms、同じ応答を待つ。ring は 4 KiB = 64 byte の frame × 64。
5. **command の投入**（`psp_command` の mutex の中）: 4 KiB の command buffer を zero にして 1024 byte の command を写す（buf_size・buf_version・frame の cmd_buf_size は Linux と同じく 0 のまま）。fence の値 = 通し番号。C2PMSG_67 から wptr（dword）を読み、frame = wptr / 16（wptr % (ring の dword 数) == 0 なら 0）。frame に command buffer の address・fence の address・fence の値を書き、memory barrier、wptr += 16（mod 1024）を C2PMSG_67 に書く（これが doorbell）。fence の語（system memory）が通し番号になるのを 10〜100 µs の sleep で最大 20000 回待つ。応答の status ≠ 0 は warning（PSP の firmware によっては 0 を書かない）、timeout は error。LOAD_IP_FW の応答の fw_addr（TMR の中の address）を記録する。
6. **command の id**（`psp_gfx_if.h`）: LOAD_TA 1、UNLOAD_TA 2、LOAD_ASD 4、SETUP_TMR 5、LOAD_IP_FW 6、DESTROY_TMR 7、LOAD_TOC 0x20、AUTOLOAD_RLC 0x21。command は command buffer の +8、payload は +28、応答は +864（status、session id、fw_addr lo/hi、tmr_size）。
7. **順**: LOAD_TOC（TOC を fw_pri に写す。応答の tmr_size が **0 なら A3 の fail**（Linux は 4 MiB の既定を応答で上書きするが、0 は異常として扱う。review m6））→ TMR を VRAM に確保（1 MiB 整列、理想は大きさで整列。窓の外でよい）→ LOAD_IP_FW SMU（type 18）→ SETUP_TMR（TMR の MC address・大きさ、flag の bit 1 = 物理 address も渡す、VRAM の物理 = FB の base からの offset）→ LOAD_IP_FW を SDMA0 9・SDMA1 10・CE 3・PFP 2・ME 1・MEC1 4・MEC2 4・RLC の RESTORE_LIST_CNTL 22・GPM_MEM 20・SRM_MEM 21・RLC_G 8 の順（MEC の JT 5・6 は送らない。image は ucode の buffer（GART の system memory、page 整列）に置く）→ AUTOLOAD_RLC → LOAD_ASD（ASD を fw_pri に、shared buffer は 0）。
8. **停止**（11 節）: UNLOAD_TA（ASD の session id）→ DESTROY_TMR → C2PMSG_64 = 0x30000（ring の破棄）→ 20 ms → 応答。
9. **TMR**: 実機の大きさは LOAD_TOC が返す（正本に数が無い）。A3 の ok の行に出す。

### 4.4 SMU の mailbox と Tier 0（A4・停止、Procedure referenced の対象。review M8）

出典（手順と値）: Linux v6.19 `pm/swsmu/smu_cmn.c`（mailbox の手順と応答の code）、`pm/swsmu/smu11/smu_v11_0.c`（register の選択、版の確認、FIRMWARE_FLAGS）、`pm/swsmu/smu11/navi10_ppt.c`（message の番号の表、metrics の読み）、`pm/swsmu/inc/smu_v11_0.h`・`pm/swsmu/inc/pmfw_if/smu_v11_0_ppsmc.h`・`pm/swsmu/inc/pmfw_if/smu11_driver_if_navi10.h`、`mp_11_0_offset.h`・`mp_11_0_sh_mask.h`、`thm_11_0_2_offset.h`・`thm_11_0_2_sh_mask.h`。

- **register**（MP1 の base は discovery から、静的な header では dword 0x16000）: message = base + 0x282（C2PMSG_66）、引数（入出力）= base + 0x292（C2PMSG_82）、応答 = base + 0x29A（C2PMSG_90）。生存の印 = SMN の 0x03B10024（MP1 の FIRMWARE_FLAGS、PCIE_INDEX2/DATA2 の間接）の bit 0。
- **手順**（`smu_mailbox` の mutex の中）: (1) 最初の message 以外は応答の register が 0 でなくなるまで待つ（前の message の完了。最大 2 s）。(2) 応答 = 0、引数 = param、message = 番号 を書く。(3) 応答が 0 でなくなるまで待つ（最大 2 s）。(4) 0x01 = OK、0xFF = 失敗、0xFE = 未知の command、0xFD = 前提の不足、0xFC = 他の command で busy（10 µs 置いて再送）、0xFB = debug。それ以外か timeout は SMU の hang と記録し、以後の message を送らない。(5) 出力の引数は引数の register から読む。
- **Tier 0 で送る message**: GetDriverIfVersion 0x03（0x37 の見込み）、GetSmuVersion 0x02、停止の時に PrepareMp1ForUnload 0x32。
- **温度**（review M5。2 つの源を A4 で両方試し、実機で比べる）: (a) THM の register CG_MULT_THERMAL_STATUS（THM の base 0x16600 + 0x5F）の ASIC_MAX_TEMP（bit 8:0）と CTF_TEMP（bit 17:9）。Linux の Navi10 の道はこれを温度に使わない（SMU 11 の thermal の割り込みの設定には THM を使う）ので、単位（℃ の見込み）は実機で (b) と比べて確かめる。(b) SMU の metrics の表: driver の表（VRAM、表の最大の大きさ）を SetDriverDramAddrHigh/Low（0x0E/0x0F）で知らせ、TransferTableSmu2Dram（0x12、引数 = 表の id 6（TABLE_SMU_METRICS）| (0 << 16)）で転送させ、HDP の invalidate の後に表の TemperatureEdge（u16、℃）を読む。表の struct は PMFW の版で 3 種（legacy・NV12・NV1X）あり、どれかは A4 で読んだ SMU の版で決める（p004 で `smu11_driver_if_navi10.h` の値を出典付きで写す）。(b) が Tier 0（feature の有効化なし）で応答するかは正本に例が無い（Linux は hw_setup の中で driver の表を知らせる）。(c) p004 の受け入れ: (a) か (b) で温度が読め、idle の値がもっともらしい（20〜70 ℃）こと。どちらも読めなければ Tier 0.5 を A5 の前の必須の段にする。
- **Tier 0.5**（必要になった時だけ、判断の項目 3）: SetAllowedFeaturesMaskHigh/Low（0x05/0x04）に FW_CTF（31）・FAN_CONTROL（32）・THERMAL（33）だけ、EnableSmuFeaturesLow/High（0x08/0x09）、GetEnabledSmuFeaturesLow/High（0x0C/0x0D）で読み戻す。pptable の転送（SetDriverDramAddr と TransferTableDram2Smu 0x13 の id 0）が先に要る可能性があり、その出所は smc.bin の soft pptable（3 つ。どれを使うかは VBIOS の firmwareinfo の id で決まり、amdrdna は VBIOS を読まない）か VBIOS の powerplayinfo。既定は「pptable 無しで feature だけ試し、拒まれたら止まる」。

## 5. GMC（memory の制御と VM）

出典（値）: Linux v6.19 `gfxhub_v2_0.c`・`mmhub_v2_0.c`・`gmc_v10_0.c`・`amdgpu_gmc.c`・`amdgpu_gart.c`・`amdgpu_vm.h`・`hdp_v5_0.c`・`amdgpu_hdp.c`・`amdgpu_ib.c`・`gc_10_1_0_offset.h`・`gc_10_1_0_sh_mask.h`・`gc_10_1_0_default.h`・`mmhub_2_0_0_offset.h`・`hdp_5_0_0_offset.h`・`hdp_5_0_0_sh_mask.h`・`nbio_2_3_offset.h`・`navi10_enum.h`。

### 5.1 HDP の規則（review B1）

HDP は BAR0 越しの VRAM の読み書きの cache。規則:

- (a) CPU が BAR0 で VRAM に書いた後、GPU・PSP・DCN に読ませる前に **flush**: NBIO の HDP_MEM_COHERENCY_FLUSH_CNTL（NBIO の seg 2 の dword 0xF7。remap の穴は使わない）に 0 を書き、NBIO の MEMSIZE を読んで posting。memory barrier を先に。
- (b) GPU が書いた VRAM を CPU が BAR0 で読む前に **invalidate**: HDP の READ_CACHE_INVALIDATE（HDP の base 0xF20 + 0xD1）に 1 を書いて読み戻す。
- (c) ring の上: IB の前に WAIT_REG_MEM（operation 1 = 書いて待つ、register space、HDP_FLUSH_REQ に engine の bit を書き FLUSH_DONE を待つ。GFX の cp0 = 0x1、SDMA0 = 0x400）、IB の後に READ_CACHE_INVALIDATE への register の書き込み。SDMA は POLL_REGMEM の HDP_FLUSH の形。
- (d) 初期化: A2 で HDP の MISC_CNTL（base + 0xD3）の FLUSH_INVALIDATE_CACHE（bit 0）を立て、1 回 flush。
- 適用: GART の表・page table・CSB・MQD・IB・shader の code・記述子を CPU で書いた後は (a)。`resource_write` は (a)、`resource_read` は (b)。client の surface を CPU で書く経路（`resource_map` の後）は user の責任にせず、present の時に (a) を 1 回行う。

### 5.2 VRAM の配置（review M6）

| 領域 | 中身 | 整列 |
| --- | --- | --- |
| 予約: GOP の framebuffer | A1 で読んだ HUBP の address − FB の base から、pitch × 高さ。console の surface のまま | — |
| 予約: 上端 1 MiB | IP discovery の表（上端 −64 KiB の 10 KiB）、PSP の runtime DB、memory training の p2c。Linux は上端を予約する | — |
| 未知: VBIOS が自分用に予約する範囲 | Linux は VBIOS の firmwareinfo の表（`vram_usagebyfirmware`）で知る。amdrdna は VBIOS を読まないので知らない。GOP の framebuffer と上端 1 MiB の外に VBIOS の予約があれば壊しうる（14 節）。対策: A1 で discovery の表の後ろ（上端 1 MiB の中）と GOP の framebuffer の前後の 1 MiB を allocator から除く（余裕） | — |
| 窓の中（BAR0 の先頭 256 MiB、予約を除く） | CPU が書く物: GART の表（1 MiB）、page table、CSB、MQD、IB、shader の code、記述子、client の surface（256 byte 整列）、cursor（4 KiB 整列）、`resource_map` される BO | 用途ごと |
| 窓の外（256 MiB 以降、予約を除く） | TMR（1 MiB 整列、PSP だけが触る）、GPU だけが触る BO（`resource_map` 不可、`resource_read/write` は MM_INDEX の間接で 1 語ずつ） | — |

allocator は窓の中と外を別の pool にし、要求に「CPU が触るか」の flag を持つ。A1 の ok の行に配置（gop・top・win の範囲）を出す。resizable BAR は使わない（判断の項目 10）。

### 5.3 FB と GART（VMID 0）

- **FB の位置**: VRAM の MC address の先頭 = GCMC_VM_FB_LOCATION_BASE の下 24 bit << 24（単位 16 MiB）。物理の offset = GCMC_VM_FB_OFFSET << 24（dGPU では 0 の見込み）。VRAM の大きさ = NBIO の MEMSIZE（MiB）。GOP（VBIOS）が設定済みで、driver は読むだけ。PTE・PDE・page table の base の register に書く「物理」= MC − FB の先頭 + FB の offset。
- **BAR0 と VRAM の offset は同じ**（CPU の窓の offset = VRAM の offset = MC − FB の先頭）。GOP の framebuffer の VRAM の offset はここから出る（A1）。
- **GART（VMID 0）**: flat の表（8 byte の entry × 512 MiB / 4 KiB = 1 MiB、VRAM、page 整列）。GART の MC の範囲は FB の外に 4 GiB 整列で置く（FB の下に 512 MiB 入れば 0 から、入らなければ FB の上）。entry = (物理 & 0x0000FFFFFFFFF000) | VALID(bit 0) | SYSTEM(bit 1) | SNOOPED(bit 2、cached の時) | READABLE(bit 5) | WRITEABLE(bit 6) | EXECUTABLE(bit 4) | MTYPE UC(3 << 48)。無効の entry は dummy page の address と flag 0。表を書いた後: memory barrier → HDP flush → 両 hub の VMID 0 の TLB invalidate。
- **両 hub に同じ VMID 0 を設定**（GFXHUB は GFX・compute・SDMA、MMHUB は display・IH・HDP・VCN）。順: GFXHUB → MMHUB の各 (a) context 0 の page table の base（物理 | VALID）と START/END（4 KiB の page 番号、32/4 bit に分ける）、(b) system aperture（AGP は無効 = BOT 0xFFFFFF・TOP 0、aperture の LOW/HIGH = FB の範囲 >> 18、default の address = scratch の page、fault の default = dummy page、CNTL2 の read retry）、(c) L1 TLB の CNTL（enable、system access mode 3、advanced driver model、MTYPE UC）、(d) L2 の CNTL 1〜5（既定の値に cache enable・fragment・識別の mode を足す。CNTL3 の既定 0x80100007、CNTL4 0xC1、CNTL5 0x3FE0）、(e) context 0 の enable（depth 0）、(f) identity aperture の無効化、(g) context 1〜15 の設定（5.4）、(h) 18 本の invalidate engine の範囲 = 全域。その後 HDP の初期化（5.1 の (d)）、fault の既定（redirect to default page）、MMHUB → GFXHUB の順に VMID 0 の invalidate。MMHUB の aperture の書き換えは GOP の scanout の経路に当たりうるので、A2 の ok の行に HUBP・OPTC の underflow を出す。
- **HDP の flush**: 5.1。
- **invalidate の REQ の語**: (1 << vmid) | 0x00F80000（flush type 0、L2 PTE・PDE0〜2・L1 PTE の invalidate）。ACK の bit = 1 << vmid。MMHUB では SEM の register で acquire/release（power gating の跨ぎで ACK が消える対策）。CPU からの invalidate は engine 17 で REQ を書き、GFXHUB では dummy read を 1 回挟んでから ACK の bit を待つ。
- **Navi1x の SDMA の errata**: VMID 0 の invalidate を VA の変換と同時に行うと memory が壊れうる。SDMA が動いた後の GART の更新は、SDMA の ring から invalidate を出す（A5 以降）。A2〜A4 では SDMA がまだ無いので CPU から行う。

### 5.4 session の VM（context 1〜15）

- context 1〜15: enable、depth 3（4 段）、block size 0（9 bit）、fault の既定 1、**retry 0（no-retry、fault の嵐を防ぐ。Linux の Navi10 の既定は retry 1 だが、回復可能な fault を作らないので no-retry）**、START 0、END = 2^36 − 1（page 番号）。
- **4 段の page table**（PDB2 → PDB1 → PDB0 → PTB、各 512 entry × 8 byte = 4 KiB、VRAM、CPU で書いて HDP flush）: VA の bit [47:39] PDB2、[38:30] PDB1、[29:21] PDB0、[20:12] PTB。PDE = 物理（64 byte 整列）| VALID（system memory の表なら SYSTEM・SNOOPED も）。PTE = 物理 | VALID | SYSTEM（system memory）| SNOOPED | READ | WRITE | EXE | MTYPE。huge page は PDE の bit 54（P）で 2 MiB（PDB0）。fragment の field（bit 11:7）は最初 0。
- **VMID の割り当て**: session ごとに静的に 1 つ（1〜15、15 session を超えたら open は ENOSPC）。Linux の job ごとの動的な割り当ては後（Future Work）。root の PD の設定は ring から（WRITE_DATA で context の base の register を書き、WAIT_REG_MEM の write-then-wait で invalidate の REQ と ACK）、GFX では PFP_SYNC_ME を続ける。
- **fault**: client VMC（0x12）src 0 が MMHUB、UTCL2（0x1B）src 0 が GFXHUB。IV の src_data[0] << 12 | (src_data[1] & 0xF) << 44 が address、src_data[1] の bit 7 RETRY・6 READ・5 WRITE・4 EXE。STATUS の register（dummy read の後）を読み、CNTL の bit 0 で再武装。STATUS の VMID（bit 23:20）で session を特定し、その session を fault にする（`drv_gpu_report_session_error`。11 節）。

## 6. IH（割り込み）

出典（値）: Linux v6.19 `navi10_ih.c`・`amdgpu_ih.c`・`amdgpu_ih.h`・`amdgpu_irq.c`・`osssys_5_0_0_offset.h`・`osssys_5_0_0_sh_mask.h`・`nbio_v2_3.c`・`soc15_ih_clientid.h`・`ivsrcid/`、`display/amdgpu_dm/amdgpu_dm.c`（DCN の割り込みの登録）、`dcn_2_0_0_sh_mask.h`（HPD の register）。

- ring 0 だけ（256 KiB、2 の冪、GART の system memory、256 byte 整列）、wptr の writeback の語も system memory。MSI 1 本（MSI-X なら vector 0）。
- 設定の順: RB_CNTL の enable を 0（GPU の timestamp は 1、INTR 0）→ RPTR・WPTR = 0 → NBIO の INTERRUPT_CNTL2 = dummy page >> 8、INTERRUPT_CNTL の dummy read override 0・nonsnoop 0 → RB_BASE = address >> 8、BASE_HI = (address >> 40) & 0xFF → RB_CNTL: MC_SPACE 4（GPUVM）、WPTR overflow の enable と clear、RB_SIZE = log2(byte / 4)（256 KiB で 16）、writeback の enable、MC_SNOOP、RPTR_REARM = MSI → WPTR_ADDR LO/HI → WPTR・RPTR = 0 → DOORBELL_RPTR = index | ENABLE → NBIO の IH の doorbell の範囲（offset = index、size 2）→ bus master → RB_CNTL の enable・INTR を 1。
- RB_CNTL の field: RB_ENABLE bit 0、RB_SIZE 5:1、GPU_TS 7、WPTR_WRITEBACK 8、WPTR_OVERFLOW_ENABLE 16、ENABLE_INTR 17、MC_SNOOP 20、RPTR_REARM 21、MC_VMID 27:24、MC_SPACE 30:28、WPTR_OVERFLOW_CLEAR 31。
- 処理（IH の thread、2 節の 8）: writeback の wptr を読み（overflow の bit が立っていれば register から読み直し rptr を wptr + 32 にして overflow を clear）、rptr ≠ wptr の間 32 byte の entry を dispatch、rptr を writeback の語に書いて doorbell（32 bit）、**その後 wptr を読み直して動いていればもう一度回す**（review m11。RPTR_REARM だけに頼らない）。entry: dw0 = client 7:0・src 15:8・ring 23:16・vmid 27:24・bit 31 = MMHUB、dw1/2 = timestamp、dw3 = pasid、dw4〜7 = src_data[0..3]。
- 使う source（client、src）: GFX の EOP = GRBM_CP 0x14、181（ring_id から me・pipe・queue）。SDMA0 の trap = 0x08、224。SDMA1 = 0x09、224。VM fault = 0x12/0x1B、0。CP の bad opcode 183・priv reg 184・priv inst 185（**device の fault**、11 節）。DCN = DCE 0x04: OTG の VSTARTUP 0x3C〜0x41（OTG 0〜5、vblank）、HUBP の flip 0x4F〜0x54、HPD 9（ctx id 0〜5、RX は 6〜11）。**amdgpu_dm で確かめた**（2026-10-15、`amdgpu_dm.c` の DCN の割り込みの登録: client DCE、VSTARTUP・VUPDATE_NO_LOCK・HUBP の flip・HPD の src id）。
- 各 source の enable は IP の側: CP_INT_CNTL_RING0 の TIME_STAMP_INT_ENABLE（bit 26）、SDMA0_CNTL の TRAP_ENABLE（bit 0）、OTG_GLOBAL_SYNC_STATUS の VSTARTUP_INT_EN（0x1）と EVENT_CLEAR（0x10）、HUBPREQ の SURFACE_FLIP_INTERRUPT の INT_MASK（0x1）と CLEAR（0x100）、**HPD（review m19）**: HPDn の DC_HPD_INT_CONTROL（HPD0 は DCN の seg 2 の 0x1F14 の組、stride 8。HPD0_DC_HPD_INT_STATUS が 0x1F14、CONTROL はその組の中。p006 で offset を出典付きで写す）の DC_HPD_INT_EN（0x10000）と DC_HPD_INT_ACK（0x1）、DC_HPD_RX_INT_EN（0x1000000）、status の DC_HPD_SENSE（0x2）。context の CNTL の fault の interrupt bit。
- doorbell の index（64 bit の slot の番号を << 1 して dword）: GFX の ring 0 = 0x8B、SDMA0 = 0x100、SDMA1 = 0x10A、IH = 0x178（KIQ 0・MEC の ring 0 = 3 は使わない）。NBIO の doorbell の aperture の enable（RCC_DOORBELL_APER_EN）を A2 で立てる。

## 7. CP と SDMA

出典（値）: Linux v6.19 `gfx_v10_0.c`・`amdgpu_gfx.c`・`amdgpu_rlc.c`・`amdgpu_ring.c`・`amdgpu_fence.c`・`nvd.h`・`clearstate_gfx10.h`・`sdma_v5_0.c`・`navi10_sdma_pkt_open.h`・`v10_structs.h`・`gc_10_1_0_*.h`、Mesa 25.3.6 `src/amd/common/sid.h`。

### 7.1 RLC・golden・constants（A5 の前半）

1. golden の表: GC 10.1 の 40 項目（CB・CGTT の clock gating・pipe steer・DB・GCR・PA_SC・SPI・SQ・TA・TCP・UTCL1 の (register, and mask, or 値) の read-modify-write）と SDMA 5 の 24 項目＋Navi10 の 2 項目。値は Linux の表から出典付きで取る（データ）。SPM の約 1050 項目は性能の計測用で入れない。
2. GRBM_CNTL の READ_TIMEOUT = 0xFF。GB_ADDR_CONFIG は hardware から読む（golden には無い）。
3. RB・CU・TCC の有効な mask: GRBM_GFX_INDEX で SE・SH を選んで読み（`grbm_select` の mutex）、broadcast に戻す。tile steering の値 = log2 の組（NUM_SC・NUM_RB_PER_SC・NUM_PACKER_PER_SC）。TCC の数（2 の冪でなければ 9.3 の cache の規則）。
4. VMID 0〜15 ごとに GRBM_GFX_CNTL で VMID を選び SH_MEM_CONFIG = 0xC00C（address mode 64、alignment unaligned、retry all、prefetch 3）、VMID ≠ 0 は SH_MEM_BASES = 共有 0x2000…・私有 0x1000… の上位 16 bit。GDS・GWS・OA は 0。
5. RLC: PSP の autoload で既に動いている。**halt・load・start はしない**。CP_STAT == 0 かつ RLC_RLCS_BOOTLOAD_STATUS の bit 31 を待つ（100 ms）。CSB（clear state の buffer、949 dword: PREAMBLE begin、CONTEXT_CONTROL、8 つの SET_CONTEXT_REG の区間（0xA000 から 215、0xA0D8 から 272、0xA1F5 から 4、0xA1FF から 158、0xA2A0 から 2、0xA2A3 から 1、0xA2A5 から 66、0xA2F5 から 203）、PA_SC_TILE_STEERING_OVERRIDE、PREAMBLE end、CLEAR_STATE）を VRAM に置き（HDP flush）RLC_CSIB の ADDR LO/HI（LO は & ~3）・LENGTH（dword）に書く。SRM の enable（AUTO_INCR | SRM_ENABLE）。clear state の表の値は Linux の `clearstate_gfx10.h` から出典付きで取る。
6. tcp harvest: SE・SH ごとに無い WGP の mask を UTCL1_UTCL0_INVREQ_DISABLE と GCRD_SA_TARGETS_DISABLE に書く（cut-down の RX 5600 XT で invalidate が絶対に待たないように）。
7. GRBM の CAM remap（*_UMD の影の register）は userspace が register を書かないので入れない。

### 7.2 GFX の ring 0（register で直に、KIQ を使わない。判断の項目 4）

Linux の既定は KIQ の MAP_QUEUES で GFX の queue を HQD として map する道だが、CP_RB0 の register の道も保守されている。amdrdna は後者（最初の driver では短く、KIQ の firmware の挙動に依らない）。ring は GART の system memory（256 byte 整列、CPU は普通に書く）。順: CP_RB_WPTR_DELAY = 0 → CP_RB_VMID = 0 → pipe 0 → CP_RB0_CNTL = RB_BUFSZ(log2(byte/8)) | RB_BLKSZ(同 − 2) → WPTR・WPTR_HI = 0 → RPTR_ADDR LO/HI（CP が 32 bit の rptr を書く、HI は & 0xFFFF）→ WPTR_POLL_ADDR LO/HI → 1 ms → CNTL を再び → RB0_BASE = MC address >> 8、BASE_HI = (address >> 8) >> 32 → CP_RB_ACTIVE = 1 → doorbell（CP_RB_DOORBELL_CONTROL の OFFSET = index・EN、RANGE_LOWER = index、RANGE_UPPER = 0x0FFFFFFC）→ CP_MAX_CONTEXT = 7、CP_DEVICE_ID = 1 → CP_ME_CNTL の halt の bit（CE 24・PFP 26・ME 28）を落として CP_STAT == 0 を待つ → preamble を ring に（CSB と同じ列 ＋ SET_BASE の CE partition 0x8000/0x8000）→ 256 dword に NOP で揃えて wptr を進め doorbell（64 bit、wptr は dword）。ring test: SCRATCH_REG0（uconfig の offset 0x40）を MMIO で既知の値にし、SET_UCONFIG_REG で別の値を書き、MMIO で読み戻す。

### 7.3 compute の queue は作らない（review m14）

compute の dispatch は GFX の ring で出せる（DISPATCH_DIRECT は GFX の ring でも通る。RADV の compute queue も GFX の ring と同じ PM4）。KIQ 無しの HQD（作業の文書 d の 4.5）は未知の危険で、使い道（async compute）は範囲の外なので、A5 から外す。将来の async compute では KIQ ＋ MAP_QUEUES を Linux の既定どおりに足す（Future Work）。MEC は PSP が load するが unhalt しない（CP_MEC_CNTL は触らない）。

### 7.4 SDMA0 の gfx queue

golden の後、SEM_WAIT_FAIL_TIMER = 0 → RB_CNTL の RB_SIZE = log2(byte/4) → RPTR・WPTR = 0 → WPTR_POLL の address と F32_POLL_ENABLE → RPTR_ADDR → RB_BASE = MC address >> 8、BASE_HI = address >> 40 → MINOR_PTR_UPDATE = 1 → WPTR = 0 → doorbell（GFX_DOORBELL の ENABLE、OFFSET = index）と NBIO の SDMA0 の doorbell の範囲（size 20）→ MINOR_PTR_UPDATE = 0 → SDMA0_CNTL の UTC_L1_ENABLE・MIDCMD_PREEMPT、AUTO_CTXSW → UTCL1_CNTL の RESP_MODE 3・REDO_DELAY 9、UTCL1_PAGE の policy → F32_CNTL の HALT = 0 → RB_CNTL に RB_ENABLE と RPTR_WRITEBACK → IB_CNTL の IB_ENABLE。ring test: WRITE（untiled）で writeback の語に既知の値。wptr は byte（dword << 2）で 64 bit の doorbell。SDMA1 は最初は使わない。SDMA の用途は **kernel の copy だけ**（A5 の copy の試験、GART の invalidate の errata の回避、将来の kernel の blit）。Vulkan の queue の中の copy・clear は順序を engine をまたいで守る仕組みが要るので、**全部 GFX の ring**で行う（9.3、review m15）。

### 7.5 packet の形（executor と driver が組み立てる物）

- **PM4 の type 3 の header**: bit 31:30 = 3、29:16 = body の dword 数 − 1、15:8 = opcode、bit 1 = compute（shader type）、bit 0 = predicate。1 dword の NOP は count 0x3FFF。
- **SET_*_REG の base**（dword）: CONFIG 0x2000（opcode 0x68）、CONTEXT 0xA000（0x69）、SH 0x2C00（0x76）、UCONFIG 0xC000（0x79）。SET_SH_REG_INDEX 0x9B・SET_UCONFIG_REG_INDEX 0x7A は index を dw1 の bit 31:28 に。
- **使う opcode**: NOP 0x10、SET_BASE 0x11、CLEAR_STATE 0x12、DISPATCH_DIRECT 0x15、DRAW_INDEX_2 0x27、CONTEXT_CONTROL 0x28、DRAW_INDEX_AUTO 0x2D、NUM_INSTANCES 0x2F、WRITE_DATA 0x37、WAIT_REG_MEM 0x3C、INDIRECT_BUFFER 0x3F、COPY_DATA 0x40、PFP_SYNC_ME 0x42、EVENT_WRITE 0x46、RELEASE_MEM 0x49、PREAMBLE_CNTL 0x4A、DMA_DATA 0x50、ACQUIRE_MEM 0x58。
- **fence（GFX）**: RELEASE_MEM（8 dword）: dw1 = event CACHE_FLUSH_AND_INV_TS（0x14）| index 5 << 8 | GCR の GLM_WB(12) GLM_INV(13) GL2_WB(21) SEQ(22) | cache policy 3 << 25、dw2 = DATA_SEL 1（32 bit）<< 29 | INT_SEL 2 << 24、dw3/4 = address（system memory の writeback）、dw5/6 = 値、dw7 = 0。EOP の割り込みで writeback の語を読み `drv_gpu_complete`。**fence の値だけで buffer の解放を決めない**（10.2）。
- **fence（SDMA）**: FENCE（op 5、MTYPE 3、address、32 bit の値）＋ TRAP（op 6、context 0）。
- **IB**: INDIRECT_BUFFER: dw1 = address lo（dword 整列）、dw2 = hi、dw3 = 大きさ（dword）| VMID << 24。IB の前に CONTEXT_CONTROL（0x80000000、0）と HDP の flush（5.1 の (c)）、後に invalidate。SDMA の INDIRECT（op 4、VMID << 16、32 byte 整列の address、大きさ、CSA 0）は 8 dword 境界に揃える。
- **cache**: ACQUIRE_MEM（8 dword、SIZE 0xFFFFFFFF、SIZE_HI 0xFFFFFF、BASE 0、POLL 0xA、GCR_CNTL = 0xC3B1 = GLI_INV 1 | GLM_WB | GLM_INV | GLK_INV | GLV_INV | GL1_INV | GL2_WB | GL2_INV）を IB の前に。PS/CS の partial flush は EVENT_WRITE（16・7）。
- **VM の flush（session の切り替え）**: WRITE_DATA（PFP、WR_CONFIRM）で context の PTB の base LO/HI、WAIT_REG_MEM（operation 1 = 書いて待つ、function 3、register space、engine PFP）で REQ・ACK、PFP_SYNC_ME。CP の firmware が write-then-wait を持つ条件（ME ≥ 0x46・PFP ≥ 0x68・MEC ≥ 0x5B、feature ≥ 27）は 20260410 の firmware（ME 0x65・PFP 0xA0・MEC 0x9C、feature 35）で満たす。
- **copy**: SDMA の COPY_LINEAR（7 dword、count = byte − 1、最大 4 MiB）と CONSTANT_FILL（5 dword）は kernel の copy。CP の DMA_DATA（7 dword、src/dst の select、byte count 26 bit）は queue の中の copy・fill。

## 8. DCN 2.0（display）

出典（値）: Linux v6.19 `display/dc/`（`dcn20_hubp.c`・`dcn10_hubp.c`・`dcn10_optc.c`・`dcn20_optc.c`・`dcn10_mpc.c`・`dcn20_dpp.c`・`dcn10_dpp.c`・`dcn10_link_encoder.c`・`dcn10_stream_encoder.c`・`dcn20_hwseq.c`・`dcn10_hwseq.c`・`irq_service_dcn20.c`・`dce_clock_source.c`・`bios_parser_helper.c`・`dc_hw_types.h`・`inc/hw/hubp.h`）、`dcn_2_0_0_offset.h`・`dcn_2_0_0_sh_mask.h`、`navi10_ip_offset.h`、`display/amdgpu_dm/amdgpu_dm.c`・`amdgpu_dm_plane.c`・`amdgpu_dm_crtc.c`（割り込みの登録と cursor の属性）、`gmc_v10_0.c`（VGA の mode の判定）。

### 8.0 Linux との違い（重要）

Linux の DC は desktop の DP・HDMI では **GOP の画面を保たない**（seamless boot は eDP だけ）。初期化で全 OTG を blank し plane を消し、最初の commit で full mode set をする。amdrdna の「GOP の timing・link を残して surface だけ替える」は Linux に無い道で、**topology は register から自分で読む**。mode set（PHY・PLL・link training・DML の watermark）は AtomBIOS の command table と DML の移植が要るので範囲の外（Future Work）。

### 8.1 register の addressing

DCN の base は discovery の表から（静的な header では seg 0 = 0x12、seg 1 = 0xC0、seg 2 = 0x34C0、seg 3 = 0x9000）。pipe の register の大半は seg 2。instance の stride: HUBP/HUBPREQ/HUBPRET/CURSOR 0xDC、OTG 0x80、MPCC 0x1B、MPC_OUT の mux 0x4、ODM 0x10、OPP/DPG 0x5A、DIG/DP 0x100、DP_AUX 0x1C、HPD 0x8。**DPP は 0〜3 が 0x16B の stride で 4・5 は別の位置（0x35D0・0x373B）**なので表で持つ。p002 で A1 に要る register の offset（作業の文書 f の 0 節の表）を phase.md か header に出典付きで写す。

### 8.2 GOP の readout（A1、読むだけ）

1. OTG 0〜5 の OTG_CONTROL の bit 0（MASTER_EN）。点いている物の timing: H_TOTAL・V_TOTAL（+1 が total）、H/V_BLANK_START_END（active の幅 = BLANK_START − BLANK_END）、H/V_SYNC_A と POL。
2. OTG k → OPP: ODMk_OPTC_DATA_SOURCE_SELECT の SEG0_SRC_SEL（bit 11:8）が OPP の id（NUM_OF_INPUT_SEGMENT が 1 なら ODM combine で 2 つ、SEG1 = 0xF は 1 つ）。
3. OPP j → MPCC: MPC_OUTj_MUX の bit 3:0（0xF は無し）。MPCC m: TOP_SEL（DPP の id、0xF は無し）、BOT_SEL（次の MPCC）、OPP_ID、MPCC_CONTROL の mode・alpha、STATUS。
4. DPP d = HUBP d。HUBPd の DCHUBP_CNTL の VTG_SEL（bit 7:4）が k に等しいことを確かめる。
5. HUBP の surface: SURFACE_CONFIG（PIXEL_FORMAT bit 6:0、期待 8 = ARGB8888、ROTATION 9:8 = 0）、TILING_CONFIG の SW_MODE（bit 4:0、期待 0 = linear）、SURFACE_PITCH（bit 13:0 = pitch − 1、画素）、PRIMARY_SURFACE_ADDRESS と HIGH（16 bit）、PRI_VIEWPORT の START・DIMENSION、SURFACE_CONTROL の DCC（期待 0）、HUBPRET_CONTROL の crossbar（ABGR の判定）、HUBP_CLK_CNTL と DPP_CONTROL の clock。
6. 出力: DIGn_DIG_BE_EN_CNTL の bit 0（n = PHY の index）、DIG_BE_CNTL の FE_SOURCE_SELECT（bit 14:8、one-hot A〜G）と DIG_MODE（bit 18:16: 1 DP、2 DVI、3 HDMI、5 DP MST）、DIGf_DIG_FE_CNTL の SOURCE_SELECT（bit 2:0 = OTG）。DP なら DP_VID_STREAM_CNTL の enable と status、DP_LINK_CNTL の training complete。
7. BIOS_SCRATCH_6 の bit 4（accelerated mode、GOP の後は 0 の見込み）。D1〜D6 の VGA_CONTROL（DCN の seg 1 の 0xC から）の bit 0: **立っていれば legacy の VGA の mode で HUBP の surface は使われていないので attach しない**（review m12）。OTG_MASTER_UPDATE_LOCK が 0 であること（立っていると flip が latch しない）。
8. surface の address − FB の base（5.3）= VRAM の offset。これが loader の GOP の framebuffer の物理 − BAR0 の base と一致するのが A1 の合格。

### 8.3 surface の替え（A6）と flip

- client の surface: VRAM、256 byte 整列、format 8（32 bpp）、linear、**pitch・幅・高さは GOP と同じ**（DML の watermark・DLG・TTU を GOP の値のまま使うため。大きさや format を変えると再計算が要る）。GOP の framebuffer は console の surface のままで、display の release・fault・shutdown では flip で戻る（2 節の 4）。
- flip の順（Procedure referenced）: DCSURF_FLIP_CONTROL の FLIP_TYPE（bit 1、0 = VUPDATE で latch）→ VMID_SETTINGS_0 = 0 → SURFACE_CONTROL の TMZ 0 → **ADDRESS_HIGH → ADDRESS（LOW）の順**（LOW の書き込みで全部が latch する）。CPU が surface を書いた後なら先に HDP の flush。
- 完了: FLIP_CONTROL の FLIP_PENDING（bit 8）が 0 かつ EARLIEST_INUSE（LOW/HIGH）が要求した address。HUBP の flip の割り込み（INT_MASK bit 0、CLEAR bit 8、OCCURRED bit 16、STATUS bit 17）で present の完了を `drv_gpu_complete`。
- vblank: OTG_GLOBAL_SYNC_STATUS の VSTARTUP_INT_EN（bit 0）と EVENT_CLEAR（bit 4）。frame counter = OTG_STATUS_FRAME_COUNT の 24 bit。位置 = OTG_STATUS_POSITION。
- **触らない物**: OTG の timing と VSTARTUP/VUPDATE/VREADY、ODM、pixel clock・PLL・DTO、DIG・PHY・link、OPP の FMT、MPC の木、DPP の scaler・CM、HUBP の DLG/TTU/RQ、HUBBUB の watermark、DENTIST の分周、power gating（**HUBP5 の domain は触ると chip が reset しうる**）。
- 危険: SMU が UCLK を変えると GOP の watermark が p-state の切り替えを覆わず underflow しうる。Tier 0 では UCLK DPM が無いので起きない。HUBP_UNDERFLOW_STATUS（DCHUBP_CNTL bit 30:28）と OPTC の UNDERFLOW_OCCURRED（bit 10）を A2・A6 の ok の行に出す。

### 8.4 cursor（A7）

HUBP: CURSOR_SURFACE_ADDRESS_HIGH → ADDRESS → CURSOR_SIZE（HEIGHT bit 8:0、WIDTH 24:16）→ CURSOR_CONTROL（MODE bit 10:8 = 2 premultiplied、PITCH 17:16: 0 = 64・1 = 128・2 = 256 画素、LINES_PER_CHUNK 28:24: 64 幅で 3）→ HUBPREQ の CURSOR_SETTINGS（DST_Y_OFFSET 0、CHUNK_HDL_ADJUST 3）。DPP: CNVC_CUR の CURSOR0_CONTROL の MODE（bit 6:4）と ENABLE。位置: CURSOR_POSITION（Y 13:0、X 29:16）、HOT_SPOT、DST_OFFSET（timing の offset = (x − hotspot) × dchub の refclk / pixel clock。pixel clock は DP なら DP_DTO の PHASE から。HDMI では読めないので 0 のまま試す。判断の項目 7）。負の位置は 0 に clamp して hotspot に足す。最大 256×256。cursor の buffer は VRAM の 4 KiB 整列（amdgpu_dm は BO の address をそのまま渡し、整列の検査は無い。実機で確かめる）。amdgpu_dm は mode 2（premultiplied）、pitch = buffer の幅。`cursor_set` は image を kernel の cursor の buffer に copy（HDP flush）してから address を書く。

### 8.5 release と shutdown

takeover の時に GOP の HUBP の state（address・CONFIG・PITCH・TILING・FLIP_CONTROL・CURSOR_CONTROL・flip の INT_MASK）を保存し、release（display の lease の終わり、device の fault、shutdown）では cursor を HUBP・DPP で無効にし、割り込みを止め、GOP の address に vsync の flip で戻し pending が消えるのを待ってから client の buffer を返す。timing・link を触っていないので GOP の framebuffer の絵（text console が書き続けている）がそのまま出る。warm reboot の前に BIOS_SCRATCH_6 の bit 4 を 0 にする（Linux の `dc_disable_accelerated_mode` と同じ意図、判断の項目 6）。

## 9. executor と compiler の口

### 9.1 zedBSD の Vulkan の形

nvrtx の 8.0 節と同じ: libvulkan は Venus の client、kernel の中に Venus の executor と SPIR-V → GFX10 の compiler（p007、P5）を持つ。user は PM4 を直接書かない（native の stream は拒む）。

### 9.2 compiler との約束（p007 の 3 節に合わせる、P3 の側の確認）

p007 の 3 節の program の形をそのまま受ける（2026-10-15 の P5 の記述を読んで合わせた。差分は無い）:

- 全 stage wave32（driver が DISPATCH_INITIATOR の CS_W32_EN（bit 15）、VGT_SHADER_STAGES_EN の VS_W32_EN（bit 23）、SPI_PS_IN_CONTROL の PS_W32_EN（bit 15）を立てる）。legacy VS、scratch 無し。
- user SGPR: s[0:1] = 資源の表の GPU の仮想 address（64 bit）。CS は user SGPR 2 個（TGID_X/Y/Z_EN で s2〜s4 が workgroup の id）、VS は 5 個（s2 base vertex、s3 first instance、s4 instance rate の mask）、PS は 2 個（s2 は hardware が置く prim mask）。driver は SPI_SHADER_USER_DATA_VS_0（SH 0xB130 → dword 0x2C4C）・PS_0（0xB030 → 0x2C0C）・COMPUTE_USER_DATA_0（0xB900 → 0x2E40）から順に SET_SH_REG で書く（Mesa 25.3.6 `radv_shader_info.c` の stage ごとの base）。
- VS の入力 VGPR: v0 vertex id、v3 instance id（VGPR_COMP_CNT 3）。PS は SPI_PS_INPUT_ENA の bit の順。CS は v0〜v2（TIDIG_COMP_CNT 2）。
- 資源の表（push → SSBO の V# → UBO の V# → T#＋S# → VS の頂点の V#）は compiler が program ごとに配置を決めて結果の記述に offset を入れ、driver が draw・dispatch ごとに書く。
- 結果の記述から driver が書く register: PGM_LO/HI（address >> 8、HI は >> 40）、RSRC1/2、SPI_PS_INPUT_ENA/ADDR、SPI_SHADER_COL_FORMAT・Z_FORMAT、SPI_VS_OUT_CONFIG、SPI_SHADER_POS_FORMAT、PA_CL_VS_OUT_CNTL、SPI_PS_INPUT_CNTL_0..31、SPI_PS_IN_CONTROL（NUM_INTERP）、COMPUTE_NUM_THREAD_X/Y/Z、LDS。`amdrdna_gfx10_program_registers()` が作る（p007）。
- shader の code: VRAM、256 byte 整列、末尾に 3 cache line の詰め物（prefetch）、CPU で書いた後 HDP flush。PGM_HI は program ごとに書く（RADV の 32 bit の窓の約束は使わない）。p007 の出典は Mesa **25.0.7**（P5 の表）で、driver の側は Mesa 25.3.6。block の comment にはそれぞれの版を書く（監査の表も版ごと）。

### 9.3 draw に要る state（A9、作業の文書 e の 1〜4・9 節）

- preamble（queue ごとに 1 回、IB の先頭で毎回でもよい）: CONTEXT_CONTROL、CLEAR_STATE、gfx10 の graphics の初期 state（RSRC3/4 の CU_EN、REQ_CTRL、DB の cache control、CB の cache control、binner の CNTL_1、NGG の mode CNTL、VGT の reuse、GE の uconfig 群、VERTEX_REUSE_BLOCK 14、PA_SU/CL の VTE・VTX・point size・line stipple、SPI_INTERP_CONTROL_0）と compute の初期 state（COHER_START_DELAY 0x20、STATIC_THREAD_MGMT の CU_EN、USER_ACCUM、DISPATCH_TUNNEL 0、START_X/Y/Z 0）。値は Mesa 25.3.6 `ac_cmdbuf.c`・`radv_queue.c` から出典付きで取る。
- render target（linear の 32 bpp）: CB_COLOR0_BASE（>> 8）、VIEW、INFO（FORMAT 10 = 8_8_8_8、NUMBER_TYPE 0 unorm か 6 srgb、COMP_SWAP 0 std/1 alt、BLEND_CLAMP、SIMPLE_FLOAT）、ATTRIB、ATTRIB2（MIP0 の幅・高さ − 1）、ATTRIB3（RESOURCE_TYPE 1、RESOURCE_LEVEL 1、SW_MODE 0、CMASK_PIPE_ALIGNED）、BASE_EXT。**gfx10.1 では linear の pitch は幅から決まる（256 byte に切り上げ）**: scanout の buffer を描画の対象にするには stride = align(幅 × 4, 256) が要る（client の surface は GOP と同じ pitch なので、GOP の pitch がこれを満たすかを A1 で確かめる。満たさなければ描画は別の buffer に行い GFX の ring の copy（DMA_DATA）で写す）。
- blend: CB_BLEND0_CONTROL（ONE・ONE_MINUS_SRC_ALPHA で 0x40000501）、SX_MRT0_BLEND_OPT、CB_TARGET_MASK、CB_COLOR_CONTROL（ROP3 0xCC、MODE 1）、CB_SHADER_MASK。depth 無し: DB_Z_INFO・STENCIL_INFO の format invalid、DB_DEPTH_CONTROL 0、DB_SHADER_CONTROL（Z_ORDER early、DUAL_QUAD_DISABLE 1）。
- viewport・scissor・raster: PA_SC_WINDOW_SCISSOR、SCREEN_SCISSOR_BR、VPORT_SCISSOR_0、PA_SC_MODE_CNTL_0（VPORT_SCISSOR_ENABLE）、PA_CL_VPORT_XSCALE から 6 語、VPORT_ZMIN/ZMAX、guardband、PA_SU_SC_MODE_CNTL 0、PA_CL_CLIP_CNTL（DX_CLIP_SPACE_DEF、DX_LINEAR_ATTR_CLIP）、PA_SC_AA_CONFIG 0、AA_MASK、DB_EQAA、PA_SC_MODE_CNTL_1、PA_SC_BINNER_CNTL_0（binning 無効 = 2）。
- draw: VGT_PRIMITIVE_TYPE（uconfig index 1、trilist 4）、VGT_SHADER_STAGES_EN（legacy VS、MAX_PRIMGRP_IN_WAVE 2、VS_W32_EN）、GE_CNTL（PRIM_GRP 128、VERT_GRP 256）、VGT_GS_MODE 0、NUM_INSTANCES、DRAW_INDEX_AUTO（count、AUTO_INDEX）か DRAW_INDEX_2。
- dispatch: COMPUTE_PGM_LO/HI、RSRC1/2/3、RESOURCE_LIMITS、NUM_THREAD_X/Y/Z、DISPATCH_DIRECT（groups、initiator = SHADER_EN | ORDER_MODE | TUNNEL | CS_W32_EN | FORCE_START_AT_000）。GFX の ring で出す（7.3）。
- 記述子: T#（8 語、gfx10 の layout、RESOURCE_LEVEL 1、TYPE 9 = 2D、format 56 = 8_8_8_8 unorm・130 srgb、DST_SEL、SW_MODE 0）、S#（4 語）、V#（4 語、format 22 = 32_FLOAT の raw、OOB_SELECT 3）。値は Mesa 25.3.6 `ac_descriptors.c`・`gfx10-rsrc.json`・`gfx10_format_table.py` から。
- cache: submission の終わりは RELEASE_MEM（7.5）で CB/DB を flush して L2 を writeback してから fence。sampled read の前に GLV・GL1 の invalidate、CB の書き込みの後に PS_PARTIAL_FLUSH と CB の flush。RX 5600 XT は 192 bit bus で TCC が 12（2 の冪でない）の可能性があり、その場合 CB の書き込みの後に GL2 の writeback が要る（Mesa の `tcc_rb_non_coherent` の規則）。A5 で TCC の有効な mask を読んで判定する。
- clear・copy（全部 GFX の ring、review m15）: linear の image の clear は CP の DMA_DATA（fill）か draw、buffer ↔ linear image は pitch が同じなら DMA_DATA の copy、違えば行ごと、scale 付き blit は textured draw（compositor 自身の draw と同じ）。

### 9.4 executor の作り（判断の項目 5）

nvrtx の executor のうち hardware に依らない部分（`vk-object.c`・`vk-session.c`・`vk-stream.c`・`vk-dispatch.c`・`vk-device.c`・`vk-memory.c`・`vk-resource.c`・`vk-queue.c`・`vk-reply.c`・`vk-target.c`・`vk-record.c`・`vk-command.c`・`vk-layout.c`、**計 5,993 行**（2026-10-15 の `wc -l`。`vk-resource.c` は nvrtx の hardware の語を一部含むので切り分けが要る。review m8））を **amdrdna に写して独自の名前にする**（既定）。理由: WS037 の Future Work「executor の共有の層」は未着手で、共有の層を切り出す作業は WS241（P1）が触っている file と重なる。hardware の部分（`vk-execute.c`・`vk-pipeline.c`・`draw-push.c`・`exec-push.c`・`compute-launch.c`・`image.c` に当たる物）は PM4・GFX10 の記述子・linear の image で書き直す。p008 の最初に、写す file の一覧と hardware に依る部分の切り分け、差分の方針（名前だけ変える・構造は保つ）を記録し、共有の層への寄せ直しは Future Work に残す。

## 10. 我々の interface への対応表

`struct drv_gpu_ops`（`include/drivers/gpu/gpu.h`、`DRV_GPU_INTERFACE_VERSION` 9）、`drv_gpu_display_ops`（`gpu-display.h`）、`drv_gpu_scanout_ops`（`gpu-scanout.h`）。手本は nvrtx の `runtime.c`（bind）・`present.c`（display の ops）・`share.c`（scanout）。登録の検査は `src/drivers/gpu/gpu.c`。

### 10.1 capability と callback（review M1・M2・M10・B3 を反映）

最初の集合は nvrtx と同じ: `GPU_CAP_RESOURCE | GPU_CAP_TRANSFER | GPU_CAP_COMMAND | GPU_CAP_NOTIFICATION | GPU_CAP_JOB | GPU_CAP_JOB_CAPACITY | GPU_CAP_CAPSET | GPU_CAP_BLOB | GPU_CAP_MAPPING | GPU_CAP_SHARE`。display を持つ時は `GPU_CAP_DISPLAY | GPU_CAP_DISPLAY_EVENTS | GPU_CAP_DISPLAY_CONTROL` と display・scanout の ops（nvrtx と同じ集合）。登録は 1 回（3 節の登録の規則）。

| capability / 表 | callback | amdrdna での中身 | Phase |
| --- | --- | --- | --- |
| （常に） | `get_info`、`open`・`close` | session = executor の context ＋ VMID ＋ 4 段の page table（root は VRAM）。15 session まで。close は ring に VM の flush（その VMID の invalidate）を積んで完了を待ち、終わらなければ page table を返さず隔離 | p011 |
| RESOURCE・BLOB | `resource_create`・`blob_create`・`resource_destroy` | buffer object: VRAM（5.2 の allocator）か system memory（page の列）。session の page table に map。destroy は、その BO を使った最後の fence の完了と TLB の invalidate の完了を確かめてから返す。invalidate が時間内に終わらなければ返さず隔離 | p011 |
| BLOB（任意） | `blob_create_placed` | VRAM の連続・256 byte 整列・窓の中（scanout） | p011 |
| TRANSFER | `resource_read`・`resource_write` | VRAM は BAR0 の窓（HDP の invalidate・flush を挟む）、窓の外は MM_INDEX の間接（遅い）、system memory は直接 | p011 |
| MAPPING | `resource_map` | VRAM は BAR0 を通す DEVICE の map（窓の外の BO は ENOMEM）。system memory は RAM の map | p011 |
| CAPSET | `get_capset` | executor の capset（i915・nvrtx と同じ形）。chip（GC 10.1.10、family NV、external rev）・VRAM・wave size も入れる | p011（中身は p008） |
| COMMAND | `command` | Venus の stream を executor の命令として decode。native の stream は拒む | p008 |
| NOTIFICATION | `commands.submit`・`commands.drain` | executor が組み立てた IB（VRAM）を GFX の ring に INDIRECT_BUFFER で積み、RELEASE_MEM の fence、doorbell。完了は EOP の割り込み。drain は session の未完了を全部待つ | p011・p008 |
| JOB | `jobs.reserve`・`commit`・`cancel` | 予約の領域（ring の dword と fence の slot）を前もって確保 | p011 |
| JOB_CAPACITY | `jobs.capacity` | ring の残り | p011 |
| SHARE | `share.*` | 同じ node の open の間の BO の共有（別の VMID の page table に同じ page を map） | p011 |
| DISPLAY | `display.query`・`mode`・`claim`・`release`・`present`・`wait` | query: GOP の mode（A1 の readout）、connector の種類（DIG_MODE）、`GPU_DISPLAY_HAS_CURSOR`。EDID は読まない（AUX・DDC は範囲の外。mode は GOP の 1 つだけ）。present: HUBP の flip、完了は flip の割り込み。wait: VSTARTUP の sequence（vblank）。release と fault では GOP の surface に戻す（8.5） | p006 |
| DISPLAY_EVENTS | `display.events` | **display の構成（topology）の通し番号**を IRQ-safe に返す（HPD の割り込みで進む。vblank ではない） | p006 |
| DISPLAY_CONTROL | `display.power`・`display.refresh` | power: ON/OFF は HUBP の blank（DCHUBP_CNTL の HUBP_BLANK_EN）の解除/設定（timing・link は触らない）、SUSPEND は OFF と同じ。refresh: VSTARTUP の割り込みを timeout まで待つ | p006 |
| cursor（query の HAS_CURSOR） | `display.cursor_set`・`cursor_move`・`cursor_hide` | 8.4（3 つ揃い。set は image を copy して address を書く、move は position、hide は enable を落とす） | p006 |
| scanout の表 | `scanout.query_device`・`scanout.constraints`、`import_image` = NULL | constraints: VRAM の連続・窓の中、offset 256 byte、pitch = GOP と同じ（固定）、形式は ARGB8888/ABGR8888 の linear。外の image の import は無し（ENOTSUP） | p006 |
| recovery の表 | `fault`（必須）、`stop_begin`・`stop_poll`、**`reset` = NULL、`isolate` = NULL**（nvrtx と同じ。review M2） | 11 節 | p011 |

### 10.2 安全の模型

nvrtx の 8.2 と同じ: user は PM4 を書かない。kernel の構造（他の session の fence・ring・page table・PSP の buffer）を user の VMID の page table に map しない。自分の fence は自分の空間にだけ。buffer の解放は fence の値ではなく、ring の順（その BO を使う最後の submission の fence）と TLB の invalidate の完了で決める。VM の fault は VMID で session を特定し、その session だけを fault にする。

## 11. 回復・停止・reset

- `fault`（device）: 冪等。新しい投入を止め、全ての destroy・close が不確かな DMA（VRAM・system の page）を保持するようにする。driver が自分で見つけた異常（PSP の timeout、ring の timeout、GRBM の hang、CP の priv/bad opcode の割り込み、温度の閾値）は隔離を構えてから `drv_gpu_report_error`。
- `stop_begin`/`stop_poll`（session）: その session の未完了の submission の fence を見る。KIQ が無いので firmware による preempt は無く、「止まった」は fence の完了だけで判定する。fence が時間内に完了しなければ EAGAIN 以外を返し device の fault。
- `isolate` = NULL（review M2）: GFX の ring は session をまたいで 1 本で preempt も無いので、「他の context は動き続ける」約束を守れない。stop_poll の失敗は device の fault。
- CP の priv reg・priv inst・bad opcode の割り込みは **device の fault**（Linux も ring の fault として扱う）。VM fault（no-retry で既定の page へ逃がす）だけが session の fault。
- `reset` = NULL: 最初の版では **電源の切断まで fault のまま**（自動の reset はしない）。理由: Navi10 の reset は BACO（SMU の feature が要る、Tier 0 では無い）か PSP の mode1（C2PMSG_64 = 0x70000、500 ms）で、mode1 の後は bootloader からやり直しだが display（GOP の timing）と post の印（BIOS_SCRATCH_7、未確認）も失い、amdrdna は mode set も post もできない。判断の項目 6。
- **温度の監視**（2 節の 2）: A4 以降、監視の thread が 1 秒ごとに温度を読み（4.4）、edge が閾値（既定 95 ℃、`amdrdna.temp.max=`）を超えたら device の fault。読めなくなった（SMU の hang）時も fault。
- **停止（shutdown・reboot の hook、順。`stage` までの逆順、4.1 の競合の規則）**: (1) 投入を止め、fence を待つ（timeout）。(2) EOP・TRAP の割り込みを止める。(3) CP_ME_CNTL で CE/PFP/ME を halt して CP_STAT == 0。(4) SDMA0: RB・IB の enable を 0、AUTO_CTXSW 0、F32 の HALT。RLC は止めない（Linux も止めない）。(5) display を GOP の surface に戻す（8.5）、BIOS_SCRATCH_6 の bit 4 を 0。(6) SMU に PrepareMp1ForUnload（0x32）。(7) PSP: UNLOAD_TA（ASD）、DESTROY_TMR、ring の破棄（C2PMSG_64 = 0x30000）。(8) IH を止め、bus master を切る。SOS は生きたままになる（Linux も同じ）。
- IOMMU: amd64 の zedBSD の今の形（nvrtx・i915 と同じ、DMA の物理 = bus の address）に合わせる。

## 12. 段の印・boot の parameter・試験の道

- **印**: 3 節の `kern_logf` の行。試験の image は `kmsg=console`（logo 無し）。印は GOP の framebuffer の text console に出る（client の surface を出している間を除く）。
- **boot の parameter**（nvrtx の `stage.c` と同じく生の command line を語で照らす、`src/kern/boot.c` の表は変えない）: `amdrdna.off=1`（attach しない）、`amdrdna.stop=A3`（その段の前で止まる。**既定は A5s まで**（renderer だけの登録））、`amdrdna.disp.own=1`（A6〜A8 を走らせ display 付きで登録する。p006 の実機の合格の後に既定 on。判断の項目 8）、`amdrdna.disp.test=1`（A6 で色の帯を数秒出して戻す）、`amdrdna.vk=1`（A9）、`amdrdna.pause=1`（危ない段の前に数秒待つ）、`amdrdna.temp.max=`。
- **試験の道は素の起動だけ**（ユーザーの決定 2「ベアメタルでOK」）。RX 5600 XT の機械は未定（[host.md](host.md) の形で Q1 が聞く、判断の項目 9）。VFIO の passthrough は Navi10 の reset の問題（reset bug）が知られるので使わない。
- **A3 以降の試行の間は cold power cycle が要る**（review B2）: 停止の手順の後も SOS は生きていて、platform の warm reboot が GPU を reset しなければ次の起動の A0 で `sos alive` で止まる。最初の試行で warm reboot の後の C2PMSG_81 を見て確かめ、立ったままなら以後の試行は電源の切断（遠隔の電源の操作が無ければユーザーの手）。host.md の必須の項目にし、p004〜p008 の LW（各 +1）に見込む。
- **固まった時の証拠**（review m18）: 危ない段（A3・A5・A6）の begin の行の後に log を disk に同期してから hardware に書く（`amdrdna.pause=1` の時はその間に数秒待つ）。次の起動で読める印（NBIO の BIOS_SCRATCH の未使用の register に段の番号を書く案）は p002 で register の空きを確かめてから決める。
- **実機の手順**: 試験の image は `plan/ws039/tests/config.mk`（amdrdna と firmware の package を on、`kmsg=console`、SSH）で build。USB から起動し、T1 が SSH で `kernel.log` の `amdrdna:` の行を取り、画面は写真かユーザーの目視（nvrtx の方法 A と同じ）。段ごとに `amdrdna.stop=` で進める。QEMU の試験は無い（QEMU に Navi10 は無い）。
- **host 試験**（実機の前、p002〜p008 の各 Phase に入れる。`plan/ws039/tests/`）: (a) firmware の container の parser: linux-firmware 20260410 の 11 file（manifest の SHA-256 を照合）を読み、4.2 の表の値（大きさ・offset・版）と rlc の 3 つの list の実測の offset を出すこと、壊した header（大きさの不一致、範囲外の offset）を拒むこと。(b) IP discovery の parser: 合成の表（署名・checksum・2 つの IP）で GC の版と base を出すこと、壊れた checksum を拒むこと。(c) PSP の frame・command buffer の layout: offsetof の static assert（frame 64 byte、command 1024 byte、応答 +864）。(d) PTE・PDE・GART の entry の組み立て、4 段の index の分解。(e) PM4・SDMA の packet の組み立て（header・RELEASE_MEM・WAIT_REG_MEM・COPY_LINEAR・DMA_DATA）を既知の語と比べる。(f) DCN の register の address の表（stride、DPP 4・5 の例外）。(g) 13.3 の監査の script。(h) p007 の compiler の試験（llvm-mc、interpreter）は P5。
- **負荷・耐久**は別枠（ユーザーに確かめてから）。Tier 0 の fan の挙動が分からないので、A9 の描画の試験は温度の監視（11 節）の下で短く流す。

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

## 14. 危険と未知（実機で観測していない）

1. IP discovery の表の中身（GC の版、base の address、header の版）。静的な header（`navi10_ip_offset.h`）と同じ値の見込み。
2. C2PMSG_81 の cold boot の値（0 の見込み）と、warm reboot で platform が GPU を reset するか（しなければ A3 以降の試行ごとに cold power cycle）。
3. TMR の大きさ（LOAD_TOC の応答）。PSP の buffer を GART の system memory に置く（Linux の bare metal と同じ）。ASD を bare metal で省けるか（省かない）。
4. 長い memory training を省くことの安定性（高い UCLK の margin）。Tier 0 では UCLK は boot の値なので影響は小さい見込み。
5. Tier 0 の fan・thermal。PMFW の既定で fan が回るか。温度を 2 つの源で読み（4.4）、閾値で fault（11 節）。THM の register の単位は未確認。SMU の metrics の表が feature 無しで転送されるかは未確認。
6. RLC と SMU の handshake: feature を有効にしない SMU で RLC が clock・power の event を待たないか。
7. GOP の pitch が 256 byte の整列を満たすか（描画の対象にできるか）。GOP の framebuffer の VRAM の offset と HUBP の address の一致。
8. VBIOS が自分用に予約する VRAM の範囲を知らない（5.2 の余裕で避ける）。
9. HUBP の cursor の address の整列（正本に無い）。HDMI の時の DST_OFFSET。
10. TCC の数（12 なら GL2 の writeback が要る）。
11. A2 の MMHUB の aperture の書き換えが GOP の scanout に影響するか（Linux も DC の前に同じ書き込みをするので壊れない見込み。underflow で観測）。
12. 素の起動だけなので、GPU が固まった時の回復は電源の切断。
13. amd64 の IOMMU（今は DMA の物理 = bus の address）。

## 15. 判断の項目（ユーザーに決めてほしい点、既定の案と理由）

**決定済み（2026-10-15 ユーザー）**: 参考の source の扱い（値は出典付き、名前・構造・comment は写さない、GPL も同じ）、試験は bare metal、到達点は「desktop が描ける」まで（VCN は後）、まず RDNA1（RDNA2・3 は認識だけ）、driver と compiler の 2 担当。

| # | 問い | 既定の案と理由 |
| --- | --- | --- |
| 1 | firmware の package: `userland/firmware/amd-navi/`（package `amd-navi-firmware`）に linux-firmware 20260410 の navi10 の BLOB 11 file（sos・asd・smc・pfp・me・ce・mec・mec2・rlc・sdma・sdma1）と LICENSE.amdgpu・WHENCE・manifest を入れ、`ta`・`vcn`・`gpu_info` は入れない、既定 off でよいか | そうする（nvidia-gsp と同じ規約。ta は display・GFX に要らない。ASD は Linux と同じく load する） |
| 2 | PSP の長い memory training（VRAM の下 32 MiB を壊す。GOP の framebuffer を退避して戻す必要）を**走らせない** | 走らせない（boot の UCLK では要らない見込み。後で DPM を入れる時に再検討） |
| 3 | SMU は Tier 0（生存の確認と温度の読みだけ、boot の clock、fan・thermal の feature 無し）で進め、温度の監視の閾値（95 ℃）で fault。温度が読めない時だけ Tier 0.5（THERMAL・FAN_CONTROL・FW_CTF）を A5 の前に必須にする。Tier 0.5 の pptable の出所（smc.bin の soft pptable か VBIOS）は、feature だけで拒まれた時に改めて決める | そうする（Linux の `dpm=0` と同じ。full の DPM は pptable の転送と driver-if 0x37 の struct が要り、別の Phase） |
| 4 | GFX の ring は CP_RB0 の register で直に、compute の queue は作らない（dispatch も GFX の ring）、KIQ 無し | そうする（最初の driver では短い。KIQ・async compute は後で足せる） |
| 5 | executor は nvrtx の hardware に依らない file（5,993 行）を写して独自の名前にする（共有の層にしない） | 写す（WS241 が触る file に当たらない。共有の層は Future Work） |
| 6 | reset: 自動の reset はせず（`reset` = NULL）fault は電源の切断まで。A0 で C2PMSG_81 ≠ 0（前の driver が PSP を load 済み）なら A1 まで読むだけで止まり「cold power cycle が要る」と log に出す（mode1 reset は GOP の画面と post の印を失う見込みなので試さない）。shutdown では SOS を生かしたまま（Linux と同じ） | そうする。mode1 reset の実験（reset の後の BIOS_SCRATCH_7 と DCN の観測）は別の Phase の候補 |
| 7 | display は GOP の mode・出力先だけ（mode set・EDID・link training・hotplug の再 link は範囲の外）。cursor は hardware の cursor。HDMI では cursor の DST_OFFSET（pixel clock が要る）を 0 で試す | そうする（Guardrail の scanout の規則。mode set は AtomBIOS の interpreter と DML の移植が要り、別の WS） |
| 8 | display の takeover（A6〜A8）の既定: 最初は `amdrdna.disp.own=1` の時だけ（登録は renderer だけ）、p006 の実機の合格の後に既定 on | そうする（nvrtx の `nvrtx.disp.own` と同じ進め方） |
| 9 | RX 5600 XT を挿す機械（host.md: IP・SSH・OS・monitor・写真の撮り方・**電源の切り方（A3 以降の試行ごとに cold power cycle が要る見込み）**・使ってよい時間）。RTX 機（10.0.10.3、13400F）と同じ機械に挿し替えるか別か | Q1 がユーザーに聞く。p002 の A0 はその機械で読むだけ |
| 10 | VRAM の CPU の map は BAR0 の 256 MiB の窓だけ（resizable BAR は使わない。窓の外の VRAM の `resource_map` は ENOMEM、`resource_read/write` は MM_INDEX の間接で 1 語ずつ、lock の中で遅い） | そうする（BAR の resize は PCI の層の変更が要る。窓の外は GPU だけが触る BO に使う） |
| 11 | session ごとに静的な VMID（15 session まで）。超えたら open は ENOSPC | そうする（動的な VMID は Future Work） |
| 12 | renderer だけの登録（display の ops 無し、`disp.own=0`）を許す | 許す（A5s で `/dev/gpu` を出し、A9 を display の takeover の前に試せる） |

## 16. Phase への引き継ぎと LW

| Phase | 段 | 内容 | 依存 | 見込み |
| --- | --- | --- | --- | --- |
| [p001](phase001/phase.md) | — | この文書、license の監査、review | — | review 待ち（3〜5 LW） |
| p002 | A0・A1 | 骨格（PCI の attach、BAR の map、`CONFIG_DRIVER_PCI_AMDRDNA` は済）、RDNA1〜3 の device id の表、IP discovery の parser、GOP の readout、VRAM の配置の計算、段の印と boot の parameter、独自の名前の register の表（GC・NBIO・MP0・MP1・DCN の A0・A1 に要る物、note から design か phase.md へ出典付きで写す）、監査の script、host 試験 (b)(f)(g) | host.md（実機は読むだけ） | 2〜3 LW |
| p003 | A2 | GMC（FB、VRAM の allocator、GART、両 hub の VMID 0、context 1〜15、HDP の規則）、IH（ring、MSI、thread、dispatch）、VM fault の受け、host 試験 (d) | p002 | 3〜5 LW |
| p004 | A3・A4 | firmware の package、container の parser、root の mount 後の非同期の load と停止の競合、PSP の bootloader・ring・TOC・TMR・LOAD_IP_FW・AUTOLOAD・ASD、停止の PSP の部分、SMU の mailbox・Tier 0・温度・監視の thread、host 試験 (a)(c) | p003 | 4〜6 LW（cold power cycle の往復を含む） |
| p005 | A5 | golden・constants・RLC の autoload 待ち・CSB、GFX の ring 0、SDMA0、fence と割り込み、最初の copy、host 試験 (e) | p004 | 3〜5 LW |
| p011 | A5s | session の VMID・page table・BO・VM の flush、`drv_gpu_ops` の open〜SHARE・recovery（fault・stop）、停止の hook、**GPU device の登録（renderer だけ）**、capset の骨 | p005 | 4〜6 LW |
| p006 | A6〜A8 | client の surface と flip、vblank・flip・HPD の割り込み、cursor、display・scanout の ops と DISPLAY_CONTROL、display 付きの登録、release で GOP に戻す | p003（display 自体は PSP に依らない）、p011（登録） | 5〜8 LW |
| [p007](phase007/phase.md) | — | compiler（P5）。driver との口は 9.2 | なし（host） | 8〜14 LW |
| p008 | A9 | executor（nvrtx の写しと PM4 の hardware の部分）、記述子、draw・dispatch、renderer の node、vkquad | p011・p007 | 8〜12 LW |
| p009 | — | compositor が amdrdna で描く、reboot の後片付け | p006・p008 | 2〜4 LW |
| p010 | — | 規約の全文の見直し、出典の監査（13.3）、license の監査の表の完成 | p002〜p009・p011 | 1〜2 LW |

計 約 43〜70 LW（8 LW ≈ 2〜3 時間）。実機が届くまでは host 試験と build まで書き進め、届いたら p002 の A0 から順に実機で確かめる。
