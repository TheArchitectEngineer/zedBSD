<!-- awesome-plan project=zedbsd record=ws129-p004 -->
# ws129-p004: release の image の config と CI の release の job

Status: planned（2026-10-05 Q1: p001 cleared、BUG-134 resolved（AX211 は y）。q714）
Disposition: normal
Parent: [WS129](../ws.md)
Focused goal: fg019（ベータ1）
Queue: q714 / q714-i01（P2）
目安: 3h

## 範囲

1. release の config（`config/release/config-amd64-beta1.mk` など、p001 で決める）: fg019 の成果の program（Settings・audiod・インストーラ・各 app・packages の Emacs/vim/python3 は各 WS の到達しだい）、
   AX211 の driver（BUG-134 が解決すれば y、未解決なら n で既知の問題）、demo の利用者・自動 login を入れない、初回の利用者の作り方（インストーラ／live の利用者）。
2. `.github/workflows/ci.yml` に release の job を足す（tag の push か `workflow_dispatch` でだけ、nightly の job は変えない）。img.gz・zip・SHA-256・license の一覧を載せ、本文は release notes の file。
3. local の確認: release の config で `make` と `plan/tools/boot-test.sh`、YAML の構文の確認、job の shell の部分を local で再現（gzip・sha256sum・file の名前）。GitHub での実行は p008（ユーザーの指示）。

## 受け入れ

上の 3 の結果（PNG をユーザーに見せる）。push はしない。

## 所有 path

`config/release/`（新規）、`.github/workflows/ci.yml` の release の部分（WS112 と同じ file なので main が順を調整する）、`plan/ws129/`。

## 依存

p001、BUG-134（ws004-p051）の結果、fg019 の各 WS の到達（program の一覧は凍結の前に最終化）。

## 未決の判断

p001 の判断（配布物の範囲・prerelease）。

## 部分の実施: CI の release の job の掃除（2026-10-04、P2、ユーザー「WS129 p004はさくっと直してしまいましょう。」、U1〜U15 に依らない範囲）

- `.github/workflows/ci.yml` の nightly の release の job から、どの job も作らない `keiland-linux-*` の artifact の download と、使わない `release-source` の checkout を削った（WS112 の package の job を外した名残）。本文の中身の無い「Keiland Linux」の節も削った。build の job・tag・配布物は変えていない。
- 確かめ: `yaml.safe_load` で読めて、release の job の step は「Download artifacts」「Create GitHub Release」の 2 つ。GitHub での実行は未実施（push はユーザーの指示）。
- 残り（p004 の本体）: release の config・release.yml などは U1〜U15 の後。

## 実施（2026-10-05、q714、P2）

入力: p001 cleared（Q1、2026-10-05）、BUG-134 resolved（AX211 は y）、ユーザーの決定 U1〜U15（release.md §9）。ci.yml は WS112 がベータ4 以降で衝突なし（Q1）。zip は U6/ws088-p002 しだいで条件つき。

### release の config（`config/release/config-amd64-beta1.mk`）

`include config/ci/config-amd64.mk` の上に差分だけ:
- `ZEDBSD_RELEASE_BUILD := y`（uname の release が `VERSION` そのまま、p003）。
- `ZEDBSD_ROOT_LOCKED := y`（U10、root の password を `*` に）。
- `zedinst` を外す（U2）。clang・libcxx（開発の環境、U2）と emacs を足す（CI の config に無いので。下の「発見」）。
- `ZEDBSD_RELEASE_ZIP := n`（U6: ws088-p002 で base が確定したら y。y の時、release の job は zip を作り、draft の base を拒み、失敗なら release を失敗にする）。
- sshd・password の login は CI と同じ（U4）。`ZEDBSD_ROOTFS_DEVELOPMENT := y`・graphical boot は CI のまま。

### Makefile（release の rootfs の option）

