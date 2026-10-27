# WS290 の設計: zedBSD の GHCI の driver を GHCI 1.0（ghci.md bc7df9e）に書き直す

- Phase: [ws290-p001](phase001/phase.md)。版 1・2: 2026-10-26（P1、q1594）。版 3: 2026-10-27（P3、q1621、書きかけで中断）。**版 4: 2026-10-28（P3、q1636-i01）**。版 3 までの本文は git の履歴（f5fea4c2e など）にあり、この版は 89529de を正本に全部を書き直した。
- 親: [WS290](ws.md)。前身の設計: [WS288 design](../ws288/design.md) §6（1.1 の driver）。
- この文書は設計だけ。source は変えていない。
- **状態（2026-10-28 03:40）: 版 4 は本文を書き終え、仕様の更新 bc7df9e（§12.10.3 の追加）に §0・§12・§15.2・§19 を追従させた。GHCI の担当とのレビュー（第 10 版、N37〜N45）の回答待ち。design-reviewer には未だ通していない。**

## 0. 正本・読んだ物・確認の水準

| 読んだ物 | 版 | 使い方 |
| --- | --- | --- |
| amaterrace-gpu `docs/ghci.md`（2772 行） | **bc7df9e**（2026-10-28 03:34、`git show bc7df9e:docs/ghci.md`）。89529de（2677 行）からの差は §12.10.3「Shader Stage Register & Export Conventions (N37)」の 95 行の追加（2283〜2377 行）だけ | driver の要件の唯一の正本。下の「§n」はこの版の節。「n 行」は特に断らなければ **89529de の行**（2282 行までは bc7df9e と同じ、2283 行以降は bc7df9e で +95） |
| 同 `include/`（ghci_regs.h・ghci_cmd.h・ghci_packets.h・ghci_frame.h・ghci_layout.h・gsi1_isa.h など 20 file） | bc7df9e（89529de からの差は ghci_cmd.h の `GHCI_VS_REG_*`・`GHCI_FS_REG_*`・`GHCI_EXP_*` の 37 行だけ） | ghci.md との照合だけ。値は ghci.md から起こす（§17）。`gsi1_isa.h` は今の `compiler/gsi1_isa.h` と先頭の著作の行以外同じ（`diff` で確認） |
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
- protected context（`CMD_SESSION_OPEN` の flags[0]、`VM_FLAG_PROTECTED`、Vulkan の protected memory）は [WS292](../ws292/ws.md)（U5）。**CAP_PROT = 1 の hardware の session の fault の回収は WS290 で扱う**（§5.4。無いと driver が誤動作する。U5 の読み直しを Q1 に確認、§20 の U5）。
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
| §3.3 session の fault | CAP_PROT = 1: client の fault は `SESSION_ACTIVE[i]` = 0 と IRQ_FAULT だけ、FAULTED は立たない。CAP_PROT = 0: どの fault も FAULTED | §5.4 の 2 つの経路 | p004 |
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
| §11.1〜11.3 | host は register・doorbell・local window だけ。UMA と discrete。MEM の register | §5.6 | p005・p010 |
| §11.2 | VM_MAP・VM_UNMAP・SESSION_*、CP_HEAP（32 bit の物理、RUN の前に書く、連続でだけ広げられる）、MMU_OOM | §5.5 | p005 |
| §11.4・§11.5 | coherence は platform の性質、GPU の cache は `CMD_BARRIER` で明示 | §9.2、§11.5 | p005・p009 |
| §11.6 | HW_WALKER・PROT の 3 つの段（flat、共有の VA、session ごとの VA） | §5.5・U9 | p004・p005 |
| §12 | GSI1、純粋な code、PSO の dw13〜15、descriptor と constant bank の ABI | §12 | p006・p008 |
| §13.2・§13.3 | DISP2、DISP_SET・FLIP、事象、boot framebuffer | §13 | p011 |
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
| `cp.c` | 1262 | 直す（大: 1.0 の register・ring・fence 64・IRQ・fault の 2 経路・reset） | p004 | |
| `memory.c` | 688 | 直す（aperture を捨て、物理の確保と番地の上限） | p005 | |
| （新）`vm.c` | | session・VA・VM_MAP・CP_HEAP の勘定・再生 | p005 | §5.5 |
| `share.c` | 275 | 直す（import は輸入先の session に VM_MAP） | p005 | |
| `firmware.c`・`native.c`・`blit.c` | 2071・96・195 | 捨てる | p004 | INIT・DIAG・CPFW・native・1.1 の BLT |
| `isa.c` | 48 | 直す（NATIVE_ISA_ID と CAP_GSI1） | p006 | |
| `layout.h` | 167 | 直す（§8.5.2 の linear と tiled、64 byte の行） | p008 | |
| `compiler/*` | 10588 | 直す（header・interface 表を捨て §12 の ABI に） | p006 | |
| `display.c`・`display-scan.c`・`.h`・`adv7511.c` | 1274・1919・320 | 直す（lease・queue・record・数え直しの論理は残し、送り方を DISP_SET・FLIP（物理）に） | p011 | |
| `vk/vk-engine.*`・`vk-relay.c`・`vk-session.c` | 880・1280・487 | 捨てる | p007 | firmware の中継 |
| `vk/vk-stream.c`・`vk-node.*` | 1115・543 | 直す（transport・capset・submit・drain は残す） | p007 | |
| `vk/vk-device.c`・`vk-format.c` | 1336・282 | 直す（答えは `vk-caps.c` から） | p007 | |
| `vk/vk-memory.c`・`vk-program.c`・`vk.h` | 1415・1189・473 | 書き直す | p007・p008 | |
| （新）`ghci-regs.h`・`ghci-cmd.h`・`cmd.c`・`caps.c` | | 1.0 の値・encoder・capability の decode | p002 | |
| （新）`vk/vk-caps.c`・`vk-object.c`・`vk-reply.c`・`vk-layout.c`・`vk-pipeline.c`・`vk-pass.c`・`vk-command.c`・`vk-record.c`・`vk-execute.c`・`vk-queue.c` | | executor（amdrdna の `vk/` と同じ分け方） | p007〜p009 | §11 |

外の file: `platform/amd64/vmunix.mk`・`platform/riscv64/vmunix.mk` の list（各 Phase）、`Makefile` の `CONFIG_GHCI_NATIVE` と `include/uapi/gpu-op.h` の `GPU_OP_GHCI_*`（U1、p012）、`userland/base/ghcictl`（`diag`・`paint`・`fwload` を外す、U6、p012）、`userland/tests/ghci-blit`（削除、U6、p012）、`userland/tests/ghci-display`（残す）、`include/drivers/pci/pci-ghci.h`・`pcat.c`・`vf2-pcie.c`（残す）。`plan/ws288/tests/ghci-*`・`bringup-tb.*`・`ghci-e2e*`・`ghci-su-board.py`・`zybo-gpu-sd.dts` は 1.1 の試験で、p004 で build できなくなる。stub の部分は `plan/ws290/tests/` に移して直し、残りの削除を Q1 に頼む（p003）。`plan/ws288/tests/gsi1-*`・`gsi1/` は p006 で移す。

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

全 encoder は dword の列（`uint32_t *out`）か byte の列（`uint8_t *out`）に 1 つずつ書く（struct を GPU memory に重ねない。float は wire の bit をそのまま `uint32_t` で受け、kernel は float を計算しない。D5）。値の block ごとに `Values referenced: amaterrace-gpu docs/ghci.md (89529de) section N`（§17）。

| 群 | encoder | dwords / bytes |
| --- | --- | --- |
| 共通 | `ghci_cmd_header`、`ghci_cmd_nop`（1〜255、残りが 255 を超えれば続ける） | 1・1+N |
| session・VM | `vm_map`・`vm_unmap`・`session_open`・`session_close`・`bind_session` | 10・8・4・2・2 |
| bind | `bind_pipeline`・`bind_vertex_buffers`（n ≤ 63）・`bind_index_buffer`・`bind_descriptor_table`・`load_const`（count 1〜32） | 3・2+4n・4・4・3+n |
| state | `set_viewport`・`set_scissor`・`set_dynamic_state`（state_id ごとの関数） | 7・5・6 |
| draw | `draw`・`draw_indexed`・`draw_indirect`（7 と count の 8）・`draw_mesh_tasks`・`dispatch`・`dispatch_indirect` | 5・6・7/8・4・4・6 |
| pass | `begin_render_pass`・`end_render_pass` | 10・1 |
| 同期 | `barrier`・`fence`・`wait_fence`・`write_timestamp`・`begin_query`・`end_query` | 4・6・5・4・4・4 |
| 転送 | `copy_buffer`・`copy_image`・`blit`・`fill_buffer`・`resolve_image`・`copy_buffer_to_image`・`copy_image_to_buffer` | 8・11・12・5・11・10・10 |
| 表示 | `disp_set`（n 組）・`flip` | 2+2n・8 |
| 記述 | `ghci_pso_encode`・`ghci_texture_desc_encode`・`ghci_sampler_desc_encode`・`ghci_buffer_desc_encode`・`ghci_render_pass_desc_encode` | 128・32・16・16・192 byte |

