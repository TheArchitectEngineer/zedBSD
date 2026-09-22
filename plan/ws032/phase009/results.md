# ws032-p009 結果: イメージ統合（2026-09-23、cleared）

q315-i09。4 パッケージが menuconfig に並び、選んだものだけが disk image に載り、
選ばなければ既定構成が変わらないことを確認した。

## 1. menuconfig 登録

| 分類 | 表示名 | 要求 |
| --- | --- | --- |
| Development | `LLVM C++ runtime`（libcxx） | — |
| Languages | `clang compiler` | `devel/libcxx` |
| Security | `OpenSSL cryptography` | — |
| Network | `OpenSSH secure shell` | `security/openssl` |

いずれも既定は **未選択**（`ZEDBSD_USERLAND_PACKAGE` の第 4 引数が `n`）。既定構成の
イメージには外部パッケージが一つも入らない。分類と並び、`*`（全プラットフォーム）で
あることの検査は `menuconfig-target-host-test.py` に入れてある
（[user-requested-fixes.md](../user-requested-fixes.md)「検査」）。

## 2. イメージ投入

各パッケージの Makefile が `--file <イメージ上の位置>=<stage 上の実体>` の形で
投入物を並べる。実行ファイル・共有ライブラリ・設定・ライセンスのほか、clang は
実機で完結してコンパイルできるように、libc のヘッダ・`crt1.o`・
`libclang_rt.builtins.a`・`/usr/lib/libc.so` も置く（sysroot を持たない機械で
ドライバが見に行く場所へ）。

## 3. ライセンス通知

| パッケージ | イメージ上の位置 |
| --- | --- |
| libcxx | `/usr/share/licenses/libcxx/LICENSE.TXT` |
| clang | `/usr/share/licenses/clang/LICENSE.TXT` |
| OpenSSL | `/usr/share/licenses/openssl/LICENSE.txt` |
| OpenSSH | `/usr/share/licenses/openssh/LICENCE` |

取得物のライセンス監査そのものは [provenance.md §4](../provenance.md)（p001）。

## 4. provenance

版・ファイル・サイズ・SHA-256・入手元・同一性の検証・当てたパッチを
[provenance.md](../provenance.md) に記録した。§5 のパッチ表は WS 終了時に埋めた。

## 5. 既定構成への非影響

パッケージ既定が未選択であることに加え、WS 終了時の回帰でカーネル・base の試験
（`sh-target.sh`、`networking-target.sh`）が exit 0 であることを確認している。
