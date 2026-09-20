# ws032-p006 結果（進行中・停止）: OpenSSL（2026-09-21）

q315-i06。OpenSSL 3.5.8 のクロスビルドは **libcrypto の最後の 1 file まで到達**し、
`struct sigaction` の UAPI 問題（[p004 §4](../phase004/results.md)）で停止した。

## 1. できていること

- `Configure zedbsd-x86_64 ... shared threads` が通る。
- 生成された Makefile で `make -j16` が libcrypto の大半を compile する。
  残る失敗は `crypto/ui/ui_openssl.c` の 1 箇所（`sa.sa_handler = recsig`）のみ。
- パッチ機構（取得 → 展開 → パッチ → configure → build）が実際に回った。

## 2. 当てているパッチ（2 件）

| パッチ | 内容 | 理由 |
| --- | --- | --- |
| `0001-add-zedbsd-target.patch` | `Configurations/50-zedbsd.conf`（新規）と `os-dep/zedbsd.h`（新規） | OpenSSL に zedBSD ターゲットが無い。upstream が新しい OS 向けに用意している仕組み（Haiku と同じ形）をそのまま使う |
| `0002-skip-pre-2008-resolver-fallback.patch` | `crypto/bio/bio_addr.c` の `BIO_lookup_ex` で、`if (1) { getaddrinfo 経路 } else { 旧経路 }` の**到達しない方**を compile 対象から外す | 旧経路は `gethostbyname`/`getservbyname`/`h_errno`（POSIX が 2008 年に削除）を使う。zedBSD はこれらを持たず、getaddrinfo は常にあるので、この分岐は実行されない |

ターゲット定義の内容: `shared_target => "gnu-shared"`（`-shared -Wl,-Bsymbolic` と
`-Wl,-soname=` だけ。**version script と `-z defs` を使う `linux-shared` は使えない** —
zedBSD の ld.so は symbol versioning を持たず、`libc.so` は ld.so が渡す
`__rtld_exports` を未解決のまま持つ）、`shared_extension => ".so"`（版番号つきの
soname を作らない）、`-include os-dep/zedbsd.h`（`sys/select.h`・`sys/time.h`・`sys/uio.h`）、
`-DOPENSSL_NO_SECURE_MEMORY`（後述）。

## 3. Configure に渡している選択と理由

`no-asm`（初手は perlasm 経路を通さない）、`no-tests`、`no-docs`、`no-engine`、`no-dso`、
`no-module`、`no-deprecated`（削除済み API の `BIO_gethostbyname` が `gethostbyname` を
呼ぶため）、`shared`、`threads`。

**`-DOPENSSL_NO_SECURE_MEMORY`**: OpenSSL の secure heap は鍵素材を `mlock()` したページに
置く。zedBSD に `mlock` は無いので、この構成では通常の割り当てを使う。
**「鍵素材がロックされたメモリにある」とは主張しない**。

## 4. 未着手

staging（`DESTDIR` install）、`/lib` への配置、パッケージ登録、実機での
`openssl version` / 乱数 / `enc` / 鍵生成 / 自己署名証明書の確認。
`no-asm` を外して perlasm を通すかどうかの判断も未了。

## 5. 完了（2026-09-21）

`struct sigaction` の UAPI 修正（[p004 §4.1](../phase004/results.md)）後、
**OpenSSL 3.5.8 のクロスビルドが通った**。

| 成果物 | 大きさ | SONAME | DT_NEEDED | 契約検査 |
| --- | --- | --- | --- | --- |
| `libcrypto.so` | 5,579,520 | `libcrypto.so` | `libc.so` | PASS |
| `libssl.so` | 1,223,600 | `libssl.so` | `libcrypto.so`, `libc.so` | PASS |
| `openssl` | 1,126,744 | （PIE 実行ファイル） | `libssl.so`, `libcrypto.so`, `libc.so` | PASS |

契約検査は `tools/build/check-dynamic-elf.py` の `shared-library` / `application`
役割で、リポジトリ自身の動的 ELF と同じ規則。

## 6. パッケージとしての配線

`userland/packages/security/openssl/Makefile` に configure → build → stage →
契約検査 → stamp の規則を足し、`ZEDBSD_USERLAND_PACKAGE` で menuconfig の選択対象に
した（既定 `n`、amd64 のみ）。`make openssl` で clean state から一気に通ることを確認。

イメージへの配置（`ZEDBSD_PACKAGE_FILES`）:

| 配置先 | 出所 | 理由 |
| --- | --- | --- |
| `/lib/libcrypto.so`、`/lib/libssl.so` | stage の `usr/lib` | **ld.so が探すのは `/lib` だけ**（`userland/base/rtld/rtld.c`。RPATH/RUNPATH 以外の既定探索は `/lib`） |
| `/usr/bin/openssl` | stage の `usr/bin` | `ZEDBSD_PACKAGE_BINDIR` の既定 |
| `/etc/ssl/openssl.cnf` | stage の `etc/ssl` | `--openssldir` |
| `/usr/share/licenses/openssl/LICENSE.txt` | 展開した source | Apache-2.0 の通知をイメージに載せる |

ヘッダ（`usr/include/openssl/`）と静的ライブラリは stage には入るが、イメージには
載せていない。OpenSSH のクロスビルドは stage を直接参照する。ターゲット上での開発に
必要になった時点で改めて判断する。

## 7. イメージ統合と静的な検証

`ZEDBSD_USER_PROGRAMS` に `openssl` を足した `make disk-image` が **error/warning 0** で通り、
rootfs に `/lib/libcrypto.so`、`/lib/libssl.so`、`/usr/bin/openssl`、`/etc/ssl/openssl.cnf`、
`/usr/share/licenses/openssl/LICENSE.txt` が入った。

そのイメージを QEMU（`-machine pc -m 512 -smp 4`）で起動し、kernel log（debugcon）で
**`boot: starting init /sbin/init` まで到達**することを確認した。OpenSSL を足したことと
sigaction の UAPI 変更による起動の退行は無い。

`plan/ws032/tests/check-unresolved-symbols.py`: `libc.so`・`ld.so`・`libcrypto.so`・
`libssl.so`・`openssl` の動的 symbol の閉包を調べ、**未解決 0**。
リンクは `--allow-shlib-undefined` で通る（`libc.so` が `__rtld_exports` を loader から
受け取るため既存のツリーもそうしている）ので、この検査が無いと「リンクは通るが
ターゲットで起動できない」形の欠落を見逃す。

## 8. 残り

**ターゲット上で実際に `openssl` を実行しての確認は未実施**。zedBSD の console は
VGA text ＋ PS/2 keyboard で、serial（UART）driver が無いため、コマンドを打って出力を
読むには QMP の sendkey/screendump を使う harness か、data partition への書き戻しを
host から読む経路のどちらかが要る。どちらも本 Phase では作っていない。
静的には未解決 symbol 0 まで確認したが、**「実機で動いた」とは言えない**。

`no-asm` を外して perlasm を通すかどうかの判断も未了。

## 9. 実機での確認（2026-09-21）

console のシリアル同時出力（[user-requested-fixes.md](user-requested-fixes.md) §6）を
入れたことで、ターゲット上での実行を自動で確認できるようになった。
`plan/ws032/tests/run-target-console.py` で QEMU を起動し、PS/2 キーボードとして
打鍵してシリアルの出力を読む。

| コマンド | 結果 |
| --- | --- |
| `openssl version` | `OpenSSL 3.5.8 25 Aug 2026 (Library: OpenSSL 3.5.8 25 Aug 2026)` |
| `openssl dgst -sha256 /etc/passwd` | `SHA2-256(/etc/passwd)= 1ada0545875d28a35b49bad0a202d8c36eb73b5ec4abbb35d6018710edc01689` |
| `openssl rand -hex 16`（既定 CPU） | **失敗**: `entropy source strength too weak` |
| `openssl rand -hex 16`（`-cpu max`） | 成功 |
| `openssl genrsa 2048`（`-cpu max`） | 成功（PEM 秘密鍵を出力） |

### 乱数が既定 CPU で失敗する理由（zedBSD 側の制限）

`hal_entropy_fill`（`src/hal/amd64/lib.c`）は **RDRAND だけ**をエントロピー源にし、
CPUID.1:ECX[30] が立っていなければ false を返す。`getentropy(2)` はそれを `ENOSYS`
として返す。QEMU の既定 CPU（`qemu64`）は RDRAND を持たないので、OpenSSL は
エントロピー源が無いと正しく報告する。`-cpu max` では RDRAND があり、乱数も鍵生成も通る。

つまり **OpenSSL 側でも libc 側でもなく、その機械にエントロピー源が無い**という
正しい失敗である。ユーザー判断（2026-09-21）により、QEMU では RDRAND を持つ CPU
モデルを指定することで解決とする。`-cpu host`（KVM が使える host）または `-cpu max`
（KVM 不要）。`plan/ws032/tests/run-target-console.py` は既定で `-cpu max` を使う。

残る制限として、RDRAND を持たない実機（古い x86 など）では zedBSD 全体にエントロピーが
無い。カーネル CSPRNG（割込みタイミング等から種を取り RDRAND 無しでも供給する）を
持つかどうかは WS032 の範囲外の設計判断として記録する。

受け入れとしては、**OpenSSL は実機で動作する**（版表示、SHA-256 計算、
エントロピーのある機械での乱数生成と RSA 鍵生成）。
