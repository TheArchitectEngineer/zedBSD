# WS290 の設計: zedBSD の GHCI の driver を GHCI 1.0（ghci.md cf8be08）に書き直す

- Phase: [ws290-p001](phase001/phase.md)。版 1・2: 2026-10-26（P1、q1594）。版 3: 2026-10-27（P3、q1621、書きかけで中断）。**版 4: 2026-10-28（P3、q1636-i01）**。版 3 までの本文は git の履歴（f5fea4c2e など）にあり、この版は 89529de を正本に全部を書き直した。
- 親: [WS290](ws.md)。前身の設計: [WS288 design](../ws288/design.md) §6（1.1 の driver）。
- この文書は設計だけ。source は変えていない。
- **状態（2026-10-28、q1638 P3）: 版 4.3。U の回答（版 4.1）、design-reviewer（Opus 5.5）1 回目の H 6・M 18・L 10（版 4.2）、2 回目の H 4・M 12・L 9（版 4.3）を反映（§24）。仕様の不備は [ghci-review.md](ghci-review.md) 第 13 版（N47〜N60）に用意し、N61 を足す予定（§24 の末尾）。driver の仮の扱いは §19。**

## 0. 正本・読んだ物・確認の水準

| 読んだ物 | 版 | 使い方 |
| --- | --- | --- |
| amaterrace-gpu `docs/ghci.md`（2782 行） | **cf8be08**（2026-10-28 03:44、`git show cf8be08:docs/ghci.md`）。89529de（2677 行）からの差は bc7df9e の §12.10.3「Shader Stage Register & Export Conventions」の追加、1a1fa85 の N38〜N45 の改訂（§8.4 の dual-source、§12.1・§12.5・§12.6・§12.8・§12.10・§12.11・§12.12 の参照と CAPS の名前）、cf8be08 の 2298 行（Vulkan の戻り値の名前を除く）。main の先頭 4188a4c は回答の file だけの差 | driver の要件の唯一の正本。下の「§n」はこの版の節。「n 行」は **1917 行までは 89529de と cf8be08 で同じ**で、§12 以降（1918 行より後）は **cf8be08 の行**（版 4.2 から統一。Q23）|
| 同 `include/`（ghci_regs.h・ghci_cmd.h・ghci_packets.h・ghci_frame.h・ghci_layout.h・gsi1_isa.h など 20 file） | cf8be08（gsi1_isa.h は bea1ef9 で注記 1 行だけ直った。89529de からの差は ghci_cmd.h の `GHCI_VS_REG_*`・`GHCI_FS_REG_*`・`GHCI_EXP_*` と gsi1_isa.h の export・system の register・`GSI1_COLOR_ATTACHMENTS 8`・注記。gsi1_isa.h の header・interface 表の定義は「pre-1.0、1.0 では使わない」と明記されて残る） | ghci.md との照合だけ。値は ghci.md から起こす（§17）。`gsi1_isa.h`（bea1ef9）と今の `compiler/gsi1_isa.h` は 78 行違う（`GSI1_COLOR_ATTACHMENTS` 4 と 8、`GSI1_EXP_COLOR0_SRC1`、`GSI1_VS_REG_*`、lda の word 0〜3 など。版 4 の「同じ」は誤りだった、R18）。p006 で値を直す |
| 同 `fw/cp/ghci_cmd.c`・`cmd.c`（参考） | 89529de | CP の firmware はまだ §8.2 の PSO の配置に合っていない（`fw_cmd_bind_pipeline_pso` は `pso.cull_mode` など §8.2 に無い field を読む）。実機は仕様より遅れ得る（§16.2） |
| zedBSD `src/drivers/gpu/ghci/`（49 file、33136 行）、`include/uapi/gpu-op.h`、`include/drivers/gpu/gpu.h`、platform の vmunix.mk、`pcat.c`、`vf2-pcie.c`、`userland/base/ghcictl`、`plan/ws288/tests/` | main 6e6ebf830 | file の行き先（§3） |

- 仕様は**単一の GHCI 1.0**（2026-10-27 ユーザーの G01 の回答、ws.md）。driver は版で分岐せず、機能の有無は capability の flag（DEVICE_CAPS・EXT_CAP_FEATURE・EXT_CAP_FMT0・EXTCAP）だけで決める（§16「Versions & Capabilities」2644〜2654 行）。
- GHCI の担当とのやり取りは 2026-10-28 のユーザーの指示で再開した（ws.md、レビュー第 10 版から）。経緯の file（ghci-questions*・ghci-answers-hw-*・ghci-review*）は正本ではない。版 3 の G01〜G49 と design-review-v3 の V01〜V25 の扱いは §22。
- 確認の水準: 上の物は `git show`・cat・grep で読んだ。amaterrace-gpu には書いていない。build・試験・QEMU・実機はこの Phase では行わない。

## 1. 責務と境界

### 1.1 この driver がする事

1. GHCI の PCIe の function（§15.2）を見つけ、BAR0 の `GHCI_MAGIC`（0x47484349）と `GHCI_VERSION` の major = 1 で識別し、capability を register から読む（§5.1）。
2. ring（host memory）、doorbell（`RING_WPTR`）、64 bit の fence、割り込み（MSI・MSI-X・INTx・poll）、session の fault と装置の fault、watchdog、reset。
3. GPU の memory: host memory（UMA・discrete）と local memory（discrete、`VM_FLAG_LOCAL`）、session（0〜15）、`CMD_VM_MAP`・`CMD_VM_UNMAP` による session の VA（CP が page table を持つ、§11.2）。CP_HEAP の確保と使用量の勘定。
4. Vulkan の executor（`vk/`）: libvulkan の Venus の wire を受け、Vulkan の object を kernel が持ち、PSO（128 byte）・descriptor・render pass の記述を session の VA に焼き、記録を submit で `CMD_*` に直して ring に置く。
5. shader: SPIR-V → 共有の IR → GSI1（`compiler/`、kernel の中。Guardrail）。§12.10 の「header も interface 表も無い純粋な code」に合わせ、資源の結び方を driver の取り決め（§12）にする。
6. 表示（DISP2、§13.2）: `CMD_DISP_SET`・`CMD_FLIP` を ring に置き、`DISP_IRQ_STATUS` で事象を読む。boot framebuffer の引き継ぎ（§13.3）。

### 1.2 しない事（境界）

- **ring・PSO・descriptor を書くのは kernel だけ**（D1、U7）。§6.0 は「UMD が secondary を記録する」と書くが、primary の ring は KMD のもの（590 行）と仕様も言う。zedBSD の libvulkan は汎用の Venus の client のまま変えない（Guardrail）。
- `CMD_EXECUTE_COMMANDS`（secondary）は v1 で使わない（D6）。記録は kernel が primary に展開する。
- **CAP_PROT = 1 に関わる物は全部 [WS292](../ws292/ws.md)**（U5「全部 WS292」）: session の fault の回収（§3.3 の N05 の手順）、protected context（`CMD_SESSION_OPEN` の flags[0]）、Vulkan の protected memory、PROT の block の register の利用。WS290 は CAP_PROT = 1 の device も CAP_PROT = 0 と同じに扱う: どの fault も装置の reset で回復する（§5.4）。
- libvulkan・共有の compiler の frontend（`spirv.c`・`optimize.c`・`ir.h`）・GPU core（`gpu.c`）・HAL の API は変えない。HAL の差分は無い。
- amaterrace-gpu の sim・firmware・Verilator は使わない（ws.md の決定）。実機は VF2（riscv64）＋ KC705 で、ユーザーが用意した後に Q1 が割り当てる（§16）。
- platform binding（§15.1、device tree の窓）は書かない（今の board は PCIe の KC705）。Future Work の候補。
- 複数の ring は無い（§3.1 に ring は 1 本）。

## 2. 仕様の要件と driver の対応

| 節 | 要件 | driver の対応 | Phase |
| --- | --- | --- | --- |
| §2 | 32 bit LE の register、128 KiB の窓のうち使うのは先頭 1 KiB、読みに副作用無し、64 bit の値は LO・HI の 2 回 | BAR0 は 4 KiB 以上を受ける。`TIMESTAMP_LO` を先に読む（HI は latch） | p004 |
| §3.1 | 0x000〜0x084 の register、0x100〜0x3FF の EXTCAP | `ghci-regs.h` に名前と offset | p002 |
| §3.2 | DEVICE_CAPS（0x008）が正、EXT_CAP_FEATURE（0x084）・EXT_CAP_FMT0（0x080）は近道、重なる bit は DEVICE_CAPS を優先（269〜272 行） | `struct ghci_caps`（§15.1）。重なりは DEVICE_CAPS、食い違いは log だけ | p002・p004 |
| §3.3 | CTRL（RESET・RUN・HALT・STEP・TLB_FLUSH・WDT_EN・IRQ_GLOBAL）、STATUS（RUNNING・HALTED・FAULTED・MC_IDLE・RING_EMPTY・MEM_READY・AXI_STUCK・QUIESCE・CP_STATE） | 状態の判定（§6.1）。TLB_FLUSH は書かない（331 行） | p004 |
| §3.3 session の fault | CAP_PROT = 1: client の fault は `SESSION_ACTIVE[i]` = 0 と IRQ_FAULT だけ、FAULTED は立たない。CAP_PROT = 0: どの fault も FAULTED | U5: FAULTED に依らず、IRQ_FAULT・IRQ_WATCHDOG は全部装置の reset（§5.4）。session だけの回収は WS292 | p004 |
| §3.4・§9.2 | IRQ_STATUS（W1C）・IRQ_ENABLE・IRQ_SET（SOFT）、線 = IRQ_GLOBAL && (STATUS & ENABLE) | handler（§5.3） | p004 |
| §3.5 | FAULT_INFO（KIND・UNIT・SESSION_ID・SHADER_STAGE・SYNDROME）、W1C で再武装、FAULT_ADDR | fault の記録と log、session の特定 | p004 |
| §4 | EXTCAP の list（2 語の entry）、固定 offset（FMT 0x140、VEND 0x180、PROT 0x1D0、MEM 0x1E0、DISP2 0x200）、FMT の register | list を辿り、在る block の offset を使う（§5.1） | p002・p004 |
| §5 | CP の状態、reset が保つ物・消す物、回復の手順（564〜574 行） | §5.4 | p004 |
| §6 | ring は byte の大きさ（4 KiB〜16 MiB の 2 の冪）、pointer は dword の index、1 slot を空ける、packet は端を跨がない（NOP で埋める）、doorbell の前に可視化 | `ghci_ring_plan`（§4.4） | p002・p004 |
| §6.0 の番地の空間 | packet の番地は 3 種: session の VA、host の bus address（fence・WAIT_FENCE・特権の timestamp）、32 bit の物理（FLIP・FB） | 型で分ける（§4.2） | p002 |
| §6.0 session 0 | 0 は kernel、RUN の時に CP が root を CP_HEAP_BASE に作る、`SESSION_OPEN` 不要 | §5.5 | p005 |
| §6.4 | RPTR は parse で進む（VM_UNMAP は TLB の無効化の後）。memory の再利用は fence で守る | 寿命（§7） | p004・p005 |
| §7.1〜7.3 | header（opcode 8・dwords 8・flags 16）と全 command の形 | `cmd.c` に 1 opcode 1 関数 | p002 |
| §8.2〜8.4 | PSO 128 byte、texture 32・sampler 16・buffer 16 byte、render pass 192 byte、set 0〜3、c24〜c31 の鏡、texture の slot k は set 0 と set 1 | encoder（p002）と executor の配置（§11.4） | p002・p008 |
| §8.5 | format の表、FMT の register が正 | §15.4 | p007 |
| §8.6 | Vulkan の limit の値 | §15.3 | p007 |
| §9.1 | fence は 64 bit・signed の比較、完全、host か local | `ghci_fence_passed64` | p002・p004 |
| §10 | session の fault と装置の fault、watchdog（packet ごと、外の待ちで止まる）、quiesce、reset の順 | §5.4 | p004 |
| §11.1〜11.3 | host は register・doorbell・local window だけ。UMA と discrete。MEM の register | §5.6 | p005・p011 |
| §11.2 | VM_MAP・VM_UNMAP・SESSION_*、CP_HEAP（32 bit の物理、RUN の前に書く、連続でだけ広げられる）、MMU_OOM | §5.5 | p005 |
| §11.4・§11.5 | coherence は platform の性質、GPU の cache は `CMD_BARRIER` で明示 | §9.2、§11.5 | p005・p009 |
| §11.6 | HW_WALKER・PROT の 3 つの段（flat、共有の VA、session ごとの VA） | 3 つとも executor を出す（U9）。flat は session 0 だけで VA = bus（§5.5）、PROT = 1 は PROT = 0 と同じ扱い（U5） | p004・p005 |
| §12 | GSI1、純粋な code、PSO の dw13〜15、descriptor と constant bank の ABI | §12 | p006・p008 |
| §13.2・§13.3 | DISP2、DISP_SET・FLIP、事象、boot framebuffer | §13 | p012 |
| §14・Appendix A | MAGIC・VERSION・SCRATCH、FENCE 1 つで DMA と IRQ を確かめる、初期化の順 | §5.1 | p004 |
| §15.2 | class 03h/02h か 80h、BAR0、BAR2（任意）、MSI/MSI-X 1 本か INTx、Bus Master の前は DMA 無し | `pci-attach.c` | p004 |
| §16 | 版は 1.0 だけ、機能は flag | §5.1 | p004 |

## 3. 今の driver の file の行き先

「残す」= ほぼそのまま、「直す」= 構造を保ち 1.1 の部分を 1.0 に、「捨てる」= 消す（削除は Q1）、「新」= 新しく書く。

| file | 行 | 行き先 | Phase | 理由 |
| --- | --- | --- | --- | --- |
| `hcr.h`・`packets.h`・`frame.h` | 485・66・221 | 捨てる → `ghci-regs.h`・`ghci-cmd.h`（新） | p002〜p004 | 1.1 の register・封筒・class V |
| `state.c` | 206 | 捨てる → `cmd.c`（新） | p002・p004 | 1.1 の packet |
| `check.c` | 584 | 直す（fence を 64 bit、ring を dword の index、EXTCAP は残す。native の検査を捨てる） | p002・p004 | |
| `ghci-words.h` | 287 | 直す | p002・p004 | |
| `range.c`・`jobs.c`・`capset.c`・`service.c` | | 残す | — | |
| `ghci.h`・`ghci.c` | 835・1436 | 直す（identify、caps、session、resource） | p004・p005 | |
| `pci-attach.c` | 479 | 直す（小: match、BAR0 の大きさ、Bus Master の順） | p004 | |
| `cp.c` | 1262 | 直す（大: 1.0 の register・ring・fence 64・IRQ・fault（U5 で全部 reset）・reset） | p004 | |
| `memory.c` | 688 | 直す（aperture を捨て、物理の確保と番地の上限） | p005 | |
| （新）`vm.c` | | session・VA・VM_MAP・CP_HEAP の勘定・再生 | p005 | §5.5 |
| `share.c` | 275 | 直す（import は輸入先の session に VM_MAP） | p005 | |
| `firmware.c`・`native.c`・`blit.c` | 2071・96・195 | 捨てる | p004 | INIT・DIAG・CPFW・native・1.1 の BLT |
| `isa.c` | 48 | 直す（NATIVE_ISA_ID と CAP_GSI1） | p006 | |
| `layout.h` | 167 | 直す（§8.5.2 の linear と tiled、64 byte の行） | p008 | |
| `compiler/*` | 10588 | 直す（header・interface 表を捨て §12 の ABI に） | p006 | |
| `display.c`・`display-scan.c`・`.h`・`adv7511.c` | 1274・1919・320 | 直す（lease・queue・record・数え直しの論理は残し、送り方を DISP_SET・FLIP（物理）に） | p012 | |
| `vk/vk-engine.*`・`vk-relay.c`・`vk-session.c` | 880・1280・487 | 捨てる | p007 | firmware の中継 |
| `vk/vk-stream.c`・`vk-node.*` | 1115・543 | 直す（transport・capset・submit・drain は残す） | p007 | |
| `vk/vk-device.c`・`vk-format.c` | 1336・282 | 直す（答えは `vk-caps.c` から） | p007 | |
| `vk/vk-memory.c`・`vk-program.c`・`vk.h` | 1415・1189・473 | 書き直す | p007・p008 | |
| （新）`ghci-regs.h`・`ghci-cmd.h`・`cmd.c`・`caps.c` | | 1.0 の値・encoder・capability の decode | p002 | |
| （新）`vk/vk-caps.c`・`vk-object.c`・`vk-reply.c`・`vk-layout.c`・`vk-pipeline.c`・`vk-pass.c`・`vk-command.c`・`vk-record.c`・`vk-execute.c`・`vk-queue.c`・`vk-meta.c` | | executor（amdrdna の `vk/` と同じ分け方）。`vk-meta.c` は §11.6 | p007〜p010 | §11 |

