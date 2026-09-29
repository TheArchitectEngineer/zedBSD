<!-- awesome-plan project=zedbsd record=ws080-design -->

# WS080 の設計: `ld.coff`（PE32+ の動的ローダ）と互換の DLL の基盤

2026-09-29 ws080-p001。仕様の原文は [spec.md](spec.md)、計画と決定は [ws.md](ws.md)。この文書は v0.1（自作の最小の PE EXE + DLL が動く）までの設計と、
その後の互換の DLL（kernel32 以降）を下位のモデルが回せる作業の型を決める。kernel と HAL の差分は [proposed/hal-gs-base.md](proposed/hal-gs-base.md)
（**案。未適用。差分ごとの承認が要る**）。

用語: PE の側（PE の EXE・DLL の code）は Microsoft x64 ABI、native の側（`ld.coff` 自身・kernel）は zedBSD の System V の ABI。
OS に見える名前には「Win64」「Windows」を使わない（ws.md の決定 2）。この文書の中の「Win64」は ABI を説明する技術の用語。

## 1. 原則と範囲

- 3 つの層（spec §2）: PE/COFF の形式（`ld.coff`）、Win64 の ABI の互換（`ld.coff` の GS・TEB・PEB・呼び出しの境界）、互換の DLL（`userland/desktop/w64/`）。
- kernel は汎用の機能だけを足す: **thread ごとの user の GS base**（p002）と **exec の PE の検出**（p003）。TEB・PEB・DLL・import は kernel に入れない。
  kernel は GS base の値の意味（TEB）を知らない。
- v0.1 の範囲は spec §24。延期（spec §25）: static TLS、TLS callback、`DLL_THREAD_ATTACH/DETACH`、完全な TEB・PEB、SEH・C++ 例外・unwind の実行、
  delay import、bound import、API Set、SxS、KnownDLLs、CFG、署名の検証、PE32、ARM64。**拡張の余地だけは構造に残す**（§8 の module の field、§12 の TEB）。

## 2. 全体の流れ

```
exec("/home/kei/hello.exe", argv)          （p003 の後。p003 の前は "ld.coff hello.exe ..." と直接起動しても同じ）
  │ kernel: 先頭の "MZ" と e_lfanew の "PE\0\0"・Machine 0x8664・Magic 0x20b を見る（#! と同じ経路）
  ▼
/usr/libexec/ld.coff  argv = ["/usr/libexec/ld.coff", "/home/kei/hello.exe", 元の argv[1..]]
  │ 1. 引数・環境を読む（COFF_LIBRARY_PATH、COFF_DEBUG）
  │ 2. EXE を解析・検証 → SizeOfImage を予約 → section を配置 → relocation
  │ 3. 依存の DLL を再帰に map（深さ優先）→ relocation
  │ 4. 全ての module の import を束縛（IAT）→ section の最終の保護
  │ 5. PEB・main の thread の TEB・stack を作り、thread_self(SET_GSBASE, TEB)
  │ 6. DllMain(DLL_PROCESS_ATTACH) を依存の後順で
  ▼
EXE の ImageBase + AddressOfEntryPoint へ（新しい stack の上、MS ABI の call）
  │ entry が戻る → exit(戻り値)
```

## 3. kernel の汎用の機能（p002・p003）

### 3.1 user の GS base（p002、案 A: swapgs。HAL の承認が要る）

詳細と差分は [proposed/hal-gs-base.md](proposed/hal-gs-base.md)。要点:

- 今の amd64 の HAL は `IA32_GS_BASE` に kernel の per-CPU の pointer を置いたまま user に戻る（`swapgs` を使わない）。user の `%gs:` の参照は
  supervisor の page で fault し、CR4.FSGSBASE は立っていない（`rdgsbase`・`wrgsbase` は #UD）ので、今は漏れも書き換えも無い。
- 案 A: user から kernel に入る全ての入口で `swapgs`、user へ戻る全ての出口で `swapgs`。user の GS base は task ごとに `IA32_KERNEL_GS_BASE` の
  保存・復元（FS base と同じ扱い）。入口の判断は保存した CS の ring。**NMI・#MC・#DF・#DB は「paranoid」**（`IA32_GS_BASE` の値の上半分・下半分で判断し、
  入れ替えたときだけ戻りで戻す）。user の GS base は下半分の address だけを受け、CR4.FSGSBASE は立てない（user が任意の値を書けない）。
- hal の API（承認が要る）: `include/hal/arch/amd64.h` に `hal_amd64_task_set_user_gs_base()`・`hal_amd64_task_get_user_gs_base()`、
  `struct hal_gpregs` の `gs_base` を「予約」から「user の GS base」に（ptrace で見える・書ける）。
- UAPI（kernel、HAL の外）: `thread_self` に `KERN_THREAD_SELF_GET_GSBASE`（3）・`KERN_THREAD_SELF_SET_GSBASE`（4）。amd64 以外は `-EOPNOTSUPP`。
  `thread_create` の `args[4]`（今は 0 を要求）を「新しい thread の最初の GS base」（amd64、0 は無し）にし、新しい thread が GS 無しで走る瞬間を無くす。
  fork は親の値を写し、exec は 0 に戻す。
- XMM: kernel は `-mgeneral-regs-only`、`vmunix` の逆アセンブルで XMM を触る関数は FP の状態の保存・復元（`fxsave64`/`fxrstor64`、signal の frame、
  ptrace の fpregs、self test）だけ（2026-09-29 に ws073-p032 の vmunix で確認）。**syscall は XMM0〜15・MXCSR・x87 の CW を保つ**。
  syscall の stub は XMM を保存しない（§14）。kernel が syscall の経路で SSE を使うようになったらこの前提が崩れるので、p005 の guest の試験で固定する。

### 3.2 exec の PE の検出（p003、kernel だけ）

