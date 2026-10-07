<!-- awesome-plan project=zedbsd record=ws112-p002 -->

# ws112-p002: Debian/Ubuntu package生成を動作試験から分離

Parent: [WS112](../ws.md)
Status: cleared（2026-10-07 Q1 の判定: D-b のとおり host で生成と確認: amd64・arm64 の deb を Debian 13 と Ubuntu 26.04 の rootfs で apt-get install --simulate が exit 0、ELF の machine・manifest・test program 無し。導入・起動はしない）（旧: cleared 候補（2026-10-07 P1: 3 つのうち amd64・arm64 を生成し、形式・依存の解決を確認。Q1 の判定待ち））
Disposition: normal
Primary Milestone: MG007（WSから継承）
Queue / attempts: none / 実装未承認
Goal: Debian 13・Ubuntu 26.04 の両方に入る amd64 と arm64 の deb を、mmdebstrap の Debian 13 の rootfs の中の native の build で生成し、形式・ELF・依存の解決（Debian 13・Ubuntu 26.04 の rootfs で apt の simulate）を確かめる
Prerequisites: ws112-p001 cleared / 確定したinputsとmanifest
Investigation bound: 90分の有限1Phase Queue案。具体的なscope/timebox/commandを選定時に再確認する。

## 実装（2026-10-07、P1、D-a・D-b の後）

| 部分 | file | 中身 |
| --- | --- | --- |
| 生成の道具 | `tools/release/keiland-linux-deb/rootfs.py`・`rootfs.json` | mmdebstrap（unshare、root 不要、`--format=null` で木は mmdebstrap が消す）で対象の rootfs を作り、hook の中で build.py を走らせ、`sync-out` で deb を出す。続いて deb を検べる: field（Package・Architecture）、全 ELF の machine、試験の program が無いこと、対象の各 distribution の新しい rootfs で `apt-get install --simulate`。1 回ごとに新しい directory（`build/keiland-deb/rootfs/TARGET/STAMP/`）、何も消さない。rootfs は `/var/tmp`（`KEILAND_DEB_TMPDIR`）: namespace の root（subuid）は 0700 の home に入れない |
| build | `tools/release/keiland-linux-deb/build.py` | target に `deb13-amd64`・`deb13-arm64`・`rpios13-arm64`（rootfs.json）。architecture・版の suffix（`+deb13`・`+rpios13`）・表示名を target から。`make -j` は CPU の数。時間の上限を emulation 向けに |
| make | `Makefile` | `keiland-deb-amd64`・`keiland-deb-arm64`・`keiland-deb-rpi`（config.mk 不要の goal） |
| 移植 | `keiland-linux.mk`（`$(AR)`）、`wayland/main.c`・`mview/renderer.c`（arm64 の `cntvct_el0`）、`settings/about.c`（`about_trim` を x86 だけに） | aarch64 で build が通る（dee0721a8） |
| 文書 | `tools/release/keiland-linux-deb/README.md` | 3 つの deb の節 |

host の準備: `ubuntu-keyring`（sudo apt-get、Ubuntu の archive の鍵）。mmdebstrap 1.5.7、qemu-aarch64 の binfmt、subuid は既にある。

## 確認（host、2026-10-07）

| target | 結果 |
| --- | --- |
| `deb13-amd64` | `keiland_0~git20261006.865c749b3c66-1+deb13_amd64.deb`、build 87 秒。48 file・ELF 19（全て x86-64）・試験の program 無し。Depends `libc6 (>= 2.38), libvulkan1, mesa-vulkan-drivers, libpam-systemd, kbd`。simulate: Debian 13 で 41 package・Ubuntu 26.04（resolute）で 47 package、どちらも exit 0 |
| `deb13-arm64` | `keiland_0~git20261006.96880ce48867-1+deb13_arm64.deb`、build 695 秒（qemu-user）。48 file・ELF 19（全て AArch64）・試験の program 無し。Depends は amd64 と同じ。simulate: Debian 13 で 41・Ubuntu 26.04 で 47 package、どちらも exit 0 |

成果物（host、git の外）: `build/keiland-deb/rootfs/deb13-amd64/20261006T160300-865c749b3c66/`、`build/keiland-deb/rootfs/deb13-arm64/20261006T174209-96880ce48867/`（`out/` に deb・manifest・buildinfo・sha256、`check.json`、`check-*/simulate.txt`、`build.log`）。

未実施: 実際の導入・起動（D-b で対象外。amd64 は既存の `make keiland-linux-debian`・`-ubuntu2604` の QEMU の smoke を任意で使える）、規約の見直し（p007）。

## Scope / procedure / affected components

tools/release/keiland-linux-debとroot targetの必須経路を整理。native QEMU buildを保ち、fresh guest/runtime工程を任意経路へ分離。共通launcher収録とartifact metadata/verifierの拡張点を整える。

