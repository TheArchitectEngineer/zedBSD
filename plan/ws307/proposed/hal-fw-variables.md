<!-- awesome-plan project=zedbsd record=ws307-proposed-hal-fw-variables -->

# WS307 p007 の HAL の差分の案（改訂 2）: firmware の環境変数

- 親: [WS307](../ws.md)、[spec.md](../spec.md) §5.8。作成: P4、2026-10-29（q1665）。前の案 [hal-uefi-variables.md](hal-uefi-variables.md)（UEFI の変数の 4 関数）をこの案で置き換える。
- ユーザーの指示（2026-10-29、前の案への回答、Q1 の中継）:「UEFIに限らないファームウェア環境変数という概念で作り直してみてください。もう一度レビューして承認します。」
- 状態: **承認待ち**（AGENTS.md §7）。design-reviewer の 1 回目のレビュー（2026-10-29、blocker 2・major 8・minor 9）を §6 のとおり反映した。code はまだ書かない。

## 1. 考え方

- **firmware の環境変数**: 機械の firmware が電源を切っても持ち、起動の振る舞いを決める名前付きの値。UEFI の変数（vendor の GUID＋名前、`Boot####`・`BootOrder`）、Open Firmware の NVRAM の設定（`boot-device` など、名前＝文字の値）、u-boot の環境（`bootcmd` など）、Raspberry Pi の bootloader の EEPROM の設定（`BOOT_ORDER` など）が同じ概念に当たる。
- HAL が持つのは「名前付きの変数を firmware の service が読み書きする」物だけ: 今は UEFI の runtime services（amd64、将来の arm64 の UEFI）。名前の無い byte の列を firmware か記憶装置が持ち、OS がその形（partition・header・checksum）を解く物は、記憶装置の形として HAL の外（kernel の driver か userland の道具）: U-Boot の環境（SPI flash・eMMC の決まった場所、CRC つき）、Raspberry Pi の bootloader の EEPROM の設定（1 つの text の file を丸ごと書き、次の起動で反映）、ppc64 pseries の NVRAM（RTAS の `nvram-fetch`・`nvram-store` は offset の byte を動かすだけで、`boot-device` などは CHRP の「common」partition の中を OS が解く、Linux の nvram_64.c と同じ）、ppc32 mac99 の NVRAM（firmware を呼ばず直に読む）。sun4u の OBP の変数（`setenv` 相当の client interface）は名前付きの firmware の service で、起動の後も呼べるなら後の WS で HAL に入りうる。
- kernel の上の口（§3）は全ての名前空間を同じ形で見せる。名前空間の番号は UAPI（`include/uapi/`）に置き、hal.h には HAL が実装しうる物（UEFI・Open Firmware の client interface）だけを並べる。
- 1 つの機械に名前空間は普通 1 つ（UEFI の PC なら UEFI だけ）。HAL は使える名前空間とその性質を `hal_fw_variable_info` で示す。

## 2. 差分の案

### 2.1 `include/hal/hal.h` に追加（承認が要る本体）

