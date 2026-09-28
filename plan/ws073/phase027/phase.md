<!-- awesome-plan project=zedbsd record=ws073p027 -->

# ws073-p027: 新しい worktree の desktop の image の build が host の LLVM を作り直し、共有の build/llvm へ書きうる（BUG-089）

Status: cleared（2026-09-28）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-089](../../bugs/BUG-089.md)（main の依頼、2026-09-28）
ID: 作業中は ws073-p026 と書いたが、main が同じ ID を BUG-090（qsort）に使ったので、main の merge で ws073-p027 に移した（2026-09-28）。

## 目的と受け入れ

(a) 既存の有効な toolchain（build/llvm・build/llvm-source・build/llvm-build の identity が固定の LLVM の版と patch に一致）を、checkout の mtime では
作り直さずに使う（mtime でなく内容の hash・identity で判定）。(b) build/llvm が別の checkout を指す link のとき、toolchain の install を明確な
message で拒む（明示の上書きの変数を除く）。検証: 新しい worktree（build/llvm は scratch の toolchain の複写への link）で desktop の image の
build が toolchain に触れない。main の build は変わらない（main の tree の make -n に LLVM の作り直しが無い、main では build しない）。
toolchain が本当に無く build/llvm が link のとき guard が働く。

## 原因（再現、修正前の tree、`make -n -k`、build/llvm は scratch の複写への link）

新しい worktree で LLVM を作り直す rule は二つ。

1. `toolchain/llvm/llvm.mk` の `$(ZEDBSD_LLVM_SOURCE_STAMP): $(ZEDBSD_LLVM_PATCH) | $(ZEDBSD_LLVM_ARCHIVE_VERIFIED)`。
   sysroot の builtins（`sysroot.mk` の `| $(ZEDBSD_LLVM_SOURCE_STAMP)`）と libcxx・clang の package（`$(ZEDBSD_EXTERNAL_LLVM_VERIFIED)`）が要る。
   worktree に build/llvm-source が無い、または main の tree への link でも patch の mtime（checkout の時刻、13:56）が stamp（9/27 09:07）より新しいので
   rule が走る。order-only の archive の record は `toolchain/llvm/distfiles`（tree の中、worktree には無い）にあるため、まず 180 MB の
   LLVM の archive を curl で取り、展開する（link のときは展開の手前の「unrecognized source tree」で止まる）。
2. `$(ZEDBSD_LLVM_NATIVE_STAMP)`（clang の package が `| $(ZEDBSD_LLVM_NATIVE_STAMP)` で要る host の tblgen）→ `$(ZEDBSD_LLVM_CONFIG_STAMP)` →
   worktree の build/llvm-build で LLVM 全体（clang;lld;lldb）の cmake の configure と 4 つの tblgen の build。configure は
   `-DCMAKE_INSTALL_PREFIX=<worktree>/build/llvm`（= 共有の toolchain への link）を焼き込む。この経路は install しないが、焼き込んだ build tree から
   `install-distribution`（tool の repair の rule、または非 accepted の install の rule）が走れば、共有の build/llvm に書く。

加えて見つけたこと:

- `$(ZEDBSD_LLVM_CONFIG_IDENTITY)` は FORCE の target なので、`make -n` は main の tree でも毎回 cmake の configure と tblgen の build を表示していた
  （実際の build は identity が同じなので何もしない。dry run の偽陽性）。main の tree の `make -n toolchain`（修正前の main の Makefile）で確認。
- `external.mk` の `ZEDBSD_EXTERNAL_LLVM_COPY` は `cp -al build/llvm-source copy.tmp`。build/llvm-source が link だと `cp -a` は link そのものを
  複写し、続く `patch -d copy.tmp` が link の先（共有の検証済みの source）に package の patch を当てる。scratch で確認（`cp -al link copy1` は
  link の hard link になる）。main の build/llvm-source は検証の stamp（9/27 09:08）より新しい file も `.orig`・`.rej` も無い（被害なし）。
