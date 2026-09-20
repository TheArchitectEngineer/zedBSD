<!-- awesome-plan project=zedbsd record=ws032 -->

# WS032: 外部パッケージのクロスビルド導入（clang / OpenSSL / OpenSSH）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG002
Related Milestones: MG005, MG001
Objectives: O1, O2
Parent: [Master](https://github.com/awemorris/zedBSD/issues/1)
Queue: [q315](queue-ws032.md) active（p001–p003 cleared、p004/p006 進行中）
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
| ws032-p004 | libc・ヘッダの不足補完（`netinet/tcp.h`、`sys/param.h`、`chroot`、`dl_iterate_phdr`、`posix_memalign` ほか、実ビルドで確定した分） | in-progress | 300 分 |
| ws032-p005 | C++ランタイム: libunwind → libc++abi → libc++ をクロスビルドし、例外・RTTI・static initializer・`thread_local` を実機で確認 | planning | 360 分 |
| ws032-p006 | OpenSSL: 版pin・Configureターゲット・クロスビルド・`libcrypto.so`/`libssl.so`/`openssl`、実機確認 | cleared | 360 分 |
| ws032-p007 | OpenSSH: cross configure・privsep・`ssh`/`sshd`/`ssh-keygen`/`scp`/`sftp`、実機で公開鍵ログイン | planning | 420 分 |
| ws032-p008 | clang: LLVM クロスビルド（host tablegen 再利用）、ツール一式を `/usr/bin` へ、実機で `clang hello.c -o hello` → 実行 | planning | 480 分 |
| ws032-p009 | イメージ統合: menuconfig 登録・rootfs/disk-image 投入・ライセンス通知・provenance 記録・既定構成への非影響確認 | planning | 240 分 |
| ws032-p010 | レビュー: 規約全文確認、静的確認、回帰、制限整理 | planning | 180 分 |

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

## 現況（2026-09-21）

q315 で実行中。p001–p003 cleared、p004/p006 進行中。

取得機構とクロスビルド契約は動いており、OpenSSL 3.5.8 は取得・検証・展開・パッチ・
configure を通り、libcrypto の最後の 1 file まで compile した。そこで
`struct sigaction` が POSIX の形をしておらず（`sa_handler` が関数ポインタではなく
`uint64_t`、`sa_sigaction` が無い）、**UAPI の変更が必要**と判明したため停止し、
ユーザー承認を得て POSIX 準拠へ直した（[phase004/results.md](phase004/results.md) §4）。

その後 OpenSSL 3.5.8 は `libcrypto.so` / `libssl.so` / `openssl` まで通り、
契約検査と未解決 symbol 検査を通過し、disk image に入って起動まで確認した。
ターゲット上でも `openssl version` / `dgst -sha256` / `rand` / `genrsa 2048` の
動作を確認し、p006 を cleared とした。ユーザー指示による 6 件の修正は
[user-requested-fixes.md](user-requested-fixes.md)。
source/doc の git add/commit/push はユーザーが行う。GitHub Issue/Project への公開は
別途の指示による（本WSはローカルの `plan/ws032/` のみ）。