外の file: `platform/amd64/vmunix.mk`・`platform/riscv64/vmunix.mk` の list（各 Phase）、`Makefile` の `CONFIG_GHCI_NATIVE` と `include/uapi/gpu-op.h` の `GPU_OP_GHCI_*`（U1、p013）、`userland/base/ghcictl`（`diag`・`paint`・`fwload` を外す、U6、p013）、`userland/tests/ghci-blit`（削除、U6、p013）、`userland/tests/ghci-display`（残す）、`include/drivers/pci/pci-ghci.h`・`pcat.c`・`vf2-pcie.c`（残す）。`plan/ws288/tests/ghci-*`・`bringup-tb.*`・`ghci-e2e*`・`ghci-su-board.py`・`zybo-gpu-sd.dts` は 1.1 の試験で、p004 で build できなくなる。stub の部分は `plan/ws290/tests/` に移して直し、残りの削除を Q1 に頼む（p003）。`plan/ws288/tests/gsi1-*`・`gsi1/` は p006 で移す。

## 4. 新しい構成

### 4.1 file と責務

```
src/drivers/gpu/ghci/
  ghci-regs.h   register の offset・bit、EXTCAP の ID、MEM・FMT・VEND・PROT・DISP2 の offset、FAULT の kind・unit
  ghci-cmd.h    opcode・dwords・flag、PSO・descriptor・render pass の byte の offset と符号の値
  ghci-words.h  host で動く関数の宣言（kernel を触らない。host 試験がそのまま呼ぶ）
  cmd.c         全 command・PSO・descriptor・render pass の encoder
  check.c       ring の計画、fence の比較、EXTCAP の走査
  caps.c        register の値 → struct ghci_caps、page table の勘定（pure）
  range.c jobs.c capset.c
  ghci.h ghci.c pci-attach.c cp.c service.c memory.c vm.c share.c isa.c
  display.c display-scan.c display-scan.h adv7511.c
  compiler/     GSI1 の compiler（§12 の ABI は compiler/gsi1-abi.h に一か所）
  vk/           executor（§11）
```

### 4.2 番地の型

§6.0 の 3 つの空間を混ぜないため、関数の引数と field の名前で分ける（C の型は `uint64_t`・`uint32_t` のまま、名前で区別する）:

| 空間 | 幅 | 使う所 | 名前の接尾 |
| --- | --- | --- | --- |
| session の GPU VA | 64 bit（v1 は 32 bit の範囲だけ使う） | PSO・descriptor・buffer・image・code・render pass・query・copy | `_va` |
| host の bus address（local は ≥ LOCAL_BASE） | 64 bit（`PA_BITS` 以下） | `RING_BASE`、`CMD_FENCE`・`CMD_WAIT_FENCE` の fence、特権の `CMD_WRITE_TIMESTAMP`、`CMD_VM_MAP` の phys | `_bus` |
| 32 bit の物理 | 32 bit | `CP_HEAP_BASE/LIMIT`、`CMD_FLIP` の base・record、DISP2 の FB | `_phys32` |

### 4.3 主な型（field の名前は実装で整える）

```c
/* The capabilities of one device, read once at attach and not changed after. */
struct ghci_caps {
	uint32_t device_caps;		/* DEVICE_CAPS (0x008), authoritative */
	uint32_t feature;		/* EXT_CAP_FEATURE (0x084) */
	uint32_t fmt0;			/* EXT_CAP_FMT0 (0x080) */
	uint32_t native_isa;		/* NATIVE_ISA_ID (0x068) */
	uint32_t gclk_hz;		/* GCLK_HZ (0x064); 0 reads as unknown */
	uint32_t lane_registers;	/* RESIDENT_WARPS[23:16] */
	uint32_t va_bits;		/* 32 + FEATURE[3:0] */
	uint32_t pa_bits;		/* 40 + FEATURE[7:4] */
	uint32_t blocks;		/* bit (1 << id) for each EXTCAP entry found */
	uint32_t block_offset[12];	/* the register offset of each found block */
	uint64_t local_base;		/* MEM: LOCAL_BASE, LOCAL_SIZE, WINDOW_SIZE, HOST_LIMIT */
	uint64_t local_bytes;
	uint64_t window_bytes;
	uint32_t host_limit;
	uint32_t cache_config;		/* MEM_CACHE_CFG, for the log */
	uint32_t prot_caps;		/* PROT_CAPS, 0 without the block */
	uint32_t vendor[4];		/* VEND: vendor, device, subsystem vendor and device */
	uint32_t mismatches;		/* DEVICE_CAPS bits a mirror disagrees with, for the log */
};

/* The ring: its memory, its size and the driver's write index in dwords. */
struct ghci_ring {
	struct ghci_allocation *memory;	/* host memory; its bus address is RING_BASE */
	uint32_t dwords;		/* RING_SIZE / 4 */
	uint32_t wptr;			/* the next dword the driver writes */
};

/* One GHCI session (1..15) that an executor session holds. */
struct ghci_vm {
	uint32_t session_id;
	enum ghci_vm_state state;	/* OPEN, FAULTED, CLOSING, RETIRED (section 6.2) */
	struct ghci_mapping *mappings;
	struct ghci_range_map addresses;/* the VA inside the chunks the session owns */
	uint32_t chunks;		/* 2 MiB chunks owned (section 5.5) */
	uint32_t table_pages;		/* CP_HEAP pages counted against it */
	struct ghci_ticket close_ticket;
};

/* One mapping of an allocation into a session's GPU address space. */
struct ghci_mapping {
	struct ghci_mapping *next;
	struct ghci_allocation *allocation;
	uint64_t va;
	uint64_t bytes;
	uint32_t flags;			/* GHCI_VM_FLAG_* */
	enum ghci_mapping_state state;	/* LIVE, UNMAP_WANTED, UNMAPPING (section 6.3) */
	struct ghci_ticket ticket;
	uint64_t replayed;		/* the reset generation it was last mapped in */
};
```

`struct ghci_allocation` は物理だけを持つ（`kind` HOST・LOCAL、`bus`、`bytes`、`view`、`holds`）。1.1 の `gpu_address`（aperture の中）は無くす。`struct ghci_device` から 1.1 の `isa_id`・`caps2`・`aperture_*`・`prot_*`・`fw_*`・`diag_*`・`bounce` を外し、`caps`・`ring`・`fence_page`・`heap`（CP_HEAP の物理と勘定）・`chunk_map`（VA の 2 MiB の chunk の持ち主）・`vm[16]`・`replay_pending`・`quarantined` を足す。

### 4.4 encoder（`cmd.c`）

全 encoder は dword の列（`uint32_t *out`）か byte の列（`uint8_t *out`）に 1 つずつ書く（struct を GPU memory に重ねない。float は wire の bit をそのまま `uint32_t` で受け、kernel は float を計算しない。D5）。値の block ごとに guardrail の形の複数行の comment「Values referenced: amaterrace-gpu cf8be08 docs/ghci.md (section 7.3, CMD_VM_MAP)」（§17、R22・Q23）。

| 群 | encoder | dwords / bytes |
| --- | --- | --- |
| 共通 | `ghci_cmd_header`、`ghci_cmd_nop`（1〜255、残りが 255 を超えれば続ける） | 1・1+N |
| session・VM | `vm_map`・`vm_unmap`・`session_open`・`session_close`・`bind_session` | 10・8・4・2・2 |
| bind | `bind_pipeline`・`bind_vertex_buffers`（first + n ≤ 16、1249・1535 行。R25）・`bind_index_buffer`・`bind_descriptor_table`・`load_const`（count 1〜32） | 3・2+4n・4・4・3+n |
| state | `set_viewport`・`set_scissor`・`set_dynamic_state`（state_id ごとの関数） | 7・5・6 |
| draw | `draw`・`draw_indexed`・`draw_indirect`（7 と count の 8。INDEXED は 7 dw が dword 6 の bit 0、8 dw が header の flags[0]、他方は 0。N51）・`draw_mesh_tasks`・`dispatch`・`dispatch_indirect` | 5・6・7/8・4・4・6 |
| pass | `begin_render_pass`・`end_render_pass` | 10・1 |
| 同期 | `barrier`・`fence`・`wait_fence`・`write_timestamp`・`begin_query`・`end_query` | 4・6・5・4・4・4 |
| 転送 | `copy_buffer`・`copy_image`・`blit`・`fill_buffer`・`resolve_image`・`copy_buffer_to_image`・`copy_image_to_buffer` | 8・11・12・5・11・10・10 |
| 表示 | `disp_set`（n 組）・`flip` | 2+2n・8 |
| 記述 | `ghci_pso_encode`・`ghci_texture_desc_encode`・`ghci_sampler_desc_encode`・`ghci_buffer_desc_encode`・`ghci_render_pass_desc_encode` | 128・32・16・16・192 byte |

`ghci_ring_plan(wptr, rptr, dwords, need, *pad)`: 端までの残りが `need` より少なければ残りを NOP で埋めて 0 へ（§6.2）、空きは `(rptr − wptr − 1 + n) % n`（§6.1）。

## 5. 流れ

### 5.1 attach（p004）

1. `pci-attach.c`: class 03h（subclass 02h か 80h）か、既知の ID（KC705 の 10EE:7024、Amaterrace の 5441:4748、§3.2.3）の function を受ける（KC705 の endpoint の class code が 03h とは限らないため ID でも受ける。どちらでも識別は次の MAGIC）。BAR0 を map（4 KiB 以上）。BAR2 が有れば local window として map（discrete の boot framebuffer と CAP_BLT の無い discrete の表示の写しに使う、§13。R28）。割り込みはこの時点では取らない（前の kernel の古い IRQ を受けないよう、手順 3 の reset と IRQ_STATUS の W1C の後、手順 9 で取る。R27）。
2. identify（読みだけ）: `GHCI_MAGIC` = 0x47484349、`GHCI_VERSION[31:16]` = 1（minor は問わない。G01）。違えば `ENODEV`。
3. reset: 先に `WDT_CYCLES`（前の kernel か既定の 2^28）と `GCLK_HZ` を読み、待ちの上限を max(1000 ms, 4 × WDT_CYCLES / GCLK_HZ + 500 ms)（GCLK_HZ = 0 なら 10 s）にして（quiesce は 4 × WDT_CYCLES かかり得る、1609 行。Q20）、`CTRL.RESET` を書き `CTRL.RESET` = 0 を待つ（§5.2 の手順 1）。前の kernel が RUNNING で残した CP もここで IDLE になる（RUN が 0 に）。Bus Master の扱い（D16、手順 9 と一つ）: 既に立っていれば（firmware の GOP の表示が使う）切らず、立っていなければ手順 9 で立てる。`STATUS.MEM_READY` = 1 を待つ（Appendix A の 3）。
4. `SCRATCH` に 0x5a5a5a5a・0xa5a5a5a5 を書いて読み戻す（§14）。
5. capability: DEVICE_CAPS・EXT_CAP_FEATURE・EXT_CAP_FMT0・GCLK_HZ・NATIVE_ISA_ID・RESIDENT_WARPS を読み、EXTCAP_PTR が 0 でなければ list を辿る（entry 8 個まで（478 行）、環・窓の外・重なりを拒む。FMT・MEM・DISP2 は capability の version 1 だけを使い、他の version は log して無いと扱う。R26）。MEM・VEND・PROT は在れば register を読む。FMT・DISP2 は offset を記録する（FMT は p007、DISP2 は p012 で読む）。`ghci_caps_decode`（caps.c、pure）。DEVICE_CAPS と鏡（FEATURE[8]・[28]）が食い違えば DEVICE_CAPS を使い log（270〜271 行）。
6. 方針の判定（U9「どちらも出す」）: `CAP_HW_WALKER` = 0（flat、§11.6）も `CAP_PROT` = 0 も executor を出す。どちらも client の間の memory の保護が無い（flat は client の shader が全物理を触れる）ので、attach の log に「GPU memory isolation: none (flat)」「… shared VA (PROT=0)」と出す。`NUM_SU` = 0 か `CAP_GSI1` = 0 は executor を出さない（display だけ）。
7. CP_HEAP: 32 bit の物理（4 GiB 未満、MEM の HOST_LIMIT が 0 でなければその下）に連続の `GHCI_HEAP_BYTES`（§5.5）を取り、0 で埋め、`CP_HEAP_BASE`・`CP_HEAP_LIMIT` を書く（IDLE の間、§5.2）。flat（HW_WALKER = 0）は page table を使わないが、RUN の時の session 0 の root の確保（628 行）が flat を除外していないので、session 0 の root の 16 KiB だけを取って書く（BASE = 0 のまま RUN にしない）。
8. ring（host memory、既定 256 KiB、4 KiB〜16 MiB の 2 の冪）と fence page（host 4 KiB、64 bit の語を 0 に）を取り、`RING_BASE_LO/HI`・`RING_SIZE` を書き、`RING_WPTR` = 0。
9. 割り込みを取る（MSI-X・MSI・INTx、取れなければ poll）。`WDT_CYCLES` = `GCLK_HZ` × `GHCI_WATCHDOG_MS`（2000）/ 1000 を 64 bit で計算し 0xFFFFFFFF で clamp（GCLK_HZ = 0 なら書かず既定 2^28 のまま。R14）。`GHCI_RESET_WAIT_MS` は quiesce が 4 × WDT_CYCLES かかり得る（1609 行）ので max(1000, 4 × watchdog の ms + 500)。IRQ_STATUS を全部 W1C、`IRQ_ENABLE` = FENCE | FAULT | WATCHDOG（DISP2 が有り p012 の後は | DISPLAY。poll の device は 0）。Bus Master を立てる（3 で立てていなければ）。`CTRL` = IRQ_GLOBAL | WDT_EN | RUN（poll は IRQ_GLOBAL 無し）。CP はここで session 0 の root を CP_HEAP_BASE に作る（628 行）。
10. liveness: `CMD_FENCE`（MEM_WRITE | IRQ、seq 1）を置き doorbell。fence の語が 1 になり、割り込みが来る（poll は語だけ）のを待つ（§14）。来なければ `DIAG_CP_PC`・`DIAG_CP_STATUS`・`STATUS`・`FAULT_INFO` を log し、`CTRL.RESET`（RUN が 0 になり ring・heap への DMA は止まる）→ Bus Master は手順 9 で driver が立てた時だけ切る（firmware が立てていた物は boot の表示のため残す、D16。R27）→ memory を返して `EIO`。
11. service・poll の thread を起こし `drv_gpu_register`。名前は `ghci:<PCI vendor>:<PCI device>`（VEND が有れば log に足す）。

### 5.2 submit と門（p004、executor は p009）

**lock の順**（§8）: `device.lock`（mutex）→ `display.lock`（mutex）→ `cp_lock`（mutex）→ `irq_lock`（spin）。

**回復の印と門（R02・Q02・Q03・Q08）**: handler が IRQ_FAULT・IRQ_WATCHDOG を見た時、service が hang・liveness の失敗・計画の reset（§5.5）を決めた時に、irq_lock の中で `recovering` = 1 にする。ring に packet を置く全ての経路（submit、VM の map・unmap、session の open・close、表示）は、自分の外側の lock を取った後に `ghci_cp_enter` で cp_lock を取る。`ghci_cp_enter` は `recovering` が 1 なら cp_lock と呼び手の持つ外側の lock（device.lock・display.lock）を全部離し、`recovery_generation` が進むのを待って **RETRY** を返す。呼び手は最初からやり直し、状態を読み直す。**門を通る前には何も記録しない**: VM・session の状態の変更と packet の配置は、門を通った後の 1 区間（device.lock → cp_lock）で行い、全部を置けた時だけ状態を変える。ring の空きを待つ loop（上限 `GHCI_RING_WAIT_MS` = 2 × watchdog の ms + 1000）は、`recovering` が立てば即に抜けて RETRY（submit は下の規則）。回復をする service thread だけが門を通らない。

