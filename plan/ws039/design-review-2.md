<!-- awesome-plan project=zedbsd record=ws039-design-review-2 -->
# amdrdna の設計の review（改訂 2、ws039-p001、design-reviewer）

- 日付: 2026-10-15 23:11 JST（host の `date`）
- 対象の commit: `0ccee4921197776b9b97e6c33890dbed468ca5ef`（`git -C /home/awe/zedBSD-worktrees/p3 log -1 --format=%H`、作業ツリーに変更なし）。branch `agent/p3-ws039`
- 対象: [design.md](design.md)（改訂 2、全 626 行）。重点は冒頭の表の改訂 2 の 12 項目（3 節 A2・A3t・A4・A5・A5c・A10〜A13、4.1、4.3t、4.4、4.5、5.4、5.5、7.2〜7.4、7.7、8.6、11 節、12.5、13 節の追加の例、15 節、16 節）。第 1 版の review で確かめた節は改訂 2 との整合だけを見た。
- 読んだ物: `AGENTS.md`（プロジェクトの規則）、`plan/guardrail.md`（「MIT・GPL の出典のデータの引用」と WS039 の項、「GPU の driver の scanout の規則」、HAL の承認）、[ws.md](ws.md)（「design.md §15 #9 の回答」）、[design-review.md](design-review.md)（第 1 版の 33 件と対応の表）、[../ws250/design.md](../ws250/design.md)、design.md の旧版（`git show ef8235e60:` と `73a01c38b:`）。
- 照合に使った正本: `plan/ws039/temp/linux/drivers/gpu/drm/amd/`（Linux v6.19、sparse）の `amdgpu/gfx_v10_0.c`・`amdgpu_ring.c`・`amdgpu_fence.c`・`amdgpu_job.c`・`amdgpu_rlc.c`・`amdgpu_gfx.c`・`amdgpu_ids.c`・`amdgpu_vm.c`・`gmc_v10_0.c`・`amdgpu_psp.c`・`amdgpu_psp.h`・`psp_v11_0.c`・`amdgpu_ttm.c`・`amdgpu_atomfirmware.c`・`amdgpu_bios.c`・`amdgpu_atombios.c`・`atom.c`・`atom.h`・`amdgpu_device.c`・`amdgpu_drv.c`・`nvd.h`、`pm/swsmu/amdgpu_smu.c`・`smu11/smu_v11_0.c`・`smu11/navi10_ppt.c`・`inc/`、`include/atombios.h`・`atomfirmware.h`・`navi10_ip_offset.h`・`soc15_ih_clientid.h`・`asic_reg/{gc,mp,thm,oss,nbio,dcn}/`、`display/dc/clk_mgr/dcn20/dcn20_clk_mgr.c`。zedBSD の側: `include/drivers/gpu/gpu.h`・`gpu-display.h`、`include/uapi/gpu-display.h`、`src/drivers/gpu/gpu.c`、`src/drivers/gpu/nvrtx/runtime.c`・`vk-queue.c`、`src/drivers/pci/pci-pcat-assign.c`・`pci-pcat.c`、`src/drivers/acpi/acpi-tables.h`・`acpi-kern.c`・`aml-os.h`、`include/drivers/acpi/acpi.h`、`src/kern/platform/pcat.c`、`src/kern/text-display.c`、`src/drivers/platform/pcat/graphics/backend.c`・`text.c`、`bootloader/include/amd64-handoff.h`。
- やらなかった事: source・plan（design.md を含む）の変更、build、試験、実機・QEMU。**すべて source を読んだだけで、hardware での観測は無い**。

以下、節の番号は design.md の節、行は design.md の行（0ccee4921）。「確認」は正本・zedBSD の source の該当行を自分で読んだ事、「推測」「未確認」はそうでない事。

---

## blocker（p001 の clearance の前に直すべき物）

### B1. `stop_begin` を「LOST にして queue を reset」にすると、普通の app の終了のたびに共有の GFX の queue が reset される

- **該当**: 11.4 の表の `stop_begin` の行（495 行「READY なら LOST にして bring-up の thread に reset を頼む」）、11.3 の状態機械（477〜482 行）。
- **問題**: 共通の層は、**正常な close** でも、GPU に仕事を出したことのある session には `stop_begin` を呼ぶ（`gpu_session_fail_locked(session, ENODEV, 0)` → `stop_state = REQUESTED` → monitor が `stop_begin`）。design の `stop_begin` はこれを喪失として扱い、KIQ 経由の CP_VMID_RESET と流し直しを走らせる。app が終わるたびに、共有の 1 本の GFX の queue が止まり、全 session の仕事が backup と再投入の対象になる。11.1 の「他の app は動き続ける」を自分で壊し、reset の失敗（STUCK）が device の fault に直結するので、普通の終了が device の喪失の入口になる。
- **根拠（確認）**: `src/drivers/gpu/gpu.c:1360-1368`（graceful close が `gpu_session_fail_locked(session, ENODEV, 0U)`）、`:6786-6790`（最初の失敗で `GPU_STOP_REQUESTED`）、`:7141-7160`（native に出した session には `stop_begin`）、`:7199-7203`。手本の nvrtx の `runtime_stop_begin` は新しい投入を止めるだけ（`src/drivers/gpu/nvrtx/runtime.c:574-597`）。gpu.h の契約は「Begin and poll never block waiting for native work」「Poll returns EAGAIN while pending, and zero only after native access has stopped」（`include/drivers/gpu/gpu.h:59-66`）。
- **直し方の案**: `stop_begin` は「その session の新しい投入を EIO にする」だけにし、`stop_poll` は record の fence が全部完了すれば 0、まだなら EAGAIN にする（nvrtx と同じ）。queue の reset は **run の期限・CP の例外の割り込み・`stop_poll` の間に期限を越えた時** だけに限る。`stop_begin` の `error` 引数（ENODEV = 正常の close、それ以外 = 失敗）で分けてもよいが、正常の close でも走っている仕事は完了まで待つ。11.3 の図に「close → STOPPING（投入を止めて完了を待つ）→ FINISHED」の道を足す。

### B2. OS の中の resizable BAR（5.5）は、上流の bridge の窓と、boot の framebuffer の固定の map を考えていない。案の PCI の層の差分では動かない

- **該当**: 5.5 の手順 (3)（289 行「4 GiB より上の _CRS の窓に BAR0 を置き直す」）と PCI の層の差分の案（291 行）、3.1 の A2 の resize の行（123 行「GOP は BAR0 を使わないので画面は続く」）、2 節の 4（66 行、text console は GOP の framebuffer に書き続ける）、15 節 #7。
- **問題**:
  1. RX 5600 XT は CPU の root port（bridge）の下にある。さらに Navi10 の board は board の上の PCIe switch（upstream・downstream の 2 つの bridge）を持つ（**推測**: 一般に知られた構成。10.0.30.5 の実機の `pci` の列挙は未確認）。BAR0 を 256 MiB から 8 GiB に広げるには、GPU の上の全部の bridge の prefetchable の窓を広げて 4 GiB より上に置き直す必要がある。design の案は BAR0 を host bridge の _CRS の窓に直に置くだけで、bridge の窓を書かない。置いた address は bridge が転送しないので、BAR0 は読めない（all-ones・master abort）。Linux の `pci_resize_resource` は bridge の資源を解放して割り当て直す（`amdgpu_device_resize_fb_bar` はその上の薄い層）。
  2. zedBSD の `pci-pcat-assign.c` は「firmware が 0 のまま残した root bus の BAR」を host bridge の窓に置くだけで、bridge の窓（0x20〜0x2C）は**読んで busy にする**だけで書かない。bridge の下の device の BAR を動かす口は無い。
  3. **boot の framebuffer の map が古い address のまま残る**: GOP の framebuffer は bootloader が固定の VA（`ZBL6_FRAMEBUFFER_VIRTUAL_BASE` 0xffffffffc2000000）に GOP の物理 address で map し、HAL の早い console（`cons.c`）と pcat の graphics・text の層（kernel の text console）がそれを使う。BAR0 を動かすと、この VA は旧い物理 address を指し続け、text console の書き込み（panic の表示も）は何も無い所か、後で別の BAR（例: 置き直した BAR2 の doorbell）が来た所に行く。decode を切っている間の書き込みも master abort になる。3.1 の「GOP は BAR0 を使わないので画面は続く」は scanout については正しいが、console の書き込みについては誤り。
  4. 3 の直しには boot の framebuffer の VA の張り替え（bootloader が作った map、HAL の console）が要り、**HAL の責務の変更 = ユーザーの事前承認**の対象になりうる。計画に無い依存。
