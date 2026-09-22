# ws032-p008 結果: clang（2026-09-22、2026-09-23 cleared）

`userland/packages/lang/clang/`。コンパイラ自身と同じ LLVM 23.1.0 の tarball を使う。
**二重取得しない**——libcxx と同じ方針で、ツリーに LLVM は一つ、ライセンスも一つ。

## 作り

| 事項 | 決めたこと |
| --- | --- |
| tablegen | ホスト build (`build/llvm-build/bin`) のものを `LLVM_NATIVE_TOOL_DIR` で再利用。ビルド中に動く生成器はビルドする機械のもので、ターゲットのものではない |
| リンク | `LLVM_LINK_LLVM_DYLIB` / `CLANG_LINK_CLANG_DYLIB`。静的リンクのツールを十数個置くと、置くべきディスクより大きくなる |
| C++ ランタイム | libcxx パッケージの stage を `-nostdinc++ -isystem` と `-L` で指す |
| 依存 | `devel/libcxx` を要求。menuconfig の Languages に入る |

## 追加した API（ユーザー承認済み）

| 追加 | 種類 | 実装 |
| --- | --- | --- |
| `strsignal()` | libc | 既存の `signal_description()` を公開しただけ。`psignal` と同じ文を返す |
| `madvise()` / `posix_madvise()` | libc | **スタブ**。引数を検査して 0 を返す。助言は助言であって命令ではないので、これは適合する答えである |
| `wait4()` | **syscall 167** | 子を回収し、その子が使った時間を報告する。`process_wait_event` が子を持っている間に読む——commit すると子は消える |
| `MNT_LOCAL` | `<sys/mount.h>` | mount 側の綴り |
| `ST_LOCAL` | `<sys/statvfs.h>` + kernel | statvfs 側の綴り。**下記参照** |
| `_POSIX_ARG_MAX` | `<limits.h>` | 4096 |
| `_SC_GETPW_R_SIZE_MAX` | `<unistd.h>` + sysconf | 1024 |

### statvfs.f_flags について

**BSD のメンバ名は要らなかった。** LLVM は zedBSD に対して `<sys/statvfs.h>` と
`struct statvfs` を選ぶ（Linux/FreeBSD/AIX/managarm 以外は全部そう）。ところが
フィールド名を選ぶ側のリストには NetBSD/DragonFly/GNU/MVS しか無いので、残り全部が
`f_flags`——`struct statfs` の綴り——に落ちる。**POSIX の構造体に BSD のフィールド名**
という組み合わせで、これは LLVM 側の取りこぼしである。

`__ZEDBSD__` をそのリストに足す一行のパッチにした。`struct statvfs` は POSIX のまま
`f_flag` である。

同じ関数の下の方で `f_flag & MNT_LOCAL` をやっている。`MNT_*` は mount の値で、
`f_flag` が運ぶのは `ST_*` である。**field の綴りだけ直しても意味が合わない**ので、
`ST_LOCAL` を statvfs 側に定義し、kernel が立て、LLVM は zedBSD でそれを見るように
した（パッチ 0002）。`MNT_LOCAL` は mount 側の答えとして残してある。

どちらも zedBSD 固有の問題ではない——statvfs 分岐に来てリストに載っていない系は全部
同じところで止まる。`plan/ws032/patches-for-upstream` 行きの候補。

## API ではなかったもの

調べたら既にあった、あるいは別名を足しただけ:

| 事項 | 実態 |
| --- | --- |
| `<machine/endian.h>` | `<endian.h>` が既に `BYTE_ORDER` 等を定義している。BSD の綴りで同じものを指す別名 |
| `<sysexits.h>` | `EX_*` のマクロのみ。関数なし |
| `TIOCGWINSZ` / `struct winsize` | `<uapi/termios.h>` に**既にあった**。`<sys/ioctl.h>` がそれを出していなかっただけ——portable software はそこを見る |
| `setenv` / `dlopen` / `st_mtim` | configure が「無い」と言ったが**全部あった**。検査が C++ で、libc++ が見えていなかったので全滅していた |

## ビルド側の直し

| 直し | 理由 |
| --- | --- |
| sysroot に空の `libm.a` | 移植性のある C++ のリンクは必ず `-lm` を渡す。数学は libc にあるので、空の archive で「見つかって何も足さない」にした。最初 `INPUT(libc.a)` にして**静的 libc を動的リンクに引き込んで**しまい、`R_X86_64_32` で落ちた |
| libcxx に `LIBCXX_EXTRA_SITE_DEFINES` | rune table の define がビルドフラグだったので、**ライブラリには入るがヘッダには入らない**。`<locale>` を使う C++ プログラムが全部同じフラグを繰り返す必要があった。`__config_site` に入れたので、以後の C++ パッケージ全部に効く |

## ビルドは通り、実機で止まっている

クロスビルドは**完了**した。`libLLVM.so.23.1`（81MB）、`clang-23`、`lld`、
LLVM のツール一式。リンク契約も通る:

```
clang-23      PASS  (libc++.so.1 libc++abi.so.1 libunwind.so.1
                     libclang-cpp.so.23.1 libLLVM.so.23.1 libc.so)
libLLVM.so.23.1 PASS
```

イメージにも入る（`/usr/bin/clang`, `clang++`, `ld.lld`, `llvm-{ar,ranlib,nm,
objcopy,objdump,readelf,strip}`, `/usr/lib/libLLVM.so.23.1`,
`libclang-cpp.so.23.1`, `/usr/lib/clang/23/include/*.h`）。697MB の stage 全部
ではなく、計画が挙げた分だけ。