`ghci_cp_submit(device, vm, packets, dwords, ticket)`: 門で cp_lock を取る。packet の前に `CMD_BIND_SESSION(vm の番号)`（driver 自身の仕事は 0。flat は置かない）、後に `CMD_FENCE`（device の fence page、seq = 前 + 1、MEM_WRITE | IRQ）、最後に `CMD_BIND_SESSION(0)`（後の driver の packet が client の session で動かないように。flat は置かない）を置く。1 つの submit は cp_lock を持ったまま全部置く（D15）: 余地が無ければ cp_lock を持ったまま（device.lock は持たない）RPTR を待ち、ring より大きい submit は packet の境で分けて doorbell ごとに待つ。一部を置いた後に `recovering` で抜けた submit は、置いた部分が reset で捨てられるので、やり直さず `EIO`（その executor の session の error）。ticket = {seq（64 bit）, generation}。submit ごとに [start, end)・持ち主の `ghci_vm`・fence の seq を `submit_log`（未完の物）に残す（flat の犯人の判定、§5.4）。doorbell は §9.3 の順。

### 5.3 割り込み（p004）

handler（irq_lock の spin）: `IRQ_ENABLE` = 0 → `IRQ_STATUS` を読む → 扱う bit を W1C → `IRQ_ENABLE` を戻す。bit は FAULT・WATCHDOG を FENCE より先に扱う（Q05）:
- FAULT・WATCHDOG（2 つ同時の watchdog、1603 行、は 1 つの事象）: `recovering` = 1。`STATUS`・`FAULT_INFO`・`FAULT_ADDR`・`SESSION_ACTIVE`・`RING_RPTR`・`DIAG_CP_PC`・`DIAG_CP_STATUS` と、その時の fence の語を、今の `recovery_generation` を付けて記録する（Q06）。PROT = 1 の client の fault で CP が動き続け、その session の FENCE が書かれても仕事は NOP なので、`FAULT_INFO.SESSION_ID` = s（1〜15）と、`SESSION_ACTIVE` が 0 になった OPEN の session を、irq_lock の中の `faulted` の bitmap に入れ、その session の ticket と job marker は以後 fence が過ぎても ERROR で完了する（Q05）。service に FAULT。
- FENCE: fence page を読み completed を進め service に RECLAIM。
- DISPLAY: service に DISPLAY（p012、`DISP_IRQ_STATUS` を読み W1C してから IRQ_STATUS[3] を W1C、1567 行）。
`CTRL` は handler で触らない。`drv_gpu_complete` は service thread が呼ぶ。

### 5.4 fault・watchdog・reset（p004・p005）

U5（「全部 WS292」）により、WS290 は session だけの回収（§3.3 の N05 の手順）をしない。**IRQ_FAULT・IRQ_WATCHDOG・hang・liveness の失敗・計画の reset は、`STATUS.FAULTED` に依らず全部、装置の reset で回復する**（CAP_PROT = 1 の device も CAP_PROT = 0 と同じ扱い）。reset は「どの状態からでも」受け付けられる（§5.2 の RESETTING）。回復は service thread だけが行う。

| 段 | 持つ lock | 中身 |
| --- | --- | --- |
| 1 | device.lock | 記録の取り出しと犯人の判定 |
| 2〜6 | device.lock → display.lock → cp_lock（4 は + irq_lock） | reset、ticket、表示の数え直し、session の状態、heap の勘定、RUN、liveness |
| 8 | device.lock → cp_lock（display.lock は 6 の後に離す） | 再生と最後の確かめ |
| 7・9 | 無し | session への報告、門を開ける |

1. **記録と犯人**: service は handler の記録を取り出し、`recovery_generation` が今の物より古い記録は捨てる（reset の前に積まれた古い事象、Q06）。log（`FAULT_INFO` は KIND・UNIT・SESSION_ID だけを解き、stage と syndrome は生の値。ghci-review 第 13 版 N50）。
   - **client に帰せる fault**: `UNIT` が VF・SU・RX・TU・ROP・BLT（≠ CP）で `KIND` が 1・6・9・10、または `KIND` 4（watchdog）。driver の誤りで起こる物（`KIND` 2・3・5・7・8、`UNIT` = CP）は帰せない。
   - **持ち主（Q04）**: flat でない device は `FAULT_INFO.SESSION_ID` = s が 1〜15 で OPEN の `ghci_vm`、と（PROT = 1 で）handler の `faulted` の bitmap の OPEN の session。s = 0 は driver の packet なので装置の fault（1621 行の「Session 0 の watchdog は FAULTED」）。flat は全 client が session 0 なので、handler が fault の時に irq_lock の中で読んだ fence の語より seq が大きい、`submit_log` の最初の submit の持ち主（それが driver の submit なら装置の fault）。
   - 帰せる fault の持ち主が**犯人**: その `ghci_vm` を FAULTED、executor の session に `drv_gpu_report_session_error`（sticky、以後の submit を断る。Vulkan は DEVICE_LOST）。犯人の無い fault（帰せない物、AXI_STUCK、hang）は**装置の fault**。計画の reset（§5.5）は fault でない。
2. device.lock → display.lock → cp_lock を取る。`CTRL.RESET` を書き、0 に戻るのを `GHCI_RESET_WAIT_MS` 待つ。戻らない・`STATUS.AXI_STUCK` = 1 なら即 LOST（1609〜1611 行の「link の reset でしか戻らない」）。
3. reset は `RING_BASE`・`RING_SIZE`・`CP_HEAP_*`・`WDT_CYCLES`・`IRQ_ENABLE`・`SCRATCH` を保ち、`RING_WPTR`・`RPTR`・`IRQ_STATUS`・`FAULT_*`・`CTRL` の実行の bit・`SESSION_ACTIVE`・TLB・CP_HEAP の割当を消す（565〜566 行）。driver は保たれる register を読んで確かめ（違えば書き直す）、`ring.wptr` = 0、`submit_log` を空にする。
4. irq_lock の中で順に: (a) fence の語を読み、その値までの ticket を正常に完了する（reset の前に書かれ IRQ が未処理だった物。`faulted` の session の物は ERROR）、(b) generation を進め、(a) の値より後の古い ticket と job marker を ERROR で完了する、(c) fence の語に最後に置いた seq を書く（574 行）、(d) `faulted` を空にする。reset が成功した（quiesce した）ので古い ticket を待つ物理は返してよい。
5. （p012 の後）CP が IDLE の間に DISP2 の `STATUS[31]` が落ちるのを待ち `TAKEN0/1` を読み数え直す（2543〜2545 行。今の code と同じく display.lock の中、Q03）。
6. session の状態: 犯人（FAULTED）と、(b) で仕事を失った session は **DEAD**（hardware の session は無いが、番号・chunk・勘定・mapping の記録は executor の close まで持つ。Q01）。CLOSING は RETIRED（close の ticket は (b) で ERROR に終わり、reset が成功したので返してよい）。OPEN は「再生待ち」。CP_HEAP の勘定を DEAD でない session の物だけで数え直す（reset は heap の割当を BASE に戻す、566 行）。`CTRL` = IRQ_GLOBAL | WDT_EN | RUN。display.lock を離す。liveness: driver の `CMD_FENCE`（attach の 10 と同じ）を置き、有界に待つ（flat は再生が無いので、CP の生死はこれで見る。Q07）。通らなければ 2 からやり直す（liveness の失敗の数に足す）。
7. （cp_lock の外で後に）DEAD にした session に `drv_gpu_report_session_error`。
8. **再生**（device.lock → cp_lock のまま、門を通らない）: まず session 0 の mapping（`vm[0]` の記録。表示の一時の写しなど）を `CMD_VM_MAP(0, …)` で置き直し（574 行・2781 行の「re-map Session 0」）、次に「再生待ち」の各 `ghci_vm` について `CMD_SESSION_OPEN(s)` と LIVE の mapping の `CMD_VM_MAP` を置き（RPTR を待ちながら。待ちの間に `recovering` が立てば 1 に戻る）、OPEN に戻す。flat は session も mapping も置かない。全部を置いた後、同じ区間で「再生待ちが残っていない」を確かめ、`recovering` = 0、`recovery_generation` を進める。
9. 門で待つ thread を起こし、lock を全部離す。

**LOST**: 以後の submit は `EIO`、`drv_gpu_report_error`。GPU がまだ DMA し得るので物理を返さない（`quarantined` に記録）。detach は Bus Master を切った後にだけ返す。LOST になる時（Q07）: (a) 2 の RESET が落ちない・AXI_STUCK、(b) 6 の liveness が続けて 3 回通らない、(c) **装置の fault**（犯人の無い物、hang を含む。MMU_OOM と計画の reset を除く）が `GHCI_FAULT_STORM_MS`（60 s。hang の判定 10 s と reset の待ちの和より長く取る）の中で 3 回。犯人の有る fault は数えない（犯人は DEAD で二度と submit しない。session を開き直して fault を繰り返す client は他の client の未完の仕事を DEVICE_LOST にし得る。隔離の無い間（WS292 まで）の制限、§23）。

**hang**（Q07）: 未完の submit があり、`GHCI_HANG_MS`（10 s）の間 fence の語も `RING_RPTR` も進まない時（1 つの packet が watchdog より短くても、合計の長い submit は RPTR が進むので hang にしない）。犯人は無し。

### 5.5 session と VM（p005、§11.2・§11.6）

- **session の番号**: 0 は driver、1〜15 を executor の session に。executor が `GPU_OP_CREATE_DEVICE`（vkCreateDevice）を受けた時に、その VkDevice 1 つに 1 つ取る（1 つの fd で VkDevice を 2 つ作れば 2 つ。Q21）。取るのは門を通った 1 区間で、番号と `CMD_SESSION_OPEN(s, 0)` を同時に（Q02）。RETIRED になるまで再利用しない（R16。fd を開いただけ・資源だけの session は番号を取らない。Venus の transport の blob は VM_MAP しない。device より前に作られた・import された resource は、その device の VkDeviceMemory に結ばれた時にその session に VM_MAP する）。16 番目は `EBUSY`、vkCreateDevice は `VK_ERROR_TOO_MANY_OBJECTS`（U11「hardware の上限の 15 個」）。`CAP_PROT` = 0・1 の device は同じに番号を使う（dynamic state は session ごと、845 行）。flat は全 client が session 0 で動くが、数の上限は同じ 15。
- **状態**（§6.2）: OPEN・CLOSING・RETIRED に、reset の後の **DEAD**（Q01）。DEAD の unmap・close は packet を置かず、reset の成功を ticket として即に完了し、物理・VA・chunk・勘定・番号を返す（close で RETIRED）。
- **VA**: v1 は 32 bit の範囲だけ使う（VA_BITS = 40 の device も上位を 0 に。`gpu_va_hi` = 0）。device に 1 つの **2 MiB の chunk の表**（2048 個、chunk 0 と最後は使わない）を持ち、chunk は 1 つの session だけが持つ。session の中は自分の chunk の上の range allocator（`range.c`）で取る。2 MiB を超える allocation は連続した chunk の run を first-fit で取り、取れなければ `ENOMEM`（詰め直しはしない、v1）。理由: CAP_PROT = 0 は page table を全 session で共有し（1852 行）、CAP_PROT = 1 は session ごと（1853 行）。chunk の持ち主を一つにすると (a) どちらでも VA が重ならず、(b) 1 つの L0 table（2 MiB）が 1 つの session だけに属し、CP_HEAP の勘定が上限で決まる。
- **CP_HEAP の勘定**（悲観の読み、R09・Q10、ghci-review 第 13 版 N48 と同じ読み）: CP の内部の割当は driver に見えず、root の大きさ（628 行の 4 KiB と 1701 行の 16 KiB）も CLOSE が table を返すかも仕様に無いので、**上限で数え、閉じた session の root と中間の table、unmap した chunk の L0 は reset まで戻らない**と見なす。session ごとに root と中間の table（VA_BITS 32 は root 16 KiB。40 は root 8 KiB と、4 GiB を覆う L1 の 4 KiB が 1 GiB ごとに最大 4 枚、計 24 KiB）、chunk を初めて map した時に L0 の 4 KiB。`GHCI_HEAP_BYTES` = 16 × 24 KiB + 2048 × 4 KiB = 8.375 MiB に、閉じた session の root 16 個分の余裕を足した **8.75 MiB**（取れなければ半分ずつ減らす）。勘定が尽きる要求（新しい session、新しい chunk）は、**計画の reset** を service に頼み、未完の submit が無くなるのを有界に待って reset と再生を行い（§5.4。bind と state は毎 submit 置くので失う物は無い、§11.3）、勘定を数え直してからやり直す。待ちが切れれば `ENOMEM`（Vulkan は OUT_OF_DEVICE_MEMORY）・`EBUSY`。MMU_OOM（KIND 8）が来れば §5.4 の reset で heap ごと回収し、犯人にせず嵐にも数えない。
- **map**: `ghci_memory_alloc` で物理 → 門を通った 1 区間（device.lock → cp_lock）で chunk と VA を取り `CMD_VM_MAP(s, flags, va, bus, bytes)` を置き、置けた時だけ mapping を LIVE として記録する（Q02）。完了は待たない（packet は in-order、706 行）。flags: host は READ | WRITE | COHERENT（WC は付けない、§11.4 の「GPU は coherence を外さない」）、code は | EXEC、local は | LOCAL。
- **unmap**: 門を通った 1 区間で `CMD_VM_UNMAP` と `CMD_FENCE` を置き、mapping を UNMAPPING（その ticket）にする。ticket が過ぎたら VA を返し、物理は他の mapping・hold が無ければ返す（685 行「fence で守る」）。RETRY の間は LIVE のまま物理を hold する。VM_UNMAP は GPU の drain を待つ（1723〜1728 行）ので、map・unmap は VkDeviceMemory・blob・import と executor の heap の単位だけにする（D14）。
- **close**（R01）: 失敗できない（`gpu.h`）。門を通った 1 区間で、LIVE の全 mapping の `CMD_VM_UNMAP` → `CMD_SESSION_CLOSE(s)` → `CMD_FENCE` の順に置き、`CLOSING`。ring が足りなければ cp_lock を持ったまま RPTR を待ち（ring より大きければ VM_UNMAP の途中で分ける）、RETRY なら最初から（reset の後は再生された session に置き直すか、DEAD なら packet 無し）。SESSION_CLOSE を最後の VM_UNMAP より前に置かない（N49）。その fence の ticket の後に物理・chunk・番号を返し `RETIRED`（勘定は悲観の読みで reset まで残す）。
- **flat（HW_WALKER = 0、U9）**: `CMD_SESSION_OPEN`・`CLOSE`・`VM_MAP`・`VM_UNMAP` は KICK_ABSENT の fault になる（1851 行）ので置かない。`ghci_vm` は数と mapping の記録のためだけに作り（`session_id` = 0、`flat` = 1）、map は VA = bus address（恒等、packet 無し、chunk も CP_HEAP の勘定も使わない）、unmap は packet 無しで、cp_lock の中で読んだ「今まで置いた最後の fence の seq」を ticket にする（Q21。GPU が参照し得る submit は全部その前）。VA = bus なので executor は VA を 64 bit で持ち、32 bit に入らない番地の物は flat では確保しない（host は 4 GiB 未満、D8。discrete の flat は v1 では local を使わず unified と同じ扱い）。全 client が session 0 を共有するので、executor は submit に要る bind と dynamic state を全部その submit の中で置く（§11.3 の 2）。
- **番地の上限**: GPU に渡す host memory は bus address が `2^PA_BITS` 未満。v1 は全部を 4 GiB 未満（MEM の HOST_LIMIT が 0 でなければその下）に取る（`kern_pmem_alloc_limited`。CP_HEAP・FB・flip の record は 32 bit の物理なので必須、他は簡単のため。D8）。

### 5.6 unified と discrete（p005・p011、§11.3）

| | unified（`CAP_UMA` = 1、または MEM が無いか LOCAL_SIZE = 0） | discrete（`CAP_UMA` = 0 かつ MEM の LOCAL_SIZE > 0、flat でない） |
| --- | --- | --- |
| ring・fence・PSO・descriptor・code・render pass・query・event | host | host（D7） |
| Vulkan の host visible | host | host |
| Vulkan の device local だけ | host | local（p011、`VM_FLAG_LOCAL`）。0 埋めは `CMD_FILL_BUFFER`（CAP_BLT。無ければ §11.6 の compute の fill） |
| heap | 1 つ（DEVICE_LOCAL・HOST_VISIBLE・HOST_COHERENT） | 2 つ（p011。それまでは unified と同じ 1 つ） |

`CAP_UMA` = 0 で MEM が無い時は「MEM が無い GPU は unified」（1793 行）に従い unified として扱い log（DEVICE_CAPS が正という 270 行と食い違うが、確保の場所が host だけになるだけで安全）。

### 5.7 detach（p004）

