# GHCI レビュー 第 18 版（driver の code review の依頼と、回答 第 17.0 版（N78・N79）の確かめ、ghci.md 288c459、2026-10-28）

- 依頼: zedBSD の GHCI 1.0 driver の実装（WS290 の p002〜p012、main の commit a9626253b に入った物）を、ハードウェア設計の担当の目で読み、ghci.md d9252e4 との食い違い・仕様の読み違い・実機（KC705）で起きそうな問題を指摘してほしい。driver は ghci.md d9252e4 だけを正本にして書いた（include/ の header と firmware は照合にだけ使った）。
- 前の版: 第 17 版（N78・N79）。回答 第 17.0 版（288c459）で 2 件とも解決した（下の「確かめ」）。
- code の在り処: `~/zedBSD-claude1`（branch main）。読むだけでよい。build や試験は不要（driver の側で host 試験を流してある: words 141・ref 56・model 271・driver 1019・gsi1 50・vk 438・pipeline 175・execute 198・meta 98、全部 PASS。model は driver と同じ読み方で書いたので、hardware の振る舞いの証拠ではない）。
- 設計の文書（参考）: `~/zedBSD-claude1/plan/ws290/design.md`（版 4.15）。

## 見てほしい観点

| 番号 | 観点 | 主な file |
| --- | --- | --- |
| R1 | register と EXTCAP の読み方、attach の順（MAGIC・VERSION・RESET・MEM_READY・SCRATCH・CP_HEAP・ring・IRQ・Bus Master・RUN・最初の fence）が §3〜§5・§14・Appendix A に合うか | `~/zedBSD-claude1/src/drivers/gpu/ghci/ghci.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/cp.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/pci-attach.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/caps.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/ghci-regs.h` |
| R2 | ring・doorbell・NOP の詰め・64 bit の fence・IRQ の mask と W1C の順（§6・§9） | `~/zedBSD-claude1/src/drivers/gpu/ghci/cp.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/check.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/cmd.c` |
| R3 | fault・watchdog・hang・reset と回復の順、reset が保つ・消す register、FAULT_INFO の kind と unit の読み（client に帰せる fault の分け方）、SESSION_ACTIVE（§3.3・§3.5・§5・§10） | `~/zedBSD-claude1/src/drivers/gpu/ghci/recover.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vm.c` |
| R4 | session と VM: SESSION_OPEN・CLOSE・VM_MAP・VM_UNMAP の順（close の前に全 unmap）、CP_HEAP の上限の勘定（root・L1・L0、PROT 0 と 1）、flat（HW_WALKER = 0）の扱い、local memory の `VM_FLAG_LOCAL`（§7.3・§11） | `~/zedBSD-claude1/src/drivers/gpu/ghci/vm.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/memory.c` |
| R5 | packet の符号化の全部（§7.3）、PSO 128 byte（§8.2）、texture・sampler・buffer の descriptor（§8.3・§8.4）、render pass の 192 byte の記述 | `~/zedBSD-claude1/src/drivers/gpu/ghci/cmd.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/ghci-cmd.h`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-pipeline.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-layout.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-pass.c` |
| R6 | Vulkan の command を GHCI の command に直す所（bind・dynamic state・draw・indirect・dispatch・barrier の stage と cache の flag・転送・query・event の WAIT_FENCE・clear・resolve）、BLT の無い device で転送を shader で代える所（§7.3・§8.4・§12.11） | `~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-execute.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-meta.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-queue.c` |
| R7 | GSI1 の program: header の無い code、資源の結び方（set 0〜3、c24〜c31 の鏡、push constant と dynamic offset の語）、VS の属性の register の詰め方・operand 120・121・export の target・`lda` の語・FS の入力 register（§12、特に §12.10） | `~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/gsi1-abi.h`、`~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/gsi1_isa.h`、`~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/gsi1-io.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/gsi1-memory.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/gsi1-texture.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/gsi1-compile.c` |
| R8 | capability → Vulkan の feature・limit・format（FMT の register の読み、FMT_BUFFER の bit の読み）（§4・§8.5・§8.6・§16） | `~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-caps.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-format.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-device.c` |
| R9 | 表示 DISP2: DISP_SET・FLIP、record と TAKEN0/1、DISP_IRQ の W1C の順、HALT・reset の間の record の保留（STATUS[31]）、cursor、boot framebuffer、"memory line" を 64 byte と読んだ事（§13.2・§13.3） | `~/zedBSD-claude1/src/drivers/gpu/ghci/display.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/display-scan.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/adv7511.c` |
| R10 | discrete: local memory、window（WINDOW_CTRL）、boot framebuffer の範囲の除外（§11.1〜§11.3・§13.3） | `~/zedBSD-claude1/src/drivers/gpu/ghci/memory.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-memory.c` |

## 見てほしい file の全部

- core: `~/zedBSD-claude1/src/drivers/gpu/ghci/ghci.h`、`~/zedBSD-claude1/src/drivers/gpu/ghci/ghci-regs.h`、`~/zedBSD-claude1/src/drivers/gpu/ghci/ghci-cmd.h`、`~/zedBSD-claude1/src/drivers/gpu/ghci/ghci-words.h`、`~/zedBSD-claude1/src/drivers/gpu/ghci/cmd.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/caps.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/check.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/range.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/jobs.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/capset.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/pci-attach.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/ghci.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/cp.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/recover.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/service.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/memory.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vm.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/share.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/isa.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/layout.h`、`~/zedBSD-claude1/src/drivers/gpu/ghci/display.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/display-scan.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/display-scan.h`、`~/zedBSD-claude1/src/drivers/gpu/ghci/adv7511.c`
- executor: `~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk.h`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-node.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-stream.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-dispatch.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-object.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-reply.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-caps.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-device.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-format.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-memory.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-layout.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-pipeline.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-pass.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-command.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-execute.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-meta.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/vk/vk-queue.c`
- compiler: `~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/ghci-isa.h`、`~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/gsi1-abi.h`、`~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/gsi1_isa.h`、`~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/gsi1-compile.h`、`~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/gsi1-compile.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/gsi1-private.h`、`~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/gsi1-encode.h`、`~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/gsi1-encode.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/gsi1-io.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/gsi1-memory.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/gsi1-image.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/gsi1-texture.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/gsi1-spill.c`、`~/zedBSD-claude1/src/drivers/gpu/ghci/compiler/gsi1-schedule.c`
- build の list: `~/zedBSD-claude1/platform/amd64/vmunix.mk`（`AMD64_GHCI_SOURCES`）、`~/zedBSD-claude1/platform/riscv64/vmunix.mk`（GHCI の `RISCV64_PCI_DEVICE_SOURCES`）

