# ws032-p002: 共通取得機構

## 目的

外部リリース tarball を「宣言した同一性を満たしたときだけ」取得・展開・パッチする共通
機構を作り、ツリーの既存ライフサイクル（`make download` / `make patch`）へ接続する。

## 確定インタフェース

パッケージは `userland/packages/external.mk` を include し、次を宣言して
`$(eval $(call ZEDBSD_EXTERNAL_SOURCE,<name>))` を呼ぶ。

| 変数 | 意味 |
| --- | --- |
| `ZEDBSD_EXT_<name>_VERSION` | 版（stamp 名に入る） |
| `ZEDBSD_EXT_<name>_ROOT` | 展開後の単一根ディレクトリ名 |
| `ZEDBSD_EXT_<name>_ARCHIVE` | アーカイブのファイル名 |
| `ZEDBSD_EXT_<name>_URL` | 取得元 |
| `ZEDBSD_EXT_<name>_SIZE` | byte 数 |
| `ZEDBSD_EXT_<name>_SHA256` | SHA-256 |
| `ZEDBSD_EXT_<name>_PATCH_LEVEL` | パッチ世代タグ（stamp 名に入る） |
| `ZEDBSD_EXT_<name>_PATCHES` | 適用するパッチ（順序つき） |

生成されるもの:

| 名前 | 内容 |
| --- | --- |
| `<name>-download` | 検証済みアーカイブ（`ZEDBSD_USERLAND_DOWNLOAD_TARGETS` へ登録） |
| `<name>-source` | 展開・パッチ済みツリー（`ZEDBSD_USERLAND_PATCH_TARGETS` へ登録） |
| `ZEDBSD_EXT_<name>_SRCDIR` | `build/packages/<name>/src` |
| `ZEDBSD_EXT_<name>_BUILDDIR` | `build/packages/<name>/build` |
| `ZEDBSD_EXT_<name>_STAGEDIR` | `build/packages/<name>/stage` |
| `ZEDBSD_EXT_<name>_SRCSTAMP` | ビルドが依存すべき stamp |

後段 Phase は `ZEDBSD_EXT_<name>_SRCSTAMP` を prerequisite にし、外部ツリーの内部
依存を make の依存グラフへ展開しない。

## 検査の内容（`userland/packages/tools/archive.sh`）

`verify` はサイズ・SHA-256・単一根・絶対パス無し・`..` 無し・member 種別（通常/
ディレクトリ/symlink/hardlink のみ）・link が根の外を指さないことを検査する。
`fetch` は排他 lock の下で一時ファイルへ取得し、検証を通ったものだけを正規の名前へ
`mv` する。`extract` は staging へ展開してパッチを全て当ててから移動するので、
半端に patch の当たったツリーが宛先に現れない。

## 受け入れ

- host 試験（`plan/ws032/tests/run-external-host-test.sh`）が全件 PASS。
- 実アーカイブ 2 件で `make <name>-download` / `<name>-source` が通り、再実行が no-op。
- 標準の `make download` / `make patch` が新しいパッケージを拾う。
- 既存のパッケージ（noct、firmware）の挙動を壊さない。
