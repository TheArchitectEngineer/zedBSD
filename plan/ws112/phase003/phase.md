<!-- awesome-plan project=zedbsd record=ws112-p003 -->

# ws112-p003: Raspberry Pi OS arm64 deb

Parent: [WS112](../ws.md)
Status: cleared 候補（2026-10-07 P1: RPi OS の rootfs で arm64 の deb を生成し、形式・依存の解決を確認。Q1 の判定待ち）
Disposition: normal
Primary Milestone: MG007（WSから継承）
Queue / attempts: none / 実装未承認
Goal: Raspberry Pi OS（trixie）arm64 の deb を、mmdebstrap の RPi OS の rootfs（Debian trixie ＋ archive.raspberrypi.com）の中の native の build で生成し、形式・ELF・依存の解決を確かめる（runtime・GUI は対象外）
Prerequisites: ws112-p002 cleared / 共通stage・成果物契約（p001のRPi確定入力を使用）
Investigation bound: 90分の有限1Phase Queue案。具体的なscope/timebox/commandを選定時に再確認する。

## Scope / procedure / affected components

対象RPi OS rootfs/native arm64 toolchain/build環境を実装し、make入口とdeb metadata/依存/入力・CPU記録を追加。汎用Debian packageの名称差替えをしない。

[共通設計](../design.md)の対応節を使用する。自分のPhase以外の受け入れを変更する必要が出たら、依存/foreign Phase/WSを同時に計画修正し、material scopeの同意を確認する。

## 実装と確認（2026-10-07、P1、D-a・D-b の後）

道具は p002 と同じ `rootfs.py`（target `rpios13-arm64`、`make keiland-deb-rpi`）。rootfs は Debian trixie ＋ `archive.raspberrypi.com/debian trixie main`、`raspberrypi-archive-keyring` を入れ、build.py はその導入を確かめる（RPi の印）。
鍵: 公開の `raspberrypi.gpg.key`（CF8A1AF502A2AA2D763BAE7E82B129927FA3303E、2012 年、binding の署名が SHA-1）は apt の sqv の policy に拒まれる。そこで、その鍵（fingerprint を rootfs.json で固定）で InRelease を gpgv で検べ、InRelease の SHA256 で `main/binary-arm64/Packages.gz`、その SHA256 で `raspberrypi-archive-keyring_2025.1+rpt1_all.deb` を確かめ、中の keyring（同じ鍵、sqv の受ける binding）を apt に渡す。keyring は namespace が読める `/var/tmp/keiland-deb-keys/` に置く（一度作って使い回す）。

| 確認 | 結果 |
| --- | --- |
| 生成 | `keiland_0~git20261006.e80a76d960d0-1+rpios13_arm64.deb`、build 654 秒（qemu-user） |
| RPi の rootfs | build の rootfs に RPi の package 19 個（`libc6 2.41-12+rpt1+deb13u4`、`libdrm` の `+rpt1` ほか）: RPi OS の compiler・headers・C library で build した |
| 形式 | 48 file・ELF 19（全て AArch64）・試験の program 無し。Depends `libc6 (>= 2.38), libvulkan1, mesa-vulkan-drivers, libpam-systemd, kbd` |
| 依存の解決 | 新しい RPi OS の rootfs で `apt-get install --simulate`: 43 package、exit 0（`mesa-vulkan-drivers 26.2.2-1~bpo13+0~rpt1` は RPi の archive から） |

成果物（host、git の外）: `build/keiland-deb/rootfs/rpios13-arm64/20261006T180138-e80a76d960d0/`。
未実施: 実機・RPi kernel・GPU・GUI（ユーザーの「ビルドが通ればOK」と D-b で対象外）、規約の見直し（p007）。

## Detailed procedure / q585 investigation