- build/llvm-build の scratch の複写の `CMAKE_INSTALL_PREFIX` は main の `/home/awe/zedBSD-rpi4/build/llvm`（687 の `cmake_install.cmake`）。
  共有の build tree から install を走らせると、どの worktree からでも main の toolchain に書く。

## 修正（`toolchain/llvm/llvm.mk`、`userland/packages/external.mk`）

- 受け入れを内容で判定する（make が読む時に `$(shell)`、sysroot の identity と同じ考え）:
  - source: `.zedbsd-source-identity` が版・tag・archive の SHA-256・**patch の SHA-256**・patch level に一致し、stamp がある → stamp の rule は
    前提なし（patch の mtime も archive も見ない）。一致しなければ FORCE で archive から作り直す。
  - build tree の configuration: identity の文字列（host の compiler を含む、従来と同じ）を読む時に比べ、一致なら FORCE を外す（`make -n` の偽陽性も消える）。
  - host の tblgen: configuration が一致、`.zedbsd-native-tools` が identity より古くない、4 つの tool が実行可能 → 前提なし（job 数は stamp の
    名前に入るが生成物を変えないので問わない）。
  - install: 従来どおり `.zedbsd-install-identity`。
- 所有の guard: `ZEDBSD_LLVM_OWNED`（`realpath -m` の結果が tree の root の下か）で build/llvm・build/llvm-source・build/llvm-build のそれぞれを
  判定する。tree の中を指す link（main の `build/llvm -> llvm-zedbsd8`）は所有とみなす。所有でない tree への書き込み（展開、configure、
  build、tblgen、install、tool の repair、install の record、verify の record、`toolchain-cache`）は、make が読む時に拒否の rule になる
  （前提なし、FORCE。archive の取得や展開を先に走らせない）。非 accepted の install は build/llvm と build/llvm-build の両方の所有が要る
  （build tree の install prefix がその tree の build/llvm のため）。message は理由と三つの対処（所有する checkout で build、link を外す、
  `ZEDBSD_LLVM_ALLOW_FOREIGN=yes`）を出す。`ZEDBSD_LLVM_ALLOW_FOREIGN=yes` で従来の動作。
- `external.mk`: package（libcxx・clang の `ZEDBSD_EXTERNAL_LLVM_COPY`）の source の複写を `cp -al '<build/llvm-source>/.' copy.tmp` に
  （link でも中身の hard link の実 directory を作り、patch はその中の file を置き換える）。BUG-089 の追記（BUG-090 の agent）の危険への対処。
  新しい worktree の実 build で `build/packages/libcxx/src`・`build/packages/clang/src` が実 directory、共有の source の全項目が前後で同一。

## 検証（host。QEMU・実機の起動は無し: kernel・userland のコードは不変）

- 回帰の試験 [tests/bug089.sh](../tests/bug089.sh)（scratch の tree に toolchain/llvm だけを複写し、tree の外に accepted・stale・missing の代用の
  toolchain を作る。LLVM の取得・展開・build は無い）: 修正後 9/9 PASS。修正前の llvm.mk（`git show HEAD:toolchain/llvm/llvm.mk`、
  `ZEDBSD_LLVM_FETCH=false` で download を止めて）では 8/9 FAIL（archive の取得・`tar -xJf` に進む、拒否の message が無い）。
- 新しい worktree の desktop の image（この worktree、build/llvm・llvm-source・llvm-build は `/home/awe/scratch-bug089/` の複写への link。
  llvm と llvm-build は実の複写で install prefix を scratch に書き換え、llvm-source は main の hard link の複写）:
  - 修正前の `make -n -k`（desktop の config）: LLVM の archive の curl、展開、build/llvm-build の cmake の configure と tblgen の build。
  - 修正後の `make -n -k`: LLVM の rule は無し（sysroot と package の複写だけ）。
  - `make -j64 toolchain`（14:09:58、rc 0）と `plan/ws035/tests/build-zdesktop-image.sh build/bug089-img`（14:10〜14:16、rc 0、
    `hdd-image.img` の検査 OK。clang の package は scratch の `llvm-build/bin/lldb-tblgen` を使い、`build/packages/clang/src` は実 directory）。
    この build の後に変えたのは拒否の branch（parse 時の拒否、FORCE、message）だけで、accepted の判定は同じ。最終の llvm.mk での再実行
    （14:54、15:00、rc 0）も LLVM の rule 無し。新しい BUILD（`build/bug089-dry`）の最終の llvm.mk での `make -n -k`（2822 行）にも LLVM の
    取得・展開・configure・install は 0 行。
  - scratch と main の LLVM の三つの tree（llvm-zedbsd8、llvm-source 197164 項目、llvm-build 6783 項目）の path・種類・size・mtime・inode の
    一覧が build の前後で同一（toolchain・source・build tree のどれにも書いていない。hard link の source も in-place の書き込み無し）。