- **根拠（確認）**: `src/drivers/pci/pci-pcat-assign.c:8-22`（対象は root bus の未割り当ての BAR）、`:269-330`（bridge の窓は busy の一覧に足すだけ）、`:425-490`（`bar_assign` は BAR だけを書く）。`src/drivers/platform/pcat/graphics/backend.c:1024-1045`（`kern_boot_handoff("pcat.framebuffer")` の `physical_base` を `ZBL6_FRAMEBUFFER_VIRTUAL_BASE` の上の固定の VA で使う）、`src/hal/amd64/bsp-pcat/cons.c:362`、`bootloader/include/amd64-handoff.h:118`、`src/drivers/platform/pcat/graphics/text.c:499`（text の層はその pointer を持ち続ける）。Linux: `amdgpu_device.c:1676-1760`（`pci_resize_resource` を呼ぶ前に doorbell を外し decode を切る。bridge の再割り当ては `drivers/pci/` 側で、この sparse の tree には無いので行は**未確認**）。
- **直し方の案**: 改訂 2 の範囲では **OS の中の resize をやめ、firmware の resizable BAR だけを使う**（UEFI の "Above 4G Decoding" と "Re-Size BAR Support" が on なら firmware が BAR0 と bridge の窓を広げて置く。A0 で今の BAR0 の大きさを読み、VRAM 以上なら 1 つの pool、未満なら 256 MiB の窓の 2 つの pool）。5.5 の手順 (1)〜(3) と PCI の層の差分の案は Future Work（bridge の窓の再割り当て、boot の framebuffer の張り替えの口、HAL の承認の要否の判断を含む別の WS）に移す。ユーザーの決定 10「resizable BAR も使う」との関係は Q1 からユーザーに示す（firmware の resize で同じ効果を得る案）。0x731F の capability の quirk（0x7000 → 0x3F000）は firmware が 1 GiB までしか広げない形で現れうるので、A0 の ok の行に capability の生の値と今の大きさを出す。

### B3. A3t の memory training の間、kernel の text console が VRAM（GOP の framebuffer）に書き続け、IH も動いている。Linux は framebuffer の利用者を外し、IH を立てる前に training する

- **該当**: 4.3t の手順（199 行）、2 節の 4（66 行）、12 節（517 行「印は … A3t の blank の間を除く」= 間も console は書く前提）、3 節の段の順（A2 = GMC・HDP・**IH**、その後 A3t）、5.2（VRAM の配置に「下 32 MiB」の扱いが無い）。
- **問題**:
  1. 手順 (1) で下 32 MiB を system memory に写してから (4) で書き戻すまで（最大約 3 s ＋ 32 MiB の 2 回の転送）、`A3t begin` の行を含む kernel の log、他の thread の `kern_logf` は text console 経由で GOP の framebuffer（下 32 MiB の中の見込み、14 節の 11）に書かれる。退避の後の書き込みは (4) で上書きされて消え、**long training の最中の CPU の BAR0 の書き込みは memory controller の training と競合する**（結果は**推測**: training の失敗、PCIe の hang）。Linux は amdgpu の probe の最初に efifb・simpledrm を外す（framebuffer の利用者が居ない状態で training する）。
  2. Linux の順は「GMC の hw_init（GART・hub）→ PSP の sw_init の中で training → 後で IH の hw_init」で、training の間に IH は動いていない。design は A2 で IH の ring と MSI を enable してから A3t に入る。GART の表（VRAM、1 MiB、置き場の指定無し）が下 32 MiB に置かれると、training の間に来た割り込み（A2 で既定にした VM fault など）の IH の書き込みは壊れた GART の表で翻訳される（**推測**: 誤った物理 address への DMA）。training の間の GPU の書き込みは (4) の書き戻しで消える。
- **根拠（確認）**: `amdgpu_device.c:3148-3215`（`amdgpu_device_ip_init`: sw_init の loop の中で COMMON と GMC だけ hw_init、IH の hw_init は後）、`amdgpu_psp.c:470-503`（training は `psp_sw_init` の中）、`psp_v11_0.c:524-552`（退避・long training・書き戻し・HDP の flush）。zedBSD には text console を止める口がある: `src/kern/text-display.c:368-410`（`kern_text_suspend`・`kern_text_resume`、resume で保持した画面を描き直す）、使用例 `src/drivers/gpu/i915/display/bootfb.c:835`。
- **直し方の案**: A3t の手順に (0) `kern_text_suspend()`（以後の log は disk と serial の mirror だけ）、(7) 書き戻しと unblank の後に `kern_text_resume()` を足す。panic の表示（`kern_text_reveal_fatal`）は止まらないので、その時の扱いを 14 節に書く。段の順を Linux に合わせて「A2 = GMC・HDP（IH は enable しない）→ A3t → IH の enable → A3」にするか、A3t の間は IH の RB_ENABLE と全 source の enable を落とす。5.2 に「A3t が済むまで下 32 MiB には何も置かない（GART の表・page table・IH の物を含む）」を足す。

---

## major

### M1. GFX の MQD の HQD_CNTL に BUF_SWAP 1 を立てている（Linux は big endian の時だけ）

- **該当**: 7.3 の MQD（325 行「HQD_CNTL = RB_BUFSZ(...) | RB_BLKSZ(...) | **BUF_SWAP 1**」）。
- **問題**: amd64 は little endian。BUF_SWAP を立てると CP は ring の語を byte swap して読むので、GFX の ring の最初の packet から壊れる（ring test の fail か hang）。退路の CP_RB0 の道（327 行、第 1 版の形）も同じ field を持つ。
- **根拠（確認）**: `gfx_v10_0.c:6819-6821`（`#ifdef __BIG_ENDIAN` の中だけで `CP_GFX_HQD_CNTL.BUF_SWAP = 1`）、`:6521-6523`（CP_RB0_CNTL も同じ）。
- **直し方の案**: 325 行から `| BUF_SWAP 1` を消し、「BUF_SWAP は 0（little endian）」と書く。host 試験 (e) の MQD の期待値に HQD_CNTL の値を入れる。

### M2. KIQ の立て方と MAP_QUEUES の欄が Linux と食い違う（doorbell の enable の抜け、GFX の ME の欄、KIQ の場所、SET_RESOURCES の queue の mask）