[Origin p001](../phase001/phase.md)、[input/source survey](../phase001/survey.md)、[native環境](../native-environments.md)、[形式/CI契約](../package-contract.md)を使用。
公式2026-09-15 RPi Lite Trixie arm64 image/rootfsを候補入力とする。外側既存Debian13 pinned QEMU VM+内側RPi rootfs/native arm64 compilerのQEMU-user方式をmainの委任された通常技術判断で採用。外側の既存Debian boot例外を用い、内RPi kernelはbootしない。境界/command/補助kernel案との差は環境契約へ保存。
後続command/証拠: make keiland-linux-rpi、RPi imageのcompressed/expanded hashと利用可能な署名、RPi marker/packages/repo、compiler自体と全出力ELF AArch64、native dpkg DB/dependency/encoderと独立deb展開を保存。outer kernelとinner rootfsのidentityを区別、elapsedを工程別記録。
Prerequisitesは上記のcleared Phaseと実出力のまま。p001の残存判断と定義済み契約を照合し、当Phaseのexact Queueにinput/適用boot方法/command/timeboxを保存するまで実装を開始しない。D2は後記の採用環境に従い、実成立は担当Phaseで確認する。

## Clearance criteria / verification

make keiland-linux-rpiが実RPi OS/arm64でbuildしたdebを生成し、OS/CPU/format/payload/依存とsourceを独立確認。OS boot困難/依存不足/時間超過なら記録し、偽のRPi buildでclearしない。

RPiについてはuserの追加指示「ビルドが通ればOK」により、build/deb生成で受け入れる。user申告のQEMU GPU制約を理由に、GPU/GUI/表示・実機試験をclear条件から除外。CPU/形式等の確認はpackage整合であり、runtime代替試験を追加しない。

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

## Event history

2026-10-02 / ws112-package-plan-20261002-ws112-p003-created: userの5OS package計画をこの有限Phaseへ分割、Status planned・Queue none。RPi arm64回答を契約に反映。詳細とscopeはWS/design参照。GitHub body/comment/Project公開は保留、local/outboxに記録。

2026-10-02 / ws112-rpi-build-only-20261002: current userのRPi build-only受け入れを反映。影響するp001/p003/p007・WS/design・release方針を更新、Queue/実装許可は追加しない。GitHub event deliveryは保留。

2026-10-02 / ws112-q585-contract-detail: [p001](../phase001/phase.md)の一次資料/実source調査で当Phaseのprocedure/command/証拠を具体化。上記のnative環境/形式/CI契約へ対応づけ、既存prerequisites・受け入れ・Queue none・Status plannedを保持。再開はp001残件と必要な実出力を照合後の当Phaseだけの承認済Queue。[WS要約](../ws.md#event-history)。GitHub deliveryはmain canonical outboxへ。

2026-10-02 / ws112-q585-rpi-environment-selected: [origin p001](../phase001/phase.md)でmainのdelegated判断により外側既存Debian13 QEMU VM＋内側公式RPi rootfs/arm64 native compiler/QEMU-userを採用。環境成立/ABI/source/CPU/形式の実証commandは環境契約、bootは外側既存Debian例外。p002 clearedの共通stage/outputと当Phaseのexact Queueを引き続き必要とし、planned/実行未承認を保持。[WS](../ws.md#event-history)。

## Event history（2026-10-07 の組み替え）

2026-10-07 / ws112-reset-20261007: 2026-10-06 夜のユーザーのゴールの設定し直し（deb の 3 つだけ、Fedora・Arch・rpm・pacman は取りやめ）と、2026-10-07 のユーザーの回答（D-a「mmdebstrap の rootfs」、D-b「生成＋形式・依存の解決」、Q1 経由のクリック）により、Q1 の指示でこの Phase をmmdebstrap の RPi OS の rootfs での生成に定義し直す。p001 の「外側の Debian 13 の VM ＋内側の RPi rootfs」は D-a で置き換え。真の RPi rootfs の native compiler・headers・libc を使う条件は保つ（arm64 は host の qemu-user の binfmt）。
