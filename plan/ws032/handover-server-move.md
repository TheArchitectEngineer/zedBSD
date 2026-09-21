# WS032 引き継ぎ: サーバ移動時点の状態（2026-09-21）

ユーザーがこのツリーをコミットし、GitHub の差分をマージして push したうえで、
別のマシンへ移る。その時点の状態と、移動先で最初にすることを記録する。

## 1. 検証の状態 — ここが一番大事

**ビルドして実機で確認できているもの**（これらは信用してよい）:

| 対象 | 確認 |
| --- | --- |
| 取得機構・クロスビルド契約（p001–p003） | host 試験 26/0、15/0、ライセンス監査 5件判定済み |
| OpenSSL 3.5.8 | クロスビルド・パッケージ化・イメージ収録・**実機で version / dgst / rand / genrsa** |
| console のシリアル同時出力 | 実機で起動〜login〜シェル出力まで取得 |
| `make toolchain-cache` の修正 | sysroot 再生成で LLVM ソースビルドが走らないことを確認 |
| `syscall-smoke.c` の未初期化修正 | warning 0 でコンパイル |
| `TCP_NODELAY` / `SO_KEEPALIVE` | 実機 `SOCKOPT verdict: PASS (3/3)`、`KEEPALIVE verdict: PASS` |

**まだ一度もビルドしていないもの**（**各ファイルの構文検査しか通していない**）:

- TCP のスライディングウィンドウ化（`src/kern/net/tcp.c`、`include/kern/net/tcp-socket.h`）
- `chroot`（`src/kern/namei.c`、`src/kern/syscall.c`、`include/uapi/syscall.h`、libc）
- `/etc/services` と `getservbyname` 系、`gethostbyname` 系、`h_errno`
- `HOST_NAME_MAX`、`cfmakeraw`
- `libc.so` の unwind table 有効化（`platform/amd64/vmunix.mk`）
- ローダの `LD_LIBRARY_PATH` と `/usr/lib` 探索（`src/rtld/rtld.c`）
- clang driver の linker job（`toolchain/llvm/patches/0001-*.patch`）
- OpenSSL のパッチ 0002 削除後のビルド

構文検査は 14 ファイル全てが warning 0 で通っている。**動作は未確認**。

## 2. 移動先で最初にすること

### 2.1 ツールチェイン（ここに落とし穴がある）

`ZEDBSD_LLVM_PATCH_LEVEL` を `zedbsd3` → **`zedbsd4`** に上げた（clang に linker job を
足したため）。したがって:

- **`make toolchain-cache` は失敗する**。公開済みの `rev-0` 資産は `patch=zedbsd3` の
  identity を持つので、`toolchain-cache: extracted install identity mismatch` で止まる。
  これは意図した動作で、古いキャッシュを黙って使わないための検査である。
- 正しい手順は **`make -j<N> toolchain`**（LLVM をソースからビルド）。このマシンでは
  16 コアで約 1 時間かかった。
- ビルド後に `make llvm-host-archive` で新しいキャッシュ資産を作れる。公開するなら
  新しい release tag を切り、`toolchain/llvm/version.mk` の `ZEDBSD_LLVM_CACHE_TAG` と
  `ZEDBSD_LLVM_CACHE_SHA256` を更新する。**これはユーザーの判断・作業**。

### 2.2 検証の順番

1. `make -j<N> toolchain`（sysroot まで）
2. `make -j<N> world` — カーネルと既存ユーザーランドが warning 0 で通るか。
   TCP 書き換え・chroot・libc 追加が最初に効いてくるのはここ。
3. `sh plan/ws032/tests/run-cross-host-test.sh` — 15 checks。
   **linker job が入った clang では wrapper が冗長になっている**ので、通ったうえで
   wrapper を簡素化する（`userland/packages/tools/gen-cross-toolchain.sh`）。
   素の `clang foo.c -o foo` が wrapper 無しで動くかも確認する。
4. `make ... disk-image` して `plan/ws032/tests/run-target-console.py` で実機確認:
   - `/usr/bin/sockopt`（既存、回帰確認）
   - `/usr/bin/netdb`（新規、`plan/ws032/tests/netdb-target.c`。services / hostent / chroot）
   - `/usr/bin/throughput`（新規、`plan/ws032/tests/tcp-throughput-target.c`）
   - OpenSSL の再確認
   実行ファイルはイメージの中に入れる必要がある。`ZEDBSD_EXTRA_INPUTS` と
   `ZEDBSD_EXTRA_FILES` でパッケージを作らずに 1 回のイメージへ入れられる（Makefile に追加済み）。
5. **TCP の A/B 実測**: `CONFIG_TCP_SEND_QUEUE_MAX=1`（従来の stop-and-wait）と `8`（既定）で
   `throughput` を回して並べる。`ZEDBSD_TEST_CPPFLAGS=-DCONFIG_TCP_SEND_QUEUE_MAX=1` で作れる。
6. OpenSSL をパッチ 0002 無しで再ビルド（`make openssl`）。旧経路が
   `gethostbyname` / `getservbyname` などを使って compile できるようになったことの確認。

### 2.3 そのあと

p005（libunwind → libc++abi → libc++、`/usr/lib` へ）→ p007（OpenSSH）→ p008（clang）。
OpenSSH の前提調査は済んでおり、**塞がっている要素は無い**（pty は `src/kern/tty.c` に
実装済み、必要な libc 関数も揃った）。残る作業項目は `sshd` ユーザーと `/var/empty`、
`/etc/ssh` のホスト鍵、cross configure のキャッシュ項目。受け入れはターゲット内
ループバックでの ssh 接続（鍵生成 → sshd 起動 → 公開鍵認証 → コマンド実行）を想定。

## 3. マージ時に注意するファイル

別エージェントが `~/zedBSD/` で WS031 を進めていたため、共有ファイルの差分を挙げる。

| ファイル | 本ツリーでの変更 |
| --- | --- |
| `platform/amd64/vmunix.mk` | 2箇所だけ。(1) `AMD64_KERNEL_SOURCES` のグラフィックス群へ `src/drivers/platform/pcat/serial-mirror.c` を追加。(2) `DYNAMIC_CFLAGS` の `-fno-asynchronous-unwind-tables -fno-unwind-tables` を `-fasynchronous-unwind-tables` へ置換（C++ 例外のため） |
| `Makefile` | 集約 `patch` 目標、`ZEDBSD_USERLAND_PATCH_TARGETS` の初期化、`config.mk` 無しで使える goal の追加、`/etc/services` の収録、`ZEDBSD_EXTRA_INPUTS`/`ZEDBSD_EXTRA_FILES`、`make help` の 1 行 |
| `toolchain/llvm/version.mk` | `ZEDBSD_LLVM_PATCH_LEVEL` のみ（zedbsd3 → zedbsd4） |
| `toolchain/llvm/llvm.mk` | インストール stamp の受理判定を追加（§2.1） |
| `plan/master.md` | WS032 の登録 2 行 |

`src/drivers/gpu/i915/` と `plan/ws031/` には一切触れていない。

## 4. 生成物について

`build/` は `.gitignore` 済みでコミット対象に入らない。移動先では作り直しになる。
このマシンの `build/llvm` には **zedbsd3 の古いキャッシュ**が残っており、
`build/llvm-build` には **zedbsd4 の途中までのビルド**がある。どちらも持っていく必要はない。