- `src/kern/exec.c` の target の解決の loop（今は `#!` を見る）に、`#!` の検査の後で PE の検査を足す: 読んだ先頭の header（既存の buffer）で
  `MZ`、`e_lfanew`（0x3c）が buffer の中、`PE\0\0`、`Machine == 0x8664`、Optional header の `Magic == 0x20b`。満たせば interpreter を
  `/usr/libexec/ld.coff`、optional の引数無しとして `exec_script_argv_build()` を使う（argv は `#!` と同じ規則: `[ld.coff, EXE の path, 元の argv[1..]]`）。
  深さの上限と循環の検出は既存のもの。path を渡せない exec（fd からの exec）は `#!` と同じく ENOEXEC。
- EXE の setuid・setgid の bit は `#!` と同じく interpreter の起動では効かせない（既存の扱いに従う）。
- 他の PE（PE32、ARM64、DLL を直接 exec）は kernel は通すか拒むかを決めず、Magic・Machine の違うものは ENOEXEC（kernel は ELF として読まず失敗）。
  DLL（Characteristics の IMAGE_FILE_DLL）の exec は ld.coff が拒む。
- **既定（判断が要る点 D5）**: 元の `argv[0]`（打たれた名前）は失われ、EXE の path が PE の argv[0] になる（`#!` と同じ）。PE の command line は
  EXE の path から作る。

## 4. `ld.coff` の構成

### 4.1 形と置き場

- `/usr/libexec/ld.coff`。source は `userland/base/ld-coff/`（決定 3）。
- **静的に link した freestanding の ELF で、libc を使わない**（既定 D1。ld.so と同じ考え）。理由:
  1. PE の thread（kernel32 の `CreateThread` が UAPI で作る）から `LoadLibrary`・`GetProcAddress` の service（§11）を呼ぶ。libc を使うと
     その thread に libc の TLS（FS）が要り、PE の側が作る thread では用意できない。libc を使わなければ FS は 0 のままでよく、どの thread からでも呼べる。
  2. 起動の依存が無い（ld.so にも libc.so にも頼らない）。
  3. native の側（ld.coff）が FS を使わないので、PE の側の GS と干渉しない。
- 使う syscall: `open`・`openat`・`pread`・`read`・`close`・`fstat`・`getdents`（大文字小文字の探索）・`mmap`・`mprotect`・`munmap`・`write`（診断）・
  `exit`・`thread_self`・`usync`（loader の lock）・`clock_gettime`（無くてよい）。native の ABI の syscall の wrapper を ld.coff の中に持つ（libc の
  `crt0-amd64.S` と同じ規則）。
- memory: 小さな arena の allocator（mmap で 64 KiB ずつ、lock の中で使う）。loader の data は process の終わりまで返さない（v0.1 は unload 無し、§10.4）。

### 4.2 file の構成（p006〜p008 で作る）

| file | 役割 |
| --- | --- |
| `start.S` | ELF の `_start`（stack の argv・envp を C へ）、`coff_enter_image`（§12 の stack の切り替えと EXE の entry の call） |
| `main.c` | 引数・環境、全体の順序（§2）、失敗の報告 |
| `pe.c`・`pe.h` | PE32+ の header の解析と検証（§5）。構造体は PE の仕様の名前と固定の offset（`_Static_assert`） |
| `map.c` | 予約・section の配置・0 埋め・保護（§6） |
| `reloc.c` | base relocation（§7） |
| `module.c` | module の namespace・名前の正規化・探索（§8・§9） |
| `import.c`・`export.c` | import の束縛、export の解決と forward（§10） |
| `init.c` | 依存の graph の順序、DllMain、EXE の entry（§10.3・§12） |
| `env.c` | PEB・TEB・process parameters・command line（§13） |
| `service.c` | PE の側へ出す loader の service（§11、`ms_abi`） |
| `os.h`・`os-zedbsd.c` | platform の層（下の表）の zedBSD の実装（UAPI） |
| `os-host.c` | 同じ層の host（Linux）の実装。host の試験（§16.2）用で、image には入らない |
| `rt.c` | memcpy・memset・strlen 等の最小の部品（libc を使わないため） |

platform の層（ld.coff の他の部分は OS を直接呼ばない）:

| 関数 | 意味 |
| --- | --- |
| `coff_os_open(path)`・`coff_os_pread(fd, buf, n, off)`・`coff_os_close(fd)`・`coff_os_file_size(fd)` | file |
| `coff_os_list_dir(dir, callback)` | 大文字小文字を区別しない探索のための directory の走査 |
| `coff_os_reserve(hint, size, exact, &base)` | address の予約（`mmap` の anonymous・`PROT_NONE`、`exact` なら `MAP_FIXED_NOREPLACE`） |
| `coff_os_protect(addr, size, prot)`・`coff_os_release(addr, size)` | `mprotect`・`munmap` |
| `coff_os_set_gs_base(value)` | `thread_self(SET_GSBASE)`（host は `arch_prctl(ARCH_SET_GS)`） |
| `coff_os_thread_id()`・`coff_os_process_id()` | TEB の ClientId |
| `coff_os_lock()`・`coff_os_unlock()` | loader の lock（同じ thread の再入を許す: DllMain の中の LoadLibrary） |
| `coff_os_write_diagnostic(text)`・`coff_os_exit(status)` | 診断と終了 |

## 5. PE32+ の解析と検証（p006）

全ての値は信用しない入力として検査する（壊れた PE で loader が落ちない、fuzz の試験 §16.2）。検査の順:

1. DOS header: `e_magic == "MZ"`、`e_lfanew` が 4 の倍数で file の中。
2. `"PE\0\0"`、File header: `Machine == IMAGE_FILE_MACHINE_AMD64 (0x8664)`、`NumberOfSections` 1〜96、`SizeOfOptionalHeader` ≥ PE32+ の固定部 +
   `NumberOfRvaAndSizes * 8`。`Characteristics` に `EXECUTABLE_IMAGE`。EXE と DLL（`IMAGE_FILE_DLL`）を区別（EXE を要るところに DLL は不可）。
