# ws032-p001 結果: 設計固め・版と入手元の確定（2026-09-21）

q315-i01。source 変更なし。生成したのは `plan/ws032/**` と `build/` 以下の取得物・toolchain。

## 1. 版と入手元の確定

OpenSSL **3.5.8**（LTS 系の最新）、OpenSSH portable **10.5p1**（最新）、
LLVM **23.1.0**（ツリーが既に pin 済みの版を再利用し、C++ ランタイムと clang を同一版で揃える）。
サイズ・SHA-256・入手元・根ディレクトリ名は [`../provenance.md`](../provenance.md) §1。

取得時に確認した upstream の状況: OpenSSL は 3.0/3.4/3.5/3.6/4.0 系が併存し 4.1.0-alpha1 が
存在する。システムの土台としては LTS 系（3.5）を採り、alpha と非 LTS 系は採らない。

## 2. 同一性の検証

- OpenSSL: upstream 公開の `.sha256` と実測が**一致**。
- OpenSSH: `cdn.openbsd.org` / `ftp.openbsd.org` / `mirror.leaseweb.com` の**3 系統で同一**。
- 両者ともアーカイブ member 検査（単一根、絶対パス無し、`..` 無し、通常ファイル/ディレクトリのみ）で**異常なし**。
- **未実施**: OpenSSH の PGP 署名検証。ホストに gpg が無く、package 導入は許可外。
  `.asc` は `build/distfiles/` に保存済みで、ユーザーが別途検証できる。ミラー一致を
  署名検証の代わりとは扱わない。

## 3. ライセンス監査

`plan/ws032/tests/audit-licenses.sh`（再実行可能）。展開した全ファイルに対する GPL/LGPL
文言の全文検索で **5 件**。内訳と判定は [`../provenance.md`](../provenance.md) §4。

- `config.guess` / `config.sub`（OpenSSH）: GPL-3.0+ **WITH Autoconf 例外**。build 時のみ、
  イメージに入らない。
- camellia perlasm 2 件（OpenSSL）: GPL/LGPL/MPL/**BSD** の多重ライセンス。BSD を選択でき、
  かつ初期構成は `no-asm` で不使用。
- `Text-Template`（OpenSSL 同梱の perl モジュール）: GPL-1+ **または** Artistic。Artistic を
  選択でき、ターゲットに入らない。

**判定**: 製品に入るコードへの GPL 混入はない。停止条件には該当しないものとして続行する。
この判定はユーザーが覆せるよう、報告で明示する。

script は既知 5 件以外が現れたら非 0 で終わる。実行結果: `5 GPL-bearing file(s), all known: yes`。

## 4. ビルド前提の用意

- `make toolchain-cache`: **PASS**。検証済み rev-0 LLVM キャッシュを `build/llvm` へ展開
  （`clang`、`clang++`、`ld.lld`、`llvm-ar` 他を確認）。LLVM をソースからビルドしていない。
- `make sysroots`: 実行し、`build/amd64/sysroot` を生成（結果は p002 冒頭で再確認する）。
- ホストの道具立て: curl 8.14.1、tar 1.35、xz 5.8.1、perl 5.40.1、cmake 3.31.6、ninja 1.12.1、
  python3 3.13.5、GNU make 4.4.1、patch 2.8、nproc 16、空き 595 GiB。**gpg は無い**。

## 5. 外部設計の確定と、計画からの変更

[`../external-design.md`](../external-design.md) を実ツリーと照合し、変更なしで確定した。
p001 の実測で追加した事実は provenance へ入れた。設計本体の変更はない。

## 6. 次

p002（共通取得機構 `userland/packages/external.mk`）。本 Phase で確定した版・SHA-256 を
そのまま宣言値として使う。