- **該当**: 7.2（317 行の立て方、319 行の SET_RESOURCES）、7.3 の map（326 行「ME 0 (<< 18、me 1 は 0 と書く)」）、6 節の doorbell（303 行）。
- **問題**:
  1. Linux の KIQ の register の初期化は、最初に **CP_PQ_WPTR_POLL_CNTL の EN を 0**、最後に **CP_PQ_STATUS の DOORBELL_ENABLE を 1** にする。design の列には両方が無い。後者が無いと MEC の doorbell が効かず、KIQ の ring test（最初の PM4）が timeout する見込み（**推測**: 既定値は未確認）。
  2. MAP_QUEUES の ME の欄は `ring->me == 1 ? 0 : 1`。GFX の ring は `me = 0` なので Linux は **ME = 1** を書く。design は GFX に ME 0 を書く。
  3. Linux の Navi10 の KIQ は MEC2（me 2）の pipe 1 の queue 0（`amdgpu_gfx_kiq_acquire` は queue 0 で、MEC2 の pipe 2・3 を避ける）。design は MEC1 の pipe 0 queue 0 で、Linux では KCQ（compute ring 0）の場所。動く可能性はあるが Linux で試されていない配置。
  4. SET_RESOURCES の queue の mask は Linux では「amdgpu が map する KCQ の queue の bit」（`mec_bitmap.queue_bitmap`）で、design の「pipe 0〜3 の全 queue の bit（KIQ を除く）」と意味が違う。
  5. RLC_CP_SCHEDULERS は Linux の v10 では読み・下位 byte の置き換え・`| 0x80` の 1 回の書き込み（design は 2 回）。
- **根拠（確認）**: `gfx_v10_0.c:7022-7128`（`kiq_init_register`: `:7033` WPTR_POLL の EN = 0、`:7126` DOORBELL_ENABLE = 1）、`:3747-3762`（MAP_QUEUES、`:3754` ME の欄）、`:4921-4931`・`gfx_v10_0_gfx_ring_init`（`ring->me = me`、GFX は me 0）、`amdgpu_gfx.c:269-300`（`amdgpu_gfx_kiq_acquire`: queue 0 だけ、`mec == 1 && pipe > 1` を避ける）と `:657-715`（`amdgpu_gfx_enable_kcq`: queue の mask = queue_bitmap）、`gfx_v10_0.c:6715-6741`（kiq_setting）。
- **直し方の案**: 7.2 の列に WPTR_POLL の EN = 0（最初）と CP_PQ_STATUS の DOORBELL_ENABLE = 1（HQD_ACTIVE の後）を足す。GFX の MAP_QUEUES の ME を 1 にする（compute は me 1 → 0、me 2 → 1）。KIQ を Linux と同じ MEC2 の pipe 1 queue 0 にするか、MEC1 にする理由を書いて 14 節の危険に足す（CP_MEC_CNTL は MEC1・MEC2 の両方を unhalt）。SET_RESOURCES の mask は「map する compute の queue の bit だけ」にする。host 試験 (e) に MAP_QUEUES の dw1 の期待値（GFX・compute）を入れる。

### M3. 11.3 の queue の reset の手順が Linux の `reset_kgq`・`reset_kcq`・ring の helper と細部で違う

- **該当**: 11.3 の 2・3（485〜486 行）、7.7（357 行）。
- **問題**:
  1. **RLC の safe mode の位置が逆**: Linux の `reset_kgq`（KIQ の packet だけ）は safe mode に入らない。`reset_kcq` は UNMAP の後の MMIO の HQD_ACTIVE の poll（GRBM の選択の下）を safe mode で囲む。design は GFX の道に safe mode と `grbm_select` を書き、compute の道（(c) の MMIO の poll）に書かない。さらに Linux の `amdgpu_gfx_rlc_enter_safe_mode` は clock gating（CGCG・MGCG・3D_CGCG）が有効な時だけ入る。amdrdna は clock gating を設定しないので、要否を決めて書く。design の出典の `kiq_reset_hw_queue`（MMIO の DEQUEUE と SPI_COMPUTE_QUEUE_RESET の道）は v10 の reset では使われていない。
  2. **MQD は作り直さず init の時の写しから戻す**: Linux は init の時に MQD を backup し、reset ではそれを書き戻して ring を NOP で埋め、wptr の writeback を 0 にする。`gfx_mqd_init` を再び走らせると、`cp_gfx_hqd_rptr` などを今の register から読む（reset の後の値）。design の (c)「作り直し（rptr・wptr 0、同じ ring）」と (d)「新しい ring」は語も揺れている。
  3. **guilty の fence の packet も流し直す**: Linux は guilty の context の command は捨てるが、その fence の packet（`fence_wptr_start`〜`end`）は backup に入れて流し直し、fence の通し番号を GPU に進めさせる。また guilty の context の全部の fence に error（guilty は ETIME、他は ECANCELED）を付けてから流し直す。
  4. **流し直しの回数の上限**: Linux は 2 回目に流し直した fence を見たら全部を取り消し、backup を捨てる（同じ仕事で reset を繰り返さない）。design に上限が無い。
  5. **reset の後の VM**: Linux は ring の reset が成功すると `gpu_reset_counter` を増やし、以後の全部の job が VMID の flush・pasid の map・pipeline sync を出し直す（`amdgpu_vmid_had_gpu_reset`）。design は reset の後の VMID の状態を扱わない。
  6. **preamble**: reset の後の GFX の queue に CSB の preamble（CLEAR_STATE・SET_BASE・tile steering）を出し直すかが無い。Linux は出し直さない（`helper_end` は ring test と再投入だけ）が、Mesa の IB は自分で state を全部書く。amdrdna の executor の IB が kernel の preamble の context の state に頼るなら要る（**推測**）。
- **根拠（確認）**: `gfx_v10_0.c:9512-9577`（`reset_kgq`、safe mode 無し）、`:9579-9647`（`reset_kcq`、`:9610` safe mode）、`:3821-3876`（`kiq_reset_hw_queue`、reset の道からは呼ばれない: `grep` で v10 の呼び出しは pmf の表だけ）、`:6845-6887`（`kgq_init_queue(reset=true)`: backup の書き戻し・`amdgpu_ring_clear_ring`・wptr 0）、`:7170-7197`（kcq も同じ）、`amdgpu_rlc.c:40-55`（CG の flag の時だけ safe mode）、`amdgpu_ring.c:863-892`（helper の begin/end）、`amdgpu_fence.c:716-756`（guilty の error と 2 回目の取り消し）・`:777-821`（`always save the fence`）、`amdgpu_job.c:131-143`（成功で `gpu_reset_counter` を増やす）、`amdgpu_vm.c:753`・`:789-794`。
- **直し方の案**: 11.3 の 2 を Linux の順に書き直す（KIQ の packet だけ、safe mode 無し）。3 の (c) を「CG を有効にしないので safe mode は要らない（CG を入れる時は (c) を safe mode で囲む）」と書く。7.7 に「init の時の MQD の写し」「guilty の fence の packet は流し直す」「guilty の session の全 record に error」「2 回目は全部取り消し」を足す。reset の後は全 VMID を「flush が要る」にする。preamble を再投入するかを executor の IB の作り（9.3）と合わせて決める。host 試験 (k) にこれらの場合を足す。

### M4. 「共有の 1 本の GFX の queue」と「WS250 の形」が合わない: 期限の起点、in-flight の数、流し直しの要否が決まっていない

