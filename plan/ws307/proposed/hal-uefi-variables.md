<!-- awesome-plan project=zedbsd record=ws307-proposed-hal-uefi-variables -->

# WS307 p007 の HAL の差分の案: UEFI の変数（Boot####・BootOrder）

- 親: [WS307](../ws.md)、[spec.md](../spec.md) §5.8。作成: P4、2026-10-29（q1665）。
- 状態: **承認待ち**（AGENTS.md §7「HAL の API の変更は差分ごとの事前承認」）。承認まで code は書かない。承認までの間、installer は loader を置いて S11 で「firmware の boot menu で選ぶ」を出すだけ（spec §5.8 の承認の手順）。
- 要るユーザーの判断は下の「判断」の H1〜H3。推しは A。

## 1. なぜ HAL か

- installer は U5・U15（「Boot####の作成もWSに入れる」「末尾に足します」）で、導入先の ESP の loader を指す `Boot####`（名前「zedBSD」）を作り `BootOrder` の末尾に足す。これには UEFI の runtime services の `GetVariable`・`GetNextVariableName`・`SetVariable`・`QueryVariableInfo` を kernel から呼ぶ必要がある。
- 今の kernel は runtime services を呼べない: amd64 の handoff に EFI の system table が無い（`bootloader/include/amd64-handoff.h` の v7）。runtime の memory（`EfiRuntimeServicesCode`・`Data`）は loader が予約にして kernel の管理の外（`bootloader/uefi/memory-map-v6.c:134`）。firmware の code を実行するには、その範囲を 1:1 で実行できる page table、その間の割り込みの禁止、FPU の状態の保存、他の CPU との排他が要る。`hal_space_*` は管理された RAM の user の空間で、`HAL_SPACE_DEVICE` は実行を拒む（`include/hal/hal.h:702-807`）。kernel の driver から CR3 を直に書くのは HAL を迂回する。よって HAL の責務と API の追加になる。

## 2. 差分の案

### 2.1 loader → HAL（`bootloader/include/amd64-handoff.h`、HAL の API ではないが HAL の責務の入力）

UEFI の handoff の新しい版 v8 を v7 の後ろに足す（v7 までの形は変えない。BIOS の loader は v7 のまま）:

```c
/* One runtime-services memory range, as the firmware's map described it. */
struct zbl6_uefi_runtime_range {
	uint64_t physical_start;	/* EFI_MEMORY_DESCRIPTOR.PhysicalStart */
	uint64_t pages;			/* NumberOfPages (4 KiB) */
	uint64_t attribute;		/* Attribute (EFI_MEMORY_RUNTIME, UC/WC/WB/WT, XP/RO...) */
	uint32_t type;			/* EfiRuntimeServicesCode, EfiRuntimeServicesData, EfiMemoryMappedIO... */
	uint32_t reserved;
} __attribute__((packed));

/* What the HAL needs to call the firmware's runtime services. */
struct zbl6_uefi_runtime_handoff {
	uint64_t system_table;		/* physical address of EFI_SYSTEM_TABLE */
	uint32_t range_count;		/* at most ZBL6_UEFI_RUNTIME_RANGE_MAX (64) */
	uint32_t range_entry_size;	/* sizeof(struct zbl6_uefi_runtime_range) */
	uint64_t ranges;		/* physical address of the array, in the low block */
} __attribute__((packed));

struct zbl6_handoff_v8_uefi {
	struct zbl6_handoff_v7_uefi prefix;
	struct zbl6_uefi_runtime_handoff runtime;
} __attribute__((packed));
```

- loader は `ExitBootServices` の直前の最後の memory map から、`EFI_MEMORY_RUNTIME` の付いた記述子を全て写す（64 を越えれば v8 の runtime を空にし、Boot#### は使えないだけにする）。`SetVirtualAddressMap` は呼ばない（物理の address のまま 1:1 で呼ぶ）。

### 2.2 HAL → kernel の名前（`hal_get_arch_handoff`、hal.h は不変、HAL の責務の追加）

- `uefi.runtime`: amd64 の HAL が v8 の handoff を受け、下の 2.3 の準備に成功した時だけ非 NULL（`const uint64_t *`、値は system table の物理 address。kernel は「UEFI の変数が使える」の印としてだけ使う）。BIOS の起動・v7 以前・準備の失敗で NULL。前例: `acpi.rsdp`（WS049、`plan/guardrail.md` の表）。

### 2.3 HAL の関数（`include/hal/hal.h` に追加、**承認が要る本体**）

