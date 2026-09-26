<!-- awesome-plan project=zedbsd record=ws049-design -->

# WS049 設計: kernel 内の ACPI AML interpreter

ws049-p001 の成果（2026-09-27）。[WS049](ws.md) の Phase 分けと受け入れはこの文書に従う。

## 0. 前提と出典

- 仕様: ACPI 6.5（§5 table、§19 ASL、§20 AML の文法）。実装は仕様から独立に書く。ACPICA（Intel、BSD/GPL の二重ライセンス）、
  Linux、NetBSD の source は読まず、取り込まない（[設計方針](../master-design-policy.md) §2.1 の独立実装）。
- host の道具としての ACPICA: `acpica-tools`（Debian の 20250404-1、`sudo apt-get install acpica-tools`）の `iasl`（ASL の compile と
  逆 assemble）と `acpiexec`（AML の実行器）を **host の試験の oracle と ASL の compiler として**使う。成果物には入れない。
- 2026-09-27 ユーザー指示（サブエージェントで WS049 を進める）。HAL の API の変更は差分ごとの事前承認（[Guardrail](../guardrail.md)）。

## 1. 今の状態: kernel が ACPI の table を得る道

調べた事実（amd64、`src/hal/amd64/`）:

1. UEFI の loader（`bootloader/uefi`）は EFI の configuration table の RSDP を `zbl6_handoff_v2.rsdp` に入れて渡す
   （`bsp-pcat/boot.c`: `prekern_bsp_acpi_rsdp()` が返す。BIOS の handoff では 0 を返し、HAL が EBDA と `0xE0000`〜`0xFFFFF` を走査する）。
2. HAL は `prekern_amd64_acpi_discover()`（`bsp-pcat/acpi.c`）で RSDP を検証し（signature、checksum、revision 2 以上の拡張 checksum）、
   XSDT/RSDT から MADT と MCFG だけを読む。RSDP の物理番地は HAL の外へ出ない。
3. kernel が HAL から boot の情報を得る口は `hal_get_arch_handoff(name)`（`include/hal/hal.h`）。amd64 の名前は `boot.command-line`、
   `boot.selector`、`pcat.boot-font`、`pcat.framebuffer` だけ（`bsp-pcat/boot.c`）。kernel は `kern_boot_handoff()`（`src/kern/pmem.c`）で呼ぶ。
4. ACPI の table の memory は allocator に入らない（`page.c` は `USABLE` と `BOOT_RECLAIM` だけを使う。`ACPI_RECLAIM`・`ACPI_NVS` は残る）。
   kernel は `hal_space_map_device()` で任意の物理範囲を uncached で読める（`space.c`、低位の `0xA0000`〜`0xFFFFF` は固定の窓）。
5. **OVMF（QEMU の UEFI、boot-test の既定の `uefi-usb`）では RSDP は低位の BIOS 領域に無い**（2026-09-27、QEMU q35 + OVMF で
   `0xE0000`〜`0xFFFFF` を QMP の `pmemsave` で読んで確認。SeaBIOS では有る）。対象機（UEFI だけの laptop）でも同じと見るべき。
   したがって **kernel が自分で走査する方式は UEFI で動かない**。

結論: kernel が RSDP を得るには HAL が RSDP の物理番地を渡す必要がある。**これは HAL の責務の追加で、差分ごとの承認が要る**（§9）。
それ以外（port I/O、memory の map、IRQ、PCI の config）は既存の HAL と driver の口で足りる見込み（§7）。

## 2. 利用者（WS050・WS051・WS052）の要求と interpreter の範囲

