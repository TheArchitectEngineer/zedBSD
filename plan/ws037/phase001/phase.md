<!-- awesome-plan project=zedbsd record=ws037-p001 -->

# ws037-p001: nvrtx の文書（初期化の順・GSP・command の順・世代の差・license）

Status: in-progress（q692-i01、P2。文書と design-reviewer の review の反映済み。clearance は Q1 の判定と判断の項目の扱いの後）
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
8. **試験の道**: centris の host（ユーザーの予定、危険は design の判断の項目 15）に挿す NVIDIA の GPU の VFIO の passthrough（i915 の WS029 p006 の手順を手本、host の igpu-mode と同じような切り替え）。
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
- design-reviewer の review（2026-10-04、P2 generation13 が起動。Q1 の要約の版: 高 4・中 11・低 12・判断の項目の追加 6、同じ review の詳細の版: 重大 4・中 10・軽 4）を全部反映（generation13）:
  - 重大・高: zedBSD の Vulkan の形（libvulkan は Venus の client、i915 と同じく kernel の executor と SPIR-V の compiler が要る、design 8.0・判断の項目 16・p007 の依存・p008 の範囲）、text console の切り離し（surface を外す新しい口が pcat に要る、計画に無い依存、判断の項目 12、切り離しは booter_load の直前・GOP が BAR1 の中の時だけ）、WPR2 の前の準備の順（nouveau の順を 2 節の 3 に、P0 の段を新設、WPR2 が残った時は 1 回だけ解いて止まる、自動の再試行なし、3.1 の表）、isolate・fault・close・destroy の契約（8.3）、安全の模型（8.2、native の stream は拒む）。
  - 中: 対応表を gpu.c の検査に合わせて作り直し（8.1）、BAR1 の下の段は kernel・窓が尽きたら ENOMEM、RPC の非同期と sequence の照合、段の依存（golden context・scrubber、p006 は p005 に依存）、firmware の package（`userland/firmware/README.md` の規約、tag 20260410 の hash が d947e4e8 と同じことを確かめた）、世代の差の表に GSP の firmware と起動の仕組み（Hopper・Blackwell は FMC 型）、監査の表の抜け 23 行と html の埋め込みの GPL の script、試験の方式 B の穴、DMA の 64 KiB の vector と flush page の 40 bit、reset と display・reboot での停止、P2・P7 の順の細部（GSP の falcon の reset）、host の試験（struct の layout・queue・WPR の計算）、段の番号を ws.md に合わせた。
  - 軽: vGPU は ENODEV、boot の parameter は token の API、usermode の class 0xC461、BAR2 の番号は固定でない、acr/bl.bin の記述、host.md の SUPER の ID、改名は p003。
- 判断の項目 1〜18 は未決（Q1 がユーザーに確かめる）。計画に無い依存: 判断の項目 12（pcat の text の新しい口）、16（executor と compiler の WS）。
