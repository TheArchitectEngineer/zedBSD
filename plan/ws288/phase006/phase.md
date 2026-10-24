<!-- awesome-plan project=zedbsd record=ws288-p006 -->

# ws288-p006 zedBSD の GHCI の driver（KC705 の PCIe、amd64）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §6（§6.1 置き場所、§6.2 KC705 の PCI の attach、§6.3 memory、§6.4 drv_gpu との結び、§6.5 display）、§8 の p006 の行、§2（HCR・ring・fence・IRQ）、§11（memory・session の保護）、§15・§16（MSAA・format の capability）。
- 状態: in-progress（2026-10-25 07:50 q1566 P3）。以前の試み: branch `agent/p2-ws288-p006` 3bcd241d4（ZYBO 向けに書き始めて停止、check.c・ghci.h・hcr.h・packets.h・pl-memory.c・devcfg の変更、その branch の phase006/phase.md に記録）。
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