```c
/*
 * Firmware variables
 *
 * The named values a machine's firmware keeps across power cycles and
 * reads when it starts (UEFI variables, Open Firmware NVRAM options).
 * The HAL reaches only the spaces its firmware services own; a space
 * kept on ordinary storage (a U-Boot environment, a Raspberry Pi EEPROM
 * configuration) is not the HAL's.  Calls are serialized across CPUs,
 * may disable interrupts on the calling CPU for milliseconds, and must
 * not be made from interrupt context.  A platform without any space
 * returns HAL_ERR_UNSUPPORTED from every call.
 */

/*
 * The spaces of names the HAL can reach, one per firmware convention.
 * The kernel's interface numbers the other, storage-backed spaces (a
 * U-Boot environment, a Raspberry Pi EEPROM configuration, CHRP NVRAM
 * partitions) in the UAPI; 1 to 15 are kept for the HAL.
 */
enum hal_fw_variable_space {
	HAL_FW_VARIABLE_SPACE_UEFI = 1,		/* vendor GUID + UCS-2 name */
	HAL_FW_VARIABLE_SPACE_OPENFIRMWARE = 2	/* a firmware-served option: name = text value */
};

/*
 * The longest name, in bytes with its terminator: 512 UCS-2 units at three
 * bytes each.  UEFI names are carried in WTF-8 (UTF-8 that also encodes a
 * lone surrogate), so every name the firmware returns converts back to
 * the exact units it gave.
 */
#define HAL_FW_VARIABLE_NAME_MAX	1537
/* The largest value set accepts, in bytes. */
#define HAL_FW_VARIABLE_DATA_MAX	32768

/*
 * Attributes.  The low 16 bits are common to every space; bits 16-31
 * carry the space's own attributes unchanged: for UEFI, all of the
 * EFI_VARIABLE_* bits shifted left by 16.  get fills in both; set takes
 * the space's own bits when any is nonzero and refuses common bits that
 * disagree with them (HAL_ERR_INVALID), and otherwise derives them from
 * the common bits.
 */
#define HAL_FW_VARIABLE_NON_VOLATILE	0x0001u	/* kept across power cycles */
#define HAL_FW_VARIABLE_BOOT_ACCESS	0x0002u	/* the firmware reads it while starting */
#define HAL_FW_VARIABLE_RUNTIME_ACCESS	0x0004u	/* reachable after the OS started (in UEFI only such variables are visible at all) */
#define HAL_FW_VARIABLE_SPACE_SHIFT	16

/*
 * One variable's identity.  space chooses the convention; vendor is the
 * UEFI vendor GUID (in EFI_GUID memory order) and must be zero in other
 * spaces; name is WTF-8 (see NAME_MAX), terminated.
 */
struct hal_fw_variable_key {
	uint32_t space;
	uint8_t vendor[16];
	char name[HAL_FW_VARIABLE_NAME_MAX];
};

/* What one space offers on this machine. */
struct hal_fw_variable_space_info {
	uint32_t space;
	uint32_t flags;			/* HAL_FW_VARIABLE_SPACE_WRITABLE, ..._ENUMERABLE */
	/*
	 * For non-volatile variables reachable at run time (UEFI:
	 * QueryVariableInfo with NON_VOLATILE|BOOTSERVICE_ACCESS|RUNTIME_ACCESS);
	 * 0 when the firmware does not tell.  remaining_size is what the
	 * firmware reports, before the HAL's margin is taken off.
	 */
	uint64_t storage_size;
	uint64_t remaining_size;
	uint64_t largest_variable;
	uint64_t margin;		/* what set keeps free; 0 when it keeps nothing */
};
#define HAL_FW_VARIABLE_SPACE_WRITABLE		0x1u
#define HAL_FW_VARIABLE_SPACE_ENUMERABLE	0x2u

/*
 * Reports the index-th space this machine has (0 first).  Returns
 * HAL_ERR_INVALID past the last one, HAL_ERR_UNSUPPORTED with none.
 */
int
hal_fw_variable_space_info(
	unsigned index,
	struct hal_fw_variable_space_info *info);

/*
 * Reads a variable.  *size is the room in data on entry and the value's
 * size on HAL_OK, or the size needed with HAL_ERR_NOMEM (also above
 * DATA_MAX).  Returns HAL_ERR_INVALID for a variable that does not exist.
 */
int
hal_fw_variable_get(
	const struct hal_fw_variable_key *key,
	uint32_t *attributes,
	void *data,
	size_t *size);

/*
 * Writes a variable.  A size above DATA_MAX is HAL_ERR_INVALID.  Deleting
 * follows the space's rule: in UEFI, size 0 (or attributes 0) without
 * APPEND_WRITE deletes, size 0 with APPEND_WRITE changes nothing, and an
 * authenticated variable is deleted only with its authentication
 * descriptor as data.  The HAL refuses a write that grows the store past
 * its margin (HAL_ERR_NOMEM); a delete is never refused for room.  A
 * firmware's refusal (write protection, a failed authentication) is
 * HAL_ERR_STATE.
 */
int
hal_fw_variable_set(
	const struct hal_fw_variable_key *key,
	uint32_t attributes,
	const void *data,
	size_t size);

/*
 * Steps to the next variable of key->space.  An empty name starts the
 * walk; HAL_ERR_INVALID ends it; HAL_ERR_STATE means the previous name no
 * longer exists (deleted during the walk) and the caller starts again.
 * A space that cannot list its names returns HAL_ERR_UNSUPPORTED.
 */
int
hal_fw_variable_next(
	struct hal_fw_variable_key *key);
```