session が残れば `EBUSY`。(1) p012 の後: 表示が driver の framebuffer を scan していれば boot framebuffer に戻すか ENABLE = 0 にして `STATUS.idle` を待つ。(2) RUNNING なら `CTRL.HALT` → `STATUS.HALTED` を有界に待つ（FAULTED・LOST は飛ばす）。(3) `CTRL.RESET`、`IRQ_ENABLE` = 0、`CTRL` = 0。(4) Bus Master を切る（表示が boot framebuffer を scan していれば切らず、ring・heap を参照しない状態（RESET 済みで RUN = 0）で残す）。(5) thread を止め、ring・fence・CP_HEAP・quarantine の物理を返す。

## 6. 状態遷移

### 6.1 device

```
ATTACHING --identify・reset・liveness OK--> RUNNING
RUNNING --IRQ_FAULT・IRQ_WATCHDOG（FAULTED に依らず）・hang・計画の reset--> RECOVERING --reset と liveness OK--> REPLAYING --> RUNNING
RECOVERING --liveness の失敗--> RECOVERING（3 回続けば LOST）
REPLAYING --fault・hang・ring の待ちの timeout--> RECOVERING（再生をやり直す。R34）
RECOVERING --RESET が落ちない・AXI_STUCK--> LOST
RUNNING|REPLAYING --60 s に犯人の無い装置の fault 3 回（MMU_OOM・計画の reset を除く）--> LOST
RUNNING|LOST --stop（session 0 個）--> DETACHED
```

driver は `STATUS.RUNNING`・`HALTED`・`FAULTED`・`AXI_STUCK`・`CP_STATE` と自分の `CTRL` の書き込みで判断する（§3.3 の M10 の規則）。

### 6.2 session（ghci_vm）

`(無し)` → `OPEN`（番号を取り SESSION_OPEN を置いた、同じ区間）→ `CLOSING`（VM_UNMAP・SESSION_CLOSE・FENCE を置いた）→ `RETIRED`（ticket の後、番号・chunk を返す）。`OPEN` → `FAULTED`（§5.4 の 1 の犯人）→ reset で `DEAD`。reset で、仕事を失った `OPEN` も `DEAD`、他の `OPEN` は再生されて `OPEN`、`CLOSING` は `RETIRED`。`DEAD` → executor の close で packet 無しに `RETIRED`（Q01）。flat の `ghci_vm` は session の packet を置かず、close で直に `CLOSING`（最後の fence の ticket）→ `RETIRED`。

### 6.3 allocation と mapping

```
allocation: ALLOCATED --map--> MAPPED(1..n) --mapping も hold も無い--> FREED
mapping:    （門の区間で VM_MAP を置いた）LIVE --門の区間で VM_UNMAP を置いた--> UNMAPPING(ticket) --passed--> gone
            DEAD の session の mapping --unmap--> gone（packet 無し、reset の成功が ticket）
LOST の device: どの状態からも FREED にしない（quarantine）
```

## 7. 資源の所有権と寿命

| 資源 | 持ち主 | 返す時 |
| --- | --- | --- |
| ring・fence page・CP_HEAP | device | detach の Bus Master を切った後 |
| session の番号・chunk | `ghci_vm` | `RETIRED`（SESSION_CLOSE の ticket の後か、DEAD・CLOSING の executor の close の時） |
| CP_HEAP の勘定 | device | reset の数え直し（§5.5 の悲観の読み） |
| VA | session の allocator | UNMAPPING の ticket の後 |
| resource・blob の物理 | drv_gpu の session（export の record も hold） | 全 mapping の unmap の ticket の後 |
| executor の heap（session ごと） | vk の session | session の close（heap の unmap の ticket の後） |
| PSO・descriptor・code・render pass・一時の memory | heap の区画 | destroy・submit の後、その session の最後の fence の後。unmap はしない |
| boot framebuffer の範囲 | firmware（driver は確保から外すだけ） | 最初の自分の flip が取られた後（p012） |

規則: GPU が参照し得る物は参照を含む submit の fence が過ぎるまで返さない。reset で終わった ticket は reset が成功した時だけ返す。回収は service thread。executor の heap・PSO・descriptor・code は `resource_map` の対象にしない（client が kernel の検査した物を書き換えないように）。

## 8. 並行性とロック

順: `device.lock`（mutex: session、allocator、chunk、mapping、寿命、heap の勘定）→ `display.lock`（mutex: 表示）→ `cp_lock`（mutex: ring、doorbell、reset、`submit_log`）→ `irq_lock`（spin: completed、generation、`recovering`・`recovery_generation`、`faulted`、fault の記録、service の仕事）。外側の lock を後から取らない（離すのは順に依らない）。
- ring に置く経路は §5.2 の門を通り、待つ時は持つ lock を全部離す（device.lock・display.lock も。Q02・Q03）。門の前に状態を記録しない。
- 回復（§5.4）は service thread だけで、段ごとの lock は §5.4 の表。表示の数え直しは今の code と同じく display.lock → cp_lock（`cp.c` の `ghci_display_recover_begin`、Q03）。
- ring の空きの待ちは cp_lock（VM の操作では device.lock も）を持ったまま、上限 `GHCI_RING_WAIT_MS` で、`recovering` で即に抜ける（Q08）。
- 内部の shader（§11.6）の compile は device.lock・cp_lock の外で行い、結果を device.lock の中で登録する。
- host 試験の stub は lock の順の違反と「門で lock を持ったまま待つ」を FAIL にする（§14）。

## 9. 割り込みと DMA

### 9.1 割り込み

- MSI・MSI-X は 1 本、INTx は予備、無ければ poll（今の `take_interrupt`）。MSI は `IRQ_STATUS & IRQ_ENABLE` が 0 → 1 の時と、IRQ_STATUS・IRQ_ENABLE を書いた後にまだ 1 の時に来る（2639 行）ので、handler の「mask → 読む → W1C → unmask」で取りこぼさない。
- `CTRL.IRQ_GLOBAL` は start と reset の後（reset が消す、327 行）に書き、handler では触らない。
- poll の device は IRQ_ENABLE = 0 のまま IRQ_STATUS を読む（IRQ_STATUS は ENABLE に依らず latch する、389 行）。

### 9.2 DMA と coherence（§11.4）

host memory は `kern_pmem_alloc_limited` の連続の run（D8）。CPU の view は直の map か uncached の alias（今の `host_view`・`host_uncached`、`CONFIG_GHCI_HOST_UNCACHED`）。bus address は物理と同じ（amd64・VF2）。**VF2 の M.2 の PCIe の DMA は L2 を通る coherent**（ws288-p044 の 2 段目: uncached の alias で書いた物を GPU が見ない、`plan/ws288/tests/config-visionfive2-ghci.mk` の `CONFIG_GHCI_HOST_UNCACHED := n`、`VF2_DMA_DEFAULT_COHERENT`）なので、VF2 でも cached の直の map を使う（`kern_pmem_map_uncached` の記憶は VF2 の内蔵の GPU の物で、PCIe には当たらない）。`CONFIG_GHCI_HOST_UNCACHED` = y の非 coherent な platform だけ、HOST_COHERENT として出す user の VkDeviceMemory・blob の `resource_map` を `DRV_GPU_MAPPING_UNCACHED_RAM`（`gpu.h`）にする（R19）。

### 9.3 doorbell の順（§6.3）

ring へ書く → store barrier → `RING_WPTR`（dword の index）。非 coherent な platform では ring・fence page を uncached の view で触る（今の形）。dword は little endian で書く関数を通す。

## 10. 失敗と回復（まとめ）

| 失敗 | 検出 | 回復 | client に見える物 |
| --- | --- | --- | --- |
| 識別（MAGIC・major・reset・MEM_READY・SCRATCH・liveness） | attach | attach しない | /dev/gpuN が無い、log |
| fault・watchdog（FAULTED に依らない、U5） | handler | §5.4 の reset と再生 | 犯人と未完の仕事を失った session は DEVICE_LOST、他は続く |
| hang | service | 同上（犯人は無し） | 同上 |
| reset の失敗・AXI_STUCK・犯人の無い fault の嵐 | §5.4 | LOST（物理を返さない） | 以後 EIO |
| ring の満 | ring_plan | cp_lock を持ったまま有界に待つ | timeout で submit の失敗 |
| VM_UNMAP を置けない | vm.c | UNMAP_WANTED で hold、service が置き直す | 無し |
| VA・chunk・CP_HEAP の勘定・物理の不足 | allocator | 遅延の解放を回収し 1 回やり直す | `ENOMEM` |
| 16 個目の executor の session（U11） | vm.c | 断る | `EBUSY`（vkCreateDevice の失敗） |
| compile の失敗・flag の無い機能の要求 | compiler・vk-caps | 作らない・断る（U8「1.0 を名乗り、断って log」） | Vulkan の結果と log |

## 11. Vulkan の executor（`vk/`）

### 11.1 方針

Vulkan の object を kernel が持ち、記録を submit で `CMD_*` に直す（amdrdna の `vk/` と同じ型、D3）。wire の decode は i915 の codec（`vk-codec.c`）を共有。executor の session 1 つに GHCI の session 1 つと **executor の heap**（session の VA に一度 map し sub-allocate、足りなければ倍の heap を足す）。

### 11.2 file

| file | 役 |
| --- | --- |
| `vk.h` | 型 |
| `vk-stream.c`・`vk-node.*` | transport・capset・submit・drain・jobs・stop・fault（今の物を直す） |
| `vk-object.c`・`vk-reply.c` | wire の id → object、create・destroy の返事 |
| `vk-caps.c` | `struct ghci_caps` → feature・limit・queue・memory の型・format（§15） |
| `vk-device.c`・`vk-format.c` | instance・physical device・device・queue の答え、format の大きさ |
| `vk-memory.c` | VkDeviceMemory（物理 + VM_MAP）、buffer・image（`layout.h`）、executor の heap |
| `vk-layout.c` | descriptor set layout・pipeline layout・pool・set・update（Vulkan の set の中身を CPU 側に持つ）と、draw の時の HW の table の組み立て（§11.4） |
| `vk-pipeline.c` | shader module・cache・graphics・compute pipeline: compile → code を heap に（EXEC）→ PSO を焼く、slot の表 |
| `vk-pass.c` | render pass・framebuffer → subpass ごとの `GhciRenderPassDesc` |
| `vk-command.c`・`vk-record.c` | pool・buffer、`vkCmd*` の記録（引数を全部読み、object を hold） |
| `vk-execute.c` | 記録 → `CMD_*`（bind の検査、HW の table、LOAD_CONST、転送） |
| `vk-queue.c` | `vkQueueSubmit`、fence・semaphore・event・query |
| `vk-meta.c` | 転送の内部の shader（§11.6、CAP_BLT = 0 と CopyQueryPoolResults） |

### 11.3 submit

1. 各 command buffer の記録を `vk-execute.c` が順に `CMD_*` に直す。列の頭に `CMD_BARRIER`（INVAL_ICACHE | INVAL_TEX | INVAL_L1_DATA | INVAL_DESCRIPTOR）を置く（host が書いた code・descriptor・buffer を見せる、§11.5）。
2. bind の検査: draw・dispatch の前に、同じ command buffer の中で pipeline・使う set・要る dynamic state が bind 済みかを記録の時に検査し、欠ければ submit を断る。command buffer の頭で前の submit の bind を当てにしない（CP の bind は session ごとに持たれるが、他の submit が同じ session で変える）。
3. draw ごとの HW の table・constant は submit の一時の memory（heap の区画）に。
4. `ghci_cp_submit(vm, packets)`、最後の fence に job marker と VkFence を結ぶ。queue は 1 本（ring 1 本）で submit の順が全部の順。
5. 列の末に `CMD_BARRIER`（WAIT_IDLE | FLUSH_L1_DATA | FLUSH_L2）を置き、fence の前に GPU の書き込みを memory に出す（fence 自体も完全、1555〜1558 行）。

### 11.4 descriptor と constant の配置（driver の取り決め）

HW が決める物（§8.4）: texture の slot k は set 0 の k 番目（32 byte）と set 1 の k 番目（16 byte）の組、k = 0〜15。set 0〜3 の base は c24〜c31 に鏡。push constant は c0〜c23。driver が決める物（compiler と executor の取り決め、`compiler/gsi1-abi.h` に一か所）:

| HW の set | 中身 | 作る時 |
| --- | --- | --- |
| 0 | texture の descriptor 16 個（pipeline の slot の表の順） | draw・dispatch の時、bind された Vulkan の set と pipeline の slot の表から |
| 1 | sampler の descriptor 16 個（同じ slot） | 同上 |
| 2 | 資源の表: 16 byte の単位で、UBO・SSBO・texel buffer の buffer descriptor、storage image・input attachment の記述（32 byte の texture descriptor の写し） | 同上 |
| 3 | driver の表: scratch の base、framebuffer の大きさなど | pipeline ごと |

- 分かれた sampled image と sampler の組み合わせは、shader が使う組ごとに slot を 1 つ取る（compiler が組を数える）。slot は pipeline の全 stage で 16 個（`maxPerStageDescriptorSampledImages`・`Samplers` = 16、`maxDescriptorSetSampledImages`・`Samplers` も 16。Vulkan 1.0 の最小 96 に足りない: U8）。
- constant bank（512 語）: c0〜c23 = push constant の先頭 96 byte、c24〜c31 = HW の鏡、c32〜c39 = push constant の 96〜127 byte（`lwc` で読む）、c40〜 = dynamic offset（set ごとに順）。`maxPushConstantsSize` = 128。`CMD_LOAD_CONST` は 32 語ずつ。
- UBO は `lw` で読む（1.1 の bank への写しは無い）。`maxUniformBufferRange` 65536。

- **attachment の配置（R05、仮）**: §8.4 の attachment の記述に linear・tiled の flag も layer の pitch も無く、`mip_level`・`base_array_layer` から番地を求める方法も無い（ghci-review 第 13 版 N47、H）。答えまで: COLOR_ATTACHMENT・DEPTH_STENCIL_ATTACHMENT・INPUT_ATTACHMENT・TRANSFER_DST（blit・resolve の先）の usage の image は **linear**（§8.5.2 の 64 byte 揃えの行、subresource の配置は §8.5.2）に作り、記述の `*_va` には driver が §8.5.2 で求めた subresource（mip・layer）の先頭の番地を書き、`mip_level` = `base_array_layer` = 0、`layers` = 1。framebuffer の layers > 1 は layer 0 だけに描き（geometry shader が無いので layer を選ぶ shader は無い）、layer 1 以降の loadOp CLEAR と、ClearAttachments の baseArrayLayer > 0 は、layer ごとにその subresource の番地で clear だけの render pass を置く（Q12）。他の image は tiled にしてよい（texture の flags[0]）。答えが (b)（tiled の flag の追加）なら p008・p009 で attachment を tiled にできる。
- **PSO に無い静的な状態（R07）**: stencil の compare・write mask・reference、depth bias の constant・slope・clamp、blend constant、independentBlend の 0x07〜0x0E（CAP_MRT_BLEND = 1 の時だけ置く。0 で置けば KICK_ABSENT で reset になる、848 行。Q11）は session ごとに残り、BIND_PIPELINE が戻すのは dw9・dw11 だけ（845〜846 行）。executor は BIND_PIPELINE の度に、その pipeline の静的な値（dynamic にした物は記録された値）を `SET_DYNAMIC_STATE` で全部置く。stencil は 3 つを 1 語で書くので、executor が 3 つの値を command buffer の状態として持つ。
- **HW に field の無い Vulkan 1.0 の core（R08、U8）**: rasterizerDiscardEnable は、その pipeline の draw の scissor を 0×0 にする。sampler の unnormalizedCoordinates と image view の identity でない component swizzle は、その組を使う draw・dispatch を記録で断って log（U8）。fragment stage の無い pipeline は driver の 2 命令の FS（2432〜2434 行）を付ける。point と line（topology と polygonMode）の FS は入力の作り方が違う（2475〜2479 行）ので、FS の compile の key に primitive の種類（三角形・線・点）を入れる。
- **WaitEvents（R10・Q13）**: 単一の ring で `WAIT_FENCE` が来ない SetEvent を待つと、他の client と表示が止まり watchdog にも掛からない（1598 行）。そこで WaitEvents の event ごとに、(a) 同じ submit の前に GPU の SetEvent があれば `WAIT_FENCE`（event の語、値 1）、(b) 無ければ submit をそこで切り、残りと**その client の後の submit 全部**（submission order を保つ）を kernel が保留し、event の語が 1 になった時（host の vkSetEvent か、前の submit の GPU の SetEvent。fence の IRQ ごとに event の語を読む）に続きを置く。保留の残りは、reset で DEAD になった session と executor の close では ERROR で完了する。
- **timestamp の availability（R12）**: `WRITE_TIMESTAMP` は 8 byte だけ書く（975〜982 行）ので、直後に `CMD_FENCE`（MEM_WRITE、IRQ 無し、値 1）を slot の bus 番地 + 8 に置く（query pool は kernel の持つ host memory）。CopyQueryPoolResults の WAIT_BIT は前に `BARRIER`（WAIT_IDLE）を置く。
- **triangle fan（R30・Q18）**: 非 indexed の fan は executor が一時の index buffer（submit の一時の memory）を作る。三角形 i の頂点は Vulkan の順（i + 1, i + 2, 0）で、provoking は i + 1（flat の値を変えない）。draw の後に client の index buffer の bind を置き直す。indexed・indirect の fan は断る。
- **断り方（U8・Q14）**: pipeline・sampler・image などの create で分かる不足は、その create を Vulkan の結果（`VK_ERROR_FEATURE_NOT_PRESENT`・`VK_ERROR_FORMAT_NOT_SUPPORTED`、pipeline は `VK_ERROR_OUT_OF_DEVICE_MEMORY`）と log で断る。記録（vkCmd*、void で Venus は非同期）で分かる不足は、**その command を飛ばして log**（1 つの command buffer に 1 回）し、command buffer と device はそのまま使える。DEVICE_LOST にはしない。
- **stage ごとの上限（Q15）**: texture の slot（set 0・1 の 16 個）は pipeline の全 stage で共有なので、`maxPerStageDescriptorSampledImages`・`Samplers` に 16 を答えても、VS と FS の和が 16 を超える pipeline は作れない（create で断る、U8 の一覧に載せる）。他の per-stage の値は §8.6（UBO 16・SSBO 16・storage image 8）と input attachment 4（§15.5）。
- **robustBufferAccess（Q16）**: Vulkan 1.0 の必須の feature なので true と答える。shader の buffer の読み書きは compiler が descriptor の size で検査する。vertex fetch の範囲外は `BIND_VERTEX_BUFFERS` の `size_bytes` で VF が抑えると読む（仕様に無い、ghci-review 第 13 版 N59(3)。推測）。違えば PAGE_FAULT で reset になる（U5）ので、p009 に範囲外の vertex の場面を置き、p014 の実機で確かめる。