| 利用者 | AML に要ること |
| --- | --- |
| WS050 UCSI | `USBC000`/`PNP0CA0` の device を `_HID`/`_CID` で探す、`_STA`、`_CRS`（共有 memory の番地）、`_DSM`（UUID `6f8398c2-7ca4-11e4-ad36-631042b5008f`、function 1 = write、2 = read）。`_DSM` の中は SystemMemory の OperationRegion の Field と、EC の OperationRegion か SMI（SystemIO の `0xB2` への書き込み）。通知は `Notify (UBTC, 0x80)` を driver へ。 |
| WS051 DP Alt Mode | UCSI 経由（WS050）。AML としては WS050 と同じ。i915 の `_DSM`（GPU の device の）を使う可能性がある。 |
| WS052 S0i3 | FADT の flag、LPS0 device（`PNP0D80`）の `_DSM`（Intel UUID `c4eb40a0-6cd2-11e2-bcfd-0800200c9a66`、Microsoft UUID `11e00d56-ce64-47ce-837b-1f898f9aa461`）、各 device の `_PS0`/`_PS3`/`_PR0`/`_PR3`（PowerResource の `_ON`/`_OFF`/`_STA`）、`_DSW`、`_PRW`（wake の GPE）、`_Sx`・`_SxW`。wake: GPE の `_Lxx`/`_Exx`、EC の `_Qxx`、固定 event（電源 button）。 |
| 共通 | device の列挙（`_HID`/`_CID`/`_UID`/`_ADR`/`_STA`/`_CRS`）、`_INI`、`_OSC`（PCIe の制御の受け渡し）、`_PRT`（PCI の INTx の経路）、`_OSI`。Intel の機種は `_PDC`/`_OSC` の中で CPU の SSDT を `Load`/`LoadTable` で読み込む。 |

以上から、**AML の言語は実質的に全部**要る（実機の DSDT は iasl の出す opcode のほぼ全部を使う）。調べた実例（2026-09-27、host の
Fujitsu PRIMERGY RX2530 M4 の DSDT 272 KiB と SSDT 3 つ、QEMU q35/pc の DSDT。table は git に入れない）: `If` 5671、`Return` 4643、
`Method` 3465（うち Serialized 2529）、`Name` 3424、`Package` 2670、`CreateDWordField` 1567、`Sleep` 1263、`Notify` 615、`LoadTable` 448、
`Switch`/`Case` 156/306（AML では `While`+`If` に展開される）、OperationRegion は PCI_Config 98・SystemIO 136・SystemMemory 93、
Field の access は ByteAcc/AnyAcc/DWordAcc/WordAcc/QWordAcc、Lock、Preserve/WriteAsZeros。laptop は EmbeddedControl が加わる。

範囲（WS049）:

- 全ての AML opcode の解析と namespace の構築（ACPI 6.5 §20.2 の全ての TermObj）。
- 評価: 全ての式の opcode、制御、method の呼び出し、Serialized、method の中で作る名前の後始末、integer の幅（DSDT の revision が 2 未満なら 32 bit）。
- OperationRegion: Field・IndexField・BankField・BufferField、access の幅、Lock、Update rule、`_REG`、DataTableRegion。
  address space の handler は登録制（SystemMemory・SystemIO・PCI_Config・EmbeddedControl を zedBSD が持つ。GenericSerialBus・GPIO・
  SMBus・IPMI・PCC は handler が無ければ AE_NOT_EXIST 相当の失敗を返す。要るものは利用者の WS で足す）。
- 同期: Mutex（SyncLevel の規則）、Event、Sleep、Stall、Timer。Notify は driver が登録した handler へ。
- `Load`・`LoadTable`・`Unload`（Intel の CPU の SSDT）。`Fatal` は記録して評価を失敗させる。
- kernel への組み込み: table の発見、region の handler、`_INI`/`_STA` の初期化、SCI・GPE・固定 event、EC。

範囲外: ACPI の table のうち AML でないものの利用者側（MADT・MCFG は HAL にある）、S3/S4、`_Sx` の実行（WS052）、UCSI 自体（WS050）。

## 3. 構成と source の置き場

AML interpreter は driver として `src/drivers/acpi/` に置く（global symbol は `drv_acpi_`）。HAL の外、kernel の platform 非依存の部分。
arm64 の ACPI 機（Raspberry Pi 4 は DT）でも同じ core を使える。