- **該当**: 11.1〜11.3（464〜490 行、「WS250 と同じ」「他の app と compositor は動き続ける」）、11.2 の run の期限（470 行）、7.3（324 行、GFX の queue は 1 本）、7.7、9.4（426 行「WS250 の 4.2 と同じ形」、executor は nvrtx から写す）。
- **問題**:
  1. WS250 は「共有の channel を止めて作り直し、後ろの仕事を流し直す」案（B）を、**compositor が詰まった仕事の後ろで待たされる**ので退け、session ごとの channel（A）を採った。amdrdna の 11 節は B と同じ形で、期限（既定 2 s）の間は compositor を含む全 session の GFX の仕事が止まる（11.3 の 6 は認めている）。「WS250 と同じ」と書くのは誤解を招く。
  2. 写す nvrtx の executor は submission ごとに同期で待つ 1 本の worker（WS250 の表）なので、GFX の ring の上の他 session の仕事は実際には最大 1 つで、7.7 の record の環と流し直しはほとんど使われない。逆に executor を非同期にして複数の run を ring に積むなら、**期限の起点**（submission の時か、record が ring の先頭になった時か）を決めないと、詰まった run の後ろで待った compositor の run も同じ頃に期限を越えて LOST になる（取り違え）。Linux の scheduler の timeout は ring の先頭の job の実行の時間を測る。
  3. 11.3 の状態機械の穴: VM fault だけの LOST は「fence が全部完了したら FREED」だが、fault した shader が終わらない時（無限 loop と fault の両方）は reset の道に入らず、共通の層の stop の期限（`CONFIG_GPU_JOB_STOP_MS` 既定 10 s）→ isolate（LOST なので EIO）→ device の fault になる。STUCK の時の `stop_poll` の返り値も書かれていない（EAGAIN なら 10 s 待つ）。
  4. guilty の session が GFX と compute の両方に仕事を持つ時、GFX の reset の後に (e)「VMID を pool に返す」と、compute の queue の上のその session の仕事が同じ VMID を使い続けうる。
- **根拠（確認）**: `plan/ws250/design.md:100-109`（A と B の比較、「A を採る」）、`:46`（1 worker・1 FIFO）、`src/drivers/gpu/nvrtx/vk-queue.c:11`（submit は順に走らせて待つ）、`amdgpu_job.c`（`amdgpu_job_timedout` は scheduler の先頭の job）、`gpu.c:7104-7110`（stop の期限を越えると isolate）、`config/kernel-options.list:10`（10 s）。
- **直し方の案**: 11.1 に「共有の queue なので、取り消しまでの間（期限＋reset）は全 session の GFX の仕事が止まる。WS250 の A（session ごとの queue）は Future Work」と明記し、ユーザーに示す（決定 6 の「desktop は動き続ける」は reset の後に限る）。executor の模型（同期の 1 worker のまま写すか）を 9.4 で決め、同期なら 7.7 の流し直しを「kernel の自分の packet と compute の family だけ」に縮める。非同期にするなら期限は record が ring の先頭になった時から測ると書く。VM fault の LOST にも run の期限を当てて reset の道へ進め、STUCK の `stop_poll` は EIO（すぐ escalate）と書く。guilty の session の VMID は、その session の全 ring の record が完了か取り消しになるまで返さない。

### M5. `isolate` の後の資源の扱いが gpu.h の契約と逆。isolate が呼ばれる条件も design の想定と違う

- **該当**: 11.4 の `isolate` の行（497 行「FREED なら 0 … 返す物は TLB の invalidate 済みの VRAM・page table だけ」）、10.1 の recovery の行（454 行）、2 節の 6（68 行）。
- **問題**:
  1. 共通の層は、`stop_poll` が 0 を返せば FINISHED にして isolate を呼ばない。isolate が呼ばれるのは stop の期限（10 s）を越えた時か `stop_poll` が EAGAIN 以外の error を返した時だけ。design の「FREED なら 0」は、FREED が期限の後に来た狭い場合しか効かない。
  2. gpu.h の契約は、isolate が 0 を返した session の「descriptors and allocations」を**checked reset まで device の持ち物のまま**にし、その後の `resource_destroy`・`close` は hardware に触らず済ませること。design は isolate の後に VRAM・page table を返す。WS250 は同じ場面で quarantine の list に積んで返さない。`reset` は NULL なので、quarantine は reboot まで残る。
- **根拠（確認）**: `include/drivers/gpu/gpu.h:73-80`、`src/drivers/gpu/gpu.c:7104-7110`・`:7199-7234`・`:7243-7247`、`plan/ws250/design.md:160`・`:200`・`:231`。
- **直し方の案**: 11.4 を「isolate は FREED（GPU の上の停止を確かめた）の時だけ 0、資源は `quarantined` の list に積み、destroy・close は hardware に触らず list に残す。STUCK・LOST は EIO」に直す。2 節の 6 の「返す」は「FINISHED（`stop_poll` が 0）の通常の close の道」に限ると書く。

### M6. 動的な VMID が複数の ring（GFX 1・compute 4）にまたがる時の扱いが無い: 1 つの「最後の fence」、ring をまたぐ flush の順、invalidate の engine の割り当て

- **該当**: 5.4（276〜281 行）、7.6 の VM の flush、5.1 の (c)（244 行）。
- **問題**:
  1. 各 VMID の記録は「最後にこの VMID で投入した fence」1 つ。session は同じ VMID を GFX と compute の queue で同時に使えるので、ring ごとに完了の順が違い、「最後に投入した fence が完了」は「全部の使用が完了」を意味しない。取り上げの判定 (2) が早すぎ、他 session の PD に切り替えた後にも前の session の wave が動きうる。
  2. (1)「VMID の記録がまだこの session なら flush は要らない」: session が VMID を取った時の flush は GFX の ring の上にある。その直後に compute の queue に出した submission は、GFX の ring の flush（PTB の書き・invalidate）が実行される前に走りうる。すると compute の仕事は**前の持ち主の page table**で動く（他 session の memory の読み書き = 分離の破れ）。Linux は VMID の `last_flush` の fence が別の context で未完了なら flush を要とする。
  3. VM の flush の WAIT_REG_MEM の invalidate の engine は「ring ごとに 4.2 の表の割り当て」（278 行）とあるが、4.2 は firmware の container の header で、engine の表は design のどこにも無い（旧版にも無い）。ring ごとに別の engine でないと、同時の flush が同じ REQ/ACK の register を奪い合う。
  4. HDP の flush の engine の bit（5.1 の (c)）は旧版の「GFX の cp0 = 0x1、SDMA0 = 0x400」が改訂 2 で消え、compute の ring（MEC の pipe ごとに別の bit）の値も無い。
  5. (3) は LRU の末尾の fence の完了を CPU で待つ。同じ ring の上なら Linux は ring の順と pipeline sync に頼って待たずに取り上げる（`amdgpu_sync_peek_fence` は同じ ring の fence を除く）。16 以上の app では投入の thread が GPU の完了まで止まる（性能）。
- **根拠（確認）**: `amdgpu_ids.c:197-217`（grab_idle、`id->active` は ring ごとの fence の集合、同じ ring は除く）、`:262-266`・`:336-339`（`last_flush` が別の context で未完了なら flush）、`:403`（`active` に fence を足す）、`gmc_v10_0.c` の `emit_flush_gpu_tlb`（`ring->vm_inv_eng`）、design 旧版 `ef8235e60:plan/ws039/design.md` の 175 行（HDP の bit）。
- **直し方の案**: VMID の記録を {ring ごとの最後の fence、最後の flush の fence} にする。取り上げは全 ring の fence の完了を条件にし、同じ ring だけで使っていた VMID は ring の順で即時に取り上げてよい（pipeline sync を先に）。flush が要るかの判定に「最後の flush の fence が別の ring で未完了」を足す。ring ごとの invalidate の engine の表（GFXHUB の 18 個から、engine 17 は CPU）と HDP の flush の bit（GFX・MEC1 の pipe 0〜3・SDMA0）を 5.4・5.1 に書く。host 試験 (d) に ring をまたぐ場合を足す。

### M7. SMU の Tier F の順が Linux の `smu_smc_hw_setup` から外れている。外した物の危険が書かれていない

