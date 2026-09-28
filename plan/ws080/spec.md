# `ld.coff` — Win64 PE/COFF Dynamic Loader Specification

2026-09-28 ユーザーの仕様（原文のまま）。計画は [ws.md](ws.md)。

## 1. 目的

`ld.coff` は、Kei / zedBSD 上で Win64 PE32+ 実行形式を直接実行可能にするためのユーザーランド動的ローダである。

本機能の目的は、Windows NT のプロセス初期化機構そのものを再現することではない。PE/COFF フォーマットおよび必要な Win64 ABI 互換基盤を提供し、その上で Win64 DLL の互換実装を順次追加できる環境を構築する。

最終的には、既存の Win64 PE バイナリに対して、

```
Win64 EXE
   │
   ▼
 ld.coff
   │
   ├─ PE image mapping
   ├─ relocation
   ├─ DLL loading
   ├─ import resolution
   ├─ TLS / Win64 thread environment
   └─ initialization
   │
   ▼
PE AddressOfEntryPoint
```

という経路で実行を開始する。

Windows の `ntdll.dll` や `LdrInitializeThunk` をエントリーポイントとして使用せず、ロード完了後は PE Optional Header の `AddressOfEntryPoint` に直接制御を移す。

## 2. 基本方針

`ld.coff` は Windows NT ローダの再実装ではない。

実装対象を以下の3層に分離する。

```
PE/COFF format support
        │
        ├─ image mapping
        ├─ sections
        ├─ relocations
        ├─ imports / exports
        └─ PE TLS metadata
        │
Win64 ABI compatibility
        │
        ├─ Microsoft x64 ABI
        ├─ GS base
        ├─ minimal TEB / PEB
        └─ unwind metadata support
        │
Win32 compatibility DLLs
        │
        ├─ kernel32.dll
        ├─ ucrt*.dll
        ├─ user32.dll
        └─ その他を順次実装
```

zedBSD カーネル本体には Win32 / NT 固有の概念を極力持ち込まない。

カーネルは以下のような汎用機能のみ提供する。

* 仮想メモリマッピング
* ページ保護
* GS base 設定
* スレッド生成
* プロセス生成
* ファイルアクセス
* 必要に応じた例外・unwind支援

TEB、PEB、DLL、PE import table 等はすべてユーザーランド互換層の責務とする。

## 3. 初期対応対象

`ld.coff` v0.1 では対象を次に限定する。

```
Architecture: AMD64 / x86-64
Format:       PE32+
ABI:          Microsoft x64 ABI
Image type:   EXE / DLL
```

32-bit PE、WOW64、ARM64 等は初期対象外とする。

## 4. 実行モデル

zedBSD が PE 実行形式を検出した場合、PE を直接実行するのではなく `ld.coff` をローダとして起動する。

概念的には、

```
exec("foo.exe")
      │
      ▼
kernel PE handler
      │
      ▼
/system/libexec/ld.coff foo.exe
```

となる。

`ld.coff` は対象 EXE と依存 DLL をロード・リンクした後、最終的に

```
ImageBase + AddressOfEntryPoint
```

へ制御を移す。

## 5. PE image mapping

PE Optional Header の以下を解釈する。

```
ImageBase
SizeOfImage
SizeOfHeaders
SectionAlignment
FileAlignment
AddressOfEntryPoint
DataDirectory[]
```

まず `SizeOfImage` 分の仮想アドレス空間を確保する。

各 section は、

```
load_base + section.VirtualAddress
```

に配置する。

section の実メモリサイズは、

```
max(SizeOfRawData, VirtualSize)
```

を基準とする。

ファイルに存在しない末尾領域はゼロ初期化する。

## 6. section protection

ロード処理中とロード完了後の保護属性を分離する。

最終的な属性は `IMAGE_SCN_MEM_*` に基づいて決定する。

例:

```
.text   → RX
.rdata  → R
.data   → RW
```

可能な限り恒久的な RWX mapping は行わない。

relocation や IAT 書き換えが必要なページのみ、一時的に write permission を与える。

## 7. Base Relocation

preferred `ImageBase` に配置できない場合、base relocation を適用する。

```
delta = actual_base - preferred_image_base
```

初期対応タイプ:

```
IMAGE_REL_BASED_ABSOLUTE
IMAGE_REL_BASED_DIR64
```

`DIR64` の場合、

```
*(uint64_t *)(load_base + RVA) += delta
```

を行う。

ASLR を前提とし、preferred address への配置成功に依存しない設計とする。

## 8. DLL loader

DLL はプロセスごとの module namespace で管理する。

概念的な内部構造:

```
struct coff_module {
    char *name;
    char *path;

    uintptr_t base;
    size_t image_size;

    enum {
        COFF_LOADING,
        COFF_LOADED,
        COFF_INITIALIZED
    } state;

    unsigned refcount;
};
```

また、

```
struct coff_namespace {
    module_map modules;
    module_list load_order;
};
```

を持つ。

DLL 名比較は Win64 互換性のため、ASCII case-insensitive とする。

## 9. DLL search path

Windows の複雑な search order は初期実装では再現しない。

Kei 独自の明示的な探索順序を採用する。

例:

```
1. 実行ファイルと同一ディレクトリ
2. /System/Win64/
3. /System/Win64/compat/
4. COFF_LIBRARY_PATH
```

互換 DLL の実配置規則は将来変更可能とする。

## 10. Import resolution

`IMAGE_DIRECTORY_ENTRY_IMPORT` を解釈する。

各 `IMAGE_IMPORT_DESCRIPTOR` について依存 DLL をロードし、thunk table を解決する。

対応する import:

```
import by name
import by ordinal
```

解決した関数アドレスは IAT (`FirstThunk`) に書き込む。

概念API:

```
void *coff_lookup_symbol(
    struct coff_module *module,
    const char *name,
    uint16_t ordinal
);
```

## 11. Export resolution

DLL の `IMAGE_EXPORT_DIRECTORY` を解釈する。

以下をサポートする。

```
name export
ordinal export
forwarded export
```

forwarded export の例:

```
FOO.dll!Function
    ↓
BAR.dll.RealFunction
```

resolver は必要に応じて対象 DLL をロードし、再帰的に解決する。

forward loop 防止のため循環検出を行う。

## 12. Dependency graph

DLL 依存関係はロード時にグラフとして処理する。

例:

```
A.exe
 ├─ B.dll
 │   └─ D.dll
 └─ C.dll
```

ロード順は依存先を先に処理する。

概念的には、

```
D.dll
B.dll
C.dll
A.exe
```

の順となる。

循環 DLL 依存を考慮するため、

```
LOADING
LOADED
INITIALIZED
```

の状態を管理する。

## 13. DLL initialization

DLL の `AddressOfEntryPoint` が存在する場合、Win64 DLL ABI に従って呼び出す。

概念的には、

```
BOOL WINAPI DllMain(
    HINSTANCE instance,
    DWORD reason,
    LPVOID reserved
);
```

を想定する。

初期実装では少なくとも、

```
DLL_PROCESS_ATTACH
```

をサポートする。

DLL initialization 完了後、EXE entrypoint へ制御を移す。

## 14. Microsoft x64 ABI

既存 Win64 バイナリとの互換性のため、PE entrypoint、DLL entrypoint、TLS callback 等の呼び出しでは Microsoft x64 ABI を使用する。

最低限、

```
RCX
RDX
R8
R9
```

による引数渡し、

```
32-byte shadow space
16-byte stack alignment
```

を保証する。

`ld.coff` 内部自体は zedBSD ネイティブ ABI を使用してよい。

Win64 ABIとの境界のみ thunk で分離する。

## 15. GS base

zedBSD では GS を Win64 compatibility personality 用として利用可能とする。

Win64 実行スレッドでは、

```
GS base → Win64 compatible thread environment
```

とする。

カーネルは Win64 や TEB を認識せず、

```
set_thread_gsbase(address);
```

相当の汎用機能のみ提供する。

スレッド切り替え時には GS base を通常のスレッドコンテキストとして保存・復元する。

## 16. Minimal TEB

既存 Win64 バイナリが必要とする範囲のみ、互換 TEB を提供する。

初期状態では完全な Windows TEB の再現を目的としない。

必要になったフィールドから追加する。

代表的な互換ポイント:

```
GS:[0x30] → TEB self pointer
GS:[0x60] → PEB pointer
```

構造体の ABI offset は固定し、コンパイラの自然配置に依存させない。

`offsetof()` による compile-time assertion を使用する。

## 17. Minimal PEB

PEB も完全再現しない。

既存バイナリまたは互換 DLL が直接必要とするフィールドのみ実装する。

候補:

```
image base
process parameters
loader/module information
process heap
environment information
```

ただし可能な限り Win32 compatibility DLL 側から zedBSD ネイティブ API へ変換し、PEB 直接依存は最小化する。

## 18. TLS

TLS は2種類に分けて扱う。

### Dynamic TLS

Win32 API の、

```
TlsAlloc
TlsGetValue
TlsSetValue
TlsFree
```

は compatibility DLL が zedBSD ネイティブ TLS API へ変換する。