```
include/drivers/acpi/acpi.h     driver 向けの API（node、評価、object、notify、region の handler）
src/drivers/acpi/
  aml-internal.h                interpreter の内部の型（object、node、評価の文脈）
  aml-os.h                      interpreter が OS に求める口（kernel と host 試験がそれぞれ実装する）
  aml-object.c                  object の生成・参照数・複製・型の変換（ACPI 6.5 §19.3.5 の暗黙の変換）
  aml-namespace.c               namespace の node、NameString の解決、生成・削除、列挙、path の表示
  aml-stream.c                  byte 列の読み出し（PkgLength、NameString、整数の data、opcode）
  aml-eval.c                    評価器の核: TermList の実行、制御、method の呼び出し、局所と引数
  aml-operator.c                式の opcode（算術・論理・比較・変換・文字列と buffer・Index/DerefOf/RefOf/Match/Mid・Concatenate）
  aml-define.c                  名前付きの object の定義（Name/Scope/Device/Method/OperationRegion/Field/Mutex/...）
  aml-field.c                   OperationRegion の access、Field・IndexField・BankField・BufferField、`_REG`
  aml-table.c                   DefinitionBlock の読み込み、Load・LoadTable・Unload
  aml-sync.c                    Mutex・Event・Sleep・Stall・Timer・Notify の配送
  acpi-tables.c                 RSDP・XSDT・FADT の解析と table の複写（物理 memory の読み出しは口を通す。host で試験できる）
  acpi-kern.c                   kernel の glue（aml-os.h の実装、region の handler、boot 時の読み込み、`/dev` か sysctl の診断口）
  acpi-event.c                  SCI・GPE・固定 event
  acpi-ec.c                     Embedded Controller（EmbeddedControl の region と `_Qxx`）
```

`aml-*` は kernel の header に依存しない（`<kern/kcrt.h>` の `kern_mem*`/`kern_str*` と `aml-os.h` だけを使う）。kcrt は host では host の
libc に転送する（`include/kern/kcrt.h` の host face）ので、同じ source を host で compile して試験する。

### 3.1 API（`include/drivers/acpi/acpi.h`、p002〜p005 で確定）

- `struct drv_acpi_node`（namespace の node。driver は pointer だけを持つ）と `struct drv_acpi_object`（評価の結果、参照数を持つ）。
- `drv_acpi_lookup(scope, path, &node)`、`drv_acpi_walk(scope, depth, callback, arg)`、`drv_acpi_node_path(node, buffer, size)`。
- `drv_acpi_evaluate(node, path, arguments, count, &result)`、`drv_acpi_evaluate_integer(...)`、`drv_acpi_object_release(object)`。
- `drv_acpi_notify_install(node, handler, arg)`（`Notify` を受け取る）、`drv_acpi_region_install(space, handler, arg)`（address space の handler）。
- 状態の報告は zedBSD の errno（`uapi/errno.h`）: 名前が無い `ENOENT`、型が違う `EINVAL`、AML の実行時の誤り `EIO`、handler が無い `ENODEV`、
  memory `ENOMEM`、stack の予算を超えた `E2BIG`、時間切れ `ETIMEDOUT`。

## 4. interpreter の設計

### 4.1 object

一つの struct（`struct drv_acpi_object`）に型と参照数と値を持つ。型は ACPI の ObjectType の番号（0 Uninitialized、1 Integer、2 String、
3 Buffer、4 Package、5 FieldUnit、6 Device、7 Event、8 Method、9 Mutex、10 OperationRegion、11 PowerResource、12 Processor、
13 ThermalZone、14 BufferField、15 DDBHandle、16 Debug）と、内部の型（Reference: namespace の node への参照、Local/Arg の参照、
Package/Buffer/String の要素への Index の参照）。

- String と Buffer は長さと heap の byte 列を持つ。Package は要素の object の配列（要素は参照数で共有しない。Store で複製する）。
- 参照数は `Store` の複製の規則を守るためのもので、Package の要素、局所変数、namespace の node が持つ。0 になれば解放する。
- Store の規則（ACPI 6.5 §19.3.5.8）: Integer・String・Buffer・Package は値を複製する。名前付きの Integer/String/Buffer への Store は
  目的の型へ暗黙に変換する。Field と BufferField への Store は整数か buffer へ変換して書く。Local/Arg への Store は変換しない
  （Arg が参照を持つときは参照先へ）。CopyObject は変換しない。

### 4.2 namespace

node は 4 文字の名前（`uint32_t`）、親、最初の子、次の兄弟、object、所有者（読み込んだ table か、method の実行）を持つ木。
根に `_GPE`・`_PR_`・`_SB_`・`_SI_`・`_TZ_` と `_OSI`・`_OS_`・`_REV`・`_GL_` を予め作る。