- **該当**: 4.4 の Tier F の 1〜6（210〜216 行）、14 節の 5。
- **問題**: Linux（Navi10）の順は: FIRMWARE_FLAGS → 版 → allowed mask の決定 → **NumOfDisplays(0)** → SetDriverDramAddr → SetToolsDramAddr（PMSTATUSLOG の表があれば）→ memory pool（大きさ 0 なら送らない）→ pptable の組み立て → TransferTableDram2Smu → **RunBtc** → SetAllowedFeaturesMaskHigh/Low → PCIe の parameter → **EnableAllSmuFeatures** → GetEnabled → thermal の範囲 → THM の割り込み → … → SetMinDeepSleepDcefclk。design の違い:
  1. 有効化に EnableSmuFeaturesLow/High（0x08/0x09）を使い、EnableAllSmuFeatures（0x06）を使わない。PMFW が個別の enable を「system の起動」（最初の EnableAll）の前に受け付けるかは Linux に例が無い（**推測**: 拒まれる、または DPM の controller の初期化が抜ける）。design は「allowed mask が効けば同じ」とするが根拠が無い。
  2. NumOfDisplays(0) と RunBtc を送らない。RunBtc は Linux が無条件に送る boot time calibration で、DPM を使わない時に要るかは未確認。
  3. 版が合わない時に Tier S にする（211 行）。Linux は driver の IF の版の不一致を info で済ませる（firmware は後方互換）。20260410 の PMFW が 0x37 以外を返すと、不要に Tier S に落ちる。
  4. SetSystemVirtualDramAddr は Linux でも pool の大きさが 0（既定）なら送らないので、省いてよい（確認）。SetToolsDramAddr は表があれば送る。
- **根拠（確認）**: `pm/swsmu/amdgpu_smu.c:1655-1840`（`smu_smc_hw_setup`）、`:1914-1960`（`smu_hw_init`）、`smu11/smu_v11_0.c:195-262`（版の不一致は info）、`:627-640`（pool の大きさ 0 で何もしない）、`:707-725`・`:727-745`・`:748-779`（`system_features_control` = EnableAll/DisableAll）、`smu11/navi10_ppt.c:2802-2811`（RunBtc）。
- **直し方の案**: Tier F の既定を Linux の順に寄せる: NumOfDisplays(0) → driver の表 → pptable の転送 → RunBtc → allowed mask = {FW_CTF, FAN_CONTROL, THERMAL} → **EnableAllSmuFeatures**（allowed mask が範囲を絞る）→ GetEnabled の読み戻し。個別の EnableSmuFeaturesLow/High は EnableAll が拒まれた時の第 2 の試し（その後に Tier S）にする。版の不一致は log だけにし、Tier S の判定は「feature が立たない・message が 0x01 以外」にする。14 節の 5 に「EnableAll を allowed mask 3 bit で送った前例は Linux に無い」と書く。

### M8. A3（PSP が PMFW を load）と A4（SMU の feature）の間に、fan・thermal の制御の無い状態が長く残りうる

- **該当**: 3 節の A3・A4（102〜103 行）、3.1 の A3 の行（127 行）、3.2（段は control の node で 1 つずつ進む）、16 節（p004 の受け入れが A3 の ok、p012 は後）。
- **問題**: A3 の LOAD_IP_FW（SMU）で PMFW が新しく動き出す。A4 までは feature が 1 つも有効でない。control の node は段ごとに止まれるので、`advance A3` の後は A4 を進めるまでこの状態が続く。p004 の実機の受け入れ（A3 ok）は p012（A4）より前で、その試行の起動の残りはずっとこの状態。その間の fan の回転と THM の保護は不明（**推測**: VBIOS の post の時の fan の設定が残るか、PMFW の既定に替わるか。hardware の CTF は効く見込み）。
- **根拠**: design の 3.2・16 節の構成（確認）。PMFW の feature 無しの fan の挙動は**未確認**。
- **直し方の案**: A3 と A4 を 1 つの段（失敗の退路は Tier S）にするか、A3 の ok の直後に自動で A4（少なくとも Tier S の固定の duty と監視の thread）を走らせ、control の node で A3 と A4 の間に止まれないようにする。p004 の実機の受け入れに Tier S の最小（温度の読みと fan の固定の duty）を入れるか、p004 と p012 を実機では同じ試行で確かめる順にする。

### M9. VFCT を attach（A1v）で読む口が zedBSD に無い。PCI の driver の attach は ACPI の table の読み込みより前

- **該当**: 4.5 の image の取得（228 行「zedBSD の `drv_acpi_firmware_find` で "VFCT"」）、3 節（attach で A0・A1・A1v を自動に走らせる、93 行）。
- **問題**:
  1. `drv_acpi_firmware_find` は `src/drivers/acpi/acpi-tables.h` の内部の関数で、ACPI の driver の中の static な `firmware` を引数に取る。公開の `include/drivers/acpi/acpi.h` に生の table を引く関数は無い（`drv_acpi_os_table` も内部の `aml-os.h`）。ACPI の外の driver で生の table を読む物は無い。
  2. pcat の platform の起動は `drv_pci_pcat_init()`（PCI の driver の bind）の**後**に `drv_acpi_attach()` を呼ぶ。amdrdna の attach で A1v を走らせると、まだ ACPI の table が無い。
  3. 1 の口を足すのは ACPI の driver（WS039 の所有の外）の変更で、**計画に無い依存**。
- **根拠（確認）**: `src/drivers/acpi/acpi-tables.h:68-75`、`src/drivers/acpi/acpi-kern.c:551-565`、`src/drivers/acpi/aml-os.h:79`、`include/drivers/acpi/acpi.h:220-345`（table の口が無い）、`src/kern/platform/pcat.c:320-334`。
- **直し方の案**: A1v を attach から外し、bring-up の thread（ACPI の後、root の mount の前でもよい）で走らせる。ACPI の driver に「署名で生の table を借りる」公開の口を足す差分の案を Q1 に送り、Phase の依存（p002 の前提）として 16 節に書く。VFCT が無い時の ROM の BAR の道（Linux の `amdgpu_read_bios` の順: VFCT → ROM BAR → platform の ROM）も同じ順で書く。

### M10. `display.mode` は mode を選ぶ op ではない（UAPI では列挙と検証だけ）。mode set の引き金と hotplug の後の mode の変更の扱いが UAPI と合わない

- **該当**: 8.6 の範囲（391 行「別の connector への出力は Keiland の `display.mode`（UAPI `gpu_display_mode` の operation）の指示」）、8.6 の 10（404 行「`display.mode` は index で mode を選ぶ」）、8.6 の 9（hotplug で driver が EDID の推奨の mode に mode set）、10.1 の DISPLAY の行（449 行）、2 節の 0（62 行）、15 節 #2。
- **問題**:
  1. `struct gpu_display_mode` は「native mode の問い合わせか、副作用の無い custom mode の検証」で、operation は ENUMERATE（0）と VALIDATE（1）だけ。mode を設定する op は無い。zedBSD の UAPI で output の解像度・refresh を決めるのは present・commit の width・height・refresh_millihz と、別の output の claim。
  2. hotplug（A13）で driver が自分で別の mode に替えると、lease を持つ compositor の present の大きさ・pitch と合わなくなる（scanout の constraints の pitch は「今の mode の pitch」、453 行）。driver が mode を替えた後に compositor が知る手順（events の後の query、present の EINVAL か再 claim）が無い。