`ghci_ring_plan(wptr, rptr, dwords, need, *pad)`: 端までの残りが `need` より少なければ残りを NOP で埋めて 0 へ（§6.2）、空きは `(rptr − wptr − 1 + n) % n`（§6.1）。

## 5. 流れ

### 5.1 attach（p004）

1. `pci-attach.c`: class 03h（subclass 02h か 80h）か、既知の ID（KC705 の 10EE:7024、Amaterrace の 5441:4748、§3.2.3）の function を受ける（KC705 の endpoint の class code が 03h とは限らないため ID でも受ける。どちらでも識別は次の MAGIC）。BAR0 を map（4 KiB 以上）。BAR2 が有れば local window として map（driver は使わない）。割り込みを取る（MSI-X・MSI・INTx、取れなければ poll）。
2. identify（読みだけ）: `GHCI_MAGIC` = 0x47484349、`GHCI_VERSION[31:16]` = 1（minor は問わない。G01）。違えば `ENODEV`。
3. reset: `CTRL.RESET` を書き、`CTRL.RESET` = 0 を `GHCI_RESET_WAIT_MS` 待つ（§5.2 の手順 1）。前の kernel が RUNNING で残した CP もここで IDLE になる（RUN が 0 に）。Bus Master の扱い: 立っていなければ reset の後に立てる。既に立っていれば（firmware の GOP の表示が使う）切らない（D16）。`STATUS.MEM_READY` = 1 を待つ（Appendix A の 3）。
4. `SCRATCH` に 0x5a5a5a5a・0xa5a5a5a5 を書いて読み戻す（§14）。
5. capability: DEVICE_CAPS・EXT_CAP_FEATURE・EXT_CAP_FMT0・GCLK_HZ・NATIVE_ISA_ID・RESIDENT_WARPS を読み、EXTCAP_PTR が 0 でなければ list を辿る（entry 32 個まで、環・窓の外・重なりを拒む）。MEM・VEND・PROT は在れば register を読む。FMT・DISP2 は offset を記録する（FMT は p007、DISP2 は p011 で読む）。`ghci_caps_decode`（caps.c、pure）。DEVICE_CAPS と鏡（FEATURE[8]・[28]）が食い違えば DEVICE_CAPS を使い log（270〜271 行）。
6. 方針の判定（U9）: `CAP_HW_WALKER` = 0（flat、§11.6）は attach するが executor を出さない（display だけ）。`NUM_SU` = 0 か `CAP_GSI1` = 0 も executor を出さない。
7. CP_HEAP: 32 bit の物理（4 GiB 未満、MEM の HOST_LIMIT が 0 でなければその下）に連続の `GHCI_HEAP_BYTES`（§5.5）を取り、0 で埋め、`CP_HEAP_BASE`・`CP_HEAP_LIMIT` を書く（IDLE の間、§5.2）。
8. ring（host memory、既定 256 KiB、4 KiB〜16 MiB の 2 の冪）と fence page（host 4 KiB、64 bit の語を 0 に）を取り、`RING_BASE_LO/HI`・`RING_SIZE` を書き、`RING_WPTR` = 0。
9. `WDT_CYCLES` = `GCLK_HZ` × `GHCI_WATCHDOG_MS` / 1000（GCLK_HZ = 0 なら書かず既定 2^28 のまま）。IRQ_STATUS を全部 W1C、`IRQ_ENABLE` = FENCE | FAULT | WATCHDOG（DISP2 が有り p011 の後は | DISPLAY。poll の device は 0）。Bus Master を立てる（3 で立てていなければ）。`CTRL` = IRQ_GLOBAL | WDT_EN | RUN（poll は IRQ_GLOBAL 無し）。CP はここで session 0 の root を CP_HEAP_BASE に作る（628 行）。
10. liveness: `CMD_FENCE`（MEM_WRITE | IRQ、seq 1）を置き doorbell。fence の語が 1 になり、割り込みが来る（poll は語だけ）のを待つ（§14）。来なければ `DIAG_CP_PC`・`DIAG_CP_STATUS`・`STATUS`・`FAULT_INFO` を log し、`CTRL.RESET` → Bus Master を切る → memory を返して `EIO`。
11. service・poll の thread を起こし `drv_gpu_register`。名前は `ghci:<PCI vendor>:<PCI device>`（VEND が有れば log に足す）。

### 5.2 submit（p004、executor は p009）

`ghci_cp_submit(device, vm, packets, dwords, ticket)`: cp_lock を取り、`replay_pending` なら待つ。packet の前に `CMD_BIND_SESSION(vm の番号)`（driver 自身の仕事は 0）、後に `CMD_FENCE`（device の fence page、seq = 前 + 1、MEM_WRITE | IRQ）を置く。1 つの submit は cp_lock を持ったまま全部置く（D15）: 余地が無ければ cp_lock を持ったまま RPTR を有界に待ち、ring より大きい submit は packet の境で分けて doorbell ごとに待つ。前半を置いた後の timeout は hang として回復（§5.4）。doorbell は §9.3 の順。ticket = {seq（64 bit）, generation}。最後に `CMD_BIND_SESSION(0)` を置く（後の driver の packet が client の session で動かないように）。

### 5.3 割り込み（p004）

handler（irq_lock の spin）: `IRQ_ENABLE` = 0 → `IRQ_STATUS` を読む → 扱う bit を W1C → `IRQ_ENABLE` を戻す。FENCE: fence page を読み completed を進め service に RECLAIM。FAULT・WATCHDOG: `STATUS`・`FAULT_INFO`・`FAULT_ADDR`・`SESSION_ACTIVE` を記録し service に FAULT。DISPLAY: service に DISPLAY（p011、`DISP_IRQ_STATUS` を読み W1C してから IRQ_STATUS[3] を W1C、1567 行）。`CTRL` は handler で触らない。`drv_gpu_complete` は service thread が呼ぶ。

### 5.4 fault・watchdog・reset（p004・p005）

service thread が FAULT を受けたら `STATUS.FAULTED` で分ける:

**A. session の fault**（FAULTED = 0。CAP_PROT = 1 の client、または client の watchdog・MMU_OOM・VM の衝突）: `FAULT_INFO.SESSION_ID` = s（1〜15）を読み、`FAULT_INFO` を読んだ値で W1C（再武装）。s の `ghci_vm` を FAULTED にし、その executor の session に `drv_gpu_report_session_error`（sticky、以後の submit を断る。Vulkan は DEVICE_LOST）。driver は s へ VM_MAP を置かない。`CMD_SESSION_CLOSE(s)` と `CMD_FENCE` を置き、その ticket が過ぎたら s の mapping の記録と chunk・table の勘定を返し、番号を `RETIRED` にする（物理は fence の後に返す。§7）。他の session と装置は動き続ける（N05・N06）。s = 0 の fault は FAULTED を伴うので B へ。

**B. 装置の fault**（FAULTED = 1、AXI_STUCK、hang、liveness の失敗）:
1. cp_lock を取る。`CTRL.RESET` を書き、0 に戻るのを `GHCI_RESET_WAIT_MS` 待つ。戻らない・`STATUS.AXI_STUCK` = 1 なら即 LOST（§10、628 行の「link の reset でしか戻らない」）。
2. reset は `RING_BASE`・`RING_SIZE`・`CP_HEAP_*`・`WDT_CYCLES`・`IRQ_ENABLE`・`SCRATCH` を保ち、`RING_WPTR`・`RPTR`・`IRQ_STATUS`・`FAULT_*`・`CTRL` の実行の bit・`SESSION_ACTIVE`・TLB・CP_HEAP の割当を消す（565〜566 行）。driver は保たれる register を読んで確かめ（違えば書き直す）、`ring.wptr` = 0、fence の語に最後に置いた seq を書く（574 行）。
3. generation を進め、古い generation の ticket と job marker を ERROR で完了する。reset が成功した（quiesce した）ので古い ticket を待つ物理は返してよい。
4. （p011 の後）CP が IDLE の間に DISP2 の `STATUS[31]` が落ちるのを待ち `TAKEN0/1` を読み数え直す（2543〜2545 行）。
5. CP_HEAP の勘定を session 0 の root だけに戻す。全 `ghci_vm` を「再生待ち」にし、`replay_pending` = 1。`CTRL` = IRQ_GLOBAL | WDT_EN | RUN（CP が session 0 の root を作り直す）。cp_lock を離す。
6. 未完の仕事を失った session に `drv_gpu_report_session_error`。
7. 段 B（service thread、device.lock → cp_lock）: 生きている各 `ghci_vm`（OPEN）について `CMD_SESSION_OPEN(s)`、その LIVE・UNMAP_WANTED の mapping を全部 `CMD_VM_MAP` で置き直す（RPTR を待ちながら）。FAULTED・CLOSING の session は置かず RETIRED にする。終われば `replay_pending` = 0。途中の装置の fault は 1 からやり直す（`replayed` が古い物だけを次に置く）。

