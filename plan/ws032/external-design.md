# WS032 外部設計: userland/packages の外部プロジェクト取り込み機構

本書はWS032の外部設計（WS単体で自足する正本）である。全体構成、ディレクトリ境界、
共通 `.mk` の契約、クロスビルド契約、イメージ投入とライセンス表示、3パッケージ個別の
要点を規定する。各Phaseの詳細設計（確定インタフェース、触れる／触れないファイル、
受け入れ）は、そのPhaseの計画時に `plan/ws032/phaseNNN/phase.md` へ書く。

作成 2026-09-20。本書は計画であり、実装・ビルド・実機確認はまだ行っていない。
版・SHA-256・configure オプションなど、実際に取得しなければ確定できない値は
本書では確定せず、各Phaseが実測して記録する（推測値を書かない）。

## 1. 位置づけと境界

`userland/packages/` は、**システムの動作にどうしても必要だが、ソースツリーへ直接
取り込めない外部プロジェクト**を置く場所である。将来は独自実装に置き換えたい対象
であり、base（`userland/base/`）と同じ扱いにはしない。

- 外部プロジェクトのソースはツリーに取り込まない。リリースの source tarball を
  **make で取得・検証・展開**し、ビルド用パッチを当ててからビルドする。
- **クロスビルドのみ**を対象とする。ターゲット上でのネイティブビルドは考慮しない
  （ネイティブのパッケージビルドシステムは別途作る）。
- ツールチェインは `build/llvm/bin/` のクロス用 clang と `build/<arch>/sysroot` を使う。
- 対象は clang（＋ターゲット用 C++ ランタイム）、OpenSSL、OpenSSH の3つ。

境界外: ネイティブビルド、パッケージ配布形式・依存解決・アップグレード、
リポジトリサーバ、`userland/base/` の再編、HAL/UAPI の変更。

## 2. 調査で確定した既存の事実（2026-09-20、本ツリー）

この設計はすべて既存機構の上に載る。新しい枠組みを別に作らない。

| 既存資産 | 内容 |
| --- | --- |
| `userland/base/package.mk` | `ZEDBSD_USERLAND_PACKAGE` 登録マクロと、単体 `make -C` 用の `build`/`install` |
| `userland/download.mk` | `download` → `patch` → `build` → `install` のライフサイクル。`ZEDBSD_USERLAND_DOWNLOAD_TARGETS` / `..._PATCH_TARGETS` に各パッケージが追加する |
| ルート `Makefile` 176–210行 | `userland/*/*/Makefile` と `userland/*/*/*/Makefile` を全部 include してメタデータを収集。`make download` は全パッケージの取得目標を集約（390行） |
| `userland/packages/editors/remacs/Makefile` | 現状唯一の例。git clone 方式で、tarball 方式の手本ではない |
| `toolchain/llvm/llvm.mk` | **tarball 取得の手本**。URL・サイズ・SHA-256・アーカイブ member の安全性検査（根ディレクトリ外・絶対パス・`..`・非通常 member・脱出する link）・排他 lock・検証済みstamp |
| `toolchain/llvm/version.mk` | 版の一元管理。LLVM 23.1.0（`llvm-project-23.1.0.src.tar.xz`、SHA-256 済み）を既に pin 済み |
| `toolchain/llvm/sysroot.mk` | `build/<arch>/sysroot` を生成。`usr/include`、`usr/lib/libc.a`・`libc.o`・`libzedbsd-compiler-rt.a`・`libclang_rt.builtins.a`・`crt0.o`・`crt1.o`・linker script |
| `platform/amd64/vmunix.mk` 686–830行 | 動的リンクの実装。`build/<plat>/dynamic/` に `ld.so`・`libc.so`・`libwayland-client.so`・`libvulkan.so`、動的実行ファイルは `vkdemo`/`wltest` |
| `tools/build/check-dynamic-elf.py` | 生成 ELF の役割別検査（`application` / `shared-library` / `libc` / `module` ほか） |
| `userland/base/licenses/llvm-runtime/Makefile` | ライセンス通知を `data` クラスのパッケージとして rootfs に載せる形 |

### 2.1 動的リンクの実測契約