### 2.2 名前空間ごとの写し方（HAL の実装の方針、承認の後）

| 名前空間 | 名前と値 | 属性 | 列挙 | 容量 | 実装する HAL（この WS） |
| --- | --- | --- | --- | --- | --- |
| UEFI | vendor の GUID＋名前（UTF-8 ⇄ UCS-2）、値は byte 列 | 共通の 3 bit ⇄ `EFI_VARIABLE_NON_VOLATILE`・`BOOTSERVICE_ACCESS`・`RUNTIME_ACCESS`、bit 16〜31 に EFI の属性の全部（認証つきの書き込みの bit など、HAL は解釈せず渡す） | `GetNextVariableName` | `QueryVariableInfo` | amd64（UEFI の loader で起動した時）。arm64 の UEFI は後 |
| Open Firmware（client interface で名前の service を持つ物、sun4u の OBP など） | option の名前（vendor 0）、値は文字（終端なし） | NON_VOLATILE・BOOT_ACCESS・RUNTIME_ACCESS | firmware による | firmware による | 無し（後の WS。今は UNSUPPORTED） |
| CHRP NVRAM（ppc64 pseries の RTAS、ppc32 mac99 の直の NVRAM）・U-Boot の環境・Pi の EEPROM の設定 | 名前＝文字の値（記憶装置の形を OS が解く） | 同上 | 記憶装置の形から | 記憶装置の形から | HAL の外（kernel の driver か userland の道具、番号は UAPI）。Pi の EEPROM の書き込みは「次の起動で反映」の保留で、1 つずつの set と意味が違う（その道具が示す） |

- UEFI の実装（amd64）:
  - loader の handoff v8（`EFI_MEMORY_RUNTIME` の範囲と system table、v7 の形は不変、§2.3）。
  - HAL だけの page table: kernel の上半分は共有、下半分に runtime の範囲を 1:1（PTE は supervisor だけ、SMEP・SMAP に触れない。code は実行可、data は書き込み可・実行不可、MMIO は UC）、HAL の bounce buffer と stack（16 KiB）も 1:1。`SetVirtualAddressMap` は呼ばない。
  - 直列化と割り込み: 呼ぶ側の直列化は kernel の眠れる lock（mutex）で、待つ CPU は割り込みを止めない（TLB の shootdown の IPI を塞がない）。割り込みを止めるのは firmware の呼び出しそのものの間だけ。最悪の時間は `SetVariable` の flash の reclaim で数百 ms から秒（未確認、firmware による）。他の CPU は止めない（firmware の code は 1 つの CPU でだけ走る）。
  - 呼び出しの前の状態: kernel の FPU の状態を保存し、MXCSR 0x1F80・FCW 0x037F・DF 0 にする。Microsoft x64 の呼び出し規約（shadow space 32 byte、16 byte 境界の stack）。
  - 障害: 呼び出しの間の #PF・#GP・#UD は、通常の #PF の処理より前に HAL の復帰点が受ける（下半分の address を今の task の user の memory と取り違えない）。受けたら firmware の状態は分からないので、その名前空間を以後ずっと使えない（全ての呼び出しが HAL_ERR_UNSUPPORTED）にし、HAL_ERR_IO を返す。
  - 余裕（margin）: 書く前に `QueryVariableInfo`（NON_VOLATILE|BOOTSERVICE_ACCESS|RUNTIME_ACCESS）で残りを見て、残りが要求＋margin（既定 4 KiB、`space_info.margin`）未満なら書かない。delete は余裕で拒まない（満杯に近い store でも死んだ entry を消せる）。firmware が `QueryVariableInfo` を持たない（EFI 1.10）時は余裕を確かめず書き、`margin` を 0 で示す。
  - WRITABLE: UEFI 2.8 の `EFI_RT_PROPERTIES_TABLE` があれば、その `RuntimeServicesSupported` に `SetVariable` が無い時に WRITABLE を落とす（U-Boot の EFI・Pi の EDK2 は runtime の `SetVariable` を持たない、後の arm64 に効く）。表が無ければ WRITABLE。ENUMERABLE は `GetNextVariableName` の有無。
  - 値の出典: code の EFI の属性の bit と status の値には Guardrail の「Value referenced:」の block の comment を付ける。