- **根拠（確認）**: `include/uapi/gpu-display.h:39-40`・`:141-157`、`include/drivers/gpu/gpu-display.h:23-29`、`include/uapi/gpu-display.h:189-204`（present に width・height・refresh_millihz）。
- **直し方の案**: 8.6 の 10 と 10.1 を「`display.mode` = EDID の mode の列挙と検証。mode set の引き金 = lease の持ち主の present・commit の大きさ・refresh が今の mode と違う時（GOP の output）、別の output は Keiland の claim と present」に直す。hotplug の後は driver は同じ mode の re-light だけをし、別の monitor で今の mode が出せない時だけ EDID の推奨に替えて events を進め、古い大きさの present は EINVAL にする、などの約束を書く。i915（WS051・WS113）の実装と同じ形かを p014 の前に確かめる。

### M11. mode set（A12・A13）の DISPCLK・DPPCLK と電圧の扱いが無い。DPM 無しでは boot の clock を越える mode を出せない

- **該当**: 8.6 の 6（400 行、「OPTC の clock」「DPP の clock と DTO」）・8（402 行「DISPCLK/DCFCLK/UCLK は boot の値」）、3 節の A13 の例（114 行「new monitor 2560x1440」）。
- **問題**: Linux の DCN 2.0 の clock manager は、DISPCLK・DPPCLK を上げる前に SMU に電圧を頼み（`set_voltage_by_freq`、`set_hard_min_dcfclk_by_freq`、display の数）、DENTIST の divider（DCCG）を書く。amdrdna は SMU の DPM を有効にしない（Tier F）ので、電圧の要求は送れないか効かない（**推測**）。8.6 は DCCG・DENTIST を部品に挙げず、boot の DISPCLK で出せる pixel rate の上限を確かめる手順も無い。2560x1440@60（約 241 MHz）は GOP の 1920x1080@60（148.5 MHz）より高く、boot の DISPCLK・DPPCLK で足りるかは未確認。
- **根拠（確認）**: `display/dc/clk_mgr/dcn20/dcn20_clk_mgr.c:216-330`（`dcn2_update_clocks`: `:254` display の数、`:264` DCFCLK、`:317-318` DISPCLK の電圧、`:325`・`:329` DENTIST）。
- **直し方の案**: 8.6 に「clock」の部品を足す: A1 で DISPCLK・DPPCLK・DPREFCLK・DCFCLK の今の値（DENTIST・DCCG の register）を読み、mode の pixel rate と DPP の要求が boot の値の中に収まる mode だけを ENUMERATE で出す（VALIDATE は ERANGE）。boot の値を越える mode は DPM（別の WS）の後。A13 の例を boot の値の中の mode に替え、14 節の 16 に足す。

### M12. 「第 1 版のとおり」で値を旧版に預けたまま、旧版の節を消している（24 箇所）。「第 1 版」が指す版も曖昧

- **該当**: 4 行、5.3（265 行）、5.4（274 行）、6 節（299・302 行）、7.1（311 行）、7.3 の退路（327 行）、7.5（340 行）、8.2〜8.5（373〜385 行）、9.2・9.3（418・422 行）、13 節（544 行）など。
- **問題**: design は「note は commit しないので、実装の Phase で使う値はこの文書か phase.md に出典付きで移す」（34 行）を規則にしているのに、改訂 2 は第 1 版の具体の列（hub の register の順 a〜h、RB_CNTL の field、golden の項目数、SDMA の列、DCN の readout の 1〜8、cursor の bit、13.1 の名前の表と 13.2・13.3）を本文から消して「第 1 版のとおり」に置き換えた。値は git の旧版にしか無い。さらに「第 1 版」は `73a01c38b`（review 前）と `ef8235e60`（第 1 版の review を反映した版、design-review.md は「第 2 版」と呼ぶ）のどちらかが本文から分からない。B1（HDP）などの review の直しは `ef8235e60` にしか無いので、`73a01c38b` を読むと review 前の誤りに戻る。
- **根拠（確認）**: `git log -- plan/ws039/design.md`（`73a01c38b` 16:33、`ef8235e60` 17:06、`0ccee4921` 22:52）、`git show ef8235e60:plan/ws039/design.md` の 5.3・7.4・8.2〜8.5・13.1〜13.3、[design-review.md](design-review.md) の「対応（… design.md の第 2 版に反映）」。
- **直し方の案**: 消した節を本文に戻す（推奨）か、各「第 1 版のとおり」を「`ef8235e60:plan/ws039/design.md` の §x.y のとおり」と commit と節で指す。冒頭に版の呼び方（第 1 版 = 73a01c38b、review 反映版 = ef8235e60、改訂 2 = 0ccee4921）を定める。

### M13. 出典の規則: AtomBIOS の opcode の名前を code の名前に写す例（13 節）と、Linux の code を test の oracle として tree に置く案（15 節 #8）

- **該当**: 13 節の表の最後の行（559 行 `ATOM_OP_MOVE` → `AMDRDNA_ATOM_OP_MOVE`、「AMD の公開の bytecode の語。Mesa の mnemonic の扱いと同じ」）、8.6 の 1（395 行、`plan/ws039/tests/atom-oracle/`）、12 節の (m)、15 節 #8（597 行）。
- **問題**:
  1. ユーザーが code の名前に許したのは「AMD が公開する RDNA1 ISA の文書の命令名」だけ（Guardrail の WS039 の補い）。AtomBIOS の bytecode の opcode の名前は公開の ISA の文書ではなく Linux の `atom.c` の関数名（`atom_op_move`）・表と `atom.h` の define（`ATOM_OP_CNT` など）が出典で、`AMDRDNA_ATOM_OP_MOVE` は Linux の symbol を大文字にして接頭辞を付けた形。「Mesa の mnemonic の扱いと同じ」も誤り（Guardrail は Mesa の symbol を不可としている）。13 節の例は判断が出る前に規則の例として載っている。
  2. oracle の案は「Linux の `atom.c` を host で compile」する。`atom.c` は MIT だが、Guardrail は「code・comment・構造は MIT であっても写さない」とし、AGENTS.md は外部の source を tree に取り込まず tarball を取得・検証して使う規則。`plan/ws039/tests/atom-oracle/` に Linux の source（と kernel の header の代わりの shim）を commit すると、どちらにも触れる。DML の oracle（12 節の (m)）は DC の大きな header 群と kernel の FPU の macro に依存し、host で compile する手間が LW に入っていない。
- **根拠（確認）**: `plan/guardrail.md:108`（WS039 の項と mnemonic の補い）、`:101-103`（MIT でも写さない）、`AGENTS.md` の「コード」の節、`amdgpu/atom.c:800`（`atom_op_move`）・`:1090-1098`（`opcode_table`）、`amdgpu/atom.h:65-66`。
- **直し方の案**: 13 節の最後の行を消し、opcode の名前は振る舞いから付けた独自の名前（例 `AMDRDNA_BYTECODE_COPY` など）にする。値（opcode の番号 = 表の位置、operand の種類）はデータとして出典付きで使う。15 節 #8 の問いを「Linux の `atom.c`・DML を **tree に入れず**、test の script が Linux の tarball（版と SHA-256 を固定）を取得して host で build する」形に書き直してユーザーに聞く。DML の oracle は p015 の LW に見積もりを足すか、実機の underflow と GOP の値（同じ mode）との比較だけにする。

---

## minor

### m1. 5.5 の capability の bit の説明の誤り（286 行）
「bit 23 = 512 MiB」は誤りで、bit n = 2^(n−4) MiB の式どおり bit 23 = 512 **GiB**。式と quirk の値（生の 0x7000 = 256 MiB〜1 GiB、0x3F000 = 256 MiB〜8 GiB）は合っている。Linux の quirk は shift した後の 0x700 → 0x3f00 で比べる（`drivers/pci/` は tree に無く**未確認**、design も p003 で確かめるとしている）。B2 で firmware の resize に寄せる場合も A0 の parse に使う。

