<!-- awesome-plan project=zedbsd record=ws037-p001 -->

# ws037-p001: nvrtx の文書（初期化の順・GSP・command の順・世代の差・license）

Status: in-progress（q692-i01 P2 → 2026-10-14 P1 が再開。文書と review の反映済み、判断の項目は下の「締め」で既定を決め、ユーザーの判断が要る 4 点を Q1 に送った。clearance は Q1 の判定）
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
- 判断の項目: 1〜4 は 2026-10-04 ユーザーが決定（1 Turing、2 MIT の定義も取り込まず自分で書く、3 GSP の道だけ、4 570.144）、design §11 に記録。5〜18 は未回答。計画に無い依存: 判断の項目 12（pcat の text の新しい口）、16（executor と compiler の WS）。

## 判断の項目の締め（2026-10-14、P1）

ユーザー（2026-10-14）「事情が変わりました。…RTX対応ができる可能性があります。やりましょう。」で再開。目標はベータ2（10/16 夕）に既定 off で入れること。host は 10.0.30.5（[host.md](../host.md)、iGPU 無し・画面は RTX 2070 だけ・zedBSD が素で動く）。

既定で決めた物（技術の判断、design §11 の案のとおり）:

| # | 決め |
| --- | --- |
| 5 | GSP の firmware は既定 off の package `nvidia-gsp-firmware`（`userland/firmware/nvidia-gsp/`、WS233 の形: license の同意、notice、manifest、WHENCE）。install は `/lib/firmware/nvidia/tu102/` だけ。TU104・TU106 の名前は linux-firmware では link だが、image の builder は file しか置けないので複写せず、driver が TU10x の chip を `tu102` の directory に対応させる |
| 6 | 10.0.30.5 は iGPU が無く VFIO は取れないので**方式 A（素の起動）だけ**。B は使わない。道の細部（kernel の差し替えか USB）は Q1 とユーザー（下の C） |
| 7 | そうする（P8 で RM の display、出力先は GOP の出力先だけ） |
| 9 | 案のとおり（8.3） |
| 10 | 範囲の外（後の Phase） |
| 11 | native の stream は拒む、UAPI を足さない |
| 13 | tag `20260410`（commit `dc85cced…`）。5 つの file・LICENCE.nvidia・WHENCE の大きさと SHA-256 を 2026-10-14 に取り直して監査の表と一致を確かめた |
| 17 | そうする |
| 18 | 方式 A（RTX が primary）なので true から試す（p004） |
| 追加 | ベータ2 の既定: `CONFIG_DRIVER_PCI_NVRTX=n`。有効でも N0・N1（読むだけ）まで。GSP の起動（P0 以降）は boot の parameter `nvrtx.gsp=1` がある時だけ |

後回し（ベータ2 に関係しない）: 8・16（kernel の executor と SPIR-V → SM75 の compiler、p008）。

Q1 に送った（ユーザーの判断が要る、2026-10-14）:

- A（14・15）: WPR2 が残った時は 10.0.30.5 の電源の切断（ユーザーの手）まで RTX を再試行できない。受け入れるか。
- B（12）: text console から GOP の surface を外す口（pcat、範囲の外）。既定は WS037 の範囲を広げて p004 で最小の口を足す。GSP の起動の後は P8 まで画面が消える見込み（ssh は残る）。
- C（6 の細部）: 10.0.30.5 への kernel の入れ方（ssh で差し替え・旧 kernel を残す、か USB）。N0 の証拠は ssh の klog、画面はユーザーの写真。
- D: ベータ2 の既定（上の「追加」）。

Q1 の回答（2026-10-14、ユーザーが 10.0.30.5 に Linux（Debian 13 の予定）を入れる: 「パススルーできるLinuxが必要ですか？もしそうなら、すぐにOSをインストールします。」）:

- C: 主な道は**方式 B（VFIO）**。host は headless（ssh だけ）、guest に QEMU の仮想の VGA を primary として足し、RTX は secondary。console は仮想の画面で QMP の screendump。方式 A（素の起動）は最後の確かめだけ。上の 6 の「方式 A だけ」はこれで置き換える。
- A: VFIO では WPR2 が残ったら host の reboot（ssh で T1・Q1）で戻せる見込み。自動の再試行なし・fault のまま、で可。FLR・bus reset で WPR2 が消えるかを p004 の試験で記録する。
- B: 範囲の拡大を Q1 が許可（`src/drivers/platform/pcat/` の最小の detach/reattach の口、`include/hal/` には触れない）。VFIO では急がず、p004 の後（素の起動の前）。
- D: 可（既定 n、有効でも N0 まで、GSP は `nvrtx.gsp=1` の時だけ）。
- host の準備（IOMMU、vfio-pci の bind、QEMU・OVMF）は Q1。p004 の試験の手順は方式 B で書く。