- 名前空間の有無は `hal_get_arch_handoff` の名前でなく `hal_fw_variable_space_info` で示す（前の案の H2 は要らなくなる）。

### 2.3 loader → HAL（`bootloader/include/amd64-handoff.h`）

前の案の §2.1 と同じ（`struct zbl6_uefi_runtime_handoff`、`struct zbl6_handoff_v8_uefi`）。HAL の API ではないが HAL の責務の入力。
- 互換の規則: 今の HAL の handoff の検査（`src/hal/amd64/bsp-pcat/handoff-validation.c`）は版を完全に一致で受ける。v8 の HAL は v7 と v8 の両方を受け（v7 なら UEFI の名前空間を出さない）、v8 の loader は古い v7 だけの kernel を起動できない。導入先には installer が同じ build の loader と kernel を必ず組で置くので、この組み合わせは起きない（loader だけを入れ替える道具は無い）。

## 3. kernel と userland（HAL の外、承認の後に p007 で）

- `/dev/system` の root だけの要求 `KERN_SYSTEM_FW_VARIABLE_SPACES`・`GET`・`SET`・`NEXT`（struct は §2.1 の key と同じ形に版と大きさを付ける）。HAL の名前空間に加え、後で kernel の driver が U-Boot の環境などを同じ要求の下に出せる形（今は HAL だけ）。
- base の道具: 汎用の `fwvar`（`spaces`・`get`・`set`・`delete`・`list`）と、UEFI の boot entry の道具 `efiboot`（`list`・`create`・`delete`・`order`。`fwvar` と同じ要求を使い、Boot#### の device path を作る）。UEFI でない機械では `efiboot` は ENOTSUP。
- install-admin（p009）は spec §5.8 のとおり `efiboot` の code（共有の library）で Boot#### を作る。

## 4. 判断（ユーザーへ）

- **F1** 抽象の形: **A（推し）上の 1 つの key（名前空間＋vendor＋UTF-8 の名前）と 4 関数** ／ B 名前空間ごとに別の関数の組（`hal_uefi_variable_*`・`hal_of_nvram_*`）。A は kernel の上の口と道具を 1 つにでき、名前空間を足しても API が変わらない。
- **F2** HAL が持つ範囲: **A（推し）firmware を呼ぶ物だけ（UEFI、後に RTAS・OBP）。U-Boot・Pi の EEPROM は名前空間の番号だけ予約し driver・道具の仕事** ／ B 全部を HAL に（HAL が記憶装置の driver を持つことになる）。
- **F3** 承認の範囲: **A（推し）§2.1 の宣言と §2.2 の UEFI の実装の方針と §2.3 の handoff を一度に**。他の名前空間の実装は、その時に差分を示す ／ B 宣言だけ先に。

## 5. 承認の後の試験（p007）