3. Optional header: `Magic == 0x20b`、`NumberOfRvaAndSizes` ≤ 16（それ以上は 16 と読む）、`SectionAlignment` ≥ 4096 かつ 2 の冪、
   `FileAlignment` 512〜64 KiB の 2 の冪（**v0.1 は `SectionAlignment` < page の「low alignment」の image を拒む**、既定 D12）、
   `SizeOfImage` が `SectionAlignment` の倍数で 0 でなく上限（2 GiB）以下、`SizeOfHeaders` ≤ 最初の section の `VirtualAddress`、
   `AddressOfEntryPoint` が 0（DLL で entry 無し）か image の中の実行可能な section。`ImageBase` が 64 KiB の倍数。
4. Section table: file の中。各 section の `VirtualAddress` が昇順・`SectionAlignment` の倍数・重ならない・`max(VirtualSize, SizeOfRawData)` を
   `SectionAlignment` に丸めた末尾が `SizeOfImage` 以下。`SizeOfRawData` > 0 なら `PointerToRawData + SizeOfRawData` が file の中
   （file の末尾で切れた raw data は読める所まで読み、残りを 0 埋め、が Windows の振る舞いだが v0.1 は拒む）。全ての加算は overflow を検査する。
5. Data directory: 使うもの（EXPORT 0、IMPORT 1、EXCEPTION 3、BASERELOC 5、TLS 9、IAT 12）は RVA + size が `SizeOfImage` の中。
   CLR（14）が非 0 なら「.NET の image は対象外」と報告して拒む。`DELAY_IMPORT`（13）は v0.1 は無視して警告（延期）。

## 6. image の配置と保護（p006）

- 予約: まず `ImageBase` に `SizeOfImage` を `MAP_FIXED_NOREPLACE`・`PROT_NONE` で。取れなければ kernel の選ぶ所（mmap の領域は 4 GiB から上。
  EXE の既定 0x140000000・DLL の既定 0x180000000 と重なりうる）。`IMAGE_FILE_RELOCS_STRIPPED` の image が `ImageBase` に置けないときは失敗。
  **ASLR は前提にする（spec §7）が、kernel の mmap は乱数を使わないので、v0.1 の配置は決まった位置になる**。乱数の配置は後（既定 D13、ld.coff が
  hint を乱数で選べばよく kernel の変更は不要）。
- 配置: 予約の全体を一旦 `RW` にし、header（`SizeOfHeaders`）と各 section の raw data を `pread` で置く。raw data の後ろから `VirtualSize` の末尾
  までは予約の時点で 0（anonymous の map）。file の mapping（共有の page）は使わない（v0.1。image の page を複写する分 memory を使うが単純で、
  file の後の変更の影響を受けない。将来 `MAP_PRIVATE` の file の map に替えられる）。
- 保護（spec §6）: relocation と import の束縛が済むまで `RW`、済んだら section ごとに `IMAGE_SCN_MEM_READ/WRITE/EXECUTE` から
  `R`・`RW`・`RX`・`RWX`（下）・無し を決め、header の page は `R`、section の間の隙間は `PROT_NONE`。1 つの page に複数の section が
  入ることは `SectionAlignment` ≥ 4096 なので無い。
- **RWX**: section 自身が `MEM_WRITE | MEM_EXECUTE` を宣言したときだけ `RWX` にし、`COFF_DEBUG` が無くても 1 回だけ診断を出す（既定 D7。
  互換のために必要な宣言は尊重し、loader は自分からは RWX を作らない）。
- 実行時の `LoadLibrary` も同じ手順（新しい module は束縛と保護が済んで公開されるまで他の thread から見えない）。

## 7. base relocation（p006）

- `delta = 実際の base − ImageBase`。0 なら何もしない。`BASERELOC` の directory の block（`VirtualAddress`、`SizeOfBlock`）を順に読み、
  `SizeOfBlock` ≥ 8・偶数・directory の中を検査。entry（type 4 bit・offset 12 bit）: `IMAGE_REL_BASED_ABSOLUTE`（0）は飛ばす（詰め物）、
  `IMAGE_REL_BASED_DIR64`（10）は `*(uint64_t *)(base + RVA) += delta`（RVA + 8 ≤ `SizeOfImage`）。**他の type は v0.1 は image を拒む**
  （`HIGHLOW` 3 は PE32+ でまれ。見つかったら type を報告）。
- 未整列の 8 byte の書き込みがありうるので、`memcpy` で読んで書く。

## 8. module と namespace（p007）

```c
/* One mapped PE image of the process: the EXE, a DLL, or the loader itself. */
struct coff_module {
	char *name;			/* 正規化した base name（小文字にはしない、比較は ASCII の大文字小文字を無視） */
	char *path;			/* 開いた file の path（synthetic は NULL） */
	uintptr_t base;			/* HMODULE（Windows と同じく image の base） */
	size_t image_size;
	const struct coff_pe_headers *headers;	/* base の中の header を指す */
	enum coff_module_state state;	/* COFF_LOADING, COFF_LOADED, COFF_BOUND, COFF_INITIALIZED */
	unsigned refcount;
	unsigned flags;			/* COFF_MODULE_EXE, COFF_MODULE_DLL, COFF_MODULE_SYNTHETIC, COFF_MODULE_ENTRY_CALLED */
	struct coff_module **dependencies;	/* import の順 */
	size_t dependency_count;
	uintptr_t entry;		/* 0 は無し */
	uint32_t pdata_rva, pdata_size;	/* unwind の範囲（spec §20、登録だけ） */
	uint32_t tls_rva, tls_size;	/* static TLS の directory（v0.1 は記録だけ） */
	struct coff_module *next;	/* load の順（初期化の順とは別） */
};

/* The process's modules: one namespace, created by ld.coff at start. */
struct coff_namespace {
	struct coff_module *first, *last;	/* load の順 */
	/* 名前の検索は module の数が小さいので線形（v0.1）。数百を超えたら hash に */
};
```