既存の動的実行ファイル（`vkdemo`）とライブラリ（`libvulkan.so`）の実際のリンク行から、
外部パッケージが守るべき契約は次のとおり。

- 実行ファイル: `-nostdlib -pie -Wl,--no-relax -Wl,--hash-style=sysv,-z,now,-z,relro,-z,separate-code -Wl,-z,stack-size=0x100000,--allow-shlib-undefined -Wl,--dynamic-linker=/lib/ld.so`、先頭に `sysroot/usr/lib/crt1.o`、libc は `-l:libc.so`。
- 共有ライブラリ: `-shared -soname <name>.so --hash-style=both -z now -z relro -z separate-code`。
- **symbol versioning は使えない**。`check-dynamic-elf.py` は `application` / `shared-library` に `.gnu.version_d` / `.gnu.version_r` があると失敗させる（105–107行）。OpenSSL・libc++ は既定で version script を使うので、明示的に無効化する。
- rtld（`src/rtld/rtld.c`）は sysv hash・`DT_VERSYM` の読み飛ばし・`init_array`・`dlopen`/`dlsym`/`dladdr`・TLS descriptor・`dl_iterate_phdr`・`LD_LIBRARY_PATH`・起動時集合の静的 TLS 領域（実行ファイル自身の `PT_TLS` と initial-exec）を持つ。**GNU_HASH・`R_X86_64_COPY`・IRELATIVE(ifunc)・`FINI_ARRAY`/`PREINIT_ARRAY` は持たない**。copy relocation が無いため実行ファイルは PIE で作る（既存の扱いと同じ）。
- `build/<arch>/sysroot/usr/lib` には **`libc.so` が無い**（静的な `libc.a`/`libc.o` のみ）。動的 `libc.so` は `build/<plat>/dynamic/` にある。外部パッケージのリンクはこの差を吸収する必要がある（§4.3）。

### 2.2 libc の充足と不足

OpenSSL/OpenSSH が要求する POSIX は概ね揃っている（確認済み: `getentropy`、`arc4random`、
`crypt`、`openpty`/`forkpty`、`setresuid`/`setreuid`/`setgroups`/`initgroups`、`getpwnam`、
`getaddrinfo`、`poll`/`select`、`sigaction`、`posix_spawn`、`setrlimit`、`clock_gettime`、
`statvfs`、`realpath`、`mkstemp`、`strlcpy`）。

現時点で確認した**不足**（in-tree の追加で解消する。p004 の対象）:

| 不足 | 必要とする側 |
| --- | --- |
| `netinet/tcp.h`（`TCP_NODELAY`） | OpenSSH、OpenSSL の一部試験 |
| `sys/param.h` | OpenSSH（多数の箇所で include） |
| `chroot` | OpenSSH の privilege separation |
| `dl_iterate_phdr` | libunwind（動的実行での `.eh_frame_hdr` 探索） |
| `posix_memalign` | libc++ / OpenSSL の一部経路 |
| `madvise`、`getauxval` | 任意。必要と判明した場合のみ |

不足はPhaseで実際のビルド失敗から確定する。事前の一覧は着手順の見積であって網羅ではない。

## 3. ディレクトリ構成

```
userland/packages/<category>/<name>/
    Makefile          # 版・URL・SHA-256・ビルド手順・登録
    patches/          # ビルド用パッチ（連番、ツリー内で保持する唯一の外部差分）
    files/            # 設定ファイル等、rootfs へ載せる小さな添付物
build/distfiles/                       # 取得した tarball（アーキテクチャ非依存で共有）
build/packages/<name>/src/             # 展開先（パッチ適用済み）
build/packages/<name>/build/           # ビルドツリー（out-of-tree）
build/packages/<name>/stage/           # DESTDIR 相当。ここからイメージへ載せる
build/<plat>/packages/toolchain/       # クロス用 wrapper と cache/toolchain file（§4）
```

category は `devel`（clang と C++ ランタイム）、`security`（OpenSSL、OpenSSH）とする。
`editors/remacs` の既存 category はそのまま残す。

外部ソースは `build/` 以下にだけ存在し、`.gitignore` の対象である。ツリーに入るのは
Makefile・patches・files だけ。

## 4. 共通機構の契約

### 4.1 取得・検証・展開・パッチ（`userland/packages/external.mk`、新規）