### 11.5 Vulkan の command と GHCI の command

| Vulkan | GHCI | 無い時（flag） |
| --- | --- | --- |
| BindPipeline・VertexBuffers・IndexBuffer | `BIND_PIPELINE`・`BIND_VERTEX_BUFFERS`（stride は動的に）・`BIND_INDEX_BUFFER` | — |
| BindDescriptorSets・PushConstants | §11.4 の table と `BIND_DESCRIPTOR_TABLE` ×4・`LOAD_CONST` | — |
| SetViewport・Scissor・DepthBias・BlendConstants・StencilCompareMask・WriteMask・Reference・LineWidth | `SET_VIEWPORT`・`SET_SCISSOR`・`SET_DYNAMIC_STATE`（stencil は 3 つを合わせた 1 語） | — |
| BeginRenderPass・NextSubpass・EndRenderPass | subpass ごとに `BEGIN_RENDER_PASS`（192 byte の記述、clear は `clear_color_table_va` で 128 bit）・`END_RENDER_PASS`。subpass の間は store → load。attachment の番地は下の「attachment の配置」。color 0 の resolve は記述の `resolve`（1398 行）、color 1〜7 の subpass resolve は render pass の後に `RESOLVE_IMAGE`（CAP_BLT = 0 は §11.6）。R13 | — |
| Draw・DrawIndexed | `DRAW`・`DRAW_INDEXED`。triangle fan は §11.4 の後の注（R30） | — |
| DrawIndirect・DrawIndexedIndirect | `DRAW_INDIRECT`（7 dw） | CAP_INDIRECT_DRAW = 0: 記録で断る（U8） |
| Dispatch・DispatchIndirect | `DISPATCH`・`DISPATCH_INDIRECT` | DispatchIndirect は CAP_INDIRECT_DRAW と NUM_SU の両方が要る（N52 の仮）。無ければ U8 で飛ばして log（Q19） |
| PipelineBarrier・WaitEvents | `BARRIER`（stage と cache の flag へ写す）。host の SetEvent を待つ時は `WAIT_FENCE`（event の 64 bit の語の bus address、値 1） | — |
| SetEvent・ResetEvent | `FENCE`（MEM_WRITE、IRQ 無し、event の語に 1・0）。event の memory は host で driver が持つ | — |
| BeginQuery・EndQuery | `BEGIN_QUERY`・`END_QUERY`（AVAILABILITY、16 byte の slot、PRECISE は flag から） | — |
| WriteTimestamp | `WRITE_TIMESTAMP`（flags[0] = 0、pool の VA） | — |
| ResetQueryPool | `FILL_BUFFER` | CAP_BLT = 0: §11.6 の fill |
| UpdateBuffer・FillBuffer | heap に写して `COPY_BUFFER`・`FILL_BUFFER` | CAP_BLT = 0: §11.6 の copy・fill（U10） |
| CopyBuffer・CopyImage・BlitImage・ResolveImage・CopyBufferToImage・CopyImageToBuffer・CopyQueryPoolResults | `COPY_*`・`BLIT`・`RESOLVE_IMAGE`（slice ごと）。CopyQueryPoolResults は §11.6 の compute（BLT の有無に依らない。64・32 bit と availability の詰め直しは COPY_BUFFER でできない）。R11: `COPY_BUFFER` は 4 byte 単位（992 行）なので、4 byte に揃わない offset・size の CopyBuffer は揃った中央を `COPY_BUFFER`、端を §11.6 の compute で行う。CopyBufferToImage・CopyImageToBuffer は slice（3D の z・array の layer）ごとに 1 packet、bufferRowLength・bufferImageHeight から buffer の row pitch と slice の offset を求める。圧縮 format の座標と大きさは texel（4 の倍数）で書く（単位は仕様に無い、ghci-review 第 13 版 N59。仮）。D24S8・D32S8 の 1 つの aspect だけの写しは §11.6 の compute（packet は aspect を選べない） | CAP_BLT = 0: §11.6 の shader で代える（U10「shader で代える」）。shader で扱えない format は断って log（U8） |
| ClearColorImage・ClearDepthStencilImage | color・depth の attachment にできる format なら clear だけの render pass。他は断る | — |
| ClearAttachments | render pass を区切り、全体の rect なら clear で開き直す。部分の rect は v1 では断って log（U8。§11.6 の描画で後に足せる、R31） | — |
| ExecuteCommands | 記録を展開（D6） | — |

### 11.6 転送の shader による代替（`vk-meta.c`、U10）

CAP_BLT = 0 の device（と BLT の有無に依らない CopyQueryPoolResults）は、転送を driver の内部の shader で行う（U10「shader で代える」）。

- **作り方**: 内部の shader は共有の IR（`drv_gpu_shader_ir`、amdrdna の `vk-quad.c` と同じ作り方）を C で組み、GSI1 の compiler（p006）で attach の後の最初の使用の時に一度 compile して device に持つ（session 0 でなく、使う executor の session の heap に code を置く。session の VA で動くため）。SPIR-V の blob は持たない。
- **compute で行う物**（`CMD_DISPATCH`、workgroup 64 lane）: CopyBuffer・UpdateBuffer（heap に写した data から）・FillBuffer・ResetQueryPool（0 の fill）・CopyQueryPoolResults（16 byte の slot → 32・64 bit と availability）・CopyBufferToImage・CopyImageToBuffer・CopyImage（同じ texel の大きさの非圧縮・圧縮の block の写し）・（resolve は下の描画で行う、R13）。番地は image の layout（`layout.h`、§8.5.2 の linear と tiled）を shader が計算する（compiler の storage image の番地の計算と同じ式を一か所から使う）。GSI1 の global memory は word の読み書きだけ（§12.8）なので、4 byte に揃わない端の word は `atom` の and・or で byte を書く（他の byte を壊さない）。
- **描画で行う物**: ResolveImage と color 1〜7 の subpass resolve（R13）は、dst を color attachment にした 1 枚の三角形の render pass で、FS が src の各 sample を多 sample の texture として読んで平均し（2452〜2459 行。整数の format は sample 0）、format の encode（sRGB・RGB565・f16）は ROP に任せる。depth・stencil の resolve は Vulkan 1.0 に無い。BlitImage（拡大・縮小・反転・NEAREST・LINEAR）は、src を texture の slot 0 に、dst を color attachment にした 1 枚の三角形の render pass で行う（format の変換と filter は TU と ROP がする）。dst が color attachment にできない format（FMT の COLOR_ATTACHMENT の bit が無い）、depth・stencil の blit（Vulkan は NEAREST の写しだけ）は compute の texel の写しに落とし、それもできない format は断って log。
- **descriptor・constant**: 内部の shader も §11.4 の取り決め（set 2 の buffer descriptor、set 0・1 の slot、c0〜 の push constant に region の値）で資源を受ける。executor の普通の pipeline と同じ経路で PSO を焼く。
- **同期**: 転送の前後の `CMD_BARRIER` は Vulkan の barrier の写しのまま（転送の stage を compute・color attachment の stage と読む）。転送は client の session で動くので、client の bind（pipeline・table・constant・dynamic state）を壊す。executor は転送の後に、次の draw・dispatch の前に client の bind を置き直す（記録の時に「転送の後は bind を全部 dirty」にする）。
- **試験**（p010）: `no-blt` の profile で、model が受けた `CMD_DISPATCH`・`BEGIN_RENDER_PASS` の列と PSO・descriptor の byte を見る。compute の内部の shader は、U2 で写す amaterrace の `tools/gsi1model.py`（compute の dispatch を走らせる参照の model。texture は扱わない）で走らせ、結果の bytes を手で書いた期待値と比べる（端の byte、tiled の image、query の詰め直し）。描画の blit は packet と byte だけ（実際の画素は p014 の実機）。

## 12. compiler（`compiler/`）

§12.10 で program は header も interface 表も無い純粋な code になり（2247〜2259 行）、資源は descriptor table と constant bank で結ぶ（2261〜2281 行）。直す所:

1. **出力**: `struct ghci_gsi1_program` は code の語、`used_regs`、`lds_bytes`、`local_size`、flag（depth export・discard・centroid・sample shading）、varying の数、そして **executor への資源の表**（GPU には渡さない: texture・sampler の slot の組、set 2 の各 entry の Vulkan の set・binding・要素・種類、dynamic offset の語、push constant の範囲、scratch の bytes per warp）を持つ。header・interface 表を作る code（`gsi1_add_interface` など）を捨てる。
2. **資源の読み方**: UBO・SSBO・texel buffer は set 2 の buffer descriptor（c28〜c29 の base + entry の offset）を `ld.64` で読み、`lw`・`sw`・`atom` で触る（robustBufferAccess は size で検査）。storage image・input attachment は set 2 の 32 byte の記述から番地を計算（今の kind 9・10 の計算を移す）。sampled image は slot（§11.4）。push constant と dynamic offset は §11.4 の bank の語。scratch の base は set 3 の語。
3. **stage の I/O**: cf8be08 §12.10.3（cf8be08 の 2283〜2392 行）に従う。vertex の属性は PSO の slot i（format ≠ 0 が有効）ごとに、前の有効な slot の成分の数の累積和 base_i から C_i 個（format の成分の数）の register r[base_i..base_i + C_i − 1] に届く（足りない成分の (0, 0, 0, 1) は compiler が補う。合計 60 を超える pipeline は driver が作成で断り、`VK_ERROR_OUT_OF_DEVICE_MEMORY` と log）。driver は SPIR-V の Location を slot 0 から詰めて付け直し、その表を compiler に渡す。VertexIndex r60・InstanceIndex r61 は `used_regs_vs` ≥ 62 の時だけ届く（使う program は 62 以上にする）、r62 は compiler の物。export は POS 0x000〜0x003・POINT_SIZE 0x004（無視、大きさ 1）・VARYING 0x010 + w（w = 4L + c、PSO dw13 `varying_count` は vec4 の数）。`lda` の即値は [8:2] PT word（0〜3 = clip の POS、4 + w = varying）・[1:0] 頂点（vertex k は b_k の頂点、Vulkan の順、vertex 0 が provoking で flat はそれを読む）。補間は b0〜b2 と `lda` の積和、noperspective と FragCoord.w は word 3 の w から compiler が作る。clip・割り算・viewport は Vulkan の規則（depth clamp は PSO flags[4]）。fragment の入力 r0〜r9、export COLOR 0x100 + 16k + c（k = 0〜7）・DEPTH 0x104・SAMPLE_MASK 0x105・COLOR0_SRC1 0x108〜0x10B。値は ghci_cmd.h の `GHCI_VS_REG_*`・`GHCI_FS_REG_*`・`GHCI_EXP_*` と照合し、`gsi1-abi.h` の一か所に置く。
4. **options**: registers = min(63, `RESIDENT_WARPS[23:16]`（0 なら 63）)、samples は CAP_MSAA の時 4（2・4）、sample mask の入力は CAP_MSAA、subgroup・fp16 の命令は出さない（Vulkan 1.0 の SPIR-V では使えない。WS226 で）。
5. **捨てる制約**: §12.9「Not in version 1」（anisotropic・cube array・MinLod・gather の動的 offset）は compiler が断る（今と同じ）。
6. 試験: `plan/ws288/tests/gsi1-*`・`gsi1/` を `plan/ws290/tests/` へ移し、参照の python は U2 で `plan/ws290/tests/gsi1-tools/` に 89529de の物を写す。

## 13. 表示（DISP2）

- DISP2（EXTCAP ID 11）が有る時だけ display の capability を出す。DISP（ID 1、CP の無い controller）は扱わない（KC705 は DISP2）。
- 変える所は送り方: 1.1 の DISP_SET・FLIP の packet → 1.0 の `CMD_DISP_SET`（0x50、offset と値の組、flags[0] で idle 待ち）と `CMD_FLIP`（0x51、8 dw、base は 32 bit の物理、record は 32 bit の物理、flag VSYNC_WAIT・IMMEDIATE・PLANE_CURSOR・RECORD_ENABLE）。FB・cursor・record は session の VA ではなく物理（2518 行）なので、scanout の image は 4 GiB 未満の host memory（UMA）か local（discrete、p011）に取る。
- 事象: IRQ_STATUS[3] → `DISP_IRQ_STATUS`（+0x18: VSYNC・FLIP_DONE・CURSOR_FLIP_DONE・HOTPLUG・UNDERFLOW・DISP_ERROR）を読み W1C。`DISP_IRQ_ENABLE`（+0x1C）。
- 今の lease・present の shared と copied・queue の深さ 2・mailbox・record と TAKEN0/1 の数え直し・cursor の plane 1・hotplug・DISP_ERROR の数・idle の論理は残す。copied の present と cursor の staging の写しは、UMA は CPU、discrete は `COPY_BUFFER`（session 0 に一時の mapping、CAP_BLT）か CPU（local window。CAP_BLT = 0 の discrete はこちら。window は framebuffer の用で §11.1、毎 frame の写しには遅いが CAP_BLT の無い discrete の表示はこれで足りる）。
- panic console: `CTRL.HALT` → HALTED → host が S の register を書く（2565〜2567 行）。
- boot framebuffer（§13.3）: attach で ENABLE = 1 なら `FB_BASE`・`FB_STRIDE`・`FB_SIZE` の範囲を確保から外し（discrete は local の allocator からその範囲を外し window を保つ。UMA の host の物理は、boot の memory map（UEFI の GOP の framebuffer は予約の領域）が既に kernel の確保から外しているので driver は何もしない。boot の後に物理を予約する kern の API は無く、足さない。R29）、最初の自分の flip が取られた後に返す（discrete だけ）。p012 までは scanout に触らない（Guardrail）。
- adv7511.c（DC_CAPS[2] の外付けの送信器）は残す。

## 14. 試験の設計

- host 試験（ws.md の決定）と、実機（VF2 + KC705、§16）。1 thread の stub では競合が出ないので、R01（close の順）・R02（再生の窓の map・close・open）・R03（犯人の判定）・R06（fence が書かれ IRQ が未処理のままの fault）・R14（watchdog の 2 bit）は、stub の「待ちの途中の注入」で場面を作る。build: amd64 は `plan/ws290/tests/config-amd64-ghci.mk`、riscv64 は `config-visionfive2-ghci.mk`（各 Phase で warning 0）。出力は `BUILD=build/ws290-<phase>/`。
- 部品（`plan/ws290/tests/`）:

| file | 中身 |
| --- | --- |
| `ghci-words-host-test.c` | `cmd.c`・`check.c`・`caps.c`・`range.c`・`jobs.c`・`capset.c` を直に呼ぶ。期待値は ghci.md の表から手で書く（encoder の表から作らない） |
| `ghci-ref-host-test.c` | 独立の照合（R23）: 試験の時に `git -C ~/amaterrace-gpu show cf8be08:include/ghci_cmd.h`（1a1fa85 と同じ内容）と `cf8be08:include/ghci_regs.h` を `build/tmp/` の新しい directory に出し、host 試験だけがそれを include して、各 packet・PSO・descriptor・render pass の struct の `sizeof`・`offsetof` と、encoder が書いた byte の位置を比べる（driver には写さない。ghci.md と header が食い違う所（FAULT_INFO の配置など、第 13 版 N50・N60）と、header に struct の無い packet（BIND_VERTEX_BUFFERS の可変長、DISPATCH_INDIRECT、DISP_SET、NOP）は比べない一覧に入れる）。amaterrace-gpu が読めない時は **FAIL**（p002・p015 の受け入れが SKIP で通らないように。Q24） |
| `ghci-model.c`・`.h` | GHCI 1.0 の device の model: register（§3.1）と EXTCAP の block（profile で組む）、host と local の memory、doorbell で ring を読む CP（header・dwords・端・未知の opcode → kind 1、flag の無い command → kind 2）、`CMD_FENCE`（64 bit、MEM_WRITE・IRQ）、受けた `CMD_*` の記録、session（ACTIVE の bitmap、OPEN・CLOSE・BIND）、VM（session ごとか共有の表、PROT で切替、衝突 → kind 10、閉じた session → kind 1。flat は SESSION_OPEN(≥ 1)・VM_MAP・VM_UNMAP → kind 2 で VA = 物理）、PROT = 1 の client の fault（FAULTED = 0、その session の draw は NOP、fence は書く）。仕様に無い読みは model の切り替えにし、driver を悲観の側で試す: CLOSE が heap を返すか（N48）、PROT = 0 の CLOSE が共有の table の PTE を消すか（N49）、閉じた session への VM_UNMAP が fault か no-op か（N49）、VF の範囲外の読み（N59）、CP_HEAP の使用量（上限を超えれば MMU_OOM）、reset の保つ・消す、HALT、fault・watchdog の注入（session か装置）、IRQ の level、Bus Master が切れた間の DMA の検出、p012 で DISP2 |
| `ghci-stubs.c` | kernel の service と GPU core の stub（1 thread、偽の時計、lock の順の記録、待ちの途中の注入、`drv_gpu_register` の検査を `gpu.c` と同じに）。今の `plan/ws288/tests/ghci-driver-host-test.c` の stub の部分を移す |
| `ghci-driver-host-test.c`・`ghci-vk-host-test.c` | driver（と vk/・codec）+ model + stub の場面 |
| `ghci-host-test.sh` | build（`-std=gnu11 -Wall -Wextra -Werror -Wdeclaration-after-statement -fsanitize=address,undefined`、出力は `build/tmp/` の新しい directory）と実行、PASS/FAIL |

- capability の profile: `kc705`（am-ws003 の既定に当たる値。HW_WALKER・GSI1・NUM_SU 1・BLT・DISP、PROT・MSAA は profile の変種で。UMA・MEM・LOCAL_SIZE は KC705 の板の DDR3 から discrete と推測するが未確認なので、`kc705-uma` と `kc705-discrete` の 2 つを持ち、p014 の段 1 の読みで実機の値に合わせる。R24）、`minimal`（HW_WALKER・GSI1 だけ、EXTCAP 無し）、`full`、`flat`（HW_WALKER = 0、executor を出す）、`prot`（CAP_PROT = 1、PROT = 0 と同じ扱いになることを確かめる）、`no-blt`、`no-su`、`discrete`（UMA 0 と MEM）、`mismatch`（鏡の食い違い）。
- **flag ごとの差の試験**（p007）: §15.2 の各行で `full` とその flag だけ落とした profile の答えの差が、試験の側に手で書いた差とちょうど一致する。
- model は driver と同じ読み方で書かれるので、試験は「driver が ghci.md（と §19 の推し）に合う packet と register の順を作る」ことの証拠で、hardware の振る舞いの証拠ではない。

Phase ごとの場面（すべて PASS が受け入れ）:

| Phase | 場面 |
| --- | --- |
| p002 | 全 encoder（境界: NOP 255 超、vertex buffer 16、LOAD_CONST 32、DISP_SET n）、PSO・descriptor・render pass の byte、ring の計画（端・満・1 slot）、fence64 の比較（回り込み）、EXTCAP の走査（正常・環・窓の外・32 個）、caps の decode（全 bit、VA_BITS・PA_BITS の符号化、鏡の食い違い）、CP_HEAP の勘定の関数 |
| p003 | model の自己試験（手で組んだ列、reset の表、IRQ、VM の 3 つの段、session の fault と装置の fault の注入、MMU_OOM、Bus Master、stub の lock の順の検出） |
| p004 | attach（VERSION 0x00010000・0x00010003 を受け 0x00020000・0 を拒む、MAGIC、SCRATCH、MEM_READY、liveness の失敗 → EIO、FAULTED・RUNNING で残った device、no-su は executor を出さず flat・prot は出す、flat の CP_HEAP は 16 KiB）、register の順、submit・fence・IRQ・poll、4 KiB の ring で 1000 回、ring より大きい submit、prot の client の fault（FAULTED = 0）でも reset され、犯人の session だけが DEAD・他の session は再生されて続く、PROT = 1 の犯人の後の fence が SUCCESS にならない（Q05）、犯人の有る fault は嵐に数えない、fence が書かれ IRQ が未処理のまま fault（完了した submit は成功で終わり犯人にならない、R06・Q04）、session 0 の packet の watchdog（犯人無し、Q04）、reset の前に積まれた古い fault の事象を捨てる（Q06）、長い正当な submit を hang にしない・liveness が 3 回通らず LOST（Q07）、ring の空きを待つ間の fault で待ちが即に抜ける（Q08）、門で lock を持ったまま待たない（Q02・Q03）、装置の fault → reset → 古い ticket が ERROR → 次が動く、watchdog、hang、reset の失敗 → LOST、嵐 → LOST、detach、lock の順の違反 0 |
| p005 | session の open（16 個目 EBUSY）、VM_MAP（VA・bus・flag が model の表に一致、chunk の持ち主が一つ）、PROT 0 と 1 の両方で VA が重ならない、flat は packet 無しで VA = bus・unmap は最後の fence の ticket、CP_HEAP の勘定を超える要求が ENOMEM で MMU_OOM が起きない、unmap → fence の後に物理が返る、置き損じ → 置き直し、import、close → RETIRED まで番号を再利用しない、reset の後の再生（SESSION_OPEN と VM_MAP、ring より大きい再生、犯人の session は再生しない）、flat は再生しない、犯人（DEAD）の後の destroy・close と別の client の open で番号・VA が二重に使われない（Q01）、再生の途中の map・close・open が RETRY でやり直され重複の SESSION_OPEN・閉じた session への操作が出ない（Q02）、CP_HEAP の勘定が尽きた時の計画の reset（Q10） |
| p006 | gsi1 の回帰（移した試験）、code に header が無い、資源の表、§11.4 の bank と set の読み方、§12.10.3 の I/O（属性の詰め方と 60 の上限、r60・r61 と `used_regs_vs`、`lda` の word 0〜3、flat の vertex 0、MRT 8） |
| p007 | profile ごとの feature・limit・format・queue・memory の型、flag ごとの差、FMT の register を読む・無い時の表、vkCreateDevice の超過は FEATURE_NOT_PRESENT、memory・buffer・image、fence、capset、16 個目の device の失敗（U11）、flat・prot でも device が出る |
| p008 | 代表の pipeline（三角形・blend・depth・stencil・MSAA・compute）の PSO の 128 byte、slot の表、HW の set の byte、flag の無い profile で断る pipeline |
| p009 | 1 frame の記録 → model の `CMD_*` の列、indirect（no-indirect で断る）、barrier、転送（BLT の有る profile）、query・timestamp・event、clear、bind の無い draw を断る、大きな submit。no-blt の転送は p010 まで ENOTSUP で断る |
| p010 | no-blt: §11.6 の各転送の packet の列、compute の内部の shader の結果（gsi1model.py）、転送の後の client の bind の置き直し、shader で扱えない format を断る、CopyQueryPoolResults（BLT の有無の両方） |
| p011 | discrete: local の確保と FILL の 0 埋め、upload・readback、heap 2 つ、window と boot framebuffer |
| p012 | DISP2: mode、present（shared・copied）、queue、cursor、hotplug、error、reset の後の数え直し、panic の HALT、detach |
| p014 | 実機（§16.2） |
| p015 | 全 host 試験、amd64・riscv64 の build の warning 0、`git diff --check` |

## 15. capability と Vulkan への対応（`vk/vk-caps.c`）

### 15.1 読み方

`struct ghci_caps`（§4.3）を attach で一度作り、判定は全部 `ghci_caps_*` の小さな関数（`ghci_caps_has_blt(caps)` など）を通す（driver の他の file は bit を直に見ない）。重なる bit は DEVICE_CAPS（270〜271 行）。EXT_CAP_FEATURE の CAP_DUAL_SOURCE[8]・CAP_FP16[28] は鏡で、食い違いは log。MAX_MRT（FEATURE[19:16]）が 0 なら 1 と読む。

### 15.2 flag → Vulkan

分類: **V10** = Vulkan 1.0 の feature・limit・format・queue・memory として答える（WS290）。**V1x** = Vulkan 1.1 以降・extension（zedBSD の header と libvulkan に無い。WS226 で）。**DRV** = driver の中だけ。

| flag | 有る時 | 分類 | 無い時 |
| --- | --- | --- | --- |
| DEVICE_CAPS[0] UMA | heap 1 つ | V10 | discrete（§5.6） |
| [1] IOMMU | log | DRV | — |
| [2] HW_WALKER | VM（chunk・VM_MAP）、executor | DRV | flat: executor は出す（U9「どちらも出す」）、session 0 だけで VA = bus（§5.5）、隔離無しを log |
| [3] INDIRECT_DRAW | `multiDrawIndirect`・`drawIndirectFirstInstance`、`maxDrawIndirectCount` 2^32−1 | V10 | false、1。DrawIndirect を断る（U8） |
| [4] SECONDARY_RING | 使わない（D6） | DRV | — |
| [5] DUAL_SOURCE | `dualSrcBlend`、`maxFragmentDualSrcAttachments` 1 | V10 | 0、SRC1 の factor の pipeline を断る |
| [6] FP16 | `shaderFloat16` | V1x | — |
| [7] GSI1、[23:16] NUM_SU | executor | DRV | 出さない |
| [15:8] PHYS_LANES | log（subgroup は 32） | DRV | — |
| [24] PROT | WS290 は使わない（U5「全部 WS292」）。PROT = 0 と同じ扱い（どの fault も reset、§5.4）。PROT の block は log だけ | DRV | 同じ |
| [25] TEX_FFILT | float の format の FILTER_LINEAR | V10（format） | 落とす |
| [26] ROP_PACK | 整数・32 bit float の COLOR_ATTACHMENT | V10（format） | 落とす |
| [27]・[28] TASK・MESH | `taskShader`・`meshShader` | V1x | — |
| [29] BLT | 転送の command | DRV | §11.6 の shader で代える（U10「shader で代える」）。代えられない format は断って log |
| [30] DISP | display（DISP2 が list に在る時） | DRV | display を出さない |
| [31] MSAA | sample count 1・4（framebuffer・sampled image。2 sample は標準の位置が仕様に無いので出さない、N58）、`standardSampleLocations` = true（1 と 4、2408〜2411 行）。`sampleRateShading` は false（InterpolateAt* と interpolation offset を compiler が作らない、v1。Q17） | V10 | 1 だけ、false |
| FEATURE[3:0]・[7:4] | VA_BITS・PA_BITS | DRV | — |
| FEATURE[9]・[10]・[12] | COARSE_SCAN・FAST_CLEAR・EARLY_Z | DRV（log） | — |
| FEATURE[11] LOGIC_OP | `logicOp` | V10 | false、断る |
| FEATURE[13] HW_CURSOR | display の cursor（DC_CAPS[8] と両方） | DRV | 出さない |
| FEATURE[14] COLOR_LUT | 使わない | DRV | — |
| FEATURE[15] MRT_BLEND | `independentBlend`（SET_DYNAMIC_STATE 0x07〜0x0E） | V10 | false |
| FEATURE[19:16] MAX_MRT | `maxColorAttachments`・`maxFragmentOutputAttachments` = MAX_MRT（1〜8。export は k = 0〜7、CAP_ROP_PACK に依らない、§12.10.3） | V10 | — |
| FEATURE[27:20] LDS_KB | `maxComputeSharedMemorySize` = KiB × 1024（上限 16384、§8.6） | V10 | 0 なら Workgroup の変数の program を断る |
| FEATURE[29] SUBGROUP | subgroup | V1x | — |
| FMT0[6:0] BC1〜7 | BC の format | V10（format） | 落とす。`textureCompressionBC` は全部（BC6H を含む）が無いので false |
| FMT0[7] ETC2 | ETC2・EAC、`textureCompressionETC2` | V10 | false |
| FMT0[9] SRGB | sRGB の format | V10（format） | 落とす |

flag に依らない答え（§8.6 と本文）: `depthClamp`（PSO flags[4]）・`depthBiasClamp`・`fillModeNonSolid`・`occlusionQueryPrecise`・`fullDrawIndexUint32`・`robustBufferAccess`（compiler）・`fragmentStoresAndAtomics`・`vertexPipelineStoresAndAtomics` は true。`imageCubeArray`・`samplerAnisotropy`・`shaderImageGatherExtended`・`shaderResourceMinLod`・`depthBounds`・`alphaToOne`・`wideLines`・`largePoints`（`pointSizeRange` [1, 1]、§12.10.3 で POINT_SIZE は無視）・`geometryShader`・`tessellationShader`・`multiViewport`・64 bit・16 bit は false（§12.9 の「Not in version 1」と PSO に field が無い物）。limit は §8.6 の表、`maxViewports` 1、`timestampPeriod` = 1e9 / GCLK_HZ（kernel は float を使わないので wire の float の bit を整数で作る）、`timestampValidBits` 64、`maxComputeWorkGroupInvocations` 256、`maxComputeWorkGroupSize` {256, 256, 64}。vendorID・deviceID は PCI の ID。wire の `apiVersion` は 1.1 のまま（libvulkan の `instance.c` が wire の 1.1 未満を `VK_ERROR_INCOMPATIBLE_DRIVER` で拒み、application には 1.0 と書き換えて見せる。今の `DEVICE_API_VERSION`）。application から見える版は 1.0 で、U8「1.0 を名乗り、断って log」と合う。

### 15.3 memory の型

unified: heap 1 つ、型 1 つ（DEVICE_LOCAL・HOST_VISIBLE・HOST_COHERENT）。discrete（p011 の後）: heap 0 = local（DEVICE_LOCAL）、heap 1 = host（HOST_VISIBLE・HOST_COHERENT）、型 2 つ。p011 の前の discrete は unified と同じ 1 つ（libvulkan は DEVICE_LOCAL の型を要る）。

### 15.4 format

FMT の block が有れば、format ごとに `FMT_INDEX` を書き `FMT_OPTIMAL`・`FMT_LINEAR`・`FMT_BUFFER` を読む（501 行「A driver reports its format properties from these」）。GHCI の bit → Vulkan の bit: 0 SAMPLED_IMAGE、1 SAMPLED_IMAGE_FILTER_LINEAR、2 COLOR_ATTACHMENT、3 COLOR_ATTACHMENT_BLEND、4 DEPTH_STENCIL_ATTACHMENT、5 VERTEX_BUFFER、6 BLIT_SRC、7 BLIT_DST、8 STORAGE_IMAGE（488〜498 行）。FMT が無ければ §8.5.1 の表を「Guard」の flag で絞った物。どちらの場合も driver が実装できない bit を落とす（STORAGE_IMAGE は compiler の対応、sRGB は FMT0[9]）。BLIT_* は CAP_BLT = 1 なら FMT の値のまま、CAP_BLT = 0 なら driver の能力から作る（R04、U10）: BLIT_SRC は SAMPLED の format（LINEAR の filter は SAMPLED_IMAGE_FILTER_LINEAR の有る物だけ）、BLIT_DST は COLOR_ATTACHMENT の format（§11.6 の描画の blit）。depth・stencil の BLIT は Vulkan 1.0 で必須でないので CAP_BLT = 0 では出さない。FMT の読みは attach の時に一度、表の全部の format で行い `struct ghci_caps` の外の表に持つ（`FMT_INDEX` は host が書く唯一の index なので device.lock の中）。Vulkan 1.0 の必須の format が欠ける時は U8。

