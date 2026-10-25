<!-- awesome-plan project=zedbsd record=ws288-p006 -->

# ws288-p006 zedBSD の GHCI の driver（KC705 の PCIe、amd64）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §6（§6.1 置き場所、§6.2 KC705 の PCI の attach、§6.3 memory、§6.4 drv_gpu との結び、§6.5 display）、§8 の p006 の行、§2（HCR・ring・fence・IRQ）、§11（memory・session の保護）、§15・§16（MSAA・format の capability）。
- 状態: cleared（2026-10-25 08:46 Q1。QEMU の passthrough・板・detach/reattach は p028）
- 方針: ZYBO は GPU の target から外した（p031）ので ZYBO の部分（pl-memory・devcfg の block の登録）は使わない。汎用の部分（ghci.h・hcr.h・packets.h・check.c）は今の GHCI（`docs/ghci.md`、amaterrace-gpu main）に合わせて見直して取り込む。KC705 の PCIe（`pci-attach.c`、BAR0 = HCR + DC、DDR3 の窓、MSI）で、host の QEMU の VFIO の passthrough の guest の zedBSD（amd64）を最初の target にする（p028 の最初のゴール）。
- 依存: p004・p005（CP・DC の RTL）、p010（compiler、cleared、vmunix への link はこの Phase）、p013（PCIe の RTL、未着手: この Phase は GHCI の仕様と sim の model で進め、実機は p028）。
- 承認: 2026-10-25 ユーザー「…そのあと、p010,p018,p019,p006,p011を実装します。KC705のDDR3をVRAMにしてPCIeのGPUボードにすることで、QEMUからパススルーで利用し、ゲストのzedBSDからVulkanを動作させることを、最初のゴールにします。…」「ゴールまで推しで進んでよいです。判断に迷ったら、Vulkan 1.0の原則論で進めてください。…」

## 範囲（design §6 と §8 の p006 の行から ZYBO を除いた物）

- `src/drivers/gpu/ghci/`: `ghci.c`（attach の共通: MAGIC/VERSION/CAPS・EXTCAP・`drv_gpu_register`）、`pci-attach.c`（KC705）、`hcr.h`・`packets.h`（amaterrace-gpu の `include/ghci_regs.h`・`ghci_packets.h` の写し）、`cp.c`（ring・doorbell・fence・IRQ・状態遷移・watchdog・fault と reset）、`memory.c`（GPU の address の allocator、session の帳簿、遅延の解放と回収の thread）、`blit.c`、`native.c`（`GPU_OP_GHCI_NATIVE`、試験の image だけ）、`state.c`・`jobs.c`・`capset.c`、display と scanout（DC、ADV7511 の初期化は KC705 の値）、compiler（`src/drivers/gpu/ghci/compiler/`、p010）の結線と vmunix への link。
- config: amd64 の GHCI の driver を入れる config（試験の image 用、`config/` の既存の形に合わせる）。
- 道具と試験: `userland/tests/ghci-blit/`・`ghci-display/`、`userland/base/ghcictl/`（名前は Q1 が確認した: 推しのまま）、host の試験（封筒・allocator・fence の wrap・状態遷移の driver 側）。
- GHCI の UAPI の番号（`GPU_OP_GHCI_NATIVE` の 0x10104 の案、U10）: 既存の番号と衝突しないことを確かめて決める（Q1 の判断で推しのまま）。
- 実機・QEMU の passthrough の試験は p028（ここでは build と host の試験まで）。

## 受け入れ

host の試験が PASS（件数）、amd64 の vmunix が GHCI の driver と compiler を入れた config で warning 0、`plan/coding-style.md` の全文に合う。detach/reattach・QEMU・実機は p028。

## 記録

