# WS032 provenance: 外部ソースの版・入手元・検証・ライセンス

外部から取得する全 tarball の同一性と由来をここに集約する。値はすべて **実際に取得して
実測したもの**であり、推測値は載せない。追加・更新した Phase と日付を必ず書く。

## 1. 取得物

| パッケージ | 版 | ファイル | サイズ (byte) | SHA-256 |
| --- | --- | --- | --- | --- |
| OpenSSL | 3.5.8 | `openssl-3.5.8.tar.gz` | 53213818 | `a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2` |
| OpenSSH portable | 10.5p1 | `openssh-10.5p1.tar.gz` | 2333659 | `d44d28a839ea9daf969cc69150fde59910b2b39361dad81a3bd6cbd19218db11` |
| LLVM（libc++ 系と clang の共通ソース） | 23.1.0 | `llvm-project-23.1.0.src.tar.xz` | 179140728 | `ab1f0e3ec52448c33e8782eaf0422504b87c7b016b22514653ee0d8fcee479ff` |

入手元:

- OpenSSL: `https://github.com/openssl/openssl/releases/download/openssl-3.5.8/openssl-3.5.8.tar.gz`
- OpenSSH: `https://cdn.openbsd.org/pub/OpenBSD/OpenSSH/portable/openssh-10.5p1.tar.gz`
- LLVM: `https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.0/llvm-project-23.1.0.src.tar.xz`
  （`toolchain/llvm/version.mk` で既に pin 済みの値をそのまま再利用する。二重取得しない）

展開後の根ディレクトリ: `openssl-3.5.8` / `openssh-10.5p1` / `llvm-project-23.1.0.src`。

## 2. 版を選んだ理由（p001、2026-09-21）

- **OpenSSL 3.5.8**: 3.5 系は LTS で、取得時点の 3.5 系最新。より新しい 4.0.2 / 4.1.0-alpha1
  も存在するが、システムの土台として長期サポート系を採る。
- **OpenSSH 10.5p1**: 取得時点の portable 最新（upstream tag `V_10_5_P1`）。
- **LLVM 23.1.0**: ツリーが既に pin・検証済みで、ホスト toolchain と同一版。ターゲット用
  C++ ランタイムと clang を同じ版で揃えられるため、別版を持ち込まない。

## 3. 同一性の検証（p001、2026-09-21）

| 対象 | 実施した検証 | 結果 |
| --- | --- | --- |
| OpenSSL | upstream 公開の `openssl-3.5.8.tar.gz.sha256` と実測 SHA-256 の照合 | **一致** |
| OpenSSH | 独立 3 系統（`cdn.openbsd.org`、`ftp.openbsd.org`、`mirror.leaseweb.com`）から取得した内容の SHA-256 照合 | **3 系統一致** |
| LLVM | `toolchain/llvm/version.mk` の既存 pin 値（サイズ・SHA-256）を使用 | 既存機構が検証 |
| 3 件共通 | アーカイブ member 検査（単一根ディレクトリ、絶対パス無し、`..` 無し、通常ファイル/ディレクトリのみ） | **異常なし**（OpenSSL 6065 member、OpenSSH 931 member） |

**未実施（制限）**: OpenSSH の PGP 署名 `openssh-10.5p1.tar.gz.asc` は取得して
`build/distfiles/` に保存したが、**検証していない**。ホストに gpg が無く、package 導入は
許可外のため。ミラー 3 系統一致は署名検証の代わりにはならない。署名検証はユーザーが
別途行える状態を保つ（`.asc` を保存し、公開鍵は `https://cdn.openbsd.org/pub/OpenBSD/OpenSSH/RELEASE_KEY.asc`）。

## 4. ライセンス監査（p001、2026-09-21）

機械監査は展開した全ファイルに対する `GNU General Public License` / `GNU Lesser General
Public` / `SPDX-License-Identifier: (L)GPL` の全文検索。

| パッケージ | 主ライセンス | 監査結果 |
| --- | --- | --- |
| OpenSSL 3.5.8 | Apache-2.0（`LICENSE.txt`） | GPL 文言を含むファイル **3 件**（下記） |
| OpenSSH 10.5p1 | BSD/ISC 系（`LICENCE`。本文に "OpenSSH contains no GPL code"） | GPL 文言を含むファイル **2 件**（下記） |
| LLVM 23.1.0 | Apache-2.0 WITH LLVM-exception | 既存 toolchain と同一物。本WSで新たな監査対象を増やさない |

### 4.1 GPL 文言を含む 5 件の判定

いずれも **製品（rootfs/イメージ）には入らない**か、**非 GPL の選択肢がある**。

| ファイル | 実際の条項 | 判定 |
| --- | --- | --- |
| `openssh-10.5p1/config.guess`、`config.sub` | GPL-3.0-or-later **WITH Autoconf configure-script exception**（「Autoconf 生成 configure を含むプログラムの一部として配布する場合、そのプログラムと同じ配布条件で含めてよい」） | build 時のみ使用。バイナリに入らず、イメージにも載せない。例外条項が本用途を明示的に許可 |
| `openssl-3.5.8/crypto/camellia/asm/cmll-x86.pl`、`cmll-x86_64.pl` | **多重ライセンス**（GPL-2+ / LGPL-2.1+ / MPL-1.1 / **BSD** のいずれかを選択可） | BSD を選択する。加えて初期構成は `no-asm` のため、このファイル自体を使用しない |
| `openssl-3.5.8/external/perl/Text-Template-1.56/LICENSE` | Perl と同条件（GPL-1+ **または** Artistic の二者択一） | ホスト側 build 時の perl モジュール。Artistic を選択でき、ターゲットに入らない |

**結論**: 製品に入るコードに GPL 系ライセンスの混入はない。ビルド時に使う autotools
補助スクリプトと perl モジュールに GPL の選択肢が含まれるが、いずれも例外条項または
非 GPL の選択肢で扱える。この判定は WS032 の停止条件「GPL 系の混入」には該当しないと
扱う。判断を変える場合はユーザー指示による。

再実行用スクリプト: `plan/ws032/tests/audit-licenses.sh`。

## 5. 適用するパッチ

ツリーに入る外部差分は `userland/packages/<category>/<name>/patches/` のみ。
各パッチの目的・必要な理由・上流に出す価値の有無を、当該 Phase の results に書く。

| パッケージ | パッチ | 目的 | 状態 |
| --- | --- | --- | --- |
| （p006 以降で記入） | | | |
