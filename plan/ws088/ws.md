<!-- awesome-plan project=zedbsd record=ws088 -->

# WS088: Windows で動く Kei-nightly.zip を CI で配布する

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG001
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p001 は draft の base の zip まで（fork の commit とユーザーの確認待ち）。p003 は準備まで（`make kei-nightly-zip`、draft の base で試験済み）。p002 の upload の後に SHA-256 を差し替えて実 URL で確認。CI の案は `ci-kei-nightly.diff`
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

「~/Kei-nightly.zipを置きました。これのdata/の中にhdd-image.imgを置くと、WindowsでVulkanを使って、Keiを動作させられます。これを、GitHub ActionsのCIで、
ZIPで配布しようと思います。Kei-nightly.zipは、clangのキャッシュと同じReleaseにアップロードして再利用して、CIでhdd-image.imgが入ったKei-nightly.zipを
配布できるようにしましょう。」

完了の条件: main の push で CI が amd64 の image を build し、Release（`rev-0`）の元の zip を SHA-256 で確かめて取得し、`Kei-nightly/data/hdd-image.img` を入れた
`Kei-nightly.zip` を nightly の Release に載せる。その zip を Windows で展開して `boot.bat` で Kei が Venus で動く（ユーザーが確認）。

## 元の zip（~/Kei-nightly.zip、2026-09-29 18:44、104 MiB、133 file）

- `Kei-nightly/`: `boot.bat`・`boot.ps1`（WHPX、virtio-vga-gl + Venus、SDL、usb-multitouch・usb-net・usb-kbd・usb-tablet、ssh の port 転送 2222〜2232、
  `data/hdd-image.img` を NVMe で）、`README.txt`、`data/edk2-x86_64-code.fd`・`data/ovmf-vars.fd`、WINQ-EMU の QEMU（`qemu-system-x86_64.exe`）と DLL（virglrenderer・SDL2・glib ほか）、`share/`。
- 気付いた点: (1) license の file が無い。QEMU は GPLv2、glib 等は LGPL、virglrenderer は MIT、SDL2 は zlib、edk2 は BSD-2-Clause-Patent。binary の配布には
  license の文面と、対応する source（fork の `awemorris/qemu-win32-vulkan`・`awemorris/virglrenderer` の commit）の案内が要る。
  (2) `qemu-built.exe`・`qemu-system-x86_64w.exe`（各 80 MiB）は `boot.ps1` が使わない。(3) README の Linux の例に `C:\Work\2hdd-image.img` と、pflash の行の `\` 抜けがある。

## 設計

- 元の zip は image を含まない `Kei-nightly-base` として `rev-0` の Release に置き、名前に版を付ける（例 `kei-nightly-base-winq-a10-1.zip`）。
  `toolchain/llvm/version.mk` の cache と同じく、名前と SHA-256 を repository の file（例 `tools/release/kei-nightly.mk`）に固定する。
- `make kei-nightly-zip`: 元の zip を取得（`build/releases/` に cache）→ SHA-256 を確かめる → 展開 → `$(BUILD)/hdd-image.img` を `Kei-nightly/data/` に → 
  `$(BUILD)/Kei-nightly.zip`。image は 2 GiB の sparse なので zip の deflate で小さくなる（Release の asset は 1 file 2 GiB まで）。
- CI（`.github/workflows/ci.yml`）: build の job で `make kei-nightly-zip` を足し、artifact と nightly の Release の files に `Kei-nightly.zip` を加える。今の `zedbsd-amd64.img.gz` も残す。
- image は CI の構成（`config/ci/config-amd64.mk`: Venus・i915・graphical boot・logo）。Windows の Venus 1.4 で動くには WS085 の kernel の変更（venus の transport・share）が要る。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [ws088-p001](phase001/phase.md) | 元の zip の整理: license の file と source の案内（`LICENSES/`・README）、使わない exe の削除、README の Linux の例の修正。ユーザーが中身を確認 | uncleared（draft まで。fork の commit とユーザーの確認待ち） | — |
| ws088-p002 | 元の zip を `rev-0` の Release に upload し、名前と SHA-256 を固定する（**外部への公開。ユーザーの承認の後**） | planning | p001 |
| [ws088-p003](phase003/phase.md) | `make kei-nightly-zip`（取得・検証・展開・image の追加・zip）と host での確認（zip の中身、Linux の QEMU で展開した image の起動） | in-progress（準備まで。draft の base で試験 PASS。実 URL は p002 の後） | p002、WS085 の取り込み |
| ws088-p004 | CI への組み込み（build の job と nightly の Release の files）。push はユーザー。案: [ci-kei-nightly.diff](ci-kei-nightly.diff) | planning | p003 |
| ws088-p005 | 確認: CI の nightly の zip を Windows で起動（ユーザー）と Linux の Venus（QEMU）での起動 | planning | p004 |