- 2026-10-25 07:50 Q1: ZYBO の版の phase.md（branch agent/p2-ws288-p006）から、KC705 の PCIe の版に書き直した。P3 に（q1566）。
- 2026-10-25 08:08 P3 → Q1: 段 1（agent/p3-p006 1317d6ef3）。hcr.h・packets.h・frame.h（amaterrace-gpu afdd3f7 の include/ghci_*.h の写し、copyright の行だけ違う）、gpu-op.h の GPU_OP_GHCI_* 0x10100〜0x10107、hardware に触れない ghci-words.h・check.c（fence の wrap、ring の計画、EXTCAP の走査、native の packet の検査）・range.c（first-fit の allocator）・state.c・capset.c・jobs.c。host 試験 plan/ws288/tests/ghci-host-test.sh 126 passed 0 failed（P3 の申告、ring の 20 万回の wrap と allocator の 10 万回の stress を含む）。
- 2026-10-25 08:36 P3 → Q1: 段 2・3（agent/p3-p006 4004764bb）。ghci.c（MAGIC・VERSION・CAPS・EXTCAP の MEM と PROT、reset と MEM_READY、ops）、pci-attach.c（class の照合、BAR0 128K の register、BAR2 の窓、MSI-X/MSI/INTx、bus master）、cp.c（ring 64K、doorbell の順序、fence と reset の世代、IRQ、fault・hang の回復 = CTRL.RESET と ring の再開、service の thread）、memory.c（窓の中・窓の外・4G 未満の host の RAM、最初の KC705 では ring・fence・bounce を窓に置き host の DMA は要らない、session の fence までの遅延の解放）、blit.c、native.c、display.c（DC の lease、BLT で 2 つの frame に写して flip、640x480 と 720p）、adv7511.c（KC705 の mux 0x74 ch5・0x39、値は ADV7511 の guide・Linux・ADI の参照、板で未確認: p024）、isa.c（compiler を vmunix に link）、service.c。既定 off の結線と plan/ws288/tests/config-amd64-ghci.mk。P3 の申告: vmunix（config-amd64-ghci.mk）exit 0・warning 0、既定の config-amd64.mk も warning 0、host 試験 126/0 と新しい driver の harness（model の GPU）76/0。harness が見つけた不具合（fence は doorbell の前に出す）を直した。
- 2026-10-25 08:44 P3（q1566-i01、worktree p3-p006 基点 7c9262ea8 → 699e2b087）: 実装・build・host 試験。段 1 1317d6ef3、段 2・3 4004764bb、段 4 d56558d49、規約の直し 699e2b087。

## 結果（2026-10-25 P3）

### 作った物

- `src/drivers/gpu/ghci/`: `hcr.h`・`packets.h`・`frame.h`（amaterrace-gpu afdd3f7 の `include/ghci_regs.h`・`ghci_packets.h`・`ghci_frame.h` の写し。著作権の行だけ zedBSD、本文は diff で同一）、`ghci-words.h` と純粋な計算 `check.c`（fence の比較、ring の空きと詰め物、EXTCAP の走査、native の検査）・`range.c`（first-fit の allocator）・`state.c`（packet）・`capset.c`（176 byte の native の record）・`jobs.c`（marker の表）、kernel の `ghci.h`・`ghci.c`（identify、reset と MEM_READY、ops）・`pci-attach.c`（class 0302/0380 を CLASS の score で、BAR0 = 128 KiB の register、BAR2 = local の窓、MSI-X/MSI/INTx、bus master）・`cp.c`（ring、doorbell、device の fence と ticket（fence + reset の世代）、IRQ、fault と hang の回復、service の 1 回分）・`service.c`（thread の起動）・`memory.c`・`blit.c`・`native.c`・`display.c`・`adv7511.c`・`isa.c`（GSI1 の backend の表、compiler を vmunix に link）。
- UAPI: `include/uapi/gpu-op.h` に `GPU_OP_GHCI_SESSION_OPEN`(0x10100)〜`GPU_OP_GHCI_BUFFER_REVOKE`(0x10107)（今の tree に 0x101xx の使用は無い、`enum gpu_op` の switch も無いことを grep で確認）。`include/drivers/pci/pci-ghci.h`。
- 配線（既定は off、Q1 の ACK 2026-10-25）: top の `Makefile`（`CONFIG_DRIVER_PCI_GHCI ?= n`・`CONFIG_GHCI_NATIVE ?= n`・`-D`・`KERN_GPU_BACKENDS`）、`config/drivers/pci.drivers`・`menu.list`（GPU の行）、`platform/amd64/vmunix.mk`、`src/kern/platform/pcat.c`。試験の image の config `plan/ws288/tests/config-amd64-ghci.mk`（config-amd64 + GHCI + native + ghcictl・ghci-blit・ghci-display、kernel の行を画面に）。
- userland: `userland/base/ghcictl`（GHCI の node の一覧: 名前・capability・limit・display の mode）、`userland/tests/ghci-blit`（mappable の blob を map して native で fill32・copy32・fill16・重なる copy の前向きと逆向き・1000 回の小さな fill で ring を回す・EINVAL の 3 種、自分の FENCE を map で待つ）、`userland/tests/ghci-display`（各 mode で color bar・ramp・白枠、最初の mode の bar は BGRA でも、`--hold`）。
- host の試験: `plan/ws288/tests/ghci-host-test.sh`（`ghci-host-test.c`: 純粋な計算、`ghci-driver-host-test.c`: GHCI の GPU の model（register の窓、local memory と窓、ring を解く CP: PAD・SET_REG・BLIT の fill/copy・FENCE・WAIT_IDLE・INVALIDATE・NOP、DRAW は fault、IRQ は driver の handler を呼ぶ）に対して ghci.c・cp.c・memory.c・blit.c・native.c を走らせる）。

### 自分で決めた事（Vulkan 1.0 の原則と GHCI の文面で）