`ld.coff` 自身の責務にはしない。

### Static PE TLS

`IMAGE_DIRECTORY_ENTRY_TLS` は `ld.coff` が処理する。

対象:

```
initial TLS image
zero-fill area
TLS index
TLS callbacks
```

各スレッド生成時に PE static TLS block を生成できる基盤を用意する。

既存 Win64 バイナリからの直接アクセスに備え、必要に応じて TEB / GS 経由の TLS layout を提供する。

## 19. TLS callbacks

PE TLS directory に callback が存在する場合、PE entrypoint より前に実行する。

初期プロセス起動時は概ね、

```
map images
↓
apply relocations
↓
resolve imports
↓
initialize TLS
↓
TLS callbacks
↓
DllMain(DLL_PROCESS_ATTACH)
↓
EXE entrypoint
```

という順序とする。

正確な DLL 間初期化順序は dependency order と併せて定義する。

## 20. Unwind metadata

Win64 x64 の `.pdata` / `.xdata` はロード時に認識する。

初期段階では完全な SEH 実装を要求しないが、各 module ごとに unwind metadata の範囲を登録可能な構造を用意する。

将来的な利用対象:

```
C++ exception handling
SEH
stack walking
debugger
RtlLookupFunctionEntry-compatible implementation
RtlVirtualUnwind-compatible implementation
```

`ld.coff` の module registry に unwind 情報への参照を保持する。

## 21. EXE entrypoint

全 DLL のロードおよび初期化完了後、

```
load_base + AddressOfEntryPoint
```

へ制御を移す。

NTDLL を経由しない。

初期 transfer は原則として tail transfer / jump とする。

entrypoint が return した場合の挙動は Kei の PE process ABI として別途定義する。

候補としては、

```
return → process exit
```

を採用できる。

## 22. Win32 compatibility DLL

Win32 API 自体は `ld.coff` に実装しない。

別 DLL として順次互換実装する。

想定例:

```
kernel32.dll
kernelbase.dll
ucrtbase.dll
msvcp*.dll
user32.dll
gdi32.dll
ws2_32.dll
advapi32.dll
```

これらは内部で zedBSD / Kei ネイティブ API を呼び出す。

例:

```
Win64 application
      │
      ▼
kernel32.dll compatibility implementation
      │
      ▼
zedBSD syscall / Kei userspace API
```

## 23. NT dependency policy

Kei の Win64 compatibility layer では、NT API を基盤ABIとはしない。

したがって初期設計では、

```
ntdll.dll
Native API
LdrInitializeThunk
Windows loader internals
```

を必須としない。

ただし既存 Windows DLL が `ntdll.dll` を import する場合に備え、将来的に互換 `ntdll.dll` を通常 DLL の一つとして実装することは許容する。

これは OS 内部の NT 化を意味しない。

## 24. `ld.coff` v0.1 必須機能

最初の実用目標を以下とする。

```
PE32+ AMD64 parsing
EXE mapping
DLL mapping
section mapping
base relocations
imports by name
imports by ordinal
exports
forwarded exports
DLL dependency resolution
IAT patching
Microsoft x64 ABI calls
DLL_PROCESS_ATTACH
EXE AddressOfEntryPoint transfer
basic GS base support
```

## 25. v0.1 以降へ延期可能な機能

以下は初期実装から除外可能。

```
PE static TLS
TLS callbacks
DLL_THREAD_ATTACH / DETACH
complete TEB
complete PEB
SEH execution
C++ exception handling
unwind execution
delay imports
bound imports
API Set schema
Side-by-Side assemblies
KnownDLLs
Control Flow Guard
Authenticode verification
WOW64
PE32
ARM64
Windows loader lock semantics
```

ただし TLS、TEB、unwind 等を後から追加できるよう、内部構造の拡張性だけは確保しておく。

## 26. 設計上の原則

この機能で一番重要なのは、

PE/Win64互換性を提供するが、zedBSDそのものをWindows NT互換カーネルにはしない

という点だと思います。

つまり設計の境界は、

```
zedBSD kernel
    ↑
generic OS primitives
    ↑
ld.coff + Win64 personality
    ↑
Win32 compatibility DLLs
    ↑
Win64 PE applications
```

にする。

これなら Windows バイナリ互換を育てながら、Kei と zedBSD 自体の設計思想は完全に独立したままにできます。

そして実装順としては、まず `ld.coff` で「自作の最小 PE EXE + 自作 DLL」が動くところまで持っていき、その後 `kernel32.dll` → CRT → より上位の DLL という順で互換層を積むのがかなり綺麗です。