`toolchain/llvm/llvm.mk` の取得・検証手順を、複数パッケージが使える形へ一般化する。
LLVM 側は**当面そのまま**にし、本WSでは移行しない（回帰範囲を分けるため）。

パッケージは次の変数を宣言し、`ZEDBSD_EXTERNAL_SOURCE` を eval する。

| 変数 | 意味 |
| --- | --- |
| `NAME` / `VERSION` | パッケージ識別と版 |
| `ARCHIVE_URL` / `ARCHIVE_NAME` | 取得元とファイル名 |
| `ARCHIVE_SIZE` / `ARCHIVE_SHA256` | 検証値。実取得で確定する |
| `ARCHIVE_ROOT` | 展開後の単一根ディレクトリ名 |
| `PATCHES` / `PATCH_LEVEL` | 適用パッチ列と、再適用判定に使う世代タグ |

生成する目標（stamp で冪等にする）:

- `$(DISTFILES)/<archive>`: 排他 lock の下で一時ファイルへ取得 → 検証 → `mv`。検証前の
  ファイルを正規の名前にしない。
- `archive-verified` stamp: サイズ・SHA-256・member 安全性（根ディレクトリ外、絶対パス、
  `..`、非通常 member、脱出する symlink/hardlink を全て拒否）。
- `src/.extracted-<version>-<patch level>` stamp: 展開してパッチ適用。パッチが変われば
  展開からやり直す（部分適用の状態を残さない）。
- `patch` / `download` 目標への接続は既存の `ZEDBSD_USERLAND_DOWNLOAD_TARGETS` /
  `ZEDBSD_USERLAND_PATCH_TARGETS` を使う。

**オフライン既定**: `menuconfig` や通常の `make` が暗黙にネットワークI/Oを起こさない。
取得は `make download`、または当該パッケージが `ZEDBSD_USER_PROGRAMS` で明示選択された
ときだけ走る（remacs が既に取っている扱いと同じ）。

### 4.2 ビルド・staging

パッケージは `configure`/`cmake`/`make` を `build/packages/<name>/build` で実行し、
`stage/` へ `DESTDIR` install する。ツリーのビルド規則は stage の成果物ファイルだけを
prerequisite として扱い、外部ビルドシステムの内部依存には関与しない
（外部ツリーを make の依存グラフへ展開しない）。

### 4.3 クロスビルド契約（本WSの中核）

外部の `configure`/`cmake` は「普通の cc が普通に動く」ことを前提にする。一方ツリーの
リンク行は `-nostdlib` と明示 `crt1.o`、`-l:libc.so`、`build/<plat>/dynamic` 上の
共有ライブラリに依存する（§2.1）。この差を**1か所**で吸収する。

**採用: wrapper 方式**。`build/<plat>/packages/toolchain/bin/` に
`zedbsd-clang` / `zedbsd-clang++` / `zedbsd-ld` と `ar`/`ranlib`/`nm`/`strip`/`objcopy` の
名前付き入口を生成し、`CC`/`CXX`/`LD`/`AR`/… として外部ビルドへ渡す。wrapper が
`--target`・`--sysroot`・crt・`-L`/`-rpath-link`・リンカ既定値を補い、`-shared` と
実行ファイルで正しい既定を選ぶ。

- 利点: LLVM の再ビルドが要らない。順序（`crt1.o` は先頭、`-l:libc.so` は後）を正しく
  制御できる。ツリー側のリンク規則を1行も変えない。
- 記録する適応: wrapper は zedBSD 側の都合であって外部プロジェクトの標準手順ではない。
  どの引数をどの理由で補うかを wrapper 冒頭のコメントと phase doc に書く。
- 将来の整理（本WSの範囲外、Future Workへ）: clang driver の `ZedBSD` toolchain
  （`toolchain/llvm/patches/0001-add-zedbsd-x86-target.patch`）へ linker job を実装すれば
  wrapper は不要になる。ただし `ZEDBSD_LLVM_PATCH_LEVEL` の更新は
  **toolchain cache（rev-0 資産）を無効化し LLVM の全再ビルドを要求する**ため、
  本WSでは行わない。

