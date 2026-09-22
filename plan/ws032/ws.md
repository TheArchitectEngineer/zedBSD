<!-- awesome-plan project=zedbsd record=ws032 -->

# WS032: 外部パッケージのクロスビルド導入（clang / OpenSSL / OpenSSH）

<!-- awesome-plan-current:start -->
Status: completed
Primary Milestone: MG002
Related Milestones: MG005, MG001
Objectives: O1, O2
Parent: [Master](https://github.com/awemorris/zedBSD/issues/1)
Implementation Queue: none
Last Queue: [q315](queue-ws032.md) finished
Phases: p001–p010 cleared
Reuse: prohibited; new objectives require a new WS
Design: plan/ws032/external-design.md
<!-- awesome-plan-current:end -->

## 単一目標

`userland/packages/` に **clang・OpenSSL・OpenSSH** を追加し、`make` でリリースの
source code tarball を取得・検証・展開し、ビルド用パッチを当てて**クロスビルド**し、
選択した disk image へ載せて実機で動作させる。これらの外部プロジェクトはソースツリーへ
直接取り込まず、外部から取得する。

`userland/packages/` の位置づけ: **システムの動作にどうしても必要だが、ソースツリーへ
取り込めない外部プロジェクト**であり、将来は独自実装に置き換えたい対象。`userland/base/`
と同じ扱いにはしない。

## 範囲と前提

- ツールチェインは `build/llvm/bin/` のクロス用 clang と `build/<arch>/sysroot` を使う。
- `userland/` はクロスビルドが前提。**ターゲット上でのネイティブビルドは考慮しない**
  （ネイティブのパッケージビルドシステムは別途作る）。
- リンク形態は**動的**（ユーザー決定、2026-09-20）。`/lib/ld.so` と `libc.so` を使い、
  実行ファイルは PIE、共有ライブラリは `-soname` 付きで作る。
- clang は **libc++ / libc++abi / libunwind を同WSに含める**（ユーザー決定）。C++ 製の
  clang をターゲットで動かすための必須前提であり、独立した別目標ではない。
- clang に同梱するツールは **clang, clang++, ld.lld, llvm-ar, llvm-ranlib, llvm-nm,
  llvm-objcopy, llvm-objdump, llvm-readelf, llvm-strip**（ユーザー決定）。実機上で
  コンパイルからリンクまで完結する状態を到達点とする。
- OpenSSH は **sshd を含む**（ユーザー決定）。privilege separation に必要な `chroot`・
  専用ユーザ・`/var/empty`・host key の扱いを範囲に入れる。

目標にしないこと: ネイティブビルド、パッケージの配布形式・依存解決・アップグレード機構、
リポジトリサーバ、実機上での clang セルフホスト（clang 自身の再ビルド）、
OpenSSL の正式な FIPS/完全な test suite 通過、`userland/base/` の再編。

## 外部設計

機構の正本は [external-design.md](external-design.md)。ディレクトリ構成、共通 `.mk` の契約
（取得・SHA-256 とアーカイブ安全性検査・展開・パッチ・stamp・オフライン既定）、
クロスビルド契約（wrapper・autoconf cross cache・CMake toolchain file・動的リンクの実測値）、
staging とイメージ投入、ライセンス表示、パッケージ別の要点を規定する。

各Phaseの詳細設計は、そのPhaseの計画時に `plan/ws032/phaseNNN/phase.md` へ書く。
前段Phaseのdocが確定インタフェースの正本で、後段はそれを参照する。

## Phase registry

| Combined ID | Phase | Status | 見積 |
| --- | --- | --- | --- |
| ws032-p001 | 設計固め: 外部設計確定、3パッケージの版・入手元の確定、ライセンス監査方針 | cleared | 180 分 |
| ws032-p002 | 共通取得機構: `userland/packages/external.mk`（取得・検証・展開・パッチ）と `download`/`patch` 接続、host試験 | cleared | 300 分 |
| ws032-p003 | クロスビルド契約: wrapper・autoconf cross cache・CMake toolchain file、動的リンクの成立を最小C/C++プログラムで確認 | cleared | 300 分 |
| ws032-p004 | libc・ヘッダの不足補完（`netinet/tcp.h`、`sys/param.h`、`chroot`、`dl_iterate_phdr`、`posix_memalign` ほか、実ビルドで確定した分） | cleared | 300 分 |
| ws032-p005 | C++ランタイム: libunwind → libc++abi → libc++ をクロスビルドし、例外・RTTI・static initializer・`thread_local` を実機で確認 | cleared | 360 分 |
| ws032-p006 | OpenSSL: 版pin・Configureターゲット・クロスビルド・`libcrypto.so`/`libssl.so`/`openssl`、実機確認 | cleared | 360 分 |
| ws032-p007 | OpenSSH: cross configure・privsep・`ssh`/`sshd`/`ssh-keygen`/`scp`/`sftp`、実機で公開鍵ログイン | cleared | 420 分 |
| ws032-p008 | clang: LLVM クロスビルド（host tablegen 再利用）、ツール一式を `/usr/bin` へ、実機で `clang hello.c -o hello` → 実行 | cleared | 480 分 |
| ws032-p009 | イメージ統合: menuconfig 登録・rootfs/disk-image 投入・ライセンス通知・provenance 記録・既定構成への非影響確認 | cleared | 240 分 |
| ws032-p010 | レビュー: 規約全文確認、静的確認、回帰、制限整理 | cleared | 180 分 |

見積は合計 3120 分の計画値であり、実行の承認でも達成の保証でもない。

p004 は一度で閉じない。p003・p005〜p008 の実ビルドで見つかった不足は同じ p004 へ
差し戻して閉じる（似た作業で新Phaseを増やさない）。

## 順序・依存

p001 → p002 → { p003, p004 } → { p005 → p008, p006 → p007 } → p009 → p010。

p005（C++ランタイム）が実機で通らないうちは p008（clang）へ進まない。
p006（OpenSSL）は p007（OpenSSH）の前提。p003 と p004 は相互に往復する。

## 適用規約・実行境界

[Guardrail](https://github.com/awemorris/zedBSD/issues/363) とローカル `plan/coding-style.md`
の全文を実装前に読む。HAL（`include/hal/hal.h`）とUAPIは不変を既定とし、必要になれば
停止して提示する。aggregate `make check` は使わない。無関係な変更を保護する。

`ZEDBSD_LLVM_PATCH_LEVEL` は変更しない（toolchain cache 無効化と LLVM 全再ビルドを
招くため）。clang driver への linker job 実装は本WSの範囲外とし、wrapper で吸収する。

取得した tarball のライセンスは機械確認する。GPL 系が混入していたら停止して提示する。
想定は LLVM/libc++ = Apache-2.0 WITH LLVM-exception、OpenSSL 3.x = Apache-2.0、
OpenSSH = BSD/ISC 系で、いずれもO1（寛容ライセンス）と整合する。

版・SHA-256・configure オプションなど、実際に取得しなければ確定できない値は計画段階で
確定せず、各Phaseが実測して `plan/ws032/provenance.md` に記録する。

## 並行作業との境界（2026-09-20）

本WSは `~/zedBSD-2/` で作業する。別エージェントが `~/zedBSD/`（同一リポジトリの別チェック
アウト、同一commit `c1d55aa7`）で WS031 を実施中である。衝突を避けるため、
`src/drivers/gpu/i915/`、`plan/ws031/`、`platform/amd64/` 配下には触らない。
ルート `Makefile`、`libc/`、`userland/base/libc/`、`toolchain/` は本WSが触る可能性が
あるため、変更は最小差分にして該当 phase doc に記録する。

2026-09-21 にユーザーがサーバを移し、以後は `~/zedBSD/`（centris）で作業した
（[handover-server-move.md](handover-server-move.md)）。`src/drivers/gpu/i915/` と
`plan/ws031/` は本WSでは触っていない。

## q315完了: 外部パッケージのクロスビルド導入（2026-09-23）

WS032 を completed とし、q315 を finished とする。active Queue なし。p001–p010 は
すべて cleared。**再利用は禁止**で、別の目標は新しい WS を立てる。

### 到達点

`userland/packages/` に **clang・OpenSSL・OpenSSH**（と、その前提の C++ ランタイム）が
あり、`make` がリリースの tarball を取得・SHA-256 で検証・安全性を検査して展開し、
パッチを当ててクロスビルドし、menuconfig で選んだものが disk image に載り、実機で動く。
外部プロジェクトのソースはツリーへ取り込んでいない。

| パッケージ | 版 | 実機で確認したこと |
| --- | --- | --- |
| `devel/libcxx` | LLVM 23.1.0 | 例外・RTTI・static initializer・`thread_local` の 7/7 PASS |
| `security/openssl` | 3.5.8 | `openssl version`、`dgst -sha256`、`rand`、`genrsa 2048` |
| `network/openssh` | 10.5p1 | init が起動した sshd への公開鍵ログイン、`scp`、`sftp`、疑似端末 |
| `lang/clang` | LLVM 23.1.0 | `clang hello.c -o hello` → 実行、終了状態の受け渡し |

clang パッケージが置くのは clang, clang++, ld.lld, llvm-ar, llvm-ranlib, llvm-nm,
llvm-objcopy, llvm-objdump, llvm-readelf, llvm-strip と、後述の lldb, lldb-server。
実機上でコンパイルからリンクまで完結する。

### ユーザー指示による範囲追加（2026-09-22）

「lldb と LLVM のツールもビルドを通してインストールし、以後 ssh+lldb でバグを追えるように」
という指示で、p008 の成果物に **lldb / lldb-server** を加えた。これは既存のツール一覧の
延長であり、別目標として新しい Phase は作っていない。

デバッガはカーネルの支援が要るため、次を実装した。承認を得た API 追加は
[user-requested-fixes.md](user-requested-fixes.md) と各 Phase の結果に記録がある。

- `ptrace(2)`（OpenBSD の API 形。`PT_TRACE_ME`〜`PT_SET_DEBUG_POINTS`、`PT_IO`、
  `PIOD_READ_AUXV`、`PT_GET_SIGINFO`）と `KERN_SYS_ptrace`。
- HAL のデバッグ面: `hal_task_{get,set}_user_{gpregs,fpregs,vregs}`、単一ステップ、
  ハードウェアデバッグ点。レジスタの形は UAPI が独立に定義し、HAL を外へ出さない。
- 動的リンカを `src/rtld/` へ移し、`r_debug` / `link_map` / `DT_DEBUG` を公開した。
- lldb 側は `userland/packages/lang/clang/patches/0004-add-the-zedbsd-process-plugin.patch`
  に process / platform / host / signal のプラグインとして入れてある。

実機で、ブレークポイント（関数名・`file:line`）、バックトレース、変数と式の表示、
`list`、step in/over/out/inst、ハードウェアウォッチポイント（複数）、`expr` による代入、
レジスタ・メモリ・ロード済みモジュールの表示が動く。

### この WS が原因側で直したもの

外部パッケージを通したことで、こちら側の不足が実地に出た。主なものを挙げる。

- `struct sigaction` が POSIX の形をしていなかった（`sa_handler` が関数ポインタでない）。
  UAPI 変更のため停止し、承認を得て直した（[p004 §4](phase004/results.md)）。
- **シグナルハンドラのスタックが呼び出し規約から一語ずれていた**。ハンドラから呼んだ
  `sigaction()` の中の `movaps` が一般保護例外を出し、lldb と lldb-server が落ちていた。
  `src/kern/signal.c` で、戻り番地を持つ形式のフレーム基点を十六の倍数から一語下へ置く。
  検査は [tests/signal-stack-target.c](tests/signal-stack-target.c)（実機 10/10 PASS）。
- `fork()` が、兄弟スレッドの pin されたページを無期限に待っていた（`read` や `waitpid` で
  止まっているスレッドがあると再現）。`vmspace_fork` が pin されたページを即時複製する。
- POSIX が要求していて欠けていた宣言: `in_addr_t`/`in_port_t`/`INADDR_LOOPBACK`、
  `SO_KEEPALIVE`、IPv6 の宣言面、`optreset`、`CSTOPB`、`tm_gmtoff`/`tm_zone`、`<ar.h>`、
  `<sys/param.h>`、`<netinet/tcp.h>`。
- `getopt_long_only` が一文字のオプションを長い名前の省略形と誤解していた。
- `_start` に `.cfi_undefined` を足し、バックトレースがそこで止まるようにした。

### 受け入れ

ターゲット試験 6 本を同一イメージで 4 並列実行し、すべて exit 0。

| 試験 | 見るもの |
| --- | --- |
| `plan/ws032/tests/sh-target.sh` | `/bin/sh` の回帰 |
| `plan/ws032/tests/clang-target.sh` | 実機でのコンパイルと実行 |
| `plan/ws032/tests/openssh-target.sh` | 鍵生成・公開鍵ログイン・`scp`・`sftp` |
| `plan/ws032/tests/sshd-service-target.sh` | init が起動する sshd |
| `plan/ws033/tests/networking-target.sh` | ネットワーク |
| `plan/ws032/tests/lldb-target.sh` | デバッガ（停止・変数・ウォッチポイント・step・終了） |

パッケージのビルドは `tools/build/check-dynamic-elf.py` で動的 ELF の契約を検査する
（OpenSSL・OpenSSH）。取得物のライセンス監査は [provenance.md §4](provenance.md)。

### 積み残し（完了にしない）

- [BUG-026](../bugs/BUG-026.md): lld の出力が通常ファイルへは全ゼロで届く。`--mmap-output-file`
  で回避したまま、原因は未特定。**本WSでデバッガが入ったので、再開の前提は整った。**
- [BUG-027](../bugs/BUG-027.md): ファイル裏付けページのフォルトが遅く、LLVM 由来の
  プログラムの起動が 10〜30 秒かかる。lldb は packet-timeout 60 秒で回避している。
- [BUG-028](../bugs/BUG-028.md): 誰も listen していない loopback ポートへの `connect()`
  が返らない。
- 目標にしないと決めたもの（ネイティブビルド、配布形式・依存解決・アップグレード、
  リポジトリサーバ、clang セルフホスト、OpenSSL の完全な test suite）は範囲外のまま。
  ネイティブのパッケージビルドと追加アプリは [WS034](../ws034/ws.md) が持つ。

### 記録

- 機構の正本: [external-design.md](external-design.md)。取得物と検証とライセンス:
  [provenance.md](provenance.md)。各Phaseの結果: `phaseNNN/results.md`。
- ユーザー指示による修正の全文: [user-requested-fixes.md](user-requested-fixes.md)。
- source/doc の git add/commit/push はユーザーが行う。GitHub Issue/Project への公開は
  していない（本WSはローカルの `plan/ws032/` のみ）。