LOST: 以後の submit は `EIO`、`drv_gpu_report_error`。GPU がまだ DMA し得るので物理を返さない（`quarantined` に記録）。detach は Bus Master を切った後にだけ返す。同じ装置の fault が `GHCI_FAULT_STORM_MS`（10 s）の中で 3 回でも LOST。session の fault は装置を止めないので数えない（fault の嵐の session は A で止まる）。

hang: fence が `GHCI_HANG_MS`（推し 10 s。watchdog は packet ごとで、ring 全体の進みは見ない）動かず未完がある時は B。

### 5.5 session と VM（p005、§11.2・§11.6）

- **session の番号**: 0 は driver、1〜15 を executor の session に。番号は executor の session の最初の VM の要求で取り（`CMD_SESSION_OPEN(s, 0)`）、閉じて `RETIRED` になるまで再利用しない。16 番目は `EBUSY`（U11）。`CAP_PROT` = 0 の device も番号を使う（dynamic state は session ごと、845 行）。
- **VA**: v1 は 32 bit の範囲だけ使う（VA_BITS = 40 の device も上位を 0 に。`gpu_va_hi` = 0）。device に 1 つの **2 MiB の chunk の表**（2048 個、chunk 0 と最後は使わない）を持ち、chunk は 1 つの session だけが持つ。session の中は自分の chunk の上の range allocator（`range.c`）で取る。理由: CAP_PROT = 0 は page table を全 session で共有し（1852 行）、CAP_PROT = 1 は session ごと（1853 行）。chunk の持ち主を一つにすると (a) どちらでも VA が重ならず、(b) 1 つの L0 table（2 MiB）が 1 つの session だけに属し、CP_HEAP の勘定が上限で決まる。
- **CP_HEAP の勘定**（MMU_OOM を起こさない、1692〜1695 行）: CP の内部の割当は driver に見えない（§11.2 冒頭）ので、上限で数える: session ごとに root（VA 32 bit は 16 KiB、40 bit は 8 KiB + L1 の 4 KiB）、chunk ごとに L0 の 4 KiB。勘定は chunk を持った時に足し、session の CLOSE の ticket の後と reset の後に返す（CP が unmap で table を返すかに依らず上限になる）。`GHCI_HEAP_BYTES` は session 0 と 15 の root と L0 2048 枚: 16 × 16 KiB + 2048 × 4 KiB = **8.25 MiB**（全 VA を map しても足りる）。取れなければ半分ずつ減らして試し、勘定を超える VM の要求は `ENOMEM`（Vulkan は OUT_OF_DEVICE_MEMORY）。MMU_OOM が来ても A・B で回収する（推測の外れへの備え）。
- **map**: `ghci_memory_alloc` で物理 → `ghci_vm_map` で chunk と VA を取り、`CMD_VM_MAP(s, flags, va, bus, bytes)` を置く（device.lock → cp_lock）。完了は待たない（packet は in-order、706 行）。flags: host は READ | WRITE | COHERENT（WC は付けない、§11.4 の「GPU は coherence を外さない」）、code は | EXEC、local は | LOCAL。
- **unmap**: mapping を UNMAP_WANTED → `CMD_VM_UNMAP` を置けたら UNMAPPING と後の fence の ticket → ticket が過ぎたら VA を返し、物理は他の mapping・hold が無ければ返す（685 行「fence で守る」）。置けない間（ring の満、再生の途中）は物理を hold して service が置き直す。VM_UNMAP は GPU の drain を待つ（1723〜1728 行）ので、map・unmap は VkDeviceMemory・blob・import と executor の heap の単位だけにする（D14）。
- **close**: 失敗できない（`gpu.h`）。mapping を全部 UNMAP_WANTED に、`CMD_SESSION_CLOSE(s)` を置き `CLOSING`。ticket の後に chunk・勘定・番号を返し `RETIRED`。
- **flat（HW_WALKER = 0）**: executor を出さないので VM は使わない（U9）。
- **番地の上限**: GPU に渡す host memory は bus address が `2^PA_BITS` 未満。v1 は扱いを一つにするため全部を 4 GiB 未満（MEM の HOST_LIMIT が 0 でなければその下）に取る（`kern_pmem_alloc_limited`。CP_HEAP・FB・flip の record は 32 bit の物理なので必須、他は簡単のため。D8）。

### 5.6 unified と discrete（p005・p010、§11.3）

| | unified（`CAP_UMA` = 1、または MEM が無いか LOCAL_SIZE = 0） | discrete（`CAP_UMA` = 0 かつ MEM の LOCAL_SIZE > 0） |
| --- | --- | --- |
| ring・fence・PSO・descriptor・code・render pass・query・event | host | host（D7） |
| Vulkan の host visible | host | host |
| Vulkan の device local だけ | host | local（p010、`VM_FLAG_LOCAL`）。0 埋めは `CMD_FILL_BUFFER`（CAP_BLT。無ければ local を出さない） |
| heap | 1 つ（DEVICE_LOCAL・HOST_VISIBLE・HOST_COHERENT） | 2 つ（p010。それまでは unified と同じ 1 つ） |

`CAP_UMA` = 0 で MEM が無い時は「MEM が無い GPU は unified」（1793 行）に従い unified として扱い log（DEVICE_CAPS が正という 270 行と食い違うが、確保の場所が host だけになるだけで安全）。

### 5.7 detach（p004）

session が残れば `EBUSY`。(1) p011 の後: 表示が driver の framebuffer を scan していれば boot framebuffer に戻すか ENABLE = 0 にして `STATUS.idle` を待つ。(2) RUNNING なら `CTRL.HALT` → `STATUS.HALTED` を有界に待つ（FAULTED・LOST は飛ばす）。(3) `CTRL.RESET`、`IRQ_ENABLE` = 0、`CTRL` = 0。(4) Bus Master を切る（表示が boot framebuffer を scan していれば切らず、ring・heap を参照しない状態（RESET 済みで RUN = 0）で残す）。(5) thread を止め、ring・fence・CP_HEAP・quarantine の物理を返す。

## 6. 状態遷移

### 6.1 device

```
ATTACHING --identify・reset・liveness OK--> RUNNING
RUNNING --session の fault--> RUNNING（その session だけ FAULTED、§5.4 A）
RUNNING --FAULTED・hang--> RECOVERING --reset OK--> REPLAYING --> RUNNING
RECOVERING --RESET が落ちない・AXI_STUCK--> LOST
RUNNING --10 s に装置の fault 3 回--> LOST
RUNNING|LOST --stop（session 0 個）--> DETACHED
```

driver は `STATUS.RUNNING`・`HALTED`・`FAULTED`・`AXI_STUCK`・`CP_STATE` と自分の `CTRL` の書き込みで判断する（§3.3 の M10 の規則）。

### 6.2 session（ghci_vm）

`(無し)` → `OPEN`（SESSION_OPEN を置いた）→ `CLOSING`（SESSION_CLOSE を置いた）→ `RETIRED`（ticket の後、番号と勘定を返す）。`OPEN` → `FAULTED`（§5.4 A）→ `CLOSING`。装置の reset で `OPEN` は再生、`FAULTED`・`CLOSING` は `RETIRED`。

### 6.3 allocation と mapping

```
allocation: ALLOCATED --map--> MAPPED(1..n) --mapping も hold も無い--> FREED
mapping:    LIVE --unmap--> UNMAP_WANTED --VM_UNMAP placed--> UNMAPPING(ticket) --passed--> gone
LOST の device: どの状態からも FREED にしない（quarantine）
```

## 7. 資源の所有権と寿命

| 資源 | 持ち主 | 返す時 |
| --- | --- | --- |
| ring・fence page・CP_HEAP | device | detach の Bus Master を切った後 |
| session の番号・chunk・heap の勘定 | `ghci_vm` | `RETIRED`（SESSION_CLOSE の ticket の後、か成功した reset の後） |
| VA | session の allocator | UNMAPPING の ticket の後 |
| resource・blob の物理 | drv_gpu の session（export の record も hold） | 全 mapping の unmap の ticket の後 |
| executor の heap（session ごと） | vk の session | session の close（heap の unmap の ticket の後） |
| PSO・descriptor・code・render pass・一時の memory | heap の区画 | destroy・submit の後、その session の最後の fence の後。unmap はしない |
| boot framebuffer の範囲 | firmware（driver は確保から外すだけ） | 最初の自分の flip が取られた後（p011） |