- QEMU の OVMF（試験ごとの書き込める `OVMF_VARS` の写しを pflash の unit 1 に）: `fwvar spaces` が UEFI を出す、`efiboot create` → `efiboot list` で読み戻し、再起動の後も `BootOrder` の末尾、`efiboot delete`。BIOS の起動・UEFI 以外の platform で `HAL_ERR_UNSUPPORTED` と ENOTSUP。
- 余裕: store を余裕の手前まで埋め、書き込みが HAL_ERR_NOMEM で拒まれ、delete は通ること。
- 列挙: walk の途中で今の名前を消すと HAL_ERR_STATE、初めからやり直して全部を 1 回ずつ見ること。
- 名前: 長い名前（512 unit）、lone surrogate、BMP の外の文字が WTF-8 で往復すること（host の試験と OVMF）。
- 属性: 共通の bit と EFI の bit が食い違う set が HAL_ERR_INVALID、EFI の bit だけ・共通の bit だけの set が期待の属性で書かれること。
- 2 CPU: 2 つの process が同時に 1,000 回ずつ set・get を繰り返し、全ての get が自分の書いた値を読み、kernel の lock の検査と firmware の呼び出しの間の割り込みの禁止の時間（計測）の最大を記録すること。

## 6. design-reviewer の 1 回目（2026-10-29）の指摘と反映

| # | 指摘 | 反映 |
| --- | --- | --- |
| 1 blocker | UTF-8 の名前は lone surrogate と 512 byte を越える名前で `GetNextVariableName` の cursor を戻せない | WTF-8、`NAME_MAX` 1537（512 unit×3＋終端）（§2.1） |
| 2 blocker | RTAS は名前付きの変数でなく byte の offset、mac99 は直の NVRAM | CHRP NVRAM（pseries・mac99）を記憶装置の形として HAL の外に（§1・§2.2） |
| 3 major | bit 16〜31 の文が壊れていた、共通の bit との食い違い | 文を直し、set の規則（食い違いは INVALID）と get が両方を埋めることを書いた |
| 4 major | Open Firmware の属性に RUNTIME_ACCESS が無い、EFI との意味の違い | 表と `RUNTIME_ACCESS` の comment を直した |
| 5 major | size 0 で消す規則が UEFI の規則と合わない | UEFI の delete の規則（attributes 0、APPEND_WRITE、認証つき）を書いた |
| 6 major | 余裕の定義、delete の除外、`QueryVariableInfo` の属性と EFI 1.10 | `space_info.margin`、delete は拒まない、属性、無い時の扱い（§2.1・§2.2） |
| 7 major | WRITABLE の出所 | `EFI_RT_PROPERTIES_TABLE` から（§2.2） |
| 8 major | reclaim の時間、spinlock の待ちが IPI を塞ぐ | 眠れる lock、割り込みを止めるのは呼び出しの間だけ、最悪の時間を書いた |
| 9 major | 障害の後の firmware の状態、#PF の順、SMEP・SMAP、FPU・DF | 名前空間を以後使えなくする、HAL の復帰点を先に、PTE は supervisor、MXCSR・FCW・DF（§2.2） |
| 10 major | v8 の handoff と版の完全一致の検査 | 互換の規則（§2.3） |
| 11 minor | 列挙の終わりと cursor の消失が区別できない、vendor の 0 | 消失は HAL_ERR_STATE、vendor は UEFI の外で 0 必須 |
| 12 minor | DATA_MAX と get・set | get は大きさを返し NOMEM、set は INVALID |
| 13 minor | U-Boot・Pi の番号を hal.h に置くこと、時の注記 | UAPI へ、hal.h の enum は HAL の 2 つだけ |
| 14 minor | Pi の EEPROM は次の起動で反映 | §2.2 の表に注記 |
| 15 minor | 容量は属性による | `space_info` の comment に属性を書いた |
| 16 minor | 状態の行が §6 の無い「レビューを通した」を書いていた | 状態の行を直し、この §6 を足した |
| 17 minor | spec §5.8 の 2 の名前の記述 | spec §5.8 を同じ commit で直した |
| 18 minor | 試験の計画の不足 | §5 に足した |
| 19 minor | 値の出典の comment | §2.2 に書いた |

- 確かめていない事（reviewer も記憶からと述べた）: UEFI の runtime の stack の大きさの要求、flash の reclaim の時間。