- main の build が変わらないこと（main の tree では build せず読むだけ）:
  - 修正後の llvm.mk を `ZEDBSD_REPO_ROOT=/home/awe/zedBSD-rpi4` で読む probe: source・configuration・tblgen・install が全て accepted、
    三つの tree とも所有（`build/llvm -> llvm-zedbsd8` を含む）。`make -n llvm-toolchain <verified の stamp> <native の stamp>` は
    clang・ld.lld の版の確認だけ、他は up to date。
  - worktree の link を main の三つの tree に向けた `make -n toolchain` と desktop の `make -n disk-image`: LLVM の rule 無し（Noct の smoke と
    版の確認だけ）。比較: 修正前の main の Makefile での main の tree の `make -n toolchain` は cmake の configure と tblgen の build を表示
    （FORCE の偽陽性）。
- guard（`/home/awe/scratch-bug089/guardtree`、tree の複写）: build/llvm が tree の外の存在しない directory への link のとき、`make llvm-toolchain`
  と `make toolchain-cache` は直ちに拒否（distfiles・llvm-source・llvm-build・releases を作らない）。stale（zedbsd7）の外の toolchain、
  stale の外の source、stale の外の build tree も拒否し、中身は不変。`ZEDBSD_LLVM_ALLOW_FOREIGN=yes` の `make -n` は従来の source build の経路。
  外の accepted な source と patch の mtime（2020 年、今）: 受け入れは変わらない。patch の内容を変えると受け入れが外れ、拒否される。
- 未実施: boot test（kernel・userland のコードは不変、image は build の検証として作っただけ）。main の tree での実 build（指示により行わない）。
  build/llvm-source も build/llvm-build も無い worktree での実 build（展開と tblgen の build は worktree の中で行われ、所有の tree なので許される。
  `make -n` で確認のみ）。

## 使い方（agent の worktree）

build/llvm、build/llvm-source、build/llvm-build の三つを main の同名に link すれば、どれも内容で accepted になり、何も作り直さず何も書かない。
build/llvm だけを link すると、worktree は自分の build/llvm-source（LLVM の archive の取得 180 MB と展開）と build/llvm-build（configure と
tblgen の build）を作る。install は起きない（build/llvm が accepted）。main の toolchain が古い・欠けているときは拒否の message が出る。

## 残り

- Noct の host の source（`userland/base/noct/Makefile` の `$(NOCT_HOST_SOURCE_STAMP): $(ZEDBSD_NOCT_PATCHES)`）も同じ mtime の問題を持つ。
  新しい worktree では patch が build/NoctLang の stamp より新しく、`Noct: refusing to replace existing source tree: build/NoctLang` で
  `make toolchain` が止まる（link でも複写でも。拒否なので共有の tree は書かれない）。この検証では worktree の Noct の patch の mtime を main の
  値に戻して進めた。Noct の identity には patch の hash が無いので、同じ直し方には identity の形式の移行が要る。別の bug の候補（ID 未割当）。
- 並列の image の build で `zedbsd-target-toolchain-ready`（sysroot の有無の検査）が sysroot の build と並んで走り、sysroot が無いと
  「Missing target sysroot」で止まる（最初の実行で観測。`make toolchain` の後は起きない）。既存の順序の仕様（`make toolchain` が先）どおり。