規則: GPU が参照し得る物は参照を含む submit の fence が過ぎるまで返さない。reset で終わった ticket は reset が成功した時だけ返す。回収は service thread。executor の heap・PSO・descriptor・code は `resource_map` の対象にしない（client が kernel の検査した物を書き換えないように）。

## 8. 並行性とロック

今の順を保つ: `device.lock`（mutex: session、allocator、chunk、mapping、寿命）→ `display.lock` → `cp_lock`（mutex: ring、doorbell、reset、`replay_pending`）。`irq_lock`（spin: completed、generation、fault の記録、service の仕事）は最も内側。
- VM の操作と再生: VA・mapping は device.lock、ring に置くのは cp_lock（device.lock → cp_lock）。
- reset（§5.4 B の 1〜5）は cp_lock だけ。session の報告と段 B は cp_lock を離してから。
- ring の満の待ちは cp_lock を持ったまま有界に。cp_lock を持つ間に device.lock・display.lock を取らない。
- session の fault（§5.4 A）は service thread で device.lock → cp_lock（SESSION_CLOSE を置く）。
- host 試験の stub は lock の順の違反を FAIL にする（§14）。

## 9. 割り込みと DMA

### 9.1 割り込み

- MSI・MSI-X は 1 本、INTx は予備、無ければ poll（今の `take_interrupt`）。MSI は `IRQ_STATUS & IRQ_ENABLE` が 0 → 1 の時と、IRQ_STATUS・IRQ_ENABLE を書いた後にまだ 1 の時に来る（2639 行）ので、handler の「mask → 読む → W1C → unmask」で取りこぼさない。
- `CTRL.IRQ_GLOBAL` は start と reset の後（reset が消す、327 行）に書き、handler では触らない。
- poll の device は IRQ_ENABLE = 0 のまま IRQ_STATUS を読む（IRQ_STATUS は ENABLE に依らず latch する、389 行）。

### 9.2 DMA と coherence（§11.4）

host memory は `kern_pmem_alloc_limited` の連続の run（D8）。CPU の view は直の map か uncached の alias（今の `host_view`・`host_uncached`、`CONFIG_GHCI_HOST_UNCACHED`。VF2 は `kern_pmem_map_uncached`）。bus address は物理と同じ（amd64・VF2）。

### 9.3 doorbell の順（§6.3）

ring へ書く → store barrier → `RING_WPTR`（dword の index）。非 coherent な platform では ring・fence page を uncached の view で触る（今の形）。dword は little endian で書く関数を通す。

## 10. 失敗と回復（まとめ）

| 失敗 | 検出 | 回復 | client に見える物 |
| --- | --- | --- | --- |
| 識別（MAGIC・major・reset・MEM_READY・SCRATCH・liveness） | attach | attach しない | /dev/gpuN が無い、log |
| session の fault（FAULTED = 0） | handler | §5.4 A | その session だけ DEVICE_LOST |
| 装置の fault・hang | handler・service | §5.4 B | 未完の仕事を失った session は DEVICE_LOST、他は続く |
| reset の失敗・AXI_STUCK・装置の fault の嵐 | §5.4 B | LOST（物理を返さない） | 以後 EIO |
| ring の満 | ring_plan | cp_lock を持ったまま有界に待つ | timeout で submit の失敗 |
| VM_UNMAP を置けない | vm.c | UNMAP_WANTED で hold、service が置き直す | 無し |
| VA・chunk・CP_HEAP の勘定・物理の不足 | allocator | 遅延の解放を回収し 1 回やり直す | `ENOMEM` |
| 17 個目の executor の session | vm.c | 断る | `EBUSY`（vkCreateDevice の失敗） |
| compile の失敗・flag の無い機能の要求 | compiler・vk-caps | 作らない・断る（U8） | Vulkan の結果と log |

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

### 11.5 Vulkan の command と GHCI の command

| Vulkan | GHCI | 無い時（flag） |
| --- | --- | --- |
| BindPipeline・VertexBuffers・IndexBuffer | `BIND_PIPELINE`・`BIND_VERTEX_BUFFERS`（stride は動的に）・`BIND_INDEX_BUFFER` | — |
| BindDescriptorSets・PushConstants | §11.4 の table と `BIND_DESCRIPTOR_TABLE` ×4・`LOAD_CONST` | — |
| SetViewport・Scissor・DepthBias・BlendConstants・StencilCompareMask・WriteMask・Reference・LineWidth | `SET_VIEWPORT`・`SET_SCISSOR`・`SET_DYNAMIC_STATE`（stencil は 3 つを合わせた 1 語） | — |
| BeginRenderPass・NextSubpass・EndRenderPass | subpass ごとに `BEGIN_RENDER_PASS`（192 byte の記述、clear は `clear_color_table_va` で 128 bit）・`END_RENDER_PASS`。subpass の間は store → load | — |
| Draw・DrawIndexed | `DRAW`・`DRAW_INDEXED`。triangle fan は非 indexed なら executor が index を作る。indexed の fan は断る（v1） | — |
| DrawIndirect・DrawIndexedIndirect | `DRAW_INDIRECT`（7 dw） | CAP_INDIRECT_DRAW = 0: 記録で断る（U8） |
| Dispatch・DispatchIndirect | `DISPATCH`・`DISPATCH_INDIRECT` | — |
| PipelineBarrier・WaitEvents | `BARRIER`（stage と cache の flag へ写す）。host の SetEvent を待つ時は `WAIT_FENCE`（event の 64 bit の語の bus address、値 1） | — |
| SetEvent・ResetEvent | `FENCE`（MEM_WRITE、IRQ 無し、event の語に 1・0）。event の memory は host で driver が持つ | — |
| BeginQuery・EndQuery | `BEGIN_QUERY`・`END_QUERY`（AVAILABILITY、16 byte の slot、PRECISE は flag から） | — |
| WriteTimestamp | `WRITE_TIMESTAMP`（flags[0] = 0、pool の VA） | — |
| ResetQueryPool | `FILL_BUFFER` | CAP_BLT = 0: slot ごとの `FENCE`（MEM_WRITE、値 0、pool の bus address） |
| UpdateBuffer・FillBuffer | heap に写して `COPY_BUFFER`・`FILL_BUFFER` | CAP_BLT = 0: 8 byte 揃えなら `FENCE` の書き込みで（buffer の bus address を kernel が求める）。揃わなければ断る（U10） |
| CopyBuffer・CopyImage・BlitImage・ResolveImage・CopyBufferToImage・CopyImageToBuffer・CopyQueryPoolResults | `COPY_*`・`BLIT`・`RESOLVE_IMAGE`（slice ごと） | CAP_BLT = 0: 断る（U10） |
| ClearColorImage・ClearDepthStencilImage | color・depth の attachment にできる format なら clear だけの render pass。他は断る | — |
| ClearAttachments | render pass を区切り、全体の rect なら clear で開き直す。部分の rect は断る（v1） | — |
| ExecuteCommands | 記録を展開（D6） | — |

## 12. compiler（`compiler/`）

§12.10 で program は header も interface 表も無い純粋な code になり（2247〜2259 行）、資源は descriptor table と constant bank で結ぶ（2261〜2281 行）。直す所:

1. **出力**: `struct ghci_gsi1_program` は code の語、`used_regs`、`lds_bytes`、`local_size`、flag（depth export・discard・centroid・sample shading）、varying の数、そして **executor への資源の表**（GPU には渡さない: texture・sampler の slot の組、set 2 の各 entry の Vulkan の set・binding・要素・種類、dynamic offset の語、push constant の範囲、scratch の bytes per warp）を持つ。header・interface 表を作る code（`gsi1_add_interface` など）を捨てる。
2. **資源の読み方**: UBO・SSBO・texel buffer は set 2 の buffer descriptor（c28〜c29 の base + entry の offset）を `ld.64` で読み、`lw`・`sw`・`atom` で触る（robustBufferAccess は size で検査）。storage image・input attachment は set 2 の 32 byte の記述から番地を計算（今の kind 9・10 の計算を移す）。sampled image は slot（§11.4）。push constant と dynamic offset は §11.4 の bank の語。scratch の base は set 3 の語。
3. **stage の I/O**: bc7df9e §12.10.3（N37、bc7df9e の 2283〜2377 行）に従う。vertex の属性は slot L の r[4L + c]（足りない成分は (0, 0, 0, 1)）、VertexIndex r60・InstanceIndex r61、export は POS 0x000〜0x003・POINT_SIZE 0x004・VARYING 0x010 + w（w = 4L + c、PSO dw13 `varying_count` は vec4 の数）、`lda` の即値は [8:2] PT word = 4 + w・[1:0] 頂点で、補間は b0〜b2 と `lda` の積和（compiler）。fragment の入力 r0〜r9、export COLOR 0x100 + 16k + c・DEPTH 0x104・SAMPLE_MASK 0x105・COLOR0_SRC1 0x108〜0x10B。値は ghci_cmd.h の `GHCI_VS_REG_*`・`GHCI_FS_REG_*`・`GHCI_EXP_*` と照合し、`gsi1-abi.h` の一か所に置く。**未決（§19 の N38〜N42、推しで仮に書く）**: 属性の slot と Vulkan の Location の付け直しと 16 個目（N38、推し: driver が使う Location を slot 0 から詰める。合計が r59 を超える pipeline は断る）、r60・r61 の条件（N39、推し: VertexIndex・InstanceIndex を使う program は `used_regs_vs` ≥ 62）、`lda` の word 0〜3（N40、推し: POS を読む。回答が違えば noperspective・FragCoord.w は w を varying に写す）、頂点の順と provoking vertex（N41、推し: Vulkan の順、vertex 0 が provoking）、clip と viewport の規則と point size（N42、推し: Vulkan の規則、point size は 1）。
4. **options**: registers = min(63, `RESIDENT_WARPS[23:16]`（0 なら 63）)、samples は CAP_MSAA の時 4（2・4）、sample mask の入力は CAP_MSAA、subgroup・fp16 の命令は出さない（Vulkan 1.0 の SPIR-V では使えない。WS226 で）。
5. **捨てる制約**: §12.9「Not in version 1」（anisotropic・cube array・MinLod・gather の動的 offset）は compiler が断る（今と同じ）。
6. 試験: `plan/ws288/tests/gsi1-*`・`gsi1/` を `plan/ws290/tests/` へ移し、参照の python は U2 で `plan/ws290/tests/gsi1-tools/` に 89529de の物を写す。

## 13. 表示（DISP2）

- DISP2（EXTCAP ID 11）が有る時だけ display の capability を出す。DISP（ID 1、CP の無い controller）は扱わない（KC705 は DISP2）。
- 変える所は送り方: 1.1 の DISP_SET・FLIP の packet → 1.0 の `CMD_DISP_SET`（0x50、offset と値の組、flags[0] で idle 待ち）と `CMD_FLIP`（0x51、8 dw、base は 32 bit の物理、record は 32 bit の物理、flag VSYNC_WAIT・IMMEDIATE・PLANE_CURSOR・RECORD_ENABLE）。FB・cursor・record は session の VA ではなく物理（2518 行）なので、scanout の image は 4 GiB 未満の host memory（UMA）か local（discrete、p010）に取る。
- 事象: IRQ_STATUS[3] → `DISP_IRQ_STATUS`（+0x18: VSYNC・FLIP_DONE・CURSOR_FLIP_DONE・HOTPLUG・UNDERFLOW・DISP_ERROR）を読み W1C。`DISP_IRQ_ENABLE`（+0x1C）。
- 今の lease・present の shared と copied・queue の深さ 2・mailbox・record と TAKEN0/1 の数え直し・cursor の plane 1・hotplug・DISP_ERROR の数・idle の論理は残す。copied の present と cursor の staging の写しは、UMA は CPU、discrete は `COPY_BUFFER`（session 0 に一時の mapping、CAP_BLT）か CPU（local window）。
- panic console: `CTRL.HALT` → HALTED → host が S の register を書く（2565〜2567 行）。
- boot framebuffer（§13.3）: attach で ENABLE = 1 なら `FB_BASE`・`FB_STRIDE`・`FB_SIZE` の範囲を確保から外し（UMA は host の物理を予約、discrete は local と window を保つ）、最初の自分の flip が取られた後に返す。p011 までは scanout に触らない（Guardrail）。
- adv7511.c（DC_CAPS[2] の外付けの送信器）は残す。

## 14. 試験の設計

- host 試験（ws.md の決定）と、実機（VF2 + KC705、§16）。build: amd64 は `plan/ws290/tests/config-amd64-ghci.mk`、riscv64 は `config-visionfive2-ghci.mk`（各 Phase で warning 0）。出力は `BUILD=build/ws290-<phase>/`。
- 部品（`plan/ws290/tests/`）:

| file | 中身 |
| --- | --- |
| `ghci-words-host-test.c` | `cmd.c`・`check.c`・`caps.c`・`range.c`・`jobs.c`・`capset.c` を直に呼ぶ。期待値は ghci.md の表から手で書く（encoder の表から作らない） |
| `ghci-model.c`・`.h` | GHCI 1.0 の device の model: register（§3.1）と EXTCAP の block（profile で組む）、host と local の memory、doorbell で ring を読む CP（header・dwords・端・未知の opcode → kind 1、flag の無い command → kind 2）、`CMD_FENCE`（64 bit、MEM_WRITE・IRQ）、受けた `CMD_*` の記録、session（ACTIVE の bitmap、OPEN・CLOSE・BIND）、VM（session ごとか共有の表、PROT で切替、衝突 → kind 10、閉じた session → kind 1）、CP_HEAP の使用量（上限を超えれば MMU_OOM）、reset の保つ・消す、HALT、fault・watchdog の注入（session か装置）、IRQ の level、Bus Master が切れた間の DMA の検出、p011 で DISP2 |
| `ghci-stubs.c` | kernel の service と GPU core の stub（1 thread、偽の時計、lock の順の記録、待ちの途中の注入、`drv_gpu_register` の検査を `gpu.c` と同じに）。今の `plan/ws288/tests/ghci-driver-host-test.c` の stub の部分を移す |
| `ghci-driver-host-test.c`・`ghci-vk-host-test.c` | driver（と vk/・codec）+ model + stub の場面 |
| `ghci-host-test.sh` | build（`-std=gnu11 -Wall -Wextra -Werror -Wdeclaration-after-statement -fsanitize=address,undefined`、出力は `build/tmp/` の新しい directory）と実行、PASS/FAIL |

- capability の profile: `kc705`（am-ws003 の既定に当たる値。HW_WALKER・GSI1・NUM_SU 1・BLT・DISP、PROT・MSAA は profile の変種で）、`minimal`（HW_WALKER・GSI1 だけ、EXTCAP 無し）、`full`、`flat`（HW_WALKER = 0）、`prot`（CAP_PROT = 1）、`no-blt`、`no-su`、`discrete`（UMA 0 と MEM）、`mismatch`（鏡の食い違い）。
- **flag ごとの差の試験**（p007）: §15.2 の各行で `full` とその flag だけ落とした profile の答えの差が、試験の側に手で書いた差とちょうど一致する。
- model は driver と同じ読み方で書かれるので、試験は「driver が ghci.md（と §19 の推し）に合う packet と register の順を作る」ことの証拠で、hardware の振る舞いの証拠ではない。

Phase ごとの場面（すべて PASS が受け入れ）:

| Phase | 場面 |
| --- | --- |
| p002 | 全 encoder（境界: NOP 255 超、vertex buffer 63、LOAD_CONST 32、DISP_SET n）、PSO・descriptor・render pass の byte、ring の計画（端・満・1 slot）、fence64 の比較（回り込み）、EXTCAP の走査（正常・環・窓の外・32 個）、caps の decode（全 bit、VA_BITS・PA_BITS の符号化、鏡の食い違い）、CP_HEAP の勘定の関数 |
| p003 | model の自己試験（手で組んだ列、reset の表、IRQ、VM の 3 つの段、session の fault と装置の fault の注入、MMU_OOM、Bus Master、stub の lock の順の検出） |
| p004 | attach（VERSION 0x00010000・0x00010003 を受け 0x00020000・0 を拒む、MAGIC、SCRATCH、MEM_READY、liveness の失敗 → EIO、FAULTED・RUNNING で残った device、flat・no-su は executor を出さない）、register の順、submit・fence・IRQ・poll、4 KiB の ring で 1000 回、ring より大きい submit、session の fault（prot: その session だけ止まり他は続く、SESSION_CLOSE が置かれる）、装置の fault → reset → 古い ticket が ERROR → 次が動く、watchdog、hang、reset の失敗 → LOST、嵐 → LOST、detach、lock の順の違反 0 |
| p005 | session の open（16 個目 EBUSY）、VM_MAP（VA・bus・flag が model の表に一致、chunk の持ち主が一つ）、PROT 0 と 1 の両方で VA が重ならない、CP_HEAP の勘定を超える要求が ENOMEM で MMU_OOM が起きない、unmap → fence の後に物理が返る、置き損じ → 置き直し、import、close → RETIRED まで番号を再利用しない、reset の後の再生（SESSION_OPEN と VM_MAP、ring より大きい再生）、session の fault の後の CLOSE と勘定の返却 |
| p006 | gsi1 の回帰（移した試験）、code に header が無い、資源の表、§11.4 の bank と set の読み方、§12.10.3 の I/O（N38〜N42 は推し） |
| p007 | profile ごとの feature・limit・format・queue・memory の型、flag ごとの差、FMT の register を読む・無い時の表、vkCreateDevice の超過は FEATURE_NOT_PRESENT、memory・buffer・image、fence、capset、17 個目の device の失敗 |
| p008 | 代表の pipeline（三角形・blend・depth・stencil・MSAA・compute）の PSO の 128 byte、slot の表、HW の set の byte、flag の無い profile で断る pipeline |
| p009 | 1 frame の記録 → model の `CMD_*` の列、indirect（no-indirect で断る）、barrier、転送（BLT と no-blt の代替・断り）、query・timestamp・event、clear、bind の無い draw を断る、大きな submit |
| p010 | discrete: local の確保と FILL の 0 埋め、upload・readback、heap 2 つ、window と boot framebuffer |
| p011 | DISP2: mode、present（shared・copied）、queue、cursor、hotplug、error、reset の後の数え直し、panic の HALT、detach |
| p013 | 実機（§16.2） |
| p014 | 全 host 試験、amd64・riscv64 の build の warning 0、`git diff --check` |