- `COFF_BOUND` は spec の 3 状態の間に「import を束縛した」を足したもの（束縛と初期化を分けるため、§10.3）。
- **synthetic の module `ld.coff`**: loader 自身を名前 `ld.coff` の module として namespace に置く。export は loader の service（§11）。
  file は無く、探索では見つからず、名前が完全に一致する import（`ld.coff`）でだけ引ける。互換の DLL（kernel32）はこの名前から import する
  （lld-link の `/lib /def:` で `LIBRARY "ld.coff"` の import library を作れることを確認済み）。これは橋の DLL ではなく loader そのもの（決定 4 に沿う、既定 D2）。

## 9. 名前と探索（p007）

- 正規化: `\` を `/` に。path を含まない名前で拡張子が無ければ `.dll` を足す（`LoadLibrary` の規則。末尾の `.` は「拡張子無し」の印で取り除く）。
  比較は base name の ASCII の大文字小文字を無視（spec §8）。
- 探索の順（決定 2）: 1. EXE と同じ directory、2. `/usr/lib/coff64/`、3. `COFF_LIBRARY_PATH`（`:` 区切り）。path を含む名前はその path だけ。
- file system は大文字小文字を区別するので、各 directory で: 1. そのままの名前、2. 小文字の名前、3. directory を走査して大文字小文字を無視して一致する名前
  （`getdents`、結果を directory ごとに cache）。互換の DLL は小文字の名前で install する（`kernel32.dll`）。
- 既に namespace にある名前（正規化して比較）は再び開かない。

## 10. import・export・依存・初期化（p007・p008）

### 10.1 import

- `IMPORT` の descriptor を 0 の entry まで。各 DLL 名を §9 で解決して load（§10.3 の再帰）。thunk の表は `OriginalFirstThunk`（無ければ `FirstThunk`）。
  bit 63 が立てば ordinal（下 16 bit）、でなければ hint/name（hint を先に試し、違えば名前で探す）。結果を `FirstThunk`（IAT）に書く。
- **未解決の import は load の失敗**（既定 D6、Windows と同じ）。診断に `dll!name` か `dll!#ordinal` と依存の経路を出す。開発用に
  `COFF_UNRESOLVED=trap`（呼ばれたら名前を出して止まる stub を入れる）を p007 の後に足せるよう、束縛の関数を 1 か所にする。

### 10.2 export

- `EXPORT` の directory: `NumberOfFunctions`・`NumberOfNames`・`Base`、各表が image の中。名前→ordinal は名前の表の二分探索（名前の表は
  昇順が仕様。load の時に昇順を検査し、昇順でなければ線形の探索にする）。ordinal→RVA は `ordinal − Base`。
- **forward**: RVA が export の directory の範囲の中なら文字列 `"DLL.Function"` か `"DLL.#ordinal"`。DLL を load（必要なら）して再帰に解決。
  深さの上限 16 と、解決中の (module, 名前) の組の列で循環を検出して失敗（spec §11）。
- data の export（`probe_counter` のような変数）も同じ（IAT に変数の address が入る）。

### 10.3 依存の graph と順序

1. **map**: EXE から深さ優先で import の DLL を map し relocation まで（state `LOADING` → `LOADED`）。`LOADING` の module に再び出会えば循環で、
   再帰しない（spec §12）。
2. **bind**: 全ての module が map された後、load の順に import を束縛（forward の先は必要なら 1. に戻って map）→ `BOUND`。束縛は export を読む
   だけなので、相手の初期化を待たない（循環も束縛できる）。
3. **protect**: 保護を最終の値に（§6）。
4. **initialize**: 依存の後順（dependencies を先に、深さ優先の後順。循環は訪れた順で切る。Windows と同じ）で `DllMain(base, DLL_PROCESS_ATTACH, reserved)`。
   `reserved` は process の起動の時は非 NULL（静的な load、Windows と同じ）、`LoadLibrary` では NULL。`FALSE` が返れば: 起動の時は process を
   失敗で終える（Windows と同じ）、`LoadLibrary` では load を失敗にする → `INITIALIZED`。
5. 失敗の後始末: 起動の時は process を終えるだけ。`LoadLibrary` の途中の失敗は、その呼び出しで新しく map した module を全て外し namespace から除く。

### 10.4 参照の数と unload

- `LoadLibrary` は refcount を増やし、`FreeLibrary` は減らす。**v0.1 は 0 になっても unmap しない、`DLL_PROCESS_DETACH` を呼ばない**（既定 D8。
  unload は他の thread がその code を走っている競合と DllMain の順序が絡むので後に回す）。process の終了（`ExitProcess`）の時の DETACH も延期。

## 11. loader の service（PE の側から呼ぶ、p008〜p009）

synthetic の module `ld.coff` の export。全て `__attribute__((ms_abi))` の C 関数で、loader の lock を取る（同じ thread の再入可）。
文字列は UTF-8（kernel32 が UTF-16 から変換する）。

