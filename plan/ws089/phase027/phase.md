<!-- awesome-plan project=zedbsd record=ws089-p027 -->
# ws089-p027: About に版の名前（PRETTY_NAME）を出す

Status: in-progress（2026-10-05 P1 generation17 / q716-i01。実装・build・host の試験まで。QEMU の About の PNG を Q1 経由で T1 に依頼。結果の判定まで cleared にしない）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: q716 / q716-i01
目安: 0.5h

## 範囲

ws129-p003 で `/etc/os-release` が入った（PRETTY_NAME="Kei/zedBSD 1.0.0 Beta 1"、U1）。Settings の About の `se_about_read`（about.c）で PRETTY_NAME を読み、hero の card か Version の行に出す。Kernel の行は uname（`zedBSD 1.0.0-beta1+g<hash>`）のまま。file が無い時は今の表示。

## 受け入れ

build の warning 0、host の試験（os-release の有る・無い）。QEMU の About の PNG は T1 にまとめて依頼。

## 所有 path

`userland/desktop/settings/`（about.c）、`plan/ws089/phase027/`。

## 実装（2026-10-05 P1）

- `userland/desktop/settings/about.c`: `se_about_pretty_name(path, name, size)`（新、`settings.h` に宣言）が os-release(5) の `PRETTY_NAME=` の行を読む（二重引用符の中の backslash の escape、単一引用符、引用符無しは最初の空白まで、`#` の行と `PRETTY_NAMES=` のような長い key は飛ばす、CR を落とす、buffer に合わせて切る、空の名前は無い扱い）。`se_about_read` は `/etc/os-release`、無ければ `/usr/lib/os-release` から `about->system` に入れる。log は `ZSETTINGS ABOUT system=… kernel=… machine=… cores=… host=…`。
- `page-about.c`: Software の card の「Operating system」の行に `about->system`、空なら今の通り「Kei」。Kernel の行は uname のまま。hero の card は変えない。
- Linux の Settings も同じ file を読む（Debian では `Debian GNU/Linux 13 (trixie)` が出る）。

## 確認

| 確認 | 結果 |
| --- | --- |
| `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/q713 build/q713/bin/settings` | 成功、warning 0 |
| Linux の Keiland（`keiland-linux.mk all`） | 成功、warning 0 |
| `sh plan/ws089/tests/host-about.sh`（ASan・UBSan） | 10 checks passed: zedBSD の file（Makefile の行と同じ）、単一引用符、引用符無し、escape、comment と長い key、PRETTY_NAME 無し、空、空の file、file 無し、切り詰め |
| `sh plan/ws089/tests/host-build.sh`（Settings の host の描画の build） | 成功 |
| style-check（about.c・page-about.c・host-about.c） | 指摘 0 |
| QEMU（`plan/ws089/tests/settings-p027.sh BUILD`、Settings の guest） | **未実施**。T1 に依頼（Q1 経由） |

## QEMU の試験（T1 への依頼の内容）

- image: `plan/ws089/tests/build-settings-image.sh BUILD`（main の、ws129-p003 の後の build: image に `/etc/os-release` がある）。起動 `plan/ws089/tests/settings-guest.sh start`。
- 試験: `plan/ws089/tests/settings-p027.sh BUILD [OUTDIR]`（BUILD/bin/settings を写す）。
- 合格: about-system（log の system= が guest の `/etc/os-release` の PRETTY_NAME と同じ）、about-no-file（file を退けると system= が空、表示は Kei）、no-error が ok。`about.png`・`about-none.png` をユーザーに見せる。