## 15. capability と Vulkan への対応（`vk/vk-caps.c`）

### 15.1 読み方

`struct ghci_caps`（§4.3）を attach で一度作り、判定は全部 `ghci_caps_*` の小さな関数（`ghci_caps_has_blt(caps)` など）を通す（driver の他の file は bit を直に見ない）。重なる bit は DEVICE_CAPS（270〜271 行）。EXT_CAP_FEATURE の CAP_DUAL_SOURCE[8]・CAP_FP16[28] は鏡で、食い違いは log。MAX_MRT（FEATURE[19:16]）が 0 なら 1 と読む。

### 15.2 flag → Vulkan

分類: **V10** = Vulkan 1.0 の feature・limit・format・queue・memory として答える（WS290）。**V1x** = Vulkan 1.1 以降・extension（zedBSD の header と libvulkan に無い。WS226 で）。**DRV** = driver の中だけ。

| flag | 有る時 | 分類 | 無い時 |
| --- | --- | --- | --- |
| DEVICE_CAPS[0] UMA | heap 1 つ | V10 | discrete（§5.6） |
| [1] IOMMU | log | DRV | — |
| [2] HW_WALKER | VM、executor | DRV | executor を出さない（U9） |
| [3] INDIRECT_DRAW | `multiDrawIndirect`・`drawIndirectFirstInstance`、`maxDrawIndirectCount` 2^32−1 | V10 | false、1。DrawIndirect を断る（U8） |
| [4] SECONDARY_RING | 使わない（D6） | DRV | — |
| [5] DUAL_SOURCE | `dualSrcBlend`、`maxFragmentDualSrcAttachments` 1 | V10 | 0、SRC1 の factor の pipeline を断る |
| [6] FP16 | `shaderFloat16` | V1x | — |
| [7] GSI1、[23:16] NUM_SU | executor | DRV | 出さない |
| [15:8] PHYS_LANES | log（subgroup は 32） | DRV | — |
| [24] PROT | session の隔離（§5.4 A） | DRV | 装置の fault で回復 |
| [25] TEX_FFILT | float の format の FILTER_LINEAR | V10（format） | 落とす |
| [26] ROP_PACK | 整数・32 bit float の COLOR_ATTACHMENT | V10（format） | 落とす |
| [27]・[28] TASK・MESH | `taskShader`・`meshShader` | V1x | — |
| [29] BLT | 転送の command | DRV | §11.5 の代替と断り（U10） |
| [30] DISP | display（DISP2 が list に在る時） | DRV | display を出さない |
| [31] MSAA | sample count 1・2・4（framebuffer・sampled image）、`sampleRateShading`、`standardSampleLocations` | V10 | 1 だけ、false |
| FEATURE[3:0]・[7:4] | VA_BITS・PA_BITS | DRV | — |
| FEATURE[9]・[10]・[12] | COARSE_SCAN・FAST_CLEAR・EARLY_Z | DRV（log） | — |
| FEATURE[11] LOGIC_OP | `logicOp` | V10 | false、断る |
| FEATURE[13] HW_CURSOR | display の cursor（DC_CAPS[8] と両方） | DRV | 出さない |
| FEATURE[14] COLOR_LUT | 使わない | DRV | — |
| FEATURE[15] MRT_BLEND | `independentBlend`（SET_DYNAMIC_STATE 0x07〜0x0E） | V10 | false |
| FEATURE[19:16] MAX_MRT | `maxColorAttachments`・`maxFragmentOutputAttachments` = min(MAX_MRT, 4)（N43） | V10 | — |
| FEATURE[27:20] LDS_KB | `maxComputeSharedMemorySize` = KiB × 1024（上限 16384、§8.6） | V10 | 0 なら Workgroup の変数の program を断る |
| FEATURE[29] SUBGROUP | subgroup | V1x | — |
| FMT0[6:0] BC1〜7 | BC の format | V10（format） | 落とす。`textureCompressionBC` は全部（BC6H を含む）が無いので false |
| FMT0[7] ETC2 | ETC2・EAC、`textureCompressionETC2` | V10 | false |
| FMT0[9] SRGB | sRGB の format | V10（format） | 落とす |

flag に依らない答え（§8.6 と本文）: `depthClamp`（PSO flags[4]）・`depthBiasClamp`・`fillModeNonSolid`・`occlusionQueryPrecise`・`fullDrawIndexUint32`・`robustBufferAccess`（compiler）・`fragmentStoresAndAtomics`・`vertexPipelineStoresAndAtomics` は true。`imageCubeArray`・`samplerAnisotropy`・`shaderImageGatherExtended`・`shaderResourceMinLod`・`depthBounds`・`alphaToOne`・`wideLines`・`geometryShader`・`tessellationShader`・`multiViewport`・64 bit・16 bit は false（§12.9 の「Not in version 1」と PSO に field が無い物）。limit は §8.6 の表、`maxViewports` 1、`timestampPeriod` = 1e9 / GCLK_HZ（kernel は float を使わないので wire の float の bit を整数で作る）、`timestampValidBits` 64、`maxComputeWorkGroupInvocations` 256、`maxComputeWorkGroupSize` {256, 256, 64}。vendorID・deviceID は PCI の ID。`apiVersion` は 1.1 のまま（libvulkan が 1.1 未満を拒む、今の `DEVICE_API_VERSION`）。

### 15.3 memory の型

unified: heap 1 つ、型 1 つ（DEVICE_LOCAL・HOST_VISIBLE・HOST_COHERENT）。discrete（p010 の後）: heap 0 = local（DEVICE_LOCAL）、heap 1 = host（HOST_VISIBLE・HOST_COHERENT）、型 2 つ。p010 の前の discrete は unified と同じ 1 つ（libvulkan は DEVICE_LOCAL の型を要る）。

### 15.4 format

FMT の block が有れば、format ごとに `FMT_INDEX` を書き `FMT_OPTIMAL`・`FMT_LINEAR`・`FMT_BUFFER` を読む（501 行「A driver reports its format properties from these」）。GHCI の bit → Vulkan の bit: 0 SAMPLED_IMAGE、1 SAMPLED_IMAGE_FILTER_LINEAR、2 COLOR_ATTACHMENT、3 COLOR_ATTACHMENT_BLEND、4 DEPTH_STENCIL_ATTACHMENT、5 VERTEX_BUFFER、6 BLIT_SRC、7 BLIT_DST、8 STORAGE_IMAGE（488〜498 行）。FMT が無ければ §8.5.1 の表を「Guard」の flag で絞った物。どちらの場合も driver が実装できない bit を落とす（BLIT_* は CAP_BLT、STORAGE_IMAGE は compiler の対応、sRGB は FMT0[9]）。FMT の読みは attach の時に一度、表の全部の format で行い `struct ghci_caps` の外の表に持つ（`FMT_INDEX` は host が書く唯一の index なので device.lock の中）。Vulkan 1.0 の必須の format が欠ける時は U8。

## 16. platform と userland

### 16.1 platform

- amd64（PCIe）: build と host 試験。
- riscv64 VisionFive 2（M.2 の PCIe、`vf2-pcie.c` が `drv_pci_ghci_driver_register` を呼ぶ）: **実機で attach する**（2026-10-28 のユーザーの決定で U4 の「build だけ」を変えた）。割り込みは今の WS288 の VF2 の設定と同じく poll（`CONFIG_GHCI_POLL`）から始める。host memory は 4 GiB 未満（VF2 の DRAM は 0x4000_0000 から、§5.5）。非 coherent なので ring・fence・heap は `kern_pmem_map_uncached` の view（記憶: U74 は Svpbmt が無く `kern_device_map(UNCACHED)` は DRAM で効かない）。
- ZYBO・platform binding（§15.1）: 書かない（Future Work の候補）。
- endian: little endian の platform だけ。ring の dword・PSO・descriptor は LE で書く関数を通す。