1. BAR は GHCI §15.2 のとおり BAR0 = register、BAR2 = local の窓（design §6.2 の BAR1 は Q1 が直す、Q1 ACK 済み）。
2. memory の置き場（GHCI §11.3）: 窓の有る discrete の GPU は ring・fence page・bounce buffer・CPU が map する物を窓の中、device だけの物を窓の外に置き、aperture を local memory ちょうどにする（最初の KC705 の板は host memory への DMA が無くても動く: p013 の「GPU の DMA は DDR3 の上」に合う）。窓の無い GPU と unified の GPU は host の RAM（4 GiB 未満、local の範囲の外、PC の host の bus address = physical address）を使い、aperture は 32 bit の空間から最初と最後の line を除いた物。
3. capset・COMMAND（Vulkan）・JOB・NOTIFICATION・SHARE は executor（p019）ができるまで名乗らない（capset.c と jobs.c は build と host 試験だけ、Q1 ACK）。blob は MAPPABLE だけ、blob_id ≠ 0（VkDeviceMemory）は EINVAL、SHAREABLE は ENOTSUP。
4. 新しい memory は session が見る前に 0 にする（local は BLT の FILL、host は CPU）。窓の外の memory への transfer は 4 byte 単位（BLT の 32 bit の画素）だけ受け、bounce buffer を通す。
5. native の検査は design の白表より厳しくした: SET_REG は BLT の 0x000〜0x00A だけ、BLIT は FILL と COPY だけ（32/16 bit）、BLIT が読む word は同じ stream の中で設定済みであること、dst・src の矩形の全部（最初の行の最初の画素〜最後の行の終わり）が session の 1 つの allocation に入ること、FENCE の word も同じ。DRAW・LOAD_*・RESULT・WAIT_FENCE などは EINVAL。root だけ（`cred_is_superuser`）、`CONFIG_GHCI_NATIVE` の kernel だけ。
6. 回復は v1 の GHCI §10 のとおり device 全体: fault・watchdog の IRQ、または 10 s 進まない fence で CTRL.RESET → ring を 0 から、それまでの ticket は EIO、作業中だった session に `drv_gpu_report_session_error`。STEP での skip はしない。reset が終わらなければ device を lost にする。
7. display: GPU driver の scanout の規則（GOP の出力先以外に自分で scanout を始めない）に合わせ、DC は attach では点けず、lease の持ち主の最初の present で点ける。present は session の storage を BLT の COPY で driver の 2 枚の frame に写して flip（GPU で写す: 2026-10-12 の「画像の処理は GPU で」）、FB_BASE が変わるまで待って返る（storage は call の後に借りない）。frame の大きさで mode を選ぶ（ATDC の 640×480 と 1280×720、CTA-861 の timing、DC_CAPS[11:9] の数まで）。blob の present・cursor・display control は無し。events は 1 のまま（hot plug 無し）。
8. ADV7511（DC_CAPS[2]）: KC705 の I2C switch 0x74 の channel 5 を開け、0x39 の revision 0x14 を確かめてから設定の表を書く。表の値（power、固定の register、HPD、input ID 1・style・CSC 無しの YCbCr 4:4:4 の HDMI・AVI）は ADV7511 の programming guide・Linux の adv7511・ADI の KC705 の参照から**記憶で**書いた: **板で未確認**（p024 で確かめて直す。p012 の「未確認」の行と同じ）。
9. 試験の image の config は AGENTS 2026-10-04 の規則で `plan/ws288/tests/` に置いた（Q1 ACK）。`ghcictl` は base（platform `*`、既定 n）、試験の 2 つは tests（amd64）。

### 確認（自分で実行、件数は自分で数えた値）

- host: `plan/ws288/tests/ghci-host-test.sh` → `ghci-host-test: 126 passed, 0 failed`、`ghci-driver-host-test: 76 passed, 0 failed`、PASS。driver の harness が見つけた bug 1 つを直した（fence を doorbell の後に issue していたので、その fence の IRQ が completed を進めなかった → doorbell の前に issue）。
- build（名指しの target、warning 0）: `make -j16 ZEDBSD_CONFIG=plan/ws288/tests/config-amd64-ghci.mk BUILD=build/p006 build/p006/vmunix` exit 0、warning 0、`kernel include check: PASS`・`amd64 vmunix check: PASS`、`llvm-nm` で `drv_pci_ghci_driver_register`・`ghci_cp_interrupt`・`ghci_display_ops`・`ghci_gsi1_compile`・`native_owned` が vmunix に有る。既定の `config/ci/config-amd64.mk` の vmunix（`BUILD=build/p006-ci`）も exit 0・warning 0・vmunix check PASS。userland の 3 つ（`build/p006/bin/ghcictl`・`ghci-blit`・`ghci-display`）exit 0、warning 0（`-Wall -Wextra -Werror`）。
- **未実施**: QEMU・VFIO の passthrough・板（p028）。detach/reattach（p028）。ADV7511 の設定と DC の画面（p024 / p028、目視はユーザー）。armv7・rpi4 などの build（変えた共有の file は amd64 の物と `gpu-op.h` の enum の追加だけ）。