| service | 意味 | 使う側 |
| --- | --- | --- |
| `CoffLoadLibrary(const char *name, uint32_t flags, uintptr_t *module)` | §9・§10 の load。戻りは Win32 の error の番号（0 は成功） | `LoadLibraryA/W/ExW` |
| `CoffGetModuleHandle(const char *name, uintptr_t *module)` | 既に load された module（NULL の名前は EXE） | `GetModuleHandleA/W` |
| `CoffGetProcAddress(uintptr_t module, const char *name, uint32_t ordinal, uintptr_t *address)` | §10.2（name が NULL なら ordinal） | `GetProcAddress` |
| `CoffFreeLibrary(uintptr_t module)` | refcount を減らす | `FreeLibrary` |
| `CoffGetModuleFileName(uintptr_t module, char *buffer, size_t size, size_t *length)` | module の path | `GetModuleFileNameA/W` |
| `CoffCreateThreadEnvironment(size_t stack_reserve, struct coff_thread_environment *out)` | 新しい thread の TEB と stack を作る（§13） | `CreateThread` |
| `CoffDestroyThreadEnvironment(uintptr_t teb)` | thread の終わりに TEB と stack を返す | thread の終わり |
| `CoffLookupFunctionEntry(uintptr_t pc, uintptr_t *image_base, uint32_t *pdata_rva, uint32_t *count)` | unwind の範囲（登録だけ、spec §20。実装は p011） | `RtlLookupFunctionEntry` |

- 戻り値を Win32 の error の番号にするのは、kernel32 が `SetLastError` にそのまま渡せるようにするため（`ERROR_MOD_NOT_FOUND` 126、
  `ERROR_PROC_NOT_FOUND` 127、`ERROR_BAD_EXE_FORMAT` 193、`ERROR_DLL_INIT_FAILED` 1114、`ERROR_NOT_ENOUGH_MEMORY` 8）。
- HMODULE は image の base（Windows と同じ。`GetModuleHandle` の値を `GetProcAddress` に渡す）。loader は base から module を探す。

## 12. Microsoft x64 ABI の境界（p008）

- ld.coff は System V の ABI で compile する。PE の側の関数は `__attribute__((ms_abi))` の関数 pointer の型で呼ぶ
  （`typedef int32_t (__attribute__((ms_abi)) *coff_dll_main_fn)(uintptr_t, uint32_t, void *);`）。clang が RCX・RDX・R8・R9、32 byte の
  shadow space、16 byte の整列を作り、MS の callee が RSI・RDI・XMM6〜15 を保つので native の側の約束も壊れない。専用の thunk の asm は要らない。
- PE の側へ出す service は `__attribute__((ms_abi))` で定義する（§11）。
- **EXE の entry** は `coff_enter_image(entry, stack_top)`（`start.S`）で: 新しい stack に切り替え（16 byte に整列）、`sub $0x28, %rsp`
  （shadow 32 + 整列 8）、`call *entry`、`mov %eax, %edi`、`coff_os_exit` へ。entry が戻れば process の終了（spec §21 の候補、既定 D9）。
  RBX 等の native の値は entry の後に要らないので保存しない。DF は clear、MXCSR・x87 CW は process の初期値（kernel の初期の FP の image）。
- **main の thread の stack**: 新しく map（大きさは EXE の `SizeOfStackReserve`、下限 1 MiB、上限 256 MiB、下端に `PROT_NONE` の guard の 1 page）。
  kernel の作った ld.coff の stack は使わない（TEB の StackBase・StackLimit を PE の宣言どおりにするため、既定 D10）。

## 13. PEB・TEB・command line（p008）

構造体の offset は固定し、`_Static_assert(offsetof(...))` で検査する（spec §16）。無い field は 0。

TEB（1 thread に 0x2000 byte、page に整列。0x1838 が x64 の TEB の大きさで、その後ろ 0x1900〜 を Kei の private の領域にする。PE の code は
TEB の大きさを超えて読まない）:

| offset | field | v0.1 の値 |
| --- | --- | --- |
| 0x000 | `NtTib.ExceptionList` | 0（x64 は使わない） |
| 0x008 | `NtTib.StackBase` | stack の上端 |
| 0x010 | `NtTib.StackLimit` | stack の下端（guard の上）。`__chkstk` が見る |
| 0x030 | `NtTib.Self` | TEB 自身（`GS:[0x30]`） |
| 0x040 | `ClientId.UniqueProcess` | pid |
| 0x048 | `ClientId.UniqueThread` | tid |
| 0x058 | `ThreadLocalStoragePointer` | 0（static TLS は延期。p011 でここに TLS の配列） |
| 0x060 | `ProcessEnvironmentBlock` | PEB（`GS:[0x60]`） |
| 0x068 | `LastErrorValue` | 0（kernel32 の `GetLastError`・`SetLastError`） |
| 0x1480 | `TlsSlots[64]` | 0（kernel32 の `TlsAlloc` 系。spec §18 の dynamic TLS は kernel32 の責務で、ここを使うのが既存の binary と互換） |
| 0x1780 | `TlsExpansionSlots` | 0 |
| 0x1900〜 | Kei の private（`struct coff_thread_private`: stack の map の範囲、thread の終わりの後始末の情報） | ld.coff |

PEB（1 page）:

| offset | field | v0.1 の値 |
| --- | --- | --- |
| 0x002 | `BeingDebugged` | 0 |
| 0x010 | `ImageBaseAddress` | EXE の base |
| 0x018 | `Ldr` | 0（module の一覧を直接読む binary が出たら `PEB_LDR_DATA` を作る） |
| 0x020 | `ProcessParameters` | 下の最小の `RTL_USER_PROCESS_PARAMETERS` |
| 0x030 | `ProcessHeap` | 0（kernel32 が自分の DllMain で heap を作り書く） |
| 0x0B8 | `NumberOfProcessors` | 後（kernel32 の判断） |
| 0x118〜 | `OSMajorVersion` 等 | 0（どの版を名乗るかは kernel32 の段で決める、既定 D17） |

`RTL_USER_PROCESS_PARAMETERS`（最小）: `ImagePathName`（0x60、UNICODE_STRING）= EXE の path（UTF-16）、`CommandLine`（0x70）= command line、
`Environment`（0x80）= UTF-16 の環境の block（`KEY=VALUE\0...\0\0`）。kernel32 の `GetCommandLineW`・`GetEnvironmentStringsW` は Windows と同じく
ここを読む（MSVC の CRT は PEB を直接読まず kernel32 を呼ぶ）。
command line: argv（UTF-8）を UTF-16 にし、`CommandLineToArgvW` が元に戻せる規則で引用（空白・tab・`"` を含む引数を `"` で囲み、`\` の連続と `"` を
Windows の規則で escape）。argv[0] は EXE の path（§3.2）。