### 16.2 実機の確かめ（p013）

ユーザーが VF2・SwitchBot・KC705 の書き込みを済ませた後に Q1 が割り当てる。1 本の script（image の build → 配置 → 起動 → 確かめ → 回収 → 要約）。段: (1) attach・SCRATCH・liveness の fence（p004 の後）、(2) session・VM_MAP・compute の dispatch（p005・p006・p008・p009 の後）、(3) 表示（p011 の後）。KC705 の firmware・RTL が 89529de の仕様に追いついていない所（§0 の `fw_cmd_bind_pipeline_pso`）は driver の誤りと分けて記録する（FAULT_INFO・DIAG_CP_PC を要約に）。判定は serial の対話と SSH（AGENTS.md §9）。

### 16.3 userland

`ghcictl`: `list`（名前・capability の要約・display の mode）と `display` を残し、`diag`・`paint`・`fwload` を外す（U6）。`ghci-blit` は削除、`ghci-display` は残す。

## 17. ライセンスと転記

| 出典 | SPDX | 扱い |
| --- | --- | --- |
| amaterrace-gpu `docs/ghci.md`（89529de） | Zlib | 値を driver の header に。block ごとに `Values referenced: amaterrace-gpu docs/ghci.md (89529de) section N`。文章は写さない |
| 同 `include/ghci_regs.h`・`ghci_cmd.h` | Zlib、著作者は同じ | 写さない（float の field、struct の重ね、ghci.md と食い違う legacy の定義）。照合にだけ使う |
| 同 `include/gsi1_isa.h` | Zlib | 今の `compiler/gsi1_isa.h` と同じ。header・interface の定義は p006 で使わなくなる |
| 同 `tools/gsi1*.py` | Zlib | 試験の参照として 89529de の物を写す（U2） |
| Vulkan の値（VkFormat・VkBlendFactor など） | 仕様の事実 | `Values referenced:` |

GPL の参照は無い。他の driver（amdrdna）からは分け方だけを参考にし、code は写さない。

## 18. 実装の Phase の分割

8 LW ≈ 2〜3 時間。

| Phase | 内容 | 依存 | 受け入れ | LW |
| --- | --- | --- | --- | --- |
| p002 | 語の層: `ghci-regs.h`・`ghci-cmd.h`・`cmd.c`（§4.4 の全部）・`caps.c`（decode と CP_HEAP の勘定）・ring の計画と fence64・EXTCAP の走査（`check.c` に新しい名前で足す）。build に入る今の file は変えない（新しい file は build に入れない） | p001 | words の試験 PASS、`CONFIG_DRIVER_PCI_GHCI=y` の amd64 が今のまま build | 14 |
| p003 | model・stub・script（§14）。WS288 の 1.1 の試験の削除の list を Q1 へ | p002 | model の自己試験 PASS | 16 |
| p004 | core: pci-attach（match・Bus Master の順）、identify・caps・CP_HEAP・ring・fence・IRQ・poll・watchdog、session の fault と装置の fault・reset・LOST、detach。1.1 の file（firmware・native・state・blit・frame・packets・hcr）と vk/・display・compiler を build から外し、呼ぶ所を消す。capability は `RESOURCE | BLOB | TRANSFER | MAPPING` | p003 | amd64・riscv64 の build の warning 0、p004 の場面 PASS、build に入る file に 1.1 の名前が無い（grep） | 24 |
| p005 | session・VM（chunk・勘定・map・unmap・close・再生）、memory（host、4 GiB の上限）、resource・blob・map・read・write、share・import。local は ENOTSUP。+ `SHARE | ALLOCATION_SHARE` | p004 | p005 の場面 PASS、build の warning 0 | 24 |
| p006 | compiler: header・interface 表を捨て §12 の ABI（`gsi1-abi.h`）、資源の表、options、compiler を build に戻す、gsi1 の試験と tools を移す | p002・p004 | gsi1 の試験 PASS、build の warning 0。graphics の I/O は §12.10.3 と N38〜N42 の推しで（回答で `gsi1-abi.h` を直す） | 22 |
| p007 | executor の土台: vk.h・stream・node・object・reply・vk-caps・device・format（FMT）・memory（heap）・command・queue（空の submit、fence・semaphore・event）。+ `CAPSET | COMMAND | NOTIFICATION | FENCE | JOB | JOB_CAPACITY` と recovery | p005・p006 | p007 の場面 PASS | 30 |
| p008 | pipeline と descriptor: vk-pipeline・vk-layout・layout.h | p007 | p008 の場面 PASS | 20 |
| p009 | 記録と実行: vk-pass・vk-record・vk-execute、draw・indirect・dispatch・barrier・dynamic state・転送・query・event・clear | p008 | p009 の場面 PASS、amd64・riscv64 の build の warning 0 | 36 |
| p010 | discrete: local memory、heap 2 つ、window と boot framebuffer | p005・p009 | p010 の場面 PASS | 12 |
| p011 | 表示: DISP_SET・FLIP、DISP_IRQ、copied・cursor の写し、reset の数え直し、panic、boot framebuffer、adv7511。+ `DISPLAY | DISPLAY_EVENTS` | p007・p009・p010 | p011 の場面 PASS | 22 |
| p012 | userland と UAPI: `GPU_OP_GHCI_*`・`CONFIG_GHCI_NATIVE` を外す、ghcictl、ghci-blit を削除の list に | p004 | ghcictl の build、amd64 の build の warning 0 | 4 |
| p013 | 実機の確かめ（VF2 + KC705、§16.2）。段ごとに Q1 の割当で | p004（段 1）、p009（段 2）、p011（段 3）、ユーザーの hardware の用意 | 段ごとの要約。KC705 の側の未実装は記録して切り分ける | 12 |
| p014 | 規約の全文の見直し: WS が変えた C の全部、全 host 試験、amd64・riscv64 の warning 0、tree に 1.1 の code が無い grep、stub の `drv_gpu_register` を `gpu.c` と照合、試験を `plan/tools.md` に登録を Q1 に依頼 | p002〜p013 | 規約の違反 0、全 PASS、warning 0 | 10 |

合計 **234 LW**（版 3 の 212 から: CP_HEAP の勘定と chunk（p005 +2）、session の fault の経路（p004 +2）、compiler の ABI の作り直し（p006 +14）、実機の Phase（p013 新、+12）、転送の代替と断り（p009 ±0）、PSO 128 byte・render pass の encoder（p002 +2）、vk-pass（p008 +2）。p010 は ±0。版 3 の `CMD_QUERY_CAPS` と block の decode・試験は無くなった（−12））。

依存の図:

```
p001 → p002 → p003 → p004 → p005 → p007 → p008 → p009 → p010 → p011
                      p004 → p006 → p007
              p002 → p006
                      p004 → p012
                      p004 → p013（段 1）、p009 → p013（段 2）、p011 → p013（段 3）
             p002..p013 → p014
外部（文脈、ここで実行しない）: N38〜N43 の回答 → p006 の graphics の I/O、ユーザーの hardware の用意 → p013
```

WS の受け入れ（ws.md）との対応: 1.1 の code が無い = p004・p007・p011・p012 の後に p014 の grep。amd64 の warning 0 = 各 Phase と p014。host 試験 = §14。規約 = p014。他の platform = §16（riscv64 は実機、p013）。

## 19. 仕様の欠け（GHCI の担当へのレビュー、推し付き）

bc7df9e の本文で決まらない物。GHCI の担当へは [ghci-review.md](ghci-review.md)（第 10 版、2026-10-28 03:38）で送った。版 4 の S01（stage の I/O が未定義）は bc7df9e §12.10.3 で一部解決し N37 になった。S02 は N43。driver は回答までは推しで書き、回答で `gsi1-abi.h`・`vk-caps.c` の一か所を直す。行は bc7df9e の物。

