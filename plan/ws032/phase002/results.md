# ws032-p002 結果: 共通取得機構（2026-09-21）

q315-i02。実装した。

## 1. 追加・変更したファイル

新規（すべて Zlib、zedBSD 規約）:

- `userland/packages/tools/archive.sh`: `verify` / `fetch` / `extract` の 3 sub-command。
  検査の内容は [phase.md](phase.md)。POSIX sh。
- `userland/packages/external.mk`: `ZEDBSD_EXTERNAL_SOURCE` マクロ。宣言から
  download/source の目標と stamp を生成し、既存の集約変数へ登録する。
- `userland/packages/security/openssl/Makefile`、`.../openssh/Makefile`:
  版・URL・サイズ・SHA-256・根・パッチ世代の宣言。**この時点では取得のみ**で、
  パッケージ登録（menuconfig 選択）とビルドは p006/p007 で足す。
- `plan/ws032/tests/run-external-host-test.sh`: host 試験。

変更（ルート `Makefile`、10 行追加・1 行変更）:

- `ZEDBSD_USERLAND_PATCH_TARGETS :=` の初期化を `ZEDBSD_USERLAND_DOWNLOAD_TARGETS` の隣に追加。
- 集約 `patch` 目標を追加（`download` と同じ扱い）。**既存の `userland/base/noct/Makefile`
  が以前から `patch: noct-target-source-verify` を宣言していた**ため、`patch` は
  noct と新パッケージの両方を前提に持つ形で自然に合流した（recipe は両方とも無いので衝突しない）。
- `config.mk` が無くても使える goal の一覧に `patch` と 4 つのパッケージ goal を追加。
  これは既存の noct/llvm の goal を静的に並べてある扱いに合わせた（当該行は Phase ごとに増える）。
- `make help` に `patch` の 1 行。

## 2. 検証

- `sh plan/ws032/tests/run-external-host-test.sh`: **26 checks, 0 failures**。
  その場で作った敵対的アーカイブで、サイズ不一致・SHA-256 不一致・根の宣言違い・
  アーカイブ不在・根が 2 つ・絶対パス member・`..` member・根の外を指す symlink・
  fifo member を**すべて拒否**。根の内側を指す symlink は受理。
  `fetch` は digest 不一致のとき**正規の名前を作らず、残骸も残さない**ことと、
  lock が取られているときに拒否することを確認。`extract` は当たらないパッチで
  **宛先も staging も残さない**こと、再展開が二重パッチにならないこと、相対パスの
  宛先と不在パッチを拒否することを確認。
- 実アーカイブ 2 件: `make openssl-source openssh-source` が取得済みアーカイブの検証 →
  展開まで通り（`build/packages/{openssl,openssh}/src`）、**2 回目は no-op**。
- `make -C userland/packages/security/openssh openssh-source`（standalone）: 成功。
- `make patch`: noct と新 2 件を前提に走り、成功。
- `git diff --check`: PASS。作業ツリーに入るのは Makefile・patches のみで、
  展開したソースは `build/`（gitignore 済み）にだけ置かれる。

## 3. 記録した判断

- **検査を shell script に置いた**。`toolchain/llvm/llvm.mk` は同じ検査を make の
  `define` に直接書いているが、`$(eval)` を通す共通マクロでは `$` の多重退避が読みにくく、
  かつ単体で試験できない。script にしたことで敵対的アーカイブでの試験が build 無しで回る。
  llvm.mk 側は**移行しない**（回帰範囲を分けるため。外部設計 §4.1 のとおり）。
- POSIX sh には local 変数が無い。検証関数が呼び出し元の `archive` を上書きして
  `mv` が自分自身を指す不具合を初回に出したため、関数内の名前を `va_` 接頭辞へ分離した
  （host 試験が検出。修正前 FAIL → 修正後 PASS）。
- 取得はネットワークに触れる唯一の段。`menuconfig` や当該パッケージを選ばないビルドは
  ネットワーク I/O を起こさない（`make download` か当該 goal の明示が要る）。

## 4. 制限

- `fetch` の lock はディレクトリ作成によるもので、異常終了した builder の lock は
  残る（`rmdir` で解除する運用）。既存 `llvm.mk` と同じ性質。
- 署名検証は行わない（ホストに gpg が無い。p001 §2 の制限と同じ）。
- `patch` 目標は noct を巻き込む。これは以前からの noct 側の宣言によるもので、
  本 Phase が作った依存ではない。

## 5. 次

p003（クロスビルド契約）。`build/amd64/sysroot` の生成完了を待って着手する。