thread の TEB: main の thread は ld.coff が作り `coff_os_set_gs_base(teb)`。他の thread は kernel32 が `CoffCreateThreadEnvironment` で TEB と stack を
得て、`thread_create`（`args[4]` = TEB、§3.1）で作る。新しい thread の入口は kernel32 の中の小さな asm（MS ABI で `lpStartAddress(lpParameter)` を
呼び、戻り値で `thread_exit`）。

## 14. syscall の stub の仕様（p005）

PE の側（互換の DLL と runtime）から zedBSD の syscall を呼ぶ stub。`coff_syscall0`〜`coff_syscall6`（MS ABI の関数）:

```
int64_t coff_syscall6(uint64_t number, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6);
```

| 役割 | MS x64 ABI（入口） | zedBSD の syscall（`syscall` 命令） |
| --- | --- | --- |
| number | RCX | RAX |
| a1 | RDX | RBX |
| a2 | R8 | R10（`int` の経路なら RCX。stub は `syscall` 命令だけを使う） |
| a3 | R9 | RDX |
| a4 | [RSP+0x28] | RSI |
| a5 | [RSP+0x30] | RDI |
| a6 | [RSP+0x38] | RBP |
| 戻り値 | RAX | RAX（負の値は `-errno`） |

register の規則（2026-09-28 ユーザー「syscall は RCX と R11 を破壊するので…明記」、この表の全てを stub の source の先頭の comment にも書く）:

- **`syscall` 命令が壊す**: RCX（戻り先）、R11（RFLAGS）。kernel が壊す: RAX（戻り値）。**それ以外の全ての整数の register を kernel は保つ**
  （`trap.S` の `SAVE_AND_CALL` と `amd64_sysret` が全て戻す。2026-09-29 に確認）。signal の配達はその signal の約束で register を変える。
- **MS ABI の volatile**: RAX・RCX・RDX・R8〜R11・XMM0〜5（と YMM・ZMM の上位）。RCX・R11 の破壊と RAX の上書きは volatile の範囲なので問題ない。
- **MS ABI の non-volatile で zedBSD の引数の register になるもの: RBX・RSI・RDI・RBP**。stub は入口で 4 つを push し、出口で pop する（**事故の本命**）。
  RDX・R10 は volatile なので保存しない。
- 5・6・7 番目の引数（a4〜a6）は stack から読む。4 つの push の後は `[rsp+0x48]`・`[rsp+0x50]`・`[rsp+0x58]`。**MS ABI には red zone が無い**ので、
  stub は RSP より下を使わない。
- XMM6〜15・MXCSR・x87 の CW: kernel が保つ（§3.1）ので stub は保存しない。kernel が syscall の経路で SSE を使うようになったら、stub が保存するよう
  変えなければならない（p005 の guest の試験がこれを検出する）。
- RFLAGS の DF: kernel の入口で clear（FMASK）、`sysretq` が R11 から戻す。MS ABI は呼び出しの前後で DF = 0 を求め、stub は DF を変えない。
- stub は leaf ではない（push する）ので、`.seh_proc`・`.seh_pushreg`・`.seh_endprologue`・`.seh_endproc` の unwind の情報を付ける（将来の stack の
  走査・SEH のため）。
- errno→Win32 の error の変換は stub でしない。kernel32 の側の表（runtime の `coff_error_from_errno()`）。

stub の試験（p005）:
1. host（Linux）: stub を ELF の中で `ms_abi` の caller から呼び、RBX・RBP・RDI・RSI・R12〜R15・XMM6〜15 に印を置いて Linux の無害な syscall
   （`getpid`、番号だけ Linux の値）を呼び、戻った後に全て保たれ、引数の register の割り当てが表どおり（`syscall` の直前の値を記録する試験用の
   変種）であることを検査する。
2. guest: PE の試験の EXE（p008 の後）で zedBSD の syscall（`getpid`・`write`）を呼び、同じ印の検査と XMM6〜15 の保存を確かめる。

## 15. PE の側の build と runtime（p004・p005）

### 15.1 build の規則（p005）

- compile: `build/llvm/bin/clang --target=x86_64-pc-windows-msvc -ffreestanding -nostdlib -fno-stack-protector -mno-stack-arg-probe
  -O2 -Wall -Wextra -Werror`（`-mno-stack-arg-probe` で `__chkstk` を呼ばない。外から来る object のために runtime にも `__chkstk` を置く）。
- link: `build/llvm/bin/lld-link /nologo /nodefaultlib /dynamicbase /entry:<入口> /subsystem:console`（DLL は `/dll /def:<name>.def`）。
  import library は `lld-link /lib /def:<name>.def /machine:x64`（`llvm-lib`・`llvm-dlltool` は共有の toolchain に無いが `lld-link /lib` で足りる）。
- **2026-09-29 に host で確かめた**（共有の toolchain を読むだけ、`build/ws080/pe-probe/`）: CRT 無しの EXE（ImageBase 0x140000000、entry 0x1000、
  import 2 つ）と DLL（ImageBase 0x180000000、export 2 つ: 関数と変数）ができる。volatile の pointer の表で DIR64 の base relocation が出る。
  `.def` で forward（`fwd_add = probe.probe_add`）と名前無しの ordinal（`@7 NONAME`）の export ができる。`LIBRARY "ld.coff"` の import library で
  EXE が名前 `ld.coff` から import する。
- 共有の toolchain は変えない・build しない（AGENTS.md）。PE の target は既存の clang・lld で足りる。