### T に流してほしい試験（p028 の環境ができてから。Q1 に一覧で送る）

1. KC705 の板を VFIO で渡した QEMU の guest（`config-amd64-ghci.mk` の image）: dmesg の `ghci: ghci:AMAT:ATG1: ready (...)`、`ghcictl list`、`ghci-blit`（`GHCI-BLIT PASS checks=10`）、`ghci-display`（画面の目視はユーザーの一覧へ）。
2. 同じ image を GHCI の無い QEMU（std VGA）で boot して login prompt（driver が何も取らない回帰、`boot-test.sh`）。

### 残件と再開点

- p019: vk/ の executor、capset・COMMAND・JOB・NOTIFICATION の配線（capset.c・jobs.c は用意済み）、SESSION_OPEN/CLOSE・GHCI_FRAME・RESULT の IRQ・reply thread、SHARE（compositor の import）。
- display: blob の present（copy なしの scanout）、DC の IRQ が PCIe の MSI に乗るかは GHCI に書かれていない（driver は FB_BASE の poll で待つので乗らなくても動く。GHCI の §13/§15.2 に「DC の IRQ は GPU の IRQ_STATUS に入るか」を足すかは Q1 の判断）。
- ADV7511 の表の値の確かめ（p024）。
- PROT（session の保護）は記録だけで、protected の session は開かない。
- 削除の依頼（Q1 の pipeline で）: worktree の `build/llvm-source`（2.7 GB、userland の build の途中で worktree の中に展開された物。共有の tree ではない）、`build/p006`・`build/p006-ci`・`build/amd64`・`build/tmp` の一時物、symlink `build/llvm`・`build/NoctLang`。
- 2026-10-25 08:46 Q1: agent/p3-p006 e8158b758（段 1〜4 と規約の直し）を main に merge（衝突 1: この phase.md、両方を残した）。merge の後 `plan/ws288/tests/ghci-host-test.sh` 126 passed・76 passed（driver）・0 failed（Q1 が流した）。toolchain の lock は全部の共有の tree で書き込み不可のまま（Q1 が status で確かめた）。P3 の userland の build が自分の worktree の build/ に LLVM の source（2.7 GB）を展開していた（共有の tree は無傷）→ Q1 が worktree の build/ を消した。
  - Q1 の判定: cleared（host 試験・vmunix の warning 0・規約）。残件: QEMU の passthrough の guest での `ghci-blit`・`ghci-display` と、GHCI device の無い QEMU での boot-test（p028）、ADV7511 の値（p024）、DC の割り込みが PCIe の MSI に届くかを GHCI §13・§15.2 に書くか（Q1 の判断: 書く。P3 の driver は FB_BASE の poll で両方に対応）、userland の 3 つの program の規約（p016）、capset・COMMAND・JOB などの結線は p019。
- 2026-10-25 13:38 Q1: P3 の 398a738（async submit の直し、T1-788 PASS）を amaterrace-gpu の main に merge = 479bbd4（親 481030c 398a738、衝突なし）。merge の後に Q1 が lint rc 0・127 clean、fw-run.sh rc 0・6 case 0 differ、sim-quick rc 0・94 PASS・0 FAIL を確かめた。
- 2026-10-25 17:16 Q1: 残件（p039 から）: GHCI の MEM の capability に HOST_LIMIT（+0xC、HCR 0x1CC）が足される。driver は host の buffer を local の外かつ HOST_LIMIT の下に置く必要がある（KC705 = 0x8000_0000）。p039 が main に入った後に driver で読む（未着手）。
- 2026-10-25 17:41 Q1（ユーザーの指摘「ホストがCPUアクセスはしなくて、GPUからDMAでpush/pullする設計にしなかったっけ？」）: GHCI §11.1・§11.3 の設計は、host は register・doorbell・local window（frame buffer 用）だけを触り、他は GPU の DMA、discrete では device-local は local・host-visible と ring・fence は host memory・upload/readback は copy の command。今の driver の kind_for（memory.c）は窓があると全部を窓（VISIBLE）に置く = KC705 に GPU → host の経路が無いための暫定で、設計からずれている。**残件**: p039（MC の ID と am_axi_split、GPU → host の経路）の後に、driver の置き場所を §11.3 どおりに（HOST_LIMIT の下に host の buffer、BAR2 は frame buffer だけ）、firmware と vk/ の upload・readback の copy の経路の確認。新しい Phase は p039 の後に Q1 が立てる。