### 15.5 §8.6 に無い limit・feature の答え（R20）

§8.6 の表の値はそのまま答える（`maxColorAttachments` は MAX_MRT、`maxComputeSharedMemorySize` は LDS_KB、`timestampPeriod` は GCLK_HZ から）。表に無い物は次の値（Vulkan 1.0 の必須の範囲の中、出典は ghci.md の行か driver の取り決め）:

| 項目 | 値 | 根拠 |
| --- | --- | --- |
| `minStorageBufferOffsetAlignment` | 4 | GSI1 の global memory は word だけ（2121 行） |
| `minUniformBufferOffsetAlignment`・`minTexelBufferOffsetAlignment` | 64・16 | driver の取り決め（UBO は `lw`、texel は 16 byte の buffer descriptor） |
| `bufferImageGranularity` | 256 | tiled の subresource の 256 byte 揃え（1501 行） |
| `optimalBufferCopyOffsetAlignment`・`RowPitchAlignment` | 64・64 | 64 byte の行（1505 行） |
| `maxSamplerLodBias` | 15 | sampler の `lod_bias_fx8`（1290 行）と mip 15 段 |
| `minTexelOffset`・`maxTexelOffset` | −8・7 | 2183 行 |
| `minTexelGatherOffset`・`max` | −8・7 | 同上（gather の動的 offset は §12.9 で無し） |
| `maxFramebufferWidth`・`Height`・`Layers` | 16384・16384・256 | render pass の width・height（1371 行）。layers は Vulkan の最小 256 を答え、§11.4 の仮の扱いで layer 0 だけに描く |
| `maxViewportDimensions`・`viewportBoundsRange` | {16384, 16384}・[−32768, 32767] | 同上、Vulkan の最小 |
| `pointSizeRange`・`lineWidthRange` | [1, 1]・[1, 1] | POINT_SIZE は無視（§12.10.3）、wideLines は false |
| `maxDescriptorSetUniformBuffers`・`StorageBuffers`・`StorageImages`・`SampledImages`・`Samplers` | 72・24・24・16・16 | set 2 の表の大きさ（§11.4）で決める。sampled image と sampler は slot 16 個（U8 で Vulkan の最小 96 に足りない） |
| `maxDescriptorSetUniformBuffersDynamic`・`StorageBuffersDynamic` | 8・4 | c40〜 の dynamic offset の語（§11.4） |
| `maxPerStageResources`・`maxDescriptorSetInputAttachments` | 128・4 | Vulkan の最小 |
| `shaderSampledImageArrayDynamicIndexing` ほか dynamic indexing | false | texture の slot は即値 sub[3:0]（2161 行） |
| scratch の大きさ | `RESIDENT_WARPS[15:0]` × bytes per warp | `warp` の範囲（1967 行）。[15:0] を容量と読む仮（ghci-review 第 13 版 N54） |
| texel buffer の format | UNIFORM は `FMT_BUFFER` か §8.5.1 の Vertex Buffer の列で、compiler が decode できる物。STORAGE は compiler が encode できる物（p006 の表） | §8.5 |

## 16. platform と userland

### 16.1 platform

- amd64（PCIe）: build と host 試験。
- riscv64 VisionFive 2（M.2 の PCIe、`vf2-pcie.c` が `drv_pci_ghci_driver_register` を呼ぶ）: **実機で attach する**（2026-10-28 のユーザーの決定で U4 の「build だけ」を変えた）。割り込みは今の WS288 の VF2 の設定と同じく poll（`CONFIG_GHCI_POLL`）から始める。host memory は 4 GiB 未満（VF2 の DRAM は 0x4000_0000 から、§5.5）。PCIe の DMA は L2 を通り coherent なので、ring・fence・heap も cached の直の map（`CONFIG_GHCI_HOST_UNCACHED := n`、WS288 の VF2 の設定のまま。§9.2）。
- ZYBO・platform binding（§15.1）: 書かない（Future Work の候補）。
- endian: little endian の platform だけ。ring の dword・PSO・descriptor は LE で書く関数を通す。

### 16.2 実機の確かめ（p014）

ユーザーが VF2・SwitchBot・KC705 の書き込みを済ませた後に Q1 が割り当てる。1 本の script（image の build → 配置 → 起動 → 確かめ → 回収 → 要約）。段: (1) attach・SCRATCH・liveness の fence（p004 の後）、(2) session・VM_MAP・compute の dispatch（p005・p006・p008・p009 の後）、(3) 表示（p012 の後）。KC705 の firmware・RTL が 89529de の仕様に追いついていない所（§0 の `fw_cmd_bind_pipeline_pso`）は driver の誤りと分けて記録する（FAULT_INFO・DIAG_CP_PC を要約に）。判定は serial の対話と SSH（AGENTS.md §9）。

### 16.3 userland

`ghcictl`: `list`（名前・capability の要約・display の mode）と `display` を残し、`diag`・`paint`・`fwload` を外す（U6）。`ghci-blit` は削除、`ghci-display` は残す。

## 17. ライセンスと転記

| 出典 | SPDX | 扱い |
| --- | --- | --- |
| amaterrace-gpu `docs/ghci.md`（cf8be08） | Zlib | 値を driver の header に。block ごとに guardrail の形の複数行の comment「Values referenced: amaterrace-gpu cf8be08 docs/ghci.md (section N, 表・packet の名前)」（R22・Q23）。文章は写さない |
| 同 `include/ghci_regs.h`・`ghci_cmd.h` | Zlib、著作者は同じ | 写さない（float の field、struct の重ね、ghci.md と食い違う legacy の定義）。照合にだけ使う |
| 同 `include/gsi1_isa.h` | Zlib | bea1ef9 と今の `compiler/gsi1_isa.h` は 78 行違う（§0）。p006 で値を bea1ef9 に直し、header・interface の定義は使わなくなる（Q22） |
| 同 `tools/gsi1*.py` | Zlib | 試験の参照として cf8be08 の物を写す（U2。tools は 89529de から変わっていない） |
| Vulkan の値（VkFormat・VkBlendFactor など） | 仕様の事実 | `Values referenced:` |

GPL の参照は無い。他の driver（amdrdna）からは分け方だけを参考にし、code は写さない。

## 18. 実装の Phase の分割

8 LW ≈ 2〜3 時間。

| Phase | 内容 | 依存 | 受け入れ | LW |
| --- | --- | --- | --- | --- |
| p002 | 語の層: `ghci-regs.h`・`ghci-cmd.h`・`cmd.c`（§4.4 の全部）・`caps.c`（decode と CP_HEAP の勘定）・ring の計画と fence64・EXTCAP の走査（`check.c` に新しい名前で足す）。build に入る今の file は変えない（新しい file は build に入れない）。`ghci-ref-host-test.c`（§14、R23） | p001 | words と ref の試験 PASS、新しい file を amd64 と riscv64 の kernel の flag で compile して warning 0（R23）、`CONFIG_DRIVER_PCI_GHCI=y` の amd64 が今のまま build | 14 |
| p003 | model・stub・script（§14）。WS288 の 1.1 の試験の削除の list を Q1 へ | p002 | model の自己試験 PASS | 16 |
| p004 | core: pci-attach（match・Bus Master の順）、identify・caps・CP_HEAP（flat は 16 KiB）・ring・fence・IRQ・poll・watchdog、fault（FAULTED に依らず reset、犯人の記録、U5）・reset・LOST、detach。1.1 の file（firmware・native・state・blit・frame・packets・hcr）と vk/・display・compiler を build から外し、呼ぶ所を消す。capability は p004 で callback を全部実装した物だけを出す（`gpu.h` の「Optional capabilities require their complete callback pair」。resource・blob・map は p005 で足す。R17） | p003 | amd64・riscv64 の build の warning 0、p004 の場面 PASS、build に入る file に 1.1 の名前が無い（grep） | 22 |
| p005 | session・VM（chunk・勘定・map・unmap・close・再生、flat の恒等の VA）、memory（host、4 GiB の上限）、resource・blob・map・read・write、share・import。local は ENOTSUP。+ `SHARE | ALLOCATION_SHARE` | p004 | p005 の場面 PASS、build の warning 0 | 26 |
| p006 | compiler: header・interface 表を捨て §12 の ABI（`gsi1-abi.h`）、資源の表、options、`compiler/gsi1_isa.h` を bea1ef9 の値（`GSI1_COLOR_ATTACHMENTS` 8、`GSI1_EXP_COLOR0_SRC1`、`GSI1_VS_REG_*`、lda の word 0〜3）に直す（R18）、FS の key に primitive の種類（R08）、compiler を build に戻す、gsi1 の試験と tools を移す | p002・p004 | gsi1 の試験 PASS、build の warning 0。graphics の I/O は cf8be08 §12.10.3 で | 22 |
| p007 | executor の土台: vk.h・stream・node・object・reply・vk-caps・device・format（FMT）・memory（heap）・command・queue（空の submit、fence・semaphore・event）。libvulkan が送る op の全部（`userland/desktop/libvulkan/` の `GPU_OP_*`、1.1 の `*2`・`EXTERNAL_*`・`GET_DEVICE_QUEUE2`・`RECORD_QUEUE_TIMELINE` を含む）の表を作り、op ごとに答える・記録する・断るを決める（R21）。session の番号は `GPU_OP_CREATE_DEVICE` で取る（R16）。+ `CAPSET | COMMAND | NOTIFICATION | FENCE | JOB | JOB_CAPACITY` と recovery | p005・p006 | p007 の場面 PASS | 30 |
| p008 | pipeline と descriptor: vk-pipeline・vk-layout・layout.h | p007 | p008 の場面 PASS | 20 |
| p009 | 記録と実行: vk-pass・vk-record・vk-execute、draw・indirect・dispatch・barrier・dynamic state・転送・query・event・clear | p008 | p009 の場面 PASS、amd64・riscv64 の build の warning 0 | 36 |
| p010 | 転送の shader による代替（§11.6、U10）: `vk-meta.c`（内部の shader を IR で組む）、compute の copy・fill・update・query・buffer と image の写し・resolve、描画の blit、転送の後の bind の置き直し。gsi1model.py での試験 | p009 | p010 の場面 PASS、build の warning 0 | 20 |
| p011 | discrete: local memory、heap 2 つ、window と boot framebuffer | p005・p009・p010 | p011 の場面 PASS | 12 |
| p012 | 表示: DISP_SET・FLIP、DISP_IRQ、copied・cursor の写し、reset の数え直し、panic、boot framebuffer、adv7511。+ `DISPLAY | DISPLAY_EVENTS` | p007・p009・p011 | p012 の場面 PASS | 22 |
| p013 | userland と UAPI: `GPU_OP_GHCI_*`・`CONFIG_GHCI_NATIVE` を外す、ghcictl、ghci-blit を削除の list に | p004 | ghcictl の build、amd64 の build の warning 0 | 4 |
| p014 | 実機の確かめ（VF2 + KC705、§16.2）。段ごとに Q1 の割当で | p004（段 1）、p009（段 2）、p012（段 3）、ユーザーの hardware の用意 | 段ごとの要約。KC705 の側の未実装は記録して切り分ける | 12 |
| p015 | 規約の全文の見直し: WS が変えた C の全部、全 host 試験、amd64・riscv64 の warning 0、tree に 1.1 の code が無い grep、stub の `drv_gpu_register` を `gpu.c` と照合、試験を `plan/tools.md` に登録を Q1 に依頼 | p002〜p014 | 規約の違反 0、全 PASS、warning 0 | 10 |

合計 **266 LW**（版 4 の表の和は 246 で、本文の「234」は足し間違いだった。版 4.1 の差: U5 で session だけの回収が WS292 へ（p004 −2）、U9 で flat の恒等の VA（p005 +2）、U10 で転送の shader の代替の Phase（p010 新、+20）。版 3 の 212 からの差の内訳は git の履歴の版 4 を見る）。

依存の図:

```
p001 → p002 → p003 → p004 → p005 → p007 → p008 → p009 → p010 → p011 → p012
                      p004 → p006 → p007
              p002 → p006
                      p004 → p013
                      p004 → p014（段 1）、p009 → p014（段 2）、p012 → p014（段 3）
             p002..p014 → p015
外部（文脈、ここで実行しない）: ユーザーの hardware の用意 → p014
```

WS の受け入れ（ws.md）との対応: 1.1 の code が無い = p004・p007・p012・p013 の後に p015 の grep。amd64 の warning 0 = 各 Phase と p015。host 試験 = §14。規約 = p015。他の platform = §16（riscv64 は実機、p014）。

## 19. 仕様の欠け

版 4 の S01・S02 は N37〜N46 で解決した（第 12 版）。版 4.1 の design-reviewer で新しく 14 件が見つかり、**[ghci-review.md](ghci-review.md) 第 13 版（N47〜N60、2026-10-28 05:08）**として main の checkout に置いた（送るのはユーザーの指示の後）。各件の推しと driver の仮の扱いはそこにある。driver に効く物:

| N | 重さ | 内容 | driver の仮の扱い（design の節） |
| --- | --- | --- | --- |
| N47 | H | render pass の attachment の linear・tiled・layer の pitch・mip/layer の番地 | attachment は linear、`*_va` は subresource の先頭、mip・layer は 0（§11.4） |
| N48 | M | root の大きさ（4 KiB と 16 KiB）、CLOSE が root を返すか、CP_HEAP の再利用 | 上限＋余裕で数え、MMU_OOM は reset（§5.5） |
| N49 | M | PROT = 0 の SESSION_CLOSE と PTE、閉じた session への VM 操作 | 全 VM_UNMAP → CLOSE の順（§5.5） |
| N50 | M | FAULT_INFO の配置（md と ghci_regs.h）と kind の名前 | KIND・UNIT・SESSION_ID だけ解く（§5.4） |
| N51 | M | DRAW_INDIRECT の INDEXED の位置 | 7 dw は dword 6、8 dw は header（§4.4） |
| N52 | L | 0x2E の guard | CAP_INDIRECT_DRAW と NUM_SU の両方 |
| N53 | M | 「Vulkan 1.0 必須に適合」と必須の format の欠け | U8: 落として断る（§15.4） |
| N54 | M | RESIDENT_WARPS[15:0] の意味 | 容量と読む（§15.5） |
| N55〜N60 | L | PA_BITS の bit、TIMESTAMP の latch、IRQ_HALTED・INVALID_ARG、2 sample の位置、descriptor_count・圧縮の単位・VF の範囲外、ghci_regs.h の旧い定義 | 各件の推しの側（ghci-review.md） |

## 20. ghci.md 以外の判断（Q1・ユーザーへ）

| ID | 判断 | 推し | 状態 |
| --- | --- | --- | --- |
| U1 | `GPU_OP_GHCI_*` と `CONFIG_GHCI_NATIVE` を消す | 消す | 決定（2026-10-26「消してよいです」） |
| U2 | gsi1 の参照の python を写す | 89529de の 4 file を `plan/ws290/tests/gsi1-tools/` に | 決定（「写してよいです」） |
| U4 | riscv64 | 実機で attach する | 決定が変わった（2026-10-28、ws.md） |
| U5 | PROT | — | **決定**（2026-10-28 ユーザー「全部 WS292」: CAP_PROT=1 に関わる物は session の fault の回収も含めて WS292。WS290 では PROT=1 の device も PROT=0 と同じに扱う）→ §1.2・§5.4（どの fault も reset、犯人の session を sticky の error） |
| U6 | ghci-blit と ghcictl の diag・paint・fwload を消す | 消す | 決定 |
| U7 | ring を kernel だけが書く | kernel だけ | 決定（ghci.md も primary は KMD、590 行） |
| U8 | Vulkan 1.0 で隠せない不足（DrawIndirect に CAP_INDIRECT_DRAW、`maxDescriptorSetSampledImages` 16 < 96、MAX_MRT < 4 の時の `maxColorAttachments`、LDS が 16 KiB 未満、必須の format の欠け、shader で代えられない転送） | — | **決定**（2026-10-28「1.0 を名乗り、断って log」: Vulkan 1.0 の device として出し、出来ない操作は呼ばれた時に断って log に残す）→ §11.5・§15。断る所は `vk-caps.c` の 1 つの検査と記録・pipeline の作成。不足の一覧は attach の log と `ghcictl list` |
| U9 | 保護の段: HW_WALKER = 0（flat、隔離無し）と CAP_PROT = 0（VA を全 session で共有） | — | **決定**（2026-10-28「どちらも出す」: flat の device（HW_WALKER=0）にも PROT=0 の device にも Vulkan を出す。process 間の memory の保護は無くなる）→ §5.1 の 6・§5.5（flat は session 0 だけ、VA = bus） |
| U10 | CAP_BLT = 0 の転送 | — | **決定**（2026-10-28「shader で代える」: CAP_BLT が無い時、copy・blit・resolve を compute か描画の shader で代える）→ §11.6、Phase p010 |
| U11 | executor の session の数 | — | **決定**（2026-10-28「hardware の上限の 15 個」: client に使える 15 個まで、超えたら vkCreateDevice を断る）→ §5.5（flat も 15） |