### 15.2 PE の側の最小の runtime（p005）

`userland/desktop/w64/runtime/`、静的な library（`coffrt.lib`、DLL ではない。各互換 DLL に link し、実行時の層は増えない）:
`memcpy`・`memmove`・`memset`・`memcmp`（clang が暗黙に呼ぶ）、`strlen` 等の文字列、UTF-8⇔UTF-16 の変換、`coff_syscall0`〜`6`（§14）、
`coff_error_from_errno()`、`__chkstk`（TEB の StackLimit を見て page ごとに触る）、`_fltused`（浮動小数を使う object が要求する記号）、
最小の書式（`coff_format`、診断用）。libc の header は使わず、`stdint.h`・`stddef.h` は clang の freestanding の header。

### 15.3 LLP64 の UAPI の header の生成（p004、決定 4 の方針）

- 既存の `include/uapi` を PE の build で直接使わない（`long` が 32 bit になり layout が変わる）。生成の script
  `userland/desktop/w64/tools/gen-llp64-uapi.py` と入力の一覧 `userland/desktop/w64/tools/llp64-uapi.list`（header、要る struct・enum・macro の接頭辞）。
- 方法: 1. 一覧の header を include する probe の C を LP64 の zedBSD の target（既存の cross clang と sysroot の header）で compile し、
  `-Xclang -fdump-record-layouts` で struct の各 member の offset・大きさ・整列を得る。2. `-E -dM` で macro の値、`-Xclang -ast-dump=json` の
  絞った出力で enum の値と各 member の型を得る。3. 型を固定幅に写す: `long`・`ssize_t`・`intptr_t`・`off_t`・`time_t` → `int64_t`、
  `unsigned long`・`size_t`・`uintptr_t` → `uint64_t`、pointer → `uint64_t`（UAPI の struct の中の user の pointer は 64 bit。PE の側で cast する）、
  `int`・`unsigned` 等はそのまま固定幅に。4. 生成する header に member の間の詰め物を明示（`uint8_t _pad_N[n]`）し、`sizeof` と全ての member の
  `offsetof` の `_Static_assert` を LP64 の値で出す。
- 生成物は `userland/desktop/w64/include/zuapi/*.h`（名前は `zuapi_` の接頭辞で既存の UAPI と区別）。tree に置き、試験で 1. 生成物が
  `x86_64-pc-windows-msvc` で compile でき（assert が LLP64 の layout = LP64 の値を保証）、2. 再生成して差が無い（UAPI の変更の検出）ことを確かめる。

## 16. 試験の型

### 16.1 fixture の PE（p005、`userland/base/ld-coff/tests/fixtures/`）

| fixture | 確かめること |
| --- | --- |
| `ret42.exe` | import 無し、entry が 42 を返す → 終了の状態 42 |
| `reloc.exe` | DIR64 の relocation（volatile の pointer の表）。ImageBase を塞いだ状態で起動して移動を強いる |
| `probe.dll` + `use-probe.exe` | 名前の import、data の export、DllMain が変数を設定、戻り値の計算 |
| `ordinal.dll` + `use-ordinal.exe` | 名前無しの ordinal の import |
| `fwd.dll`（→ `probe.dll`） | forward の解決 |
| `loop-a.dll`・`loop-b.dll` | forward の循環の検出（load の失敗） |
| `cyc-a.dll` ⇄ `cyc-b.dll` | DLL の循環の依存（load できる、初期化の順） |
| `missing.exe` | 未解決の import の失敗と診断 |
| `deps.exe`（A→B→D、A→C） | spec §12 の順（D, B, C の DllMain の順を共有の DLL の記録で確かめる） |
| `gs.exe`（p008） | `GS:[0x30]`・`GS:[0x60]` が TEB・PEB、`LastErrorValue` の読み書き |
| 壊れた PE の集合 | header の各 field の境界の値と突然変異（fuzz）で loader が落ちず ENOEXEC を返す |

### 16.2 host の試験（p006・p007・p008 の前半）

ld.coff の核（§4.2 の `os-zedbsd.c`・`start.S` 以外）を Linux の host の program として build し、`os-host.c`（Linux の `mmap`・`arch_prctl(ARCH_SET_GS)`）で
動かす。fixture の PE の code は syscall を使わない限り Linux の上でもそのまま実行できる（x86-64 の code で、呼び出しは `ms_abi`）ので、
map・relocation・import・export・forward・依存の順・DllMain・EXE の entry・GS の TEB まで host で確かめられる。ASan・UBSan でも走らせる。

### 16.3 guest の試験（p008 以降）

`plan/tools/guest/guest.sh` で fixture を guest に送り、`/usr/libexec/ld.coff X.exe`（p003 の後は `./X.exe`）を走らせて終了の状態と出力を見る。
image は main の image の複写に ld.coff と fixture を足す（clang・libcxx を含む image を worktree で build しない）。

## 17. 互換の DLL を下位のモデルが回す作業の型（p009 以降）

下位のモデル（sonnet・haiku）が迷わず回せるよう、1 回の作業の単位を「**API の小さな群**」（例: `GetStdHandle`・`WriteFile`・`ReadFile`）にし、
次の型を守る。p009 の最初の群で型の見本を作り、以後はこの節を作業の指示の本文として渡す。

- 置き場: `userland/desktop/w64/<dll>/`（`<dll>.def`、群ごとの `<群>.c`、`internal.h`）。試験は `userland/desktop/w64/tests/<dll>/<群>.c`（PE の EXE）。
- 使ってよいもの: `userland/desktop/w64/include/`（自前の Windows の型・宣言の header。Microsoft の SDK の header は取り込まない）、
  `zuapi/`（§15.3）、runtime（§15.2）、import は `ld.coff`（§11）と他の互換 DLL の公開の API だけ。libc・native の header は使わない。