- NameString の解決: `\` は根から、`^` は親へ、1 つの NameSeg の相対の参照は現在の scope から根へ向かって探す（§5.3 の search rule）。
  2 つ以上の NameSeg の相対 path は現在の scope からだけ探す。定義（Name、Device など）は探さず、現在の scope の下に作る。
- 同じ名前の再定義は、読み込み時は誤りとして記録して先の定義を残す（実機の table の重複に耐える。acpiexec の振る舞いと同じ）。
- method の中で作った名前は、その method の呼び出しが終わると削除する（呼び出しの frame が作った node の一覧を持つ）。
- `Unload` は、その table が作った node を全部削除する（node の所有者で判定する）。

### 4.3 評価の方式

AML の byte 列を直接たどる再帰下降の評価器にする（事前の構文木は作らない）。method の body は呼び出しのたびに先頭から解釈する。

- 評価の文脈（`struct drv_acpi_eval`）: 現在の method の frame（Local0〜7、Arg0〜6、現在の scope、作った node の一覧、戻り値）、
  byte 列の位置と終わり、制御の状態（Return・Break・Continue）、stack の予算。
- TermArg の位置の NameString は、解決先が Method なら呼び出し（引数の数は Method の定義から）、それ以外は値。
  SuperName・Target の位置は参照として解く（呼び出さない）。`CondRefOf` は名前が無くても失敗しない。
- 読み込み時（DefinitionBlock の最上位）の実行は method の body を解釈しない（PkgLength で飛ばす）。最上位の `If`・`Store`・式は実行する。
  前方参照は最上位では起きにくいが、method の引数の数が分からない名前（未定義か `External` だけ）は、`External` の宣言があれば
  その数を使い、無ければ 0 とする（最上位の呼び出しは稀。実機での誤りは試験で見る）。
- 誤りは errno で伝え、method の呼び出しの全体を失敗させる（途中の状態は元に戻さない。仕様も戻さない）。

### 4.4 stack の予算（kernel の stack は 16 KiB）

amd64 の kernel の thread の stack は 16 KiB（`src/hal/amd64/task.h` の `AMD64_SYS_STACK_SIZE`）。再帰下降は method の呼び出しの
入れ子と式の入れ子の分だけ C の stack を使うので、次で抑える。

- method の frame（局所・引数・一覧）は heap に置き、C の stack には pointer だけを置く。
- 評価の関数の frame を小さく保つ（`-Wframe-larger-than` と host の `-fstack-usage` で確かめる）。
- 評価の入口で stack の位置を覚え、再帰のたびに現在の位置との差を予算（kernel は 8 KiB、`aml-os.h` の定数）と比べ、超えたら
  `E2BIG` で評価を失敗させる（overflow させない）。method の呼び出しの深さ（既定 32）と `While` の反復の上限（時間で打ち切る、既定 30 秒）も持つ。
- host の試験で実機の table の評価が使う最大の深さを測り、予算の妥当性を記録する（p005）。

### 4.5 並行性

interpreter 全体を一つの sleep できる mutex（`aml-os.h` の interpreter lock）で直列にする。`Sleep`・`Acquire` の待ち・`Wait` の待ちの間だけ
lock を放す。Serialized の method は method ごとの mutex（SyncLevel 付き）を持ち、lock を放す間の再入を防ぐ。AML の Mutex は
所有者（評価の文脈）と SyncLevel を持ち、SyncLevel の規則（ACPI 6.5 §19.6.2）を検査する。`_GL_`（Global Lock）は FACS の lock を使う
（p014 で実装。kernel は event を始めた後に FACS を写して attach し、それまでは内部の mutex として扱う）。

## 5. OperationRegion と Field

- OperationRegion は space の番号、offset、長さを評価して持つ（PCI_Config は device の `_ADR`・`_BBN`・`_SEG` を初めての access で解く）。
- Field の読み書きは ACPI 6.5 §19.6.46 の規則: access の幅（AnyAcc は field の大きさと整列から選ぶ）、幅の整列した単位に分けて読み、
  書き込みは Update rule（Preserve は読んで変えて書く、WriteAsOnes/Zeros は残りを埋める）、Lock は `_GL_` を取る。
  BufferAcc（GenericSerialBus など）は handler に buffer で渡す。
- IndexField は index の field に byte offset を書いてから data の field を読み書きする。BankField は bank の field に値を書いてから読み書きする。
- region の handler は `(space, 読むか書くか, 物理番地か offset, 幅 bit, 値)` を受ける関数と文脈。登録は space ごとに一つ。
  読み込みの後の `drv_acpi_region_connect_all()` と、その後の handler の登録で、その space の region ごとに同じ scope の `_REG(space, 1)` を呼ぶ
  （p004 で実装）。SystemMemory・SystemIO は常に有るので呼ばない（ACPI 6.5 §6.5.4）。PCI_Config と EmbeddedControl は呼ぶ（acpiexec は
  PCI_Config の `_REG` を呼ばないが、仕様どおりにした）。
- 読み書きの失敗は method の評価を失敗させる。handler が無い space の access も失敗させる（誤った値を返さない）。

## 6. OS の口（`aml-os.h`）

interpreter が OS に求めるもの。kernel は `acpi-kern.c`、host 試験は試験の harness が実装する。

- memory: `drv_acpi_os_alloc(size)`・`drv_acpi_os_free(p)`（kernel は `kern_malloc`/`kern_free`）。
- 時間: `drv_acpi_os_sleep(ms)`（interpreter lock を放して眠る）、`drv_acpi_os_stall(us)`（busy wait）、`drv_acpi_os_timer()`（100 ns 単位の単調な時計）。
- 同期: interpreter lock の取得と解放、待ちの口（AML の Mutex と Event の待ちを OS の waitq で行う）。
- 記録: `drv_acpi_os_log(...)`（`Debug` への Store、`Fatal`、読み込み時の警告）。
- table: `Load`・`LoadTable` が OEM ID で table を探す口（kernel は XSDT の table の一覧から）。

## 7. kernel への組み込み（p006・p007）

- 起動時: `acpi-kern.c` の初期化（PCI の列挙の後、driver の probe の前）が `kern_boot_handoff("acpi.rsdp")` で RSDP の物理番地を得る。
  得られなければ ACPI を無効にして進む（今の HAL と、ACPI の無い platform）。`acpi-tables.c` が RSDP・XSDT・FADT を検証し、DSDT と
  全ての SSDT を heap へ複写して読み込む。`_INI`・`_STA` を仕様の順に評価する。
- region の handler: SystemMemory は `hal_space_map_device()`（page 単位の map を cache する）、SystemIO は `hal_io_inp*`/`hal_io_outp*`、
  PCI_Config は PCI の driver の config の読み書き（未列挙の function も読める口が `drivers/pci` に要る。p006 で確かめる）。
- SCI: FADT の `SCI_INT` を `hal_irq_register()` で受け、PM1 の status と GPE の status を port I/O で読む（level の GPE は `_Lxx` の後に clear）。
  AML の実行は割り込みの中でせず、kernel thread（`kthread_create`）で行う。
- EC: ECDT か `PNP0C09` の device の `_CRS`（port 0x62/0x66 など）、EmbeddedControl の region の handler、SCI_EVT で `_Qxx`。
- 診断: kernel の中の namespace と評価を user から確かめる口。p013 で UAPI を足さない text の `/dev/acpi` にした（read で namespace の
  一覧、path を write すると続く read で評価の結果。出力は host の harness と同じ `acpi-text.c`）。起動の確認は `plan/tools/boot-test.sh`、機能の確認は guest の中の command をシリアルか SSH で。

## 8. `_OSI`

`_OSI("Windows 20xx")` に真を返す版の一覧を持つ（p004 の実装は `Windows 2022` までと `Extended Address Space Descriptor`。ACPICA の既定と同じ。ユーザーの判断までの仮の既定）。Alder Lake の laptop の firmware は Windows の版で機能（modern standby、USB-C、
Thunderbolt）を切り替えるので、**Windows 10/11 の版まで（`Windows 2022` まで）真**を既定の案とする（Linux の既定と同じ考え方）。
`Linux` と `Darwin` は偽。`Module Device`・`Processor Device`・`3.0 Thermal Model`・`3.0 _SCP Extensions`・`Processor Aggregator Device` は、それを扱う driver ができるまで名乗らない（Linux は driver があるときに足す）。
**どの Windows を名乗るかはユーザーの判断**（WS049 の ws.md の判断の点）。実装は一覧を一か所の表にして変えやすくする。

## 9. HAL の差分の案（承認が要る。適用しない）

### 9.1 案 A（推奨）: `hal_get_arch_handoff("acpi.rsdp")`

意味: HAL が検証した RSDP の**物理番地**を持つ HAL 所有の `uint64_t` を返す。RSDP が無い・検証に失敗したなら NULL。
`hal.h` の宣言は変えない（名前を足すだけ）が、HAL の責務（kernel へ渡す情報）を足すので承認が要る。
提案の差分: [proposed/hal-acpi-rsdp.diff](proposed/hal-acpi-rsdp.diff)（amd64 の pcat。i386 の pcat は同じ形で後から、pc98 は ACPI が無い）。

- `bsp-pcat/acpi.c`: `find_rsdp()` が受け入れた物理番地を `discovered_rsdp_physical` に残す（loader が渡したものか、BIOS の走査で見つけたもの）。
  `amd64_acpi_rsdp_handoff()` がその番地の pointer（0 なら NULL）を返す。
- `bsp-pcat/acpi.h`: `amd64_acpi_rsdp_handoff()` の宣言。
- `bsp-pcat/boot.c`: `hal_get_arch_handoff()` に `acpi.rsdp` を足す。

名前を `pcat.` でなく `acpi.` にするのは、arm64 の ACPI 機（UEFI の server）でも同じ意味で渡せるから。

### 9.2 代案

- 案 B: kernel が RSDP を自分で走査する。BIOS の起動だけで動き、UEFI（OVMF・対象機）では動かない（§1 の 5）。**不採用**。
- 案 C: UEFI の loader が boot parameter の文字列に `acpi.rsdp=0x...` を足し、kernel が `boot.command-line` から読む。HAL を変えないが、
  利用者に見える設定の文字列に機械の情報を混ぜ、BIOS の起動では HAL の走査の結果を渡せない。**推奨しない**。
- 案 D: HAL が XSDT の table の一覧（署名、物理番地、長さ）を渡す。kernel の table の解析は減るが、HAL の持つ構造が増え、`LoadTable` の
  ための全 table の一覧が要るのは kernel の側なので、RSDP だけを渡して kernel が解析する A の方が HAL が小さい。

## 10. 試験の方法

host の試験を中心にする（guest を起動しない）。道具は `plan/ws049/tests/`、生成物は `build/ws049/`（git に入れない）。

1. **host の harness**（`plan/ws049/tests/aml-host.c` と `Makefile`）: `src/drivers/acpi/aml-*.c` を host の cc で compile し、
   `aml-os.h` を host で実装する（memory、時計、region の handler は記録つきの疑似の memory・port・PCI の config）。
   command: `aml-host load TABLE...`（namespace の一覧を acpiexec と同じ形で出す）、`aml-host eval TABLE... -- PATH`、`aml-host test ASL-AML...`。
   `-fsanitize=address,undefined` で走らせる。
2. **自作の ASL の試験**（`plan/ws049/tests/asl/*.asl`）: 各 file の `MAIN` method が自分で結果を確かめ、失敗の番号（0 は成功）を返す。
   iasl で AML に compile する（`build/ws049/asl/`）。同じ AML を `acpiexec` でも走らせて、試験そのものが正しいことを確かめる。
3. **実機と QEMU の table**: QEMU の q35・pc の table（`qemu-acpi-dump.py` が QMP の `pmemsave` で読む。console は読まない）、
   host の Fujitsu PRIMERGY の table（`sudo acpidump -b`）、対象機（Latitude 5330）の table（Linux が動いているときに `acpidump -b`。
   ユーザーの手を借りるか ssh で）。**table は git に入れない**（firmware の著作物。SHA256 だけを記録する）。
4. **oracle との比較**: 読み込んだ namespace（node の path と型）を `acpiexec -b "namespace"` と比べる。全 device の `_HID`・`_CID`・`_UID`・
   `_ADR`・`_STA`・`_CRS` の評価の結果を `acpiexec -b "evaluate ..."` と比べる。acpiexec の region は 0 で始まり書いた値を覚える疑似なので、
   harness の疑似の region も同じ振る舞いにする。
5. kernel に入れた後（p006 以降）: build（warning 0）、`plan/tools/boot-test.sh`、guest の中の診断 command（シリアルか SSH）。

## 11. Phase の分け方

| Phase | 内容 | 依存 | 受け入れ |
| --- | --- | --- | --- |
| p001 | 調査と設計（この文書） | — | この文書、HAL の差分の案 |
| p002 | object・namespace・byte 列・DefinitionBlock の読み込み（名前付き object の定義、最上位の式、method は飛ばす）、host の harness と table の入手の道具 | p001 | q35・pc・PRIMERGY の DSDT・SSDT の namespace（path と型）が acpiexec と一致。ASan/UBSan で誤り 0。規約の検査 0 件 |
| p003 | 評価器: method の呼び出し、制御、全ての式の opcode、参照（RefOf・DerefOf・Index・CondRefOf）、変換、Store/CopyObject の規則、method の中の名前の後始末、integer の幅 | p002 | 自作の ASL の試験（region を使わないもの）が全部成功し、acpiexec でも同じ結果。規約の検査 0 件 |
| p004 | OperationRegion・Field・IndexField・BankField・BufferField・DataTableRegion、region の handler の登録と `_REG` | p003 | region の ASL の試験が全部成功。q35・pc・PRIMERGY の全 device の `_HID`・`_CID`・`_UID`・`_ADR`・`_STA`・`_CRS` が acpiexec と一致 |
| p005 | 同期と OS の口: Mutex・Event・Sleep・Stall・Timer・Notify の配送・`_OSI`・Load/LoadTable/Unload・Fatal、`_INI` の初期化の順、stack の予算の測定 | p004 | 同期と Load の ASL の試験。PRIMERGY の `_INI` の全体と `_PDC`/`_OSC` の LoadTable が acpiexec と同じ node を作る。最大の stack の深さの記録 |
| p010 | p006 の前半: `acpi-tables.c`（host の疑似の物理 memory で試験）と `acpi-kern.c`（kernel の flag で compile）。共有の build の file は触らない | p005 | 疑似の firmware からの namespace が file からと一致。kernel-check warning 0 |
| p006 | kernel への組み込み（amd64）: link（`CONFIG_DRIVER_ACPI`、vmunix.mk）、`drv_acpi_attach()` の呼び出し、診断の口 | p010、**§9 の HAL の差分の承認** | QEMU（q35、OVMF）で guest の中から namespace の一覧と `_STA`・`_CRS`・`_HID` の評価。build（warning 0）、boot test |
| p011 | p007 の前半: event の核と EC を host の疑似の hardware で | p010 | GPE・固定 event・EC の ASL の試験（harness）。kernel-check warning 0 |
| p007 | SCI・GPE・固定 event（電源 button）・EC（`_Qxx`）・Notify の driver への配送を kernel で | p006、p011 | QEMU で電源 button（QMP の `system_powerdown`）の固定 event か GPE が handler に届く。EC は実機（p008） |
| p008 | 対象機: Latitude 5330 の table の host 試験と実機での確認 | p007、対象機の table（ユーザーの手か ssh） | 5330 の table を host で読み込み acpiexec と一致。実機で `_STA`・`_CRS`・`_HID` と、EC の `_Qxx`（lid か電源 button）が届く |
| p009 | 規約の全文の確認（WS の全 source）と最終の確認 | p002〜p008 | 規約の全文、build、boot test |

p002〜p005 は HAL の承認を待たずに進められる（host だけ）。

## 12. 人間の判断が要る点

1. **§9 の HAL の差分（`acpi.rsdp`）の承認**。p006 以降の前提。
2. `_OSI` で名乗る Windows の版（§8。案は `Windows 2022` まで）。
3. 対象機は Latitude 5330 でよいか。その table の取り出し（Linux が動いているときに `sudo acpidump -b`）。2026-09-27 の時点で
   5330（`10.0.10.25`）は ssh に応答しない。
4. 診断の口の形（p013 の案: text の `/dev/acpi`、device 番号 `0x000B0000`。ioctl か `/dev/system` への統合にするかの判断）。