| ID | 重さ | 事実 | 推し | 止まる所 |
| --- | --- | --- | --- | --- |
| N38 | H | 属性の Location L ∈ [0, 14]（2292 行）が PSO の 16 slot と `maxVertexInputAttributes` 16 に合わない（L = 15 は r60〜r63）。L が PSO の slot か SPIR-V の Location か、無効の slot の印、2300 行の `ATTR_DST[L]`（PSO に field が無く、driver は SET_REG を出さない）が未定義 | L = PSO の slot、format 0 は無効、ATTR_DST の文を消す。16 個を収めるため slot の register を成分の数で詰める（合計が r59 を超える pipeline は断る） | p006 の vertex の入力、p008 の PSO の attributes |
| N39 | M | r60・r61 は「when referenced」に書かれる（2311 行）が header が無い。`used_regs_vs` < 62 の時と r62 の扱い | `used_regs_vs` ≥ 62 の時だけ書く。r62 は compiler の取り決め | p006 |
| N40 | M | POINT_SIZE と VARYING(0) が同じ PT word 4（2322・2324 行）。`lda` の word 0〜3 が POS を読むか（gsi1_isa.h は「read 0」）。w を varying に写すと Vulkan の 64 成分を満たせない（2330 行） | word 0〜3 は POS、point size は lda で読めない、PT word 4 は VARYING(0) | p006 の fragment の補間 |
| N41 | M | `lda` の頂点 0〜2 と b0〜b2・topology ごとの順・provoking vertex の対応が無い | Vulkan の順、vertex 0 が provoking | p006 の flat、p009 の strip の試験 |
| N42 | M | clip の体積・割り算・viewport の写像・depth clamp の時の z、POINT_SIZE を使うか | Vulkan の規則。point size は 1（`largePoints` false、`pointSizeRange` [1, 1]） | p006・p007 の limit |
| N43 | M | export は k = 0〜3（2355 行、`GHCI_EXP_COLOR` は `& 3`）、MAX_MRT は 8 まで（293・307 行）、§8.6 は 8、render pass は 8。§12.12 の「With CAPS[30]」が MRT を ROP_PACK に結ぶようにも読める | k = 0〜7 か MAX_MRT ≤ 4 に揃える。driver は min(MAX_MRT, 4) | 無し（推しで進む） |
| N44 | L | dual-source の SRC1: §8.4 1426 行は 0x101、§12.10.3・§12.12 は 0x108〜0x10B | 0x108〜0x10B（§12.10.3） | 無し |
| N45 | L | 古い参照（§12.8 の kind 14、§12.12 の interface kinds 9〜11、2259 行の「§12.8」、旧 CAPS[24]〜[30]、gsi1_isa.h の header・interface 表） | 1.0 の語に直す | 無し |

## 20. ghci.md 以外の判断（Q1・ユーザーへ）

| ID | 判断 | 推し | 状態 |
| --- | --- | --- | --- |
| U1 | `GPU_OP_GHCI_*` と `CONFIG_GHCI_NATIVE` を消す | 消す | 決定（2026-10-26「消してよいです」） |
| U2 | gsi1 の参照の python を写す | 89529de の 4 file を `plan/ws290/tests/gsi1-tools/` に | 決定（「写してよいです」） |
| U4 | riscv64 | 実機で attach する | 決定が変わった（2026-10-28、ws.md） |
| U5 | PROT | 読み直しの推し: CAP_PROT = 1 の hardware の session の fault の回収（§5.4 A）は WS290、protected context と Vulkan の protected memory は WS292 | **Q1 に確認**（2026-10-26「PROT は実装しませんが、WSを立てて、あとで実装します」は 1.1 の aperture の PROT を指していた。89529de の PROT は session の隔離そのもので、fault の振る舞いが変わる） |
| U6 | ghci-blit と ghcictl の diag・paint・fwload を消す | 消す | 決定 |
| U7 | ring を kernel だけが書く | kernel だけ | 決定（ghci.md も primary は KMD、590 行） |
| U8 | Vulkan 1.0 で隠せない不足（DrawIndirect に CAP_INDIRECT_DRAW、`maxDescriptorSetSampledImages` 16 < 96、`maxColorAttachments` 4、LDS が 16 KiB 未満、必須の format の欠け、転送に CAP_BLT） | Vulkan 1.0 を名乗ったまま、隠せる物は隠し、隠せない物は記録・pipeline の作成で断って log。不足の一覧を `ghcictl list` に出す | **ユーザーへ**（推しで進める。断る所は `vk-caps.c` の 1 つの検査） |
| U9 | 保護の段: HW_WALKER = 0（flat、隔離無し）と CAP_PROT = 0（VA を全 session で共有） | flat は executor を出さない（client の shader が全物理を触れる）。PROT = 0 は executor を出す（client 間の隔離は無く、fault は装置の reset） | **ユーザーへ**（推しで進める） |
| U10 | CAP_BLT = 0 の転送 | copy・blit・resolve・CopyQueryPoolResults は断る。UpdateBuffer・FillBuffer（8 byte 揃え）・query の reset・event は `CMD_FENCE` の書き込みで代える。compute の代替は Future Work | **ユーザーへ**（推しで進める） |
| U11 | executor の session は hardware の上限で 15 | 16 個目の Vulkan device は失敗 | 報告（仕様の上限） |

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

- **G01〜G49**（版 3 の ghci.md の疑問）: G01 はユーザーの回答（単一の 1.0）。他は GHCI の担当とのやり取り（H・J・K・L・M・N、ghci-review.md 第 9 版で全部解決）を経て 89529de の本文で決まった。残る欠けは bc7df9e に対する N37〜N45（§19）。版 3 の G41（`CMD_QUERY_CAPS` の block）は 89529de の register（0x080・0x084・EXTCAP）で不要になった。
- **design-review-v3 の V01〜V25**:

| V | 89529de での扱い |
| --- | --- |
| V01（discrete で DEVICE_LOCAL が無い） | 反映: p010 の前の discrete は unified と同じ 1 つの型（§15.3） |
| V02（attach の未知の opcode の fault） | 不要: `CMD_QUERY_CAPS` を捨てた |
| V03・V04・V05（CAPS の bit からの誤った feature） | 反映: CAPS は無くなり、§15.2 の表を 89529de で書き直した（sampleRateShading は §12.11 で有り、alphaToOne・imageCubeArray・gather の拡張は false） |
| V06（重なる bit の AND） | 反映: DEVICE_CAPS を優先（270 行）。AND はしない |
| V07（RTL の DEVICE_CAPS） | 不要: RTL は正本でない。仕様の DEVICE_CAPS だけを読む |
| V08（WINDOW_CTRL） | 反映: MEM の register を読む・書く（§5.6・p010・§13） |
| V09・V10（CAPS・CAPS2 の場所と layout） | 不要: 無くなった |
| V11（Vulkan 1.0 の必須） | 反映: U8 の一覧（§20）、limit は §8.6（§15.2） |
| V12（DISPATCH_INDIRECT） | 解決: 0x2E が有る |
| V13（SFU） | 不要: 89529de に SFU の flag は無く、transcendental は全 GSI1 の命令（§12.5） |
| V14（U8 の経路） | 反映: `ghcictl list` は `GPU_GET_INFO` の名前と log で（ioctl を足さない）。経路の細部は p012 |
| V15（apiVersion） | 反映: §15.2 |
| V16（試験の抜け） | 反映: profile と flag ごとの差、p004 の場面（§14） |
| V17（block 無しの既定） | 不要: register は常に有る |
| V18（FMT の register を読まない） | 反映: FMT が有れば正（§15.4） |
| V19（FB の番地の空間） | 解決: 物理（2518 行、§13） |
| V20 | 反映済み（版 3） |
| V21〜V25（版 2 の残り・小さな抜け） | 全面の書き直しで消えた（V25 の vendorID は PCI の ID、§15.2） |

## 23. 自己点検（版 4）

1. **N38〜N42 の推しが外れたら**: graphics の I/O は `gsi1-abi.h` の一か所と model の期待値だけを直す。compute・資源・PSO・ring は影響を受けない。
2. **CP_HEAP の 8.25 MiB が取れない**: 半分ずつ減らし、勘定で `ENOMEM` を返す（MMU_OOM を起こさない）。VF2 でも 4 GiB 未満の連続の 8 MiB は通常取れる。
3. **chunk の 2 MiB の粒度で VA が足りない**: 4 GiB / 2 MiB = 2048 chunk。session の中は sub-allocate するので `maxMemoryAllocationCount` 4096 には chunk の数でなく VA の大きさが効く。
4. **CAP_PROT = 0 の device で client 間の隔離が無い**: 仕様どおり（1852 行）。U9 でユーザーに示す。
5. **実機の firmware が仕様に遅れている**: §0・§16.2。driver は仕様だけに合わせ、実機の差は p013 で記録して切り分ける。
6. **flag ごとの差の試験が表と同じ誤りを持つ**: 期待の差は試験の側に手で書く（§14）。

## 24. design-reviewer の指摘と対応（版 4）

**未実施**（2026-10-28 ユーザー「P3はいったん待機してください。GHCI仕様をアップデートします。」で、版 4 を書き終えた所で中断した。design-reviewer には通していない）。再開の手順は [ws.md](ws.md) の「再開点（2026-10-28 待機、P3）」。