### m2. VFCT の構造の出典（226 行）
`UEFI_ACPI_VFCT`・`VFCT_IMAGE_HEADER`・`GOP_VBIOS_CONTENT` は Linux v6.19 では `include/atombios.h:9272-9300` にあり、`amd_acpi.h` には無い（確認）。PCIBus・PCIDevice・PCIFunction・ImageLength は ULONG（4 byte）、VendorID・DeviceID は USHORT。Linux は ImageLength が 0 の entry を飛ばし、header と image が table の長さを越えないことを確かめる（`amdgpu_bios.c:374-428`）。出典の file を直し、同じ検査を書く。

### m3. PSP の runtime DB の directory の layout（197 行）
directory は `entry_count`（u16）の後に 4 byte 整列の entry の配列（{type u32, offset u16, size u16}）が続くので、配列は directory の +4 から始まる（2 byte の詰め物）。entry の数は 0x40 未満でなければ無効。BOOT_CONFIG の entry は size ≥ 8 を確かめる（`amdgpu_psp.h:303-341`、`amdgpu_psp.c` の `psp_get_runtime_db_entry`、確認）。A0 の parse と host 試験に入れる。

### m4. AtomBIOS の interpreter の PLL・MC の空間（395 行）
Linux の amdgpu は PLL・MC の空間の callback を空にしている（読みは 0、書きは何もしない）。「Navi10 では index/data の間接」は根拠が無く、間接の register に書くと別の register を壊しうる。register の空間は BAR5 の外の index（PCIE の間接）も扱う必要がある（Linux の RREG32 の道）。IIO（`atom_iio_execute`）は別の物（`amdgpu_atombios.c:1726-1771`、`atom.c:110-220`、確認）。

### m5. 壊れた参照
「11 節の案 B」（324 行）、「11 節の S7」（334 行）、p013 の「S2」（614 行）は 11 節に無い（WS250 の語）。「4.2 の表の割り当て」（278 行）は M6 の 3。直すか WS250 の該当の節を指す。

### m6. 16 節の LW の合計が表と合わない（624 行）
表の p002〜p015（p016 を除く、p010 を含む）の合計は 69〜103 LW（p007 の残り 2〜4 を足すと 71〜107）。本文は 79〜121。

### m7. compute の MQD の欄の抜けと KIQ の小さい違い（332 行）
Linux の `compute_mqd_init` は `header = 0xC0310800` を書き、`cp_hqd_pq_rptr` を今の register から読む（`gfx_v10_0.c:6913`・`:6999`）。design の欄に header が無い。

### m8. 停止の UNMAP の action（513 行）
Linux の teardown は compute を RESET_QUEUES、GFX を PREEMPT_QUEUES で外す（`amdgpu_gfx.c:534-536`・`:587-589`）。design は両方 PREEMPT。止まった compute の queue の PREEMPT は終わらない可能性がある。

### m9. THM の閾値と MP1 の割り込みの enable（217 行）
Linux は DIG_THERM_INTH に `min(255, software_shutdown_temp)`、INTL に `max(0, range.min)` を書く（TedgeLimit ではない）。MP1 の SW の割り込みは MP1_SMN_IH_SW_INT（ID 0xFE、VALID 0）と MP1_SMN_IH_SW_INT_CTRL の INT_MASK = 0 で有効にする（`smu_v11_0.c:1335-1403`）。design は ack だけ。ROM_SMUIO の client は 0x16（`soc15_ih_clientid.h:55`）、hardware CTF の src は GPIO19 = 83（`smu_v11_0.c:1403`）で、p012 を待たず書ける。監視の thread の閾値（TedgeLimit）と THM の INTH（software_shutdown_temp）の違いを書き分ける。

### m10. lock の順が無い（72〜83 行）
reset の道（bring-up の thread: `kiq` → `grbm_select` → `ring`、session の状態は `node`）、投入の道（`node` → `vmid` → `ring`、VMID の fence の待ちは lock の外）、display（`display`）の間の取る順と、VMID の待ちの間に `node` を持たないことを書く。bring-up の thread が長い advance（A3t の 3 s、A11〜A13）の最中だと reset が遅れる（11.3 の 6 の時間の見積もりに入れる）。

### m11. DP の HPD の短い pulse（link の喪失）の扱いが無い（403 行）
monitor の省電力からの復帰などで DP の sink は HPD の短い pulse（HPD_RX、6 節の src 6〜11）で再 training を求める。A13 の hotplug は長い pulse（抜き差し）だけを扱う。takeover（A6〜A8）の間も、GOP が点けた link が落ちたまま黒になる。DPCD の link の状態（0x202〜0x204）を読み、落ちていれば同じ mode で re-training する手順を p015 に足す（mode set を入れたので範囲の中）。

### m12. 実機の手順の改善（12.5）
1. A3 以降の各試行に cold power cycle が要り、ユーザーの操作を待つ。S5 の電源断（zedBSD の poweroff）と Wake-on-LAN で GPU の電源が切れるなら、ユーザー無しで cold power cycle できる（**推測**: S5 で slot の 12 V が切れること、10.0.30.5 の UEFI の WOL の設定、zedBSD の poweroff と NIC の WOL の対応は未確認）。最初の試行で確かめる項目に足す。
2. 画面の確認（A6・A11〜A13）をユーザーの目視だけに頼らず、DCN の OTG の CRC（同じ画の前後で一致すること）と underflow の register で機械的に確かめる案を足す（CRC の register は p014 で出典を写す）。

### m13. control の node の UAPI が定義されていない（136 行、15 節 #11）
production の image にも残す UAPI なのに、`include/uapi/amdrdna.h` の ioctl の番号（group の文字の衝突の確認）、struct の layout（version・size・予約 0、固定幅）、STATUS の欄、VBIOS の読み（可変長: offset と長さ）、権限（advance は root）、`/dev/amdrdna0` の登録の口が無い。p002 の前に design に書く。

### m14. 段の一覧の A5c の位置（106 行）
A5c は bring-up の段ではなく（vkhang は A9 の後に実機）、`advance`・`amdrdna.auto` の線形の段の順（A5s → A5c → A6）に置くと「A5c まで auto」が意味を持たない。A5c を段の列から外して試験の項目にするか、段の順を A9 の後に置く。p006 の依存（p011）と 624 行「p006 は p003 の後に実機で先に確かめてもよい」も食い違う（A6・A7 だけなら `disp.test` で登録無しに確かめられる、と書き分ける）。

### m15. Tier S の温度の register（220 行）
Tier S の温度は「THM の register」とあるが register を名指ししていない（218 行の CG_MULT_THERMAL_STATUS の ASIC_MAX_TEMP か）。Linux の Navi10 は温度を metrics からだけ読むので、THM の register の単位・意味は未確認のまま Tier S の監視の唯一の源になる。14 節の 5 にこの点を足し、p012 の受け入れ（Tier S で温度が読める）に「metrics と THM の値の比較」を入れる。

---

## 確かめて問題が無かった物（抜き取り）