- 1 つの API の完成の条件（全て満たして 1 件）:
  1. `.def` に export があり、宣言が Microsoft の文書の型（`WINAPI` = MS ABI、`BOOL`・`DWORD`・`HANDLE` 等の幅）と一致。
  2. 実装する意味の範囲を関数の上の comment に書く（対応する flag、対応しない flag は `ERROR_NOT_SUPPORTED`・`ERROR_INVALID_PARAMETER`、
     失敗の時の `SetLastError`）。
  3. 試験の EXE が正常の場合と少なくとも 1 つの失敗の場合を確かめ、`API:<名前>:PASS` を出して 0 で終わる（失敗は `API:<名前>:FAIL:<理由>` と非 0）。
  4. build の warning 0、`python3 plan/tools/style-check.py`（新しい file は finding 0）。
  5. guest で試験の EXE が PASS（`userland/desktop/w64/tests/run-guest.sh <dll> <群>`、p009 で作る）。
- 作業の報告: 群の名前、API ごとの PASS・未対応の flag の一覧、試験の出力。判断が要る点（どの Windows の版の振る舞いに合わせるか等）は
  既定を選んで理由を書き、上位のモデル（main）に挙げる。
- 下位のモデルがしないこと: ld.coff・kernel・HAL・toolchain の変更、他の DLL の設計の変更、`.def` の既存の export の削除。

## 18. `LoadLibrary`・`GetProcAddress` と Wayland の道

- `LoadLibrary`・`GetProcAddress`・`GetModuleHandle`・`FreeLibrary` は kernel32 が `ld.coff` の service（§11）を import して呼ぶ（橋の DLL は無い）。
- GUI（p011 以降）: user32 等は Wayland の wire protocol を PE の code として直接話す。自前の `userland/desktop/libwayland` の source を PE の target で
  build する案を p011 で調べる。要る native の機能（UNIX domain socket、`sendmsg` の `SCM_RIGHTS`、共有 memory の fd、`mmap`、`poll`）は
  `zuapi` と syscall の stub で runtime に用意する。

## 19. 判断が要る点（既定を選んだもの。ユーザーの確認を求める）

| # | 点 | 既定 | 理由 |
| --- | --- | --- | --- |
| D1 | ld.coff を libc 無しの静的な freestanding の ELF にする | する | §4.1（PE の thread から service を呼べる、起動の依存が無い、FS を使わない） |
| D2 | loader の service を synthetic の module `ld.coff` の export として出す | する | 橋の DLL を置かない決定の下で kernel32 が loader を呼ぶ唯一の file 無しの道。PEB の private の pointer より型が明示 |
| D3 | UAPI: `thread_self` に GET/SET_GSBASE（3・4）、`thread_create` の `args[4]` を最初の GS base | する | 汎用の「thread の GS base」（spec §15）。新しい thread が GS 無しで走る瞬間を無くす |
| D4 | CR4.FSGSBASE を立てない（user は `wrgsbase` を使えない） | 立てない | paranoid の入口が GS base の上半分・下半分で判断できる前提。Windows の binary は通常 `rdgsbase` を使わない |
| D5 | PE の exec の argv は `#!` と同じ（元の argv[0] は失われ EXE の path） | 同じ | kernel の変更が最小。必要なら後で ld.coff の option を足せる |
| D6 | 未解決の import は load の失敗 | 失敗 | Windows と同じ。開発用の `COFF_UNRESOLVED=trap` は後で |
| D7 | section が宣言した RWX は尊重し、1 回診断を出す | 尊重 | 互換。loader 自身は RWX を作らない |
| D8 | v0.1 は unload しない、DLL_PROCESS_DETACH を呼ばない | しない | 競合と順序の設計を後の Phase に |
| D9 | EXE の entry が戻れば、その値で process を終える | 終える | spec §21 の候補 |
| D10 | main の thread は PE の `SizeOfStackReserve` の新しい stack で走る | 新しい stack | TEB の StackBase・StackLimit と `__chkstk` の整合 |
| D11 | 探索の大文字小文字: そのまま → 小文字 → directory の走査。互換の DLL は小文字の名前で install | そう | case-sensitive な FS で Windows の名前の揺れを吸収 |
| D12 | `SectionAlignment` < page の image を v0.1 は拒む | 拒む | 普通の linker の出力は 4 KiB。後で対応できる |
| D13 | image の配置の乱数（ASLR）は v0.1 はしない（relocation は常に対応） | しない | kernel の mmap に乱数が無い。ld.coff の hint の乱数で後から足せる |
| D14 | debug の点（ptrace の hardware の watchpoint）を user の address に限る | 限る | kernel の address の点は swapgs の窓の中で #DB を起こしうる（HAL の実装の変更、[proposed](proposed/hal-gs-base.md)） |
| D15 | 名前 `ld.coff` の synthetic の module を import の名前で直接引ける | 引ける | D2 と同じ |
| D16 | TEB を 0x2000 byte、Kei の private を 0x1900 から | そう | x64 の TEB（0x1838）の外 |
| D17 | PEB の OS の版の field は 0 のまま（kernel32 の段で決める） | 0 | 名乗る版は互換の方針の判断 |

## 20. 危険と未確定

- GS の入口の変更（p002）は全ての kernel の入口に触る。paranoid の判断の前提（user の GS base は下半分、FSGSBASE 無し）が崩れると kernel の per-CPU の
  状態を user の値で読む。前提は HAL の API の中で検査し、試験で固定する（[proposed](proposed/hal-gs-base.md) の試験の節）。
- mmap の領域（4 GiB から上）と PE の既定の ImageBase が重なる。ld.coff は起動の最初に EXE を予約する（ld.coff 自身は 0x400000 に link され、
  arena は後）。
- 下位のモデルが大量の API を作るとき、Windows の振る舞いの細部（error の番号、境界の値）を誤りやすい。§17 の型で失敗の場合の試験を必須にする。