補助として、autoconf 用の **cross cache ファイル**（`ac_cv_*` の既知値）と CMake 用の
**toolchain file** を `build/<plat>/packages/toolchain/` に生成する。両者は wrapper を
参照し、値の正本を二重に持たない。

C/C++ フラグの既定:

- C: hosted。`-O2`。`-ffreestanding`/`-fno-builtin` はツリー内コード向けの指定であり、
  外部パッケージには使わない。
- C++: 例外・RTTI が要るため **unwind table を無効にしない**（`-fasynchronous-unwind-tables`）。
- 既知の制限: `libc.so` は `-fno-asynchronous-unwind-tables` でビルドされている。
  libc のフレームを跨いで C++ 例外を投げる経路（コールバック内 throw など）は
  成立しない。制限として記録し、必要になった時点で libc 側を扱う。

### 4.4 イメージ投入と登録

- `ZEDBSD_USERLAND_PACKAGE` で登録し、menuconfig の選択対象にする。既定は `n`
  （clang は数百MB規模になるため、選択しない構成を通常とする）。
- stage の成果物は `ZEDBSD_PACKAGE_INPUTS` / `ZEDBSD_PACKAGE_FILES` で rootfs / disk image
  へ載せる（remacs と同じ経路）。配置は実行ファイル `/usr/bin`、共有ライブラリ `/lib`、
  ヘッダ等 `/usr/include`・`/usr/lib`、設定 `/etc`。
- パッケージ間依存は既存の `REQUIRE`（安定パス名）で表す。
  openssh → openssl、clang → libc++（→ libc++abi → libunwind）。

### 4.5 ライセンス表示

各パッケージは自身の LICENSE を `/usr/share/licenses/<name>/` へ載せる。イメージに
バイナリが入るときに通知も必ず入る形にする（`userland/base/licenses/llvm-runtime` と
同じ `data` クラスの登録、もしくはパッケージ自身の `ZEDBSD_PACKAGE_FILES`）。
版・SHA-256・入手元・適用パッチの一覧を `plan/ws032/provenance.md` に記録する。

想定ライセンス（実取得時に機械確認する）: LLVM/libc++ = Apache-2.0 WITH LLVM-exception、
OpenSSL 3.x = Apache-2.0、OpenSSH = BSD系/ISC系。いずれも O1（寛容ライセンス）と整合する。
確認の結果 GPL 系が混入する場合は**停止して提示**する。

## 5. パッケージ別の要点

### 5.1 OpenSSL（`security/openssl`）

- 最初に着手する。依存が浅く、C のみで、OpenSSH の前提でもある。
- `Configure` はターゲット定義が要る。`Configurations/` へ zedBSD ターゲットを足す
  小さなパッチを当てる（`--cross-compile-prefix` ではなく明示ターゲット）。
- 初回は `no-asm`（perlasm 経路を避ける）、`no-tests`、`shared`、version script 無効、
  `no-dso`/`no-engine` から始め、動いた後に asm を検討する。
- ホストに perl が要る（既存の toolchain 前提と同様、host tool の要求として記録）。
- 成果物: `/lib/libcrypto.so`、`/lib/libssl.so`、`/usr/bin/openssl`、`/usr/include/openssl/`。
- 受け入れ: 実機で `openssl version`、乱数取得、`enc`、鍵生成、自己署名証明書の生成が通る。
  正式な FIPS・完全な test suite 通過は主張しない。

### 5.2 OpenSSH（`security/openssh`）

- portable 版の `configure` を `--host=x86_64-unknown-zedbsd` で走らせる。判定できない
  項目は cross cache で与え、**与えた値と根拠を phase doc に残す**（当て推量を残さない）。
- privilege separation: `chroot`（p004 で追加）、`/var/empty`、`sshd` ユーザ、`/etc/ssh` と
  host key の生成手順が要る。sandbox は移植可能な方式を選ぶ。
- `--without-pam`、zlib の扱い（`--without-zlib` から始める）、libcrypto は OpenSSL を使う。
- 成果物: `/usr/bin/ssh`・`scp`・`sftp`・`ssh-keygen`、`/usr/sbin/sshd`（配置は phase で確定）、
  `/etc/ssh/*`。