**が、そのイメージで起動しない。**

```
ata: sda op=2 lba=133120 count=0 error=5 status=C0
vfs: mount overlay-data image failed (error 5)
VFS initialization failed (5); entering idle.
```

### 原因: イメージが大きくなって FAT の種別が変わった

`make-bios-hdd-image.noct` は payload から FAT 区画の大きさを決める。clang が
入って payload が 530MB 程になり、FAT 区画が 176MiB から 537MiB になった。
その結果 **mkfs が FAT16 ではなく FAT32 を作った**:

```
$ dd if=hdd-image.img bs=512 skip=2048 count=1 | strings
:WESP        FAT32
```

`rootfs.img` は読めている（loop0 は上がる）。落ちるのは **data.img への書き込み**で、
`count=0` という長さゼロの要求が ATA に降りてきている。ドライバは FAT32 対応を
謳っている（`src/drivers/fs/fat.c:11`）ので、**FAT32 の書き込み経路にある不具合が、
その大きさのイメージが初めて作られたことで表に出た**と見るのが妥当。

これは clang の移植の問題ではなく、**イメージが一定の大きさを越えると起動しなく
なる**という既存の不具合である。clang はその引き金を引いただけで、他の大きな
パッケージでも同じことが起きる。

### 併せて見つけた: チェッカのヒープが 32bit で溢れる

`make-bios-hdd-image.noct` はチェッカのヒープをイメージ長に比例させていたが、
その値は**符号付き 32bit として読まれる**。2GiB を越えると巻き戻って、チェッカは
「Out of memory」と言う——イメージを指す言葉だが、原因は数の方にある。

掛け算そのものが溢れるので、掛ける前に上限を見るようにした。

チェッカの言い分を捨てていたのも直した。読んで捨てると、理由の無い失敗だけが
残る。今は何と言って断ったかが出る。

## 追加（2026-09-22〜23、ユーザー指示）: lldb と、それを動かすためのカーネル支援

「lldb と LLVM のツールもビルドを通してインストールし、以後 ssh+lldb でバグを追えるように」
という指示で、このパッケージの成果物に **lldb / lldb-server**（と `liblldb.so.23.1`）を
加えた。ツール一覧の延長として p008 に含め、別 Phase は作っていない。

ツールチェイン側の LLVM ビルドにも lldb が要るため `ZEDBSD_LLVM_PATCH_LEVEL` を
zedbsd5 → zedbsd6 へ上げた。計画では変更しないと決めていた項目で、LLVM の全再ビルドを
招いている。

### カーネル・libc 側で要ったもの（承認を得た API 追加）

| 追加 | 場所 |
| --- | --- |
| `ptrace(2)`（OpenBSD の API 形）と `KERN_SYS_ptrace` | `include/uapi/ptrace.h`、`src/kern/ptrace.c`、`libc/include/sys/ptrace.h` |
| `PT_IO` の `PIOD_READ_AUXV`（補助ベクタを offset で読む） | 同上。PIE のロード位置を知るのにデバッガが要る |
| `PT_GET_SIGINFO` と `struct ptrace_siginfo` | 停止を起こした信号の `siginfo_t`。ウォッチポイントが一致した番地を `si_addr` で返す |
| HAL のデバッグ面 `hal_task_{get,set}_user_{gpregs,fpregs,vregs}`、単一ステップ、デバッグ点 | `include/hal/hal.h`、`include/hal/arch/amd64.h`、`src/hal/amd64/task.c` |
| `struct reg` / `struct fpreg` / `struct xmmreg` | `include/uapi/reg.h`。HAL の形を外へ出さず、UAPI が独立に定義する |
| 動的リンカを `src/rtld/` へ移し、`r_debug`・`link_map`・`DT_DEBUG` を公開 | `src/rtld/rtld.c`、`libc/include/link.h` |

デバッグ例外は `kernel_user_fault_handler()` の cause を増やして受ける
（`HAL_TRAP_CAUSE_SINGLE_STEP`、`HAL_TRAP_CAUSE_DEBUG_POINT`）。traced な SIGTRAP は
`si_code` から breakpoint / step / watchpoint を見分けてデバッガへ伝える。

### lldb 側

`patches/0004-add-the-zedbsd-process-plugin.patch`（約 2500 行）に、process・platform・
host・signal のプラグインとして入れてある。上流の NetBSD/FreeBSD 実装は参照せず、
公開されている振る舞いの記述から独立に書いた。

### 実機で動くこと

`plan/ws032/tests/lldb-target.sh`。ブレークポイント（関数名・`file:line`）、
バックトレース、`frame variable`・`p` 式・構造体・配列・グローバル、`list`、
step in/over/out/inst、ハードウェアウォッチポイント（複数、当たった点を名指し）、
`expr` による代入、`register read`・`memory read`・`image list`、削除、終了状態。

この過程で原因側の不具合を五つ直した。中でも**シグナルハンドラのスタックが呼び出し
規約から一語ずれていた**のは、ハンドラから `sigaction()` を呼ぶだけで落ちる問題で、
デバッガに限らず全プログラムに効く。全文は
[user-requested-fixes.md](../user-requested-fixes.md) の「デバッガを一通り動かして
見つけたもの」。
