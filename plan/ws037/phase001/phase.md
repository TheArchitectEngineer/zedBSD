<!-- awesome-plan project=zedbsd record=ws037-p001 -->

# ws037-p001: nvrtx の文書（初期化の順・GSP・command の順・世代の差・license）

Status: in-progress（q692-i01、P2。commit する文書はそろった。design-reviewer の review の反映が残り）
Disposition: normal
Parent: [WS037](../ws.md)
Queue: q692
実行者: kernel-and-driver-designer（code は書かない）

## 範囲

1. **正本の一覧と license の監査**: nouveau（Linux の tag を固定、`drivers/gpu/drm/nouveau/`、特に GSP の道 `nvkm/subdev/gsp/`）、Mesa の `src/nouveau/`（NVK・NAK、MIT）、NVIDIA の open-gpu-kernel-modules と open-gpu-doc（MIT の側）、GSP の firmware（linux-firmware の `nvidia/*/gsp/`）。file ごとに path・SHA-256・license の表（[i915-license-audit](../../ws029/i915-license-audit.md) の形）。GPL の file は作業の文書（`plan/ws037/temp/`、commit しない）だけに使う。
2. **初期化の順**（作業の文書、temp）: PCI・BAR・GSP の firmware の load と起動・RPC の初期化・MMU・channel・display の順を、i915 と同じ段（N0・N1・P1・P2 …）に分けて。
3. **command の投入の順**（temp）: channel・GPFIFO・push buffer・semaphore の fence、3D・compute の class の起動、display の page flip。
4. **TU106 の具体**（RTX 2070、最初の対象）: PCI の device ID（例 0x1f02・0x1f07・Super の 0x1ec2・0x1ec7）、chip の ID、BAR の構成、GSP の firmware の file（linux-firmware の `nvidia/tu106/gsp/` の版）、VBIOS の FWSEC の扱い、reset の手段（FLR の有無）を正本で確かめる。
5. **世代の差**（commit 可、自分の言葉で）: Turing・Ampere・Ada・Blackwell の class の番号、GSP の firmware の版、NAK の encoder（SM70 系の共通と SM ごとの分岐、Blackwell の差分）を Mesa の source で確かめた表。**最初の対象の世代を提案**（予約の時は RTX 2070）。
6. **我々の interface への対応表**（commit 可）: `drv_gpu_interface`・resident display・buffer object・fence と nvrtx の概念の対応、i915 の何を手本にするか。
7. **段の印の設計**（commit 可）: GOP の framebuffer に段の印を出す口（display の引き継ぎの後の出し先）。
8. **試験の道**: centris の host に挿す NVIDIA の GPU の VFIO の passthrough（i915 の WS029 p006 の手順を手本、host の igpu-mode と同じような切り替え）。
9. **判断の項目**: 最初の対象の世代、BLOB の扱いの細部、試験の機材。

## 受け入れ

- `plan/ws037/temp/`（commit しない）に初期化の順・command の順の作業の文書。
- commit する `plan/ws037/` の文書（例 `nvrtx-design.md`・`nvrtx-license-audit.md`）に、正本と監査の表・世代の差の表・対応表・段の印の設計・試験の道・判断の項目がそろい、GPL の code・comment・定数の名前を含まない（design-reviewer の review）。

## 検証

design-reviewer の review。code・build・QEMU は無い。

## 結果（2026-10-04、q692-i01、P2 generation12、途中）

- 正本（commit しない、`plan/ws037/temp/`）: Linux v6.19 の nouveau、Mesa 25.3.6 の `src/nouveau`、open-gpu-kernel-modules 570.144（sparse）、open-gpu-doc、linux-firmware `d947e4e8`（`nvidia/tu102`・`tu106`）。
- 作業の文書（commit しない）: `temp/gsp-boot.md`（689 行）、`temp/channel-mmu-display.md`（726 行）、`temp/mesa-generations.md`（460 行）。実機の観測は無い。
- commit する文書: [nvrtx-license-audit.md](../nvrtx-license-audit.md)、[nvrtx-design.md](../nvrtx-design.md)（最初の対象 TU106、段 N0〜P8、command の順、段の印、試験の道、世代の差の表、対応表、BLOB、危険、判断の項目 10）。
- 残り: design-reviewer の review（2026-10-04 に起動、結果の反映が未了）。判断の項目 1〜10 は未決。