```c
/*
 * UEFI variables
 *
 * The firmware's variable services, called through its runtime services
 * on the physical (1:1) mapping the loader left.  Only an amd64 kernel
 * started by the UEFI loader has them; elsewhere every call returns
 * HAL_ERR_UNSUPPORTED.  Calls are serialized across CPUs and run with
 * interrupts disabled on the calling CPU, so they must not be made from
 * interrupt context and may take milliseconds.  Names are UTF-16 with a
 * terminating zero; vendor is the 16-byte EFI_GUID in its in-memory
 * (little-endian fields) order.  An EFI status the HAL cannot map is
 * HAL_ERR_IO.
 */

#define HAL_UEFI_VARIABLE_NAME_MAX	512	/* UTF-16 units, terminator included */
#define HAL_UEFI_VARIABLE_DATA_MAX	32768	/* bytes */

/*
 * Reads a variable.  On entry *size is the room in data; on HAL_OK it is the
 * variable's size.  Returns HAL_ERR_INVALID when the variable does not exist
 * (EFI_NOT_FOUND), HAL_ERR_NOMEM with *size set when data is too small
 * (EFI_BUFFER_TOO_SMALL), HAL_ERR_UNSUPPORTED without the services.
 */
int
hal_uefi_variable_get(
	const uint16_t *name,
	const uint8_t vendor[16],
	uint32_t *attributes,
	void *data,
	size_t *size);

/*
 * Writes a variable (size 0 deletes it).  Returns HAL_ERR_NOMEM when the
 * firmware is out of variable storage (EFI_OUT_OF_RESOURCES), HAL_ERR_STATE
 * when it refuses the write (EFI_WRITE_PROTECTED, EFI_SECURITY_VIOLATION).
 */
int
hal_uefi_variable_set(
	const uint16_t *name,
	const uint8_t vendor[16],
	uint32_t attributes,
	const void *data,
	size_t size);

/*
 * Steps to the next variable name (GetNextVariableName).  name and vendor
 * hold the previous name (an empty name starts the walk) and receive the
 * next; *name_size is the room in name in bytes.  Returns HAL_ERR_INVALID
 * at the end of the list (EFI_NOT_FOUND).
 */
int
hal_uefi_variable_next(
	uint16_t *name,
	size_t *name_size,
	uint8_t vendor[16]);

/*
 * Reports the firmware's variable storage (QueryVariableInfo) for the given
 * attributes: the whole storage, what remains, and the largest variable.
 */
int
hal_uefi_variable_info(
	uint32_t attributes,
	uint64_t *storage_size,
	uint64_t *remaining_size,
	uint64_t *variable_size);
```

### 2.4 HAL の実装の方針（amd64、`src/hal/amd64/`、承認の後）

- 起動の時（`acpi.rsdp` と同じ所）: v8 の handoff を検査（system table の signature・revision・CRC、runtime services の table の signature、各関数の pointer が runtime の code の範囲の中）。HAL だけが持つ page table を作る: kernel の上半分は kernel の page table と共有、下半分に runtime の範囲を 1:1（code は実行可・書き込み不可が attribute で示されれば従う、data は書き込み可・実行不可、MMIO は UC）、HAL の bounce buffer（引数と結果の写し、64 KiB 未満）と専用の stack（16 KiB）も 1:1。
- 呼び出し: 1 つの spinlock で全 CPU を排他 → 割り込みを禁止 → kernel の FPU の状態を保存（firmware は SSE を使いうる）→ CR3 を HAL の page table に → 引数を bounce buffer に写し、Microsoft x64 の呼び出し規約（shadow space 32 byte、16 byte 境界の stack）で関数を呼ぶ → CR3・FPU・割り込みを戻す → 結果を写し戻す。NMI はその間も来うるので、NMI の handler は HAL の page table の上でも kernel の上半分で動く（上半分を共有する理由）。
- 呼び出しの前に `QueryVariableInfo` で NVRAM の空きを見る（`hal_uefi_variable_set` の中で、残りが要求の大きさ＋4 KiB 未満なら書かずに HAL_ERR_NOMEM）。firmware の NVRAM を使い切ると起動しなくなる機械がある（Linux の efivars と同じ用心）。
- boot services の memory には触らない（`ExitBootServices` の後は返されている）。runtime の code がそれに触る firmware の不具合は、範囲外の page fault として HAL が捕まえられない（1:1 の map の外は not present）ので、呼び出しの間の #PF は「firmware の不具合」として HAL_ERR_IO で返す（呼び出しの間だけ #PF の handler に復帰点を置く）。

### 2.5 kernel と userland（HAL の外、承認の後に p007 で）

- `/dev/system` に root だけの要求（`KERN_SYSTEM_EFI_VARIABLE_GET`・`SET`・`NEXT`・`INFO`、struct に版と大きさ、name は 512 unit まで、data は 32 KiB まで）。HAL_ERR を errno に。
- base の道具 `efiboot`（`list`・`create`・`delete`・`order`）。UEFI の無い所では ENOTSUP。
- install-admin（p009）は spec §5.8 のとおり Boot#### を作る（device path は `HD(番号, GPT, PARTUUID, 開始, 大きさ)/File(\EFI\…\BOOTX64.EFI)`、BootOrder の末尾、同じ device path の entry の再使用、死んだ「zedBSD」の entry の削除）。

## 3. 判断（ユーザーへ）

- **H1** HAL の関数の形: **A（推し）変数の 4 関数**（上の 2.3。installer に要るのは変数だけで、時計・reset など他の runtime services を kernel に出さない）／ B 汎用の 1 関数 `hal_uefi_runtime_call(service, args[5], &status)`（後で他の service にも使えるが、引数の pointer の扱いを HAL が型無しで検査できない）。
- **H2** handoff の名前 `uefi.runtime` を足すこと（A 承認 ／ B 名前を足さず、`hal_uefi_variable_info` が HAL_ERR_UNSUPPORTED を返すかで kernel が判断する）。推しは A（他の名前と同じ「HAL が使える物を名前で示す」形）。
- **H3** 承認の範囲: **A（推し）2.1〜2.4 を一度に承認**（loader の v8、名前、4 関数、amd64 の実装の方針）。案から変わる時（関数の引数・意味）は改めて差分を示す ／ B hal.h の宣言だけ先に承認し、実装の方針は実装の後に見る。

## 4. 承認の後の試験（p007）

- QEMU の OVMF（変数の store は `OVMF_VARS` の写し）で `efiboot create` → `efiboot list` で読み戻し、再起動の後も `BootOrder` の末尾にあること、`efiboot delete` で消えること。host で `virt-fw-vars` などで vars の file を読んで照らす（読めれば）。
- BIOS の起動・v7 の handoff では `HAL_ERR_UNSUPPORTED`、`efiboot` は ENOTSUP。
- 2 CPU から同時に呼んで壊れないこと（排他）。