[共通設計](../design.md)の対応節を使用する。自分のPhase以外の受け入れを変更する必要が出たら、依存/foreign Phase/WSを同時に計画修正し、material scopeの同意を確認する。

## Detailed procedure / q585 investigation

[Origin p001](../phase001/phase.md)、[input/source survey](../phase001/survey.md)、[native環境](../native-environments.md)、[形式/CI契約](../package-contract.md)を使用。
既存native QEMU buildをruntimeから分離し、共通stageとsource archiveの外側gzip時刻/filename固定を提供。既存2inputを保持、launcher/test app除外・conffiles・native dpkg-shlibdeps/dpkg-debと独立dpkg query/extractionを照合する。
後続command/証拠: make keiland-linux-debian / make keiland-linux-ubuntu2604、専用outputの各deb+3sidecars、source hash再現、readelf CPU/SONAME/RUNPATH、package展開と共通manifest比較。任意runtimeの入口と過去WS108証拠は保存。
Prerequisitesは上記のcleared Phaseと実出力のまま。p001の残存判断と定義済み契約を照合し、当Phaseのexact Queueにinput/適用boot方法/command/timeboxを保存するまで実装を開始しない。D2は[環境契約](../native-environments.md)の採用方式に従い、実成立は担当Phaseで確認する。

## Clearance criteria / verification

make keiland-linux-debian / make keiland-linux-ubuntu2604で各debと付随記録を生成、独立format/payload/CPU/ELF依存監査が成立。runtime/PNGなしでもpackage検証が成立し、既存test-only payloadを含まない。

形式/metadata/checksum/build確認と実行時の動作確認は別。CI runtimeはユーザー指定で対象外、合格したとは記録しない。
調査上限で未知の依存や未決定事項が残れば、当該attemptをunclearedとし証拠・再開条件を残す。未実行Phaseをclearしない。

## Standards / exceptions / constraints

[Guardrail](../../guardrail.md)、[package方針全文](../../standards/ws112-linux-packages.md)、[C規約全文](../../coding-style.md)、[automation](../../standards/automation.md#ws112-linux-package-coverage-2026-10-02)。
非Cは既存近傍形式と全文/manual review。新Cが必要になれば編集前に全文を読む。移動styleの過去WS例外を流用しない。
専用stage/buildで処理し、HAL/toolchain/共有build/.internal/host /optを変更しない。make check禁止。FreeBSD source install/既存Linux GDM direct entryを維持。
push/remote release/Issues公開は本計画では承認されていない。

## Evidence / findings / resume

Commands/results/commit/environment/artifacts: 未実施（計画のみ）。Skipped: implementation/build/guest/CI/release/導入・runtime。
Resume: prerequisitesの実出力と判断を照合し、当PhaseだけのQueueに実行承認を記録してから開始する。
[q591 scope/verification候補](queue-candidate.md)をmainの依頼で準備。同documentは実行承認ではなく、p001 clearanceとexact Queue保存まで開始しない。

## Event history

2026-10-02 / ws112-package-plan-20261002-ws112-p002-created: userの5OS package計画をこの有限Phaseへ分割、Status planned・Queue none。RPi arm64回答を契約に反映。詳細とscopeはWS/design参照。GitHub body/comment/Project公開は保留、local/outboxに記録。

2026-10-02 / ws112-q585-contract-detail: [p001](../phase001/phase.md)の一次資料/実source調査で当Phaseのprocedure/command/証拠を具体化。上記のnative環境/形式/CI契約へ対応づけ、既存prerequisites・受け入れ・Queue none・Status plannedを保持。再開はp001残件と必要な実出力を照合後の当Phaseだけの承認済Queue。[WS要約](../ws.md#event-history)。GitHub deliveryはmain canonical outboxへ。

2026-10-02 / ws112-q591-p002-candidate: main指示で後続候補のexact scope/必要出力/資源/command/有限verificationを別documentへ具体化。現行Status planned・Queue none、実装未実行を保持。origin p001とWSへ準備eventを保存、mainが後続Queue選択/承認を所有する。

## Event history（2026-10-07 の組み替え）

2026-10-07 / ws112-reset-20261007: 2026-10-06 夜のユーザーのゴールの設定し直し（deb の 3 つだけ、Fedora・Arch・rpm・pacman は取りやめ）と、2026-10-07 のユーザーの回答（D-a「mmdebstrap の rootfs」、D-b「生成＋形式・依存の解決」、Q1 経由のクリック）により、Q1 の指示でこの Phase をamd64・arm64 の共通の deb の生成に定義し直す。QEMU の VM の build と GUI の試験は既存の `make keiland-linux-debian`・`-ubuntu2604` に残し、必須の経路から外す。
