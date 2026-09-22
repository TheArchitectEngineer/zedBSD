<!-- awesome-plan project=zedbsd record=ws034 -->

# WS034: アプリケーション拡充とカーネル・libcの是正

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG002
Related Milestones: MG001, MG007
Objectives: O1, O2
Parent: [Master](https://github.com/awemorris/zedBSD/issues/1)
Queue: なし（計画のみ。実行Queue未作成）
GitHub: 未公開（ローカル計画。同期キャッシュ未構築のチェックアウトで作成）
<!-- awesome-plan-current:end -->

## 単一目標

ユーザーがzedBSD上で使いたいアプリケーション一式（下表）をzedBSDへ追加し、
ターゲット上で実際に動かす。その過程で見つかったカーネル・libcの設計/実装ミスと
機能不足を、場当たりの回避策ではなく原因側で修正する。アプリケーションは
libc・カーネルの動作確認の役割も兼ねる。

到達点: 下表の全アプリケーションが選択したdisk imageに入り、amd64ターゲット上で
代表操作が通る。発見したlibc/カーネル不足は修正済み、または証拠付きでBug/Future
Workへ移されている。

2026-09-23 ユーザー指示「アプリケーション拡充を目的とした wsを作成します。このWSは、
私がこのOSで利用したいアプリケーションを追加しながら、カーネルやlibcの設計・実装ミスや
機能不足を修正していくものです。」

| アプリ | 置き場所（2026-09-23ユーザー指定） | 形態 | 備考 |
| --- | --- | --- | --- |
| which | `userland/base/which/` | 独自実装（base、Zlib） | |
| lspci | `userland/base/lspci/` | 独自実装（base） | `/dev/system` ioctl（決定済み） |
| lsusb | `userland/base/lsusb/` | 独自実装（base） | 同上 |
| bash | `userland/packages/shell/bash/` | 外部tarball＋パッチ | |
| GNU coreutils | `userland/packages/utils/coreutils/` | 外部tarball＋パッチ | `/usr/bin`へ入れ、PATHで`/bin`のbaseより優先 |
| vim | `userland/packages/editors/vim/` | 外部tarball＋パッチ | |
| emacs | `userland/packages/editors/emacs/` | 外部tarball＋パッチ | libc・カーネルの動作確認を兼ねる |
| git | `userland/packages/development/git/` | 外部tarball＋パッチ | 依存ライブラリも再帰的に追加 |
| gcc 16 | `userland/packages/lang/gcc-16/` | 外部tarball＋パッチ | gcc/g++/gfortran。binutils・GMP・MPFR・MPC等も再帰的に追加 |
| curl | `userland/packages/network/curl/` | 外部tarball＋パッチ | 2026-09-23追加。libcurlはgitも使う |
| wget | `userland/packages/network/wget/` | 外部tarball＋パッチ | 2026-09-23追加（GNU Wget、GPL） |
| gdb | `userland/packages/development/gdb/` | 外部tarball＋パッチ | |
| Rust | `userland/packages/lang/rust/` | 外部tarball＋パッチ | |

配置は2026-09-23のユーザー指定（分類あり）。表にない依存パッケージの置き場所はp001で決める。
エージェントの既定案は次のとおり。binutilsは`development/`、
zlib・expat・GMP・MPFR・MPC・ISLは新しい`libs/`。既存の`devel/libcxx`（WS032）は移動しない。

WSは1つの目標を持つというルールがある。ここでは上表の一式を1つの目標とする。
WSが完了する前にユーザーが対象アプリを追加した場合は、この目標の範囲変更として
Phaseを追加する。完了後の追加は新しいWSで扱う。

## ライセンス境界

[設計方針 §2.1](../master-design-policy.md) により、packagesとして配布するソフトウェアは
GNUを含む第三者ライセンスのままビルド・提供してよい。base systemの方針は変わらない。
したがって bash / coreutils / emacs / gcc / binutils / gdb / git / wget（GPL）は
`userland/packages/` に置き、ソースツリーへは取り込まない（取得したtarballと、zedBSD側の
パッチ・Makefileだけを置く）。WS032の停止条件「GPL系ライセンスの混入」はWS032の
3パッケージに限った条件であり、このWSには適用しない。ライセンス表示とprovenanceは
WS032と同じ形式（`/usr/share/licenses/<pkg>/`、版・SHA-256・パッチ一覧）で残す。

一方、which / lspci / lsusb はbaseに置くため、独自実装（Zlib）とする。GNU等のコードは
参照・転記しない。

## 実行開始条件（2026-09-23ユーザー指示）

このWSの実行は、別エージェントが作業中のWS031とWS032が完了するまで始めない。
計画はそれ以前に進めてよい。両WSの完了後は、WS035のrefactor（p002–p004）を最優先で行い、
その後にこのWSを実行する。

## 前提・依存

- WS032（q315 active、別エージェント）の成果に依存する。
  - p002 `userland/packages/external.mk`（取得・SHA-256・展開・パッチ）: cleared
  - p003 クロスビルド契約（wrapper、autoconf cross cache、CMake toolchain file、動的リンク）: cleared
  - p006 OpenSSL: cleared（gitのHTTPS経路で使う）
  - p004 libc不足補完: in-progress。p005 C++ランタイム、p008 clang: planning
- `external.mk` のtripleは現状amd64/i386だけである。このWSはamd64を対象とする。
  arm64（RPi4）などへの拡張は範囲外とし、必要ならユーザーが決める。
- 受入環境は**QEMU amd64のみ**（2026-09-23ユーザー決定）。実機での受入は行わない。
  GUIアプリはhost側をLavapipeにしたVenus（WS014のvirtio-gpu経路）で確認する。
- ptraceはWS032の作業でlldbが動く水準にある（commit `cde8e875`）。gdbは独自の
  native targetの移植が要る。
- `/dev/system` には `KERN_SYSTEM_GET_DEVICE` 等がある。PCI/USBの列挙ioctlはまだ無い。
  カーネル内部には `drv_pci_foreach_device()` と `drv_usb_foreach_device()` がある。

## 制約（Guardrail）

- HAL（`include/hal/hal.h`、`src/hal/`）の変更は、具体的な差分ごとにユーザーの事前承認を得る。
  UAPI（`include/uapi/`）の追加は該当Phaseの設計として提示し、合意してから実装する。
- libc/カーネルの修正は、アプリ側の回避パッチで済ませず原因側で直す（WSの目的）。
  ただし、別エージェントが担当するWS032/WS031の領域（`src/drivers/gpu/i915/`、
  `plan/ws031/`、WS032のlibc作業中ファイル）と衝突する場合は調整してから進める。
- aggregate `make check` は使わない。Phaseごとに有限の確認を行う。git add/commit/pushはユーザーが行う。
- 全文規約 [coding-style.md](../coding-style.md) を、base実装とカーネル/libc修正に適用する。
  外部パッケージへのパッチはupstreamの書式に合わせる。

## Phase一覧

近い順に具体化し、遠い項目は目的と主要リスクだけを書く。見積はQueue作成時に確定する。

| Combined ID | Phase | Status | 依存 |
| --- | --- | --- | --- |
| ws034-p001 | 設計固め: 版・入手元・依存パッケージの配置・依存グラフ・PATH変更の影響範囲・各Phaseの具体化 | planning | — |
| ws034-p002 | which（base独自実装） | planning | p001 |
| ws034-p003 | PCI列挙UAPI（`/dev/system`）とlspci | planning | p001 |
| ws034-p004 | USB列挙UAPI（`/dev/system`）とlsusb | planning | p001, p003（UAPI形式を共有） |
| ws034-p005 | libc・カーネル是正の受け皿（各パッケージで見つかった不足を集約して修正） | planning | p001 |
| ws034-p006 | bash | planning | p001, p005 |
| ws034-p007 | GNU coreutils | planning | p001, p005 |
| ws034-p008 | vim | planning | p001, p005 |
| ws034-p009 | git と依存ライブラリ（zlib、expat 等） | planning | p005, p017 |
| ws034-p010 | emacs | planning | p005 |
| ws034-p011 | binutils と GMP / MPFR / MPC / ISL | planning | p005 |
| ws034-p012 | gcc 16（gcc / g++ / gfortran、libstdc++・libgfortran） | planning | p011 |
| ws034-p013 | gdb | planning | p005, p011（bfd/opcodesを共有する場合） |
| ws034-p014 | Rust（rustc / cargo / std） | planning | p005, WS032-p005/p008（LLVM・C++ランタイム） |
| ws034-p015 | イメージ統合・menuconfig・ライセンス表示・provenance | planning | p002–p014, p017 |
| ws034-p016 | 全文規約確認・回帰・制限整理（必須の最終確認） | planning | p002–p015, p017 |
| ws034-p017 | curl（libcurl含む）と wget（`network/`） | planning | p005, WS032-p006 |

p017は2026-09-23の追加指示で後から加えた。IDは追加順で、実行はp009（git）より前に行う。

p005はWS032-p004と同じように一度では閉じない。後続Phaseで見つかった不足はp005へ戻して直す。

## Phaseの要点とリスク

- **p001**: 版を確定する（bash 5.x、coreutils 9.x、vim 9.1、emacs 30.x、git 2.5x、
  gcc 16.x、binutils 2.4x、gdb 16/17、Rust stable）。取得元とSHA-256、各パッケージの
  cross-build方式、依存パッケージの配置、依存グラフ、QEMU amd64での受入手順
  （GUIはLavapipe Venus）を決め、以降のPhaseを具体化する。
- **p002 which**: POSIX外のコマンドなので挙動の基準を決める（`-a`、PATHの空要素、
  実行権限の判定）。host試験とターゲット試験で確かめる。
- **p003/p004**: UAPIは次のとおり（2026-09-23ユーザー承認）。`KERN_SYSTEM_GET_PCI_DEVICE` / `KERN_SYSTEM_GET_USB_DEVICE` を
  index指定の `_IOWR` とし、既存の `KERN_SYSTEM_GET_DEVICE` と同じ列挙方式にする。
  返す項目は、PCIがbus/dev/fn・vendor/device・class/subclass/progif・revision・
  subsystem・bound driver名、USBがbus/address/port path・VID/PID・class・
  bound driver名。ID→名前の表（pci.ids/usb.ids）は同梱しない（ライセンスとサイズのため。数値表示を基本とする）。
  UAPIのサイズ・layoutを`_Static_assert`で固定し、ILP32/LP64の両方で確かめる。
- **p006–p008**: autoconfのcross cacheで対応できる見込み。bashはjob control/termios/
  `wait`系、coreutilsは`stat`/`statfs`/locale/`fts`系、vimはterminfo/curses
  （baseの独自curses）が主な確認点になる。
- **p007のPATH順序**: coreutilsは`/usr/bin`、baseの同名コマンドは`/bin`に入る。
  GNUがある場合はGNUを優先する（ユーザー決定）ため、既定PATHは`/usr/bin`を`/bin`より前に置く。
  現状は`/bin`が先で、次の箇所に分散している（2026-09-23調査）。
  `userland/base/login/main.c`、`src/kern/exec.c`（init環境）、`userland/base/sh/main.c`と
  `builtins.c`、`userland/base/newgrp/main.c`、`userland/base/common/command.c`、
  `userland/base/libc/posix.c`（`confstr(_CS_PATH)`など3箇所）、`libc/include/paths.h`
  （`_PATH_DEFPATH`/`_PATH_STDPATH`）。まとめて順序を変え、base側のscriptやserviceが
  `/bin`のbase実装を前提にしている箇所が無いかも確認する。
- **p017 curl/wget**: curlはOpenSSL（WS032）とzlibを使い、`libcurl.so`と`curl`を入れる。
  wgetもOpenSSLを使う（GnuTLSは使わない）。どちらもDNS解決（`getaddrinfo`）、
  TLS証明書の置き場所（CA bundle）、`poll`/非blocking connectの確認点になる。
  CA bundleの入手元とライセンスはp001で決める。
- **p009 git**: `NO_PERL`/`NO_TCLTK`/`NO_GETTEXT`で依存を絞る。HTTPSはp017のlibcurl＋OpenSSL
  （WS032）で行う。ssh経由のcloneはWS032のOpenSSH（p007 planning）に依存する。
- **p010 emacs**: cross-compileでは、ビルド時にtemacsの実行（pdumper）と.elcの
  byte-compileが必要なことが最大のリスクである。hostで同じ版のemacsを使ったlisp
  byte-compileと、ターゲットでのdump（初回またはイメージ作成時）を検討する。端末版
  （`--without-x`）を初期範囲とする。
- **p011/p012 gcc**: build上で「x86_64-linux → x86_64-zedbsd」のクロスgccを先に作り、
  それを使ってhost=zedbsdのgcc（Canadian cross）を作る。`config.gcc`/`config.sub`
  （binutilsも含む）へzedbsd targetを加えるパッチが要る。gfortranのためlibgfortranと
  libquadmathも扱う。ld.soとcrtの配置はzedBSDの動的リンク契約に合わせる。
- **p013 gdb**: zedbsdのnative target（ptrace・register・thread・`/proc`相当の代替）の
  移植が主な作業。lldbで既に使えているptrace ABIを基準にする。
- **p014 Rust**: 新しいOS targetとして、target spec、`libc` crate、std（unix系）の
  zedbsd対応が要る。host上でのcross-compile（std付き）を先に成立させ、次にrustc/cargoを
  ターゲットへ載せる。LLVMはRust同梱版かWS032のLLVMかを選ぶ必要がある。最も大きいPhaseである。

## 決定事項（2026-09-23ユーザー回答）

1. packagesは分類する: `lang/gcc-16`、`editors/emacs`、`editors/vim`、`shell/bash`、
   `utils/coreutils`、`development/git`、`development/gdb`、`lang/rust`。
2. lspci/lsusbは `/dev/system` のindex指定ioctl（`KERN_SYSTEM_GET_PCI_DEVICE` /
   `KERN_SYSTEM_GET_USB_DEVICE`）で実装する。
3. coreutilsはpackagesなので`/usr/bin`に入る。baseの`ls`等は`/bin`にある。PATHは
   `/usr/bin`を優先し、GNUがある場合はGNUが使われる。
4. 受入環境はQEMU amd64のみ。GUIアプリはLavapipeを使うVenusで確認する。

残る未決事項は無い。依存パッケージの置き場所は上記の既定案をp001で確定する。

## 現状

計画を作っただけで、Queue・実装・試験はまだ無い。GitHub Issueも未作成である。