- `ZEDBSD_ROOT_LOCKED ?= n`: y なら `$(BUILD)/gen/shadow`（tree の shadow の root の行の hash を `*` に、他の行はそのまま、root の行が無ければ失敗）を `/etc/shadow` に入れる。login は先頭の `*` を lock として扱う（`userland/base/login/verify.c:84`）。
- `make release-info`: `version=`・`name=`・`zip=` を出す（config が無くても動く。release.yml が使う）。

### `.github/workflows/release.yml`（新規、nightly の ci.yml は変えない）

- trigger: `push: tags: zedbsd-*` と `workflow_dispatch`（`tag`、`from_rc`）。`concurrency: release-<tag>`。input は env 経由で shell に渡す。
- `classify`（contents: read）: tag を checkout（無い tag はここで失敗し、tag を作る step は無い）→ `tools/release/release-tag.sh classify`（`zedbsd-<VERSION>-rc<N>` は build、`zedbsd-<VERSION>` は promote、他・VERSION と食い違う tag は失敗）→ `make release-info`。
- `build`（rc）: nightly と同じ準備 → license の門（`license-inventory.py --config`、open があれば止まる。LICENSES.md と image の `/usr/share/licenses/INDEX` を作る）→ release の config で `make`（INDEX を `ZEDBSD_EXTRA_FILES` で入れる）→ `--rootfs` で本文の実在を確かめる → `zedbsd-<VERSION>-amd64.img.gz` →（zip=y の時）`zedbsd-<VERSION>-windows.zip`（`KEI_NIGHTLY_ALLOW_DRAFT=` で draft を拒む）→ 2 GiB 未満の確かめ・`SHA256SUMS`・notes（`docs/release/zedbsd-<VERSION>.md`、U8。無ければ失敗）。
- `publish`（contents: write）: `gh release create <tag> --verify-tag --prerelease --latest=false`、題「Kei/zedBSD 1.0.0 Beta 1 RC N」（U1）、asset は img.gz・zip（ある時）・SHA256SUMS・LICENSES.md。第三者の action を使わず、runner の `gh` だけ（write の権限で動く action の SHA の固定の問題を無くした）。
- `promote`（最終の tag、contents: write）: 作り直さない。`release-tag.sh rc`（`from_rc`、または最終の tag の history にある最大の rc）→ `promotable`（rc の commit が history にあり、差が `docs/release/` だけ）→ rc の asset を `gh release download`・`sha256sum -c` → 最終の Prerelease（題「Kei/zedBSD 1.0.0 Beta 1」、本文は最終の tag の commit の notes）。Latest への昇格はユーザーが手で（U7）。
- 試走は無し（U12）。

### `tools/release/release-tag.sh`（新規）

workflow の tag の検査を local でも同じに流せるように shell の script にした（`classify`・`rc`・`promotable`）。Noct は toolchain の後でないと無いので、toolchain の前の classify の job では使えない。

### 発見（Q1 に報告）

0. **自動 login**: CI の image（したがって release の image）は `/etc/keiland/autologin`（kei）で起動の時に kei で自動の graphical login をする（2026-09-29 のユーザーの決定、master.md）。release.md §4 は「自動 login 入らない（今と同じ）」と書いていたが事実と違う。U3（既知の password を notes に）・U10 との組み合わせをユーザーが確かめる必要がある（release の config で autologin を外すかどうか）。

1. **CI の config から clang・libcxx・emacs・zedinst が消えている**: `config/ci/config-amd64.mk` は 60a1d3f（2026-10-04 10:55）で `ZEDBSD_USER_PROGRAMS` を書き直した時に 4 つが落ちた（52e15c0 には有った）。意図か事故かは記録に無い。release の config では U2 に従い clang・libcxx・emacs を足し、zedinst は外したまま。nightly をどうするかは Q1・ユーザー。
2. **U10（root の lock）で管理の手段が無くなる**: image に `su`・`doas`・`sudo` が無く（`admin` は SCCS）、`kei` は wheel にも入っていない。root を lock すると、利用者が root の権限で何かをする手段が無い（Settings 経由の操作は daemon が行うので影響しない）。決定どおり y にしたが、ユーザーの再確認が要る（y のまま／root の password を残して notes に書く U3 の形／`su` を足す別の Phase）。なお 2026-09-28 にデモの image で同じ件（root を lock し su も無いと実機で管理ができない）にユーザーが「root に password を設定する」と答えている（master.md）。