## driver が仕様の決めていない所を読んだ所（合っているか見てほしい）

| 項目 | driver の読み |
| --- | --- |
| `GCLK_HZ` = 0 の `timestampPeriod` | 10 ns（名目の 100 MHz） |
| `FMT_BUFFER` の bit | bit 5 → vertex buffer、bit 0 → uniform texel buffer、bit 8 → storage texel buffer |
| `textureCompressionETC2` | FMT0[7] と FMT0[9] の両方が有る時だけ |
| 表示の "memory line" | 64 byte |
| `DISP_IRQ_ENABLE` | どの状態でも host が書ける |

## 第 17 版（N78・N79）への回答 第 17.0 版の確かめ

- 読んだ回答: `git -C ~/amaterrace-gpu show 288c459:docs/ghci-review-reply.md`（1 行目「# GHCI Review Reply - Version 17.0 (2026-10-28) - Commit: 052a0f3」）と、`git -C ~/amaterrace-gpu diff d9252e4 288c459 -- docs/ghci.md include/ghci_cmd.h`。

| ID | 確かめた所（288c459） | 結果 |
| --- | --- | --- |
| N78 | ghci.md §12.10.3 の累積の base_i に `GHCI_VS_REG_ATTR_COMP(base_i, comp)` の注、`include/ghci_cmd.h` の `GHCI_VS_REG_ATTR(base, comp)` と `GHCI_VS_REG_ATTR_LEGACY` | 解決。driver は既に累積の base_i で compile している（変更なし） |
| N79 | ghci.md §8.4（attachment の `row_pitch_bytes` は hardware がそのまま使う、P_min 以上、推奨 roundup(P_min, 64)）と §12.11（multisample の行は `row_pitch_bytes`） | 解決。driver は仮に P_min を使っていたが、推奨の roundup(P_min, 64) に直す（`~/zedBSD-claude1/src/drivers/gpu/ghci/layout.h` の行の pitch、次の commit） |

- 以後、driver の正本は ghci.md **288c459**（d9252e4 からの差は N78・N79 の 3 か所だけと確かめた）。この版の code review は d9252e4 で書いた code に対する物だが、288c459 との差は上の 2 件だけ。

## 回答の形

- 指摘は 1 件ずつ ID（C01〜）を付け、file と行、ghci.md の節と行、重さ（H・M・L）、理由を書いてほしい。N78・N79 の確かめに異論があれば同じ形で書いてほしい。

DONE