- 受け入れ: 実機で `ssh-keygen` による鍵生成、`sshd` 起動、外部ホストから公開鍵認証で
  ログインしてコマンドを実行し、正常に切断できる。

### 5.3 C++ ランタイム（`devel/libcxx`、clang の前提）

- **clang の必須前提**。同じ LLVM 23.1.0 tarball（`toolchain/llvm/version.mk` で pin 済み・
  SHA-256 検証済み）を再利用し、二重取得しない。
- libunwind → libc++abi → libc++ の順にクロスビルド。`dl_iterate_phdr` が要る（p004）。
- 検証: 例外の throw/catch、RTTI、static initializer（`init_array`）、`thread_local`、
  `std::string`/`std::vector`/iostream が動く小さな C++ プログラムを実機で実行する。
  ここが通らないうちは clang へ進まない。

### 5.4 clang（`devel/clang`）

- 同じ LLVM tarball から、**ターゲット上で動く** clang をクロスビルドする。
- CMake クロスビルドは host 側の tablegen 等を要求する。`build/llvm` の既存ホスト
  ツールを `LLVM_NATIVE_TOOL_DIR` 相当で再利用し、host 版 LLVM を二重に建てない。
- 構成: `LLVM_TARGETS_TO_BUILD=X86`、distribution components は既存の
  `ZEDBSD_LLVM_INSTALLED_TOOL_NAMES` と同じ一式（clang, clang++, ld.lld, llvm-ar,
  llvm-ranlib, llvm-nm, llvm-objcopy, llvm-objdump, llvm-readelf, llvm-strip）。
- 配置は `/usr/bin` と clang resource headers。ターゲット上の既定 sysroot/リンク既定は、
  実機で使える形（§4.3 の wrapper 相当を driver 既定として持たせるか、`/etc` の設定か）を
  phase で決める。
- 受け入れ: 実機上で `clang hello.c -o hello` → 実行できる（リンクまで実機で完結）。
  セルフホスト（実機上で clang 自身をビルド）は目標にしない。

## 6. 順序と依存

```
p001 設計固め
  └ p002 共通取得機構 ─┬ p003 クロスビルド契約（wrapper/cache/toolchain file）
                        └ p004 libc・ヘッダ不足の補完
                              ├ p005 C++ランタイム ── p008 clang
                              └ p006 OpenSSL ── p007 OpenSSH
                                                   └ p009 イメージ統合・ライセンス
                                                        └ p010 レビュー
```

p004 の不足一覧は p003 と p006 の実ビルド失敗から確定するため、p004 は一度で閉じず、
後続Phaseが見つけた不足を同じPhaseへ差し戻して閉じる（新Phaseを増やさない）。

## 7. 受け入れ（WS全体）

1. `make download` で3パッケージ（＋C++ランタイム）の tarball を検証付きで取得でき、
   検証失敗・改竄 member・部分取得が拒否される。
2. クロスビルドが通り、生成 ELF が `check-dynamic-elf.py` の検査を通る。
3. 3パッケージを選択した disk image がビルドでき、実機／QEMU で §5 各項の受け入れが通る。
4. 3パッケージを選択しない既定構成のビルドが、本WS導入前と同じく warning 0 で通る
   （既存構成への影響が無いこと）。
5. ライセンス通知がイメージに入り、provenance（版・SHA・パッチ）が記録されている。

## 8. 制約・停止条件

- HAL（`include/hal/hal.h`）と UAPI は不変。必要になったら停止して提示する。
- 取得した tarball に GPL 系ライセンスが混入していたら停止して提示する。
- `ZEDBSD_LLVM_PATCH_LEVEL` の変更（＝toolchain cache 無効化・LLVM 全再ビルド）は行わない。
- aggregate `make check` は使わない。対象を限定したビルドと試験を使う。
- 並行作業（別エージェントが `~/zedBSD/` で WS031 を実施中）と衝突しないよう、
  `src/drivers/gpu/i915/`、`plan/ws031/`、`platform/amd64/` 配下は触らない。
  ルート `Makefile` と `libc/`・`userland/base/libc/` は本WSが触る可能性があるため、
  変更時は最小差分にして phase doc に記録する。
- source/doc の git add/commit/push はユーザーが行う。GitHub Issue/Project への公開は
  別途の指示による。