### 確認（host・local）

- `sh plan/ws129/tests/host-release.sh`: 18 passed（tag の分類・rc の選び方・promote の差の検査を使い捨ての clone の tag で、`release-info`、root の lock、workflow の「sizes・sums・notes」の step を release.yml から取り出して stand-in の asset で実行、notes が無ければ失敗）。
- `release.yml` は `yaml.safe_load` で読める（job は classify・build・publish・promote）。fresh clone（build/ 無し）で `make -s ZEDBSD_CONFIG=config/release/config-amd64-beta1.mk release-info` が動く。
- license の門: `license-inventory.py --config config/release/config-amd64-beta1.mk` → 24 components, 0 open items。
- release の config から clang・libcxx を除いた `plan/ws129/tests/config-amd64-release-noclang.mk`（subagent は target の clang・libcxx を build しないため）で image を build（`BUILD=build/p2-rel`、INDEX を `ZEDBSD_EXTRA_FILES` で）: exit 0、自前の warning 0（残りは openssl・openssh・NoctLang の外部の source）、kernel include check PASS。
  rootfs: `/etc/shadow` の root は `root:*:`、`/etc/os-release` は `ZEDBSD_RELEASE=1.0.0-beta1`（+g なし）・`PRETTY_NAME="Kei/zedBSD 1.0.0 Beta 1"`、`/usr/share/licenses/INDEX` あり、`/bin/emacs` あり、`zedinst` なし。
  `license-inventory.py --rootfs build/p2-rel/rootfs`: 22 components, 0 open。raw の image 2216689664 byte、gzip -6 で 40050779 byte（2 GiB の制限の十分下）。
- この build で p003 の不具合を見つけて直した: amd64 の kernel の `check-kernel-includes.noct` が `$(BUILD)/gen/zedbsd-version.h` を tree の外として拒む → `-DZEDBSD_VERSION` と content-stable の stamp に（cbbe492、main への統合を至急で依頼）。

### 未実施

- GitHub での実行（p008、ユーザーの指示）。clang・libcxx を含む完全な release の image の build（toolchain の package、main か CI）。
- QEMU（T1）: 下の依頼。

### T1 への依頼（QEMU）

image: `plan/ws129/tests/config-amd64-release-noclang.mk`（INDEX は `license-inventory.py --config config/release/config-amd64-beta1.mk --index <BUILD>/INDEX` で作り `ZEDBSD_EXTRA_FILES` で入れる、config の注記の通り）。
PASS: `plan/tools/boot-test.sh` PASS（graphical login か desktop の PNG）、guest で `uname -r` が `1.0.0-beta1`（+g なし）、root の password の login が拒まれ（SSH）、kei の password の login が通る（SSH）、`/usr/share/licenses/INDEX` がある。

## ユーザーの決定（2026-10-05 未明、Q1 が 1 問ずつ聞いた）

- (1) 自動 login: **保つ**（release でも kei が自動で login）。
- (2) root の lock: ユーザー「su, sudoを実装してください。」→ 新しい [WS160](../../ws160/ws.md)（ベータ1）。root は lock のまま、管理は kei（wheel）の sudo。
- (3) nightly の CI の config: **今のまま**（clang・libcxx・emacs・zedinst を外したまま）。
- (4) passwd が無い件: **passwd を実装する**（WS160）。手引き（ws129-p013）は WS160 の後に p005 で直す。