## 21. 選ばなかった案と理由

| ID | 選んだ案 | 選ばなかった案 | 理由 |
| --- | --- | --- | --- |
| D1 | ring・PSO・descriptor は kernel | UMD | libvulkan を変えない、VM_MAP は特権 |
| D3 | amdrdna の型（記録 → packet） | 仮想の firmware | 1.0 の command は Vulkan の object を前提にしない |
| D5 | byte・dword を 1 つずつ書く encoder | include の struct を重ねる | 詰め物・endian・float、host 試験で比べやすい |
| D6 | 記録を primary に展開 | `CMD_EXECUTE_COMMANDS` | ring で足りる、後で最適化 |
| D7 | CPU が書き GPU が読む物は discrete でも host | local に置いて copy | 毎回の upload が要らず UMA と同じ形 |
| D8 | host memory は 4 GiB 未満の連続の run | 64 bit・不連続 | CP_HEAP・FB・record は 32 bit が必須、user の map は 1 つの範囲。広げるのは後 |
| D13 | VA は device の 2 MiB の chunk の表 + session の中の allocator | session ごとの独立の VA 空間 | CAP_PROT = 0 は table を共有し VA が重なると壊れる。chunk の持ち主を一つにすると CP_HEAP の上限が決まる |
| D14 | executor の heap を一度 map して sub-allocate | object ごとの VM_MAP | VM_UNMAP は GPU の drain を待つ |
| D15 | submit は cp_lock を持ったまま全部置く | 分割の間に離す | 他の session の packet・reset が間に入る |
| D16 | reset の後に Bus Master を立てる（既に立っていれば切らない） | attach の初めに立てる・常に切る | 古い RING_BASE への DMA（reset で RUN が 0 になれば止まる）、boot の表示を壊さない |
| D17 | 装置の fault の後に session を開き直し mapping を再生 | 全 session を失敗にする | 未完の仕事の無い session は続けられる |
| D18 | CP_HEAP を上限で勘定し MMU_OOM を起こさない | MMU_OOM を受けてから広げる | 連続の拡張しかできず（1691 行）、session 0 の OOM は装置の fault |

## 22. 版 3 の G・V の扱い

- **G01〜G49**（版 3 の ghci.md の疑問）: G01 はユーザーの回答（単一の 1.0）。他は GHCI の担当とのやり取り（H・J・K・L・M・N、ghci-review.md 第 9 版で全部解決）を経て 89529de の本文で決まった。N37〜N46 は cf8be08 までに解決した。新しい N47〜N60 は §19（Q25）。版 3 の G41（`CMD_QUERY_CAPS` の block）は 89529de の register（0x080・0x084・EXTCAP）で不要になった。
- **design-review-v3 の V01〜V25**:

| V | 89529de での扱い |
| --- | --- |
| V01（discrete で DEVICE_LOCAL が無い） | 反映: p011 の前の discrete は unified と同じ 1 つの型（§15.3） |
| V02（attach の未知の opcode の fault） | 不要: `CMD_QUERY_CAPS` を捨てた |
| V03・V04・V05（CAPS の bit からの誤った feature） | 反映: CAPS は無くなり、§15.2 の表を 89529de で書き直した（sampleRateShading は §12.11 で有り、alphaToOne・imageCubeArray・gather の拡張は false） |
| V06（重なる bit の AND） | 反映: DEVICE_CAPS を優先（270 行）。AND はしない |
| V07（RTL の DEVICE_CAPS） | 不要: RTL は正本でない。仕様の DEVICE_CAPS だけを読む |
| V08（WINDOW_CTRL） | 反映: MEM の register を読む・書く（§5.6・p011・§13） |
| V09・V10（CAPS・CAPS2 の場所と layout） | 不要: 無くなった |
| V11（Vulkan 1.0 の必須） | 反映: U8 の一覧（§20）、limit は §8.6（§15.2） |
| V12（DISPATCH_INDIRECT） | 解決: 0x2E が有る |
| V13（SFU） | 不要: 89529de に SFU の flag は無く、transcendental は全 GSI1 の命令（§12.5） |
| V14（U8 の経路） | 反映: `ghcictl list` は `GPU_GET_INFO` の名前と log で（ioctl を足さない）。経路の細部は p013 |
| V15（apiVersion） | 反映: §15.2 |
| V16（試験の抜け） | 反映: profile と flag ごとの差、p004 の場面（§14） |
| V17（block 無しの既定） | 不要: register は常に有る |
| V18（FMT の register を読まない） | 反映: FMT が有れば正（§15.4） |
| V19（FB の番地の空間） | 解決: 物理（2518 行、§13） |
| V20 | 反映済み（版 3） |
| V21〜V25（版 2 の残り・小さな抜け） | 全面の書き直しで消えた（V25 の vendorID は PCI の ID、§15.2） |

## 23. 自己点検（版 4.3）

1. **§12.10.3 が後で変わったら**: graphics の I/O は `gsi1-abi.h` の一か所と model の期待値だけを直す。compute・資源・PSO・ring は影響を受けない。
2. **CP_HEAP の 8.75 MiB が取れない**: 半分ずつ減らし、勘定で `ENOMEM` を返す（MMU_OOM を起こさない）。VF2 でも 4 GiB 未満の連続の 8 MiB は通常取れる。
3. **chunk の 2 MiB の粒度で VA が足りない**: 4 GiB / 2 MiB = 2048 chunk。session の中は sub-allocate するので `maxMemoryAllocationCount` 4096 には chunk の数でなく VA の大きさが効く。
4. **CAP_PROT = 0 と flat の device で client 間の隔離が無い**: 仕様どおり（1852 行）。U9 でユーザーが「どちらも出す」と決めた。attach の log に出す。
5. **実機の firmware が仕様に遅れている**: §0・§16.2。driver は仕様だけに合わせ、実機の差は p014 で記録して切り分ける。
6. **flag ごとの差の試験が表と同じ誤りを持つ**: 期待の差は試験の側に手で書く（§14）。
7. **U5 で client の fault が装置の reset になる**: CAP_PROT = 1 の device でも、1 つの client の fault で全 session の未完の仕事が DEVICE_LOST になる。犯人は sticky で止まるが、session を開き直して fault を繰り返す client は他の client の仕事を何度でも失わせ得る（嵐の LOST には数えないので装置は止まらない）。WS292 で session だけの回収に変える。
8. **flat の VA = bus**: executor の VA の計算・descriptor・PSO が 64 bit の番地を扱うこと（flat の host は 4 GiB 未満なので実際は 32 bit に入る）。flat の client の shader は物理を全部触れる（U9）。
9. **転送の shader の代替（U10）は client の session で動き client の bind を壊す**: 転送の後に bind を置き直す（§11.6）。置き直しの抜けは p010 の試験（転送の後の draw の packet の列）で見る。
10. **CP_HEAP の悲観の勘定で計画の reset が起きる**: PROT の device で vkCreateDevice と破棄を繰り返すと、勘定が尽きた所で未完の仕事の無い時に reset と再生をする（§5.5、Q10）。仕事は失わないが、表示の数え直しと再生の時間がかかる。N48 の答えで CLOSE が table を返すと分かれば、勘定を返す形に直す。

## 24. design-reviewer の指摘と対応（版 4.1）

design-reviewer（Opus 5.5、2026-10-28、2ee693d0f を対象）: H 6・M 18・L 10、仕様の不備 S1〜S14（→ ghci-review 第 13 版 N47〜N60、S13 の polygonMode は §12.12 にあり除いた）。判定は「H を直し、仕様の不備を ghci-review.md に用意すれば p002〜p005 は approve 可」。

| R | 重さ | 指摘 | 対応 |
| --- | --- | --- | --- |
| R01 | H | close で SESSION_CLOSE が VM_UNMAP より先 | 反映 §5.5 close（全 VM_UNMAP → CLOSE → FENCE、N49） |
| R02 | H | 再生の窓の VM 操作、replay の待ちのデッドロック | 反映 §5.2 の門（`ghci_cp_enter`、condvar、段 B だけ通らない） |
| R03 | H | 犯人の判定（flat、driver の誤り） | 反映 §5.4 の 1（帰せる fault の種類、`submit_log`、SESSION_ACTIVE） |
| R04 | H | BLIT bit を CAP_BLT で落とす | 反映 §15.4（CAP_BLT = 0 は driver の能力から） |
| R05 | H | attachment の tiling（S1） | 仮の扱いを §11.4 に、N47 |
| R06 | H | reset で完了済みの ticket まで ERROR | 反映 §5.4 の 4（irq_lock の中で (a)(b)(c)） |
| R07 | M | PSO に無い静的な状態 | 反映 §11.4 の注 |
| R08 | M | HW に field の無い 1.0 の core | 反映 §11.4 の注、p006 |
| R09 | M | CP_HEAP の勘定 | 反映 §5.5（余裕、MMU_OOM は reset で嵐に数えない）、N48 |
| R10 | M | host の event の WAIT_FENCE | 反映 §11.4 の注（submit を切る） |
| R11 | M | COPY_BUFFER の揃え、buffer と image の写し | 反映 §11.5 |
| R12 | M | timestamp の availability | 反映 §11.4 の注 |
| R13 | M | resolve の経路 | 反映 §11.5・§11.6（描画で） |
| R14 | M | watchdog と reset の数値 | 反映 §5.1 の 9、§5.4 の 1 |
| R15 | M | session 0 の mapping の再生 | 反映 §5.4 の 8 |
| R16 | M | 番号を取る時 | 反映 §5.5（CREATE_DEVICE、TOO_MANY_OBJECTS）、p007 |
| R17 | M | p004 の未実装の capability | 反映 §18 p004 |
| R18 | M | gsi1_isa.h が同じという誤り | 反映 §0、§18 p006 |
| R19 | M | VF2 の user の mapping | 反映 §9.2 |
| R20 | M | limit・feature の一覧 | 反映 §15.5 |
| R21 | M | libvulkan の 1.1 の op | 反映 §18 p007（表を作る） |
| R22 | M | 出典の版と行 | 反映 §4.4・§17・§5.4 の 2 |
| R23 | M | 試験の独立の照合、競合の注入、riscv64 の compile | 反映 §14（`ghci-ref-host-test.c`、注入）、§18 p002 |
| R24 | M | kc705 の profile の UMA・MEM | 反映 §14（2 つの profile） |
| R25 | L | vertex buffer は 16 | 反映 §4.4・§14 |
| R26 | L | EXTCAP 8 個、capability の version | 反映 §5.1 の 5 |
| R27 | L | Bus Master と IRQ の順 | 反映 §5.1 の 1・3・9・10 |
| R28 | L | local window | 反映 §5.1 の 1（WINDOW_CTRL は p011・p012 で） |
| R29 | L | UMA の boot framebuffer の予約 | 反映 §13（memory map に任せる、API を足さない） |
| R30 | L | triangle fan | 反映 §11.4 の注 |
| R31 | L | ClearAttachments | v1 は断って log（U8）、§11.6 で後に足せると注記 |
| R32 | L | 2 MiB を超える allocation | 反映 §5.5（chunk の run を first-fit） |
| R33 | L | ws.md の表と §18 の食い違い | Phase の確定の時に同じ commit で直す |
| R34 | L | REPLAYING の遷移 | 反映 §6.1 |

p009（36 LW）の分割の勧めは、p009 の着手の時に phase.md で中の区切り（描画と転送・query）を決める（番号は増やさない）。

### 再レビュー（版 4.2 → 4.3）

design-reviewer（Opus 5.5、2 回目、f5d542997 を対象）: H 4・M 12・L 9。判定は「p002・p003 は条件つきで approve 可、p004・p005 は H の Q01〜Q04 と M の Q05〜Q10 を文で直せば approve 可（仕様の答えを待つ物は無い）」。

| Q | 重さ | 指摘 | 対応 |
| --- | --- | --- | --- |
| Q01 | H | reset で RETIRED にした番号・chunk が executor の session の生きたまま返る | 反映: DEAD の状態（§5.4 の 6、§5.5、§6.2、§7） |
| Q02 | H | 門と再生と open・close の規則が二つ | 反映: 門の前に記録しない、1 区間で状態と packet、RETRY でやり直し（§5.2・§5.5） |
| Q03 | H | display.lock・門・reset の deadlock | 反映: 門で全 lock を離す、回復は device → display → cp（§5.2・§5.4 の表・§8） |
| Q04 | H | 犯人の判定（s = 0、IRQ 未処理の fence） | 反映: s = 0 は装置の fault、flat は handler が読んだ fence の語で（§5.4 の 1） |
| Q05 | M | PROT = 1 の犯人の fence が成功で完了 | 反映: handler が FAULT を先に扱い `faulted` の bitmap（§5.3） |
| Q06 | M | 古い fault の事象の二重処理 | 反映: `recovery_generation` を付けて古い物を捨てる（§5.3・§5.4 の 1） |
| Q07 | M | hang の誤判定、LOST に着かない | 反映: hang は fence と RPTR の両方、reset の後の liveness、3 回で LOST、嵐は 60 s（§5.4） |
| Q08 | M | 待ちが回復を遅らせる | 反映: `GHCI_RING_WAIT_MS`、`recovering` で即に抜ける（§5.2・§8） |
| Q09 | M | 段ごとの lock | 反映: §5.4 の表 |
| Q10 | M | CP_HEAP の仮の読みが ghci-review と逆 | 反映: 悲観の読み、計画の reset（§5.5、§23 の 10） |
| Q11 | M | 0x07〜0x0E は CAP_MRT_BLEND の時だけ | 反映 §11.4 |
| Q12 | M | layers > 1 の CLEAR | 反映 §11.4（layer ごとの clear の render pass） |
| Q13 | M | WaitEvents の穴 | 反映 §11.4（event の語、後の submit も保留、ERROR で完了） |
| Q14 | M | 断る時の見え方 | 反映 §11.4（create は VkResult、記録は飛ばして log） |
| Q15 | M | slot の共有と per-stage の上限 | 反映 §11.4・U8 の一覧 |
| Q16 | M | robustBufferAccess と VF の範囲外 | 反映 §11.4（推測と明記、p009・p014 で確かめる） |
| Q17 | L | MSAA の sample の位置と sampleRateShading | 反映 §15.2（1・4 sample、sampleRateShading false） |
| Q18 | L | fan の頂点の順 | 反映 §11.4 |
| Q19 | L | DispatchIndirect の guard | 反映 §11.5 |
| Q20 | L | attach の reset の待ち | 反映 §5.1 の 3 |
| Q21 | L | 番号の単位、import の時期、flat の seq | 反映 §5.5 |
| Q22 | L | §17 の gsi1_isa.h | 反映 §17 |
| Q23 | L | 行の版、出典の形 | 反映 §0・§4.4・§17 |
| Q24 | L | ref 試験の SKIP、版の名、比べない packet | 反映 §14 |
| Q25 | L | §22 の矛盾、§23 の番号、R33 | 反映 §22・§23。R33 は Phase の確定の commit で |

新しく見つけた仕様の不備（第 13 版の後、まだ ghci-review.md に無い）: **N61（M）** `CMD_DISP_SET` の大きさ。§7.2 の表と本文は「2 + 2N dword」だが、本文の配置（dword 1 + 2k に offset、2 + 2k に value）は header と N 組で 1 + 2N dword になる（1142〜1147 行）。仮の扱い: 本文の配置どおり 1 + 2N（p012 で使う。p002 の encoder は両方の読みを試験で固定せず、仮の 1 + 2N で書き、答えで直す）。次の ghci-review（第 14 版）に載せる。