- GC の register: CP_VMID_RESET 0x1E53（RESET_REQUEST 15:0、PIPE0_QUEUES 23:16、PIPE1_QUEUES 31:24）、CP_GFX_HQD_ACTIVE 0x1E80、SPI_COMPUTE_QUEUE_RESET 0x1F7B、RLC_SAFE_MODE 0x4C05（CMD bit 0、MESSAGE 4:1）、RLC_CP_SCHEDULERS 0x4CAA、CP_MEC_CNTL 0xE2D（ME1_HALT 30・ME2_HALT 28）、CP_ME_CNTL の CE 24・PFP 26・ME 28、CP_MEC_DOORBELL_RANGE 0x1DFC/0x1DFD: `gc_10_1_0_offset.h`・`gc_10_1_0_sh_mask.h`。
- Linux v6.19 は gfx v10 の GFX と compute の両方で per-queue reset を有効にし（`gfx_v10_0.c:4955-4963`）、gfx v10 に soft recovery は無い（`grep` で無し）。11 節が soft recovery を書かないのは正しい。
- MAP_QUEUES（7 dword）・UNMAP_QUEUES（6 dword、ACTION 0 PREEMPT・1 RESET・3 PREEMPT_NO_UNMAP、ENGINE_SEL GFX 4・compute 0、DOORBELL_OFFSET << 2）・SET_RESOURCES（8 dword、dw4/5 = cleaner shader の address >> 8）の形: `gfx_v10_0.c:3707-3792`（set_resources 3707、map 3726、unmap 3766）、`nvd.h:607-644`。
- compute の MQD の EOP_SIZE 8（2 KiB）、RPTR_BLOCK_SIZE 9、PRELOAD_SIZE 0x53、MIN_IB_AVAIL_SIZE 3、KMD_QUEUE・PRIV_STATE・UNORD_DISPATCH: `gfx_v10_0.c:6905-6996`。doorbell（KIQ 0x000、MEC の ring 0x003〜、GFX 0x08B）は第 1 版の review の確認のまま。
- SMU の message の番号（0x02〜0x17、0x22、0x32、0x3A、0x40）と応答の code: `smu_v11_0_ppsmc.h:28-113`。feature の bit（FW_CTF 31、FAN_CONTROL 32、THERMAL 33）と表の id（PPTABLE 0、WATERMARKS 1、SMU_METRICS 6）: `smu11_driver_if_navi10.h:105-107`・`:1196-1202`。driver の IF の版 0x37: `smu_v11_0.h:30`。FIRMWARE_FLAGS = SMN 0x03B10024 の bit 0: `smu_v11_0.c:180-193`、`mp_11_0_sh_mask.h:266-268`。
- pptable の選び方（smc の header 2.1 で pp_table_id > 0 なら soft pptable、無ければ VBIOS の powerplayinfo）と smc_dpm_info（format 4・content 5、`I2cControllers` から `BoardReserved` の手前まで）: `smu_v11_0.c:286-365`、`navi10_ppt.c:406-490`。
- THM の register（base 0x16600、INT_ENA 0xA・INT_CTRL 0xB・MULT_THERMAL_STATUS 0x5F・FDO_CTRL0〜2 0x67〜0x69・TACH_STATUS 0x6B）と field の位置、fan の rpm の式（2500 × 60 × 10000 / tach）: `thm_11_0_2_offset.h`・`thm_11_0_2_sh_mask.h`、`smu_v11_0.c:1263-1290`。MP0・MP1 の base 0x16000、C2PMSG_35/64/81、MP1 の C2PMSG_66/82/90: `navi10_ip_offset.h:477`・`:519`、`mp_11_0_offset.h`。
- memory training の値: 32 MiB（0x2000000）、p2c = VRAM − 0x8000、data 0x1000、c2p = ALIGN(VRAM − reserve − 1 MiB, 1 MiB)、reserve は firmwareinfo の content 4・5 の時の fw_reserved_size、無ければ 64 KiB、runtime DB（VRAM − 1 MiB、cookie 0x0ED5、BOOT_CONFIG type 5、bit 0x2、無ければ training する）、firmware の cap 0x400、C2PMSG_35 = 0x100000（long）、3 s の待ち: `amdgpu_psp.h:240-260`・`:286-341`、`amdgpu_ttm.c:1693-1772`、`amdgpu_atomfirmware.c:900-945`、`psp_v11_0.c:427-552`。
- VBIOS の data の表の index（smc_dpm_info 2、firmwareinfo 4、vram_usagebyfirmware 11、gfx_info 14、powerplayinfo 15、displayobjectinfo 22、dce_info 27、vram_info 28、計 35）と rom header の欄、ATOM の署名の位置（0x48 の offset + 4）: `atomfirmware.h:243-262`・master の表、`amdgpu_bios.c:50-85`。BIOS_SCRATCH_0 = 0x4C（NBIO の seg 1 0x14 + 0x38）: `nbio_2_3_offset.h:58-72`。
- IH の client（DCE 0x04、SDMA0 0x08、THM 0x0F、VMC 0x12、GRBM_CP 0x14、ROM_SMUIO 0x16、UTCL2 0x1B、MP1 0x1F）: `soc15_ih_clientid.h`。IH_VMID_0_LUT 0x0・_MM 0x10（OSSSYS の base 0x10A0）と、ring の上の pasid の map の書き込み: `osssys_5_0_0_offset.h:28-61`、`gmc_v10_0.c` の `emit_pasid_mapping`。VM の flush の順（pipeline sync → flush → pasid の map）: `amdgpu_vm.c:771-830`。
- DCN の AUX（DP_AUX0_AUX_CONTROL 0x1F50、stride 0x1C）と DDC（DC_I2C_DDC1_SPEED 0x1EA2・SETUP 0x1EA3、stride 2）: `dcn_2_0_0_offset.h:10292-10316`・`:10514-10556`。
- resizable BAR の目標の大きさ（VRAM の 6 GiB → 8 GiB）と「firmware が既に広げていれば何もしない」「root bus に 4 GiB より上の窓が無ければ何もしない」: `amdgpu_device.c:1676-1760`。

## 確かめていない物

- 10.0.30.5 の実機の PCI の topology（RX 5600 XT の上の bridge の数、board の switch の有無）と、UEFI の Re-Size BAR が BAR0 を何 GiB にするか（B2）。
- Linux の `drivers/pci/` の resizable BAR の quirk と `pci_resize_resource` の bridge の再割り当て（sparse の tree に無い、B2・m1）。
- PMFW が EnableSmuFeaturesLow/High を EnableAll の前に受け付けるか、RunBtc 無しで THERMAL・FAN_CONTROL が動くか、feature 無しの PMFW の fan の挙動（M7・M8）。
- CP_PQ_STATUS の DOORBELL_ENABLE の既定値、MEC1 の pipe 0 queue 0 の KIQ が Navi10 の firmware で動くか、GFX の MAP_QUEUES の ME の欄を 0 にした時の firmware の扱い（M2）。
- CP_VMID_RESET が context の register（preamble の state）を戻すか（M3 の 6）。
- training の最中の BAR0 越しの CPU の書き込みの結果（B3、Linux は避けているという事実だけ）。
- DCN 2.0 の boot の DISPCLK・DPPCLK の値と、SMU の DPM 無しで DENTIST だけを上げた時の安定性（M11）。
- i915 が mode を替える時に present の大きさをどう扱っているか（M10 の直しの手本）。
- nvrtx の executor の worker の数は WS250 の design の記述から読んだだけで、`vk-*.c` の全部は読んでいない（M4）。
- 8.6 の LW（p014 6〜9、p015 14〜22）の妥当性。AtomBIOS の interpreter（`atom.c` 約 1,600 行相当の振る舞い）と DP の training・pipe の組み立て・固定小数の DML の代わりを合わせると、上限に近いか越える見込み（**推測**、根拠は規模だけ）。

## 件数

| 区分 | 件数 |
| --- | --- |
| blocker | 3（B1〜B3） |
| major | 13（M1〜M13） |
| minor | 15（m1〜m15） |
| 計 | 31 |
