<!-- awesome-plan project=zedbsd record=ws031 -->

# WS031: i915ネイティブVulkan実行器

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O1, O2
Parent: [Master](https://github.com/awemorris/zedBSD/issues/1)
Queue: WS031 build-complete（p001-p010,p012 cleared、p011 は実機ビッグバンテスト待ち）
Design: plan/ws031/native-vulkan-design.md
<!-- awesome-plan-current:end -->

## 単一目標

WS029で実機動作したi915ネイティブGPU driverの上に、**GPU driver内のnative Vulkan実行器**を実装し、libvulkan（WS030）が既存のdrv_gpu UAPIへ送るVulkanコマンドを、ホスト（QEMU/Venus/Mesa ANV）に依存せず、i915のGENコマンドとして実機で実行・表示する。到達点は実機ネイティブでの`/bin/vkdemo`（textured rotating cuboid）の描画とscanout。ホストLinuxのANVを使うVM経路（WS014）とは別の成果として、新しいWSで持つ。

## 順序・依存

前提はWS029（[#386](https://github.com/awemorris/zedBSD/issues/386)、i915 driver: PCI attach/forcewake/GGTT/PPGTT/GEM/engine RCS0・BCS0/LRC/execlists/request/native stream、実機copy/fill/store/job動作）と、WS030（[#388](https://github.com/awemorris/zedBSD/issues/388)、標準libvulkan.so: Vulkan 1.0 core 137 + WSI）。libvulkanは既に`/dev/gpu0`をdrv_gpu UAPI（`GPU_GET_INFO`/`GPU_GET_CAPSET`/`GPU_BLOB_CREATE`/`GPU_COMMAND_SUBMIT`）で直接叩いており、送信payloadはVenus wire形式（virglrenderer 1.1.0のop番号を再利用したVulkanコマンドの直列化）である。これは**ホストVenusランタイムへの依存ではなく、単なるVulkanコマンドの直列化**として扱う。本WSは、その直列化を**i915 driver内でデコードし、GENへ変換して実行するnative実行器**を新規に持つ。

WS029のdisplay/scanout後続（[ws029-f003](https://github.com/awemorris/zedBSD/issues/386)）とuserland Vulkanのnative経路（[ws029-f004](https://github.com/awemorris/zedBSD/issues/386)）は、本WSが正式に引き取る。WS014/WS030の実装責任はこの計画更新だけで移管しない。

## 範囲・受け入れの具体化

対象機はWS029と同じDell Latitude 5330（Alder Lake-P、Gen12 Xe-LP、`8086:46a8`）。native実行器はi915 driver（in-kernel）に置き、次の要素で構成する。

- Vulkanコマンドdecoderとobject/handle table（instance/device/queue/memory/buffer/image/pipeline/descriptor等をi915資源へ対応付ける）。
- image/sampler/descriptor/binding tableのi915資源モデル（GEM/GGTT/PPGTT、tiling、surface state）。
- **in-kernel SPIR-V→GEN baseline compiler**（1対1・最適化なし。SIMD8、SSA値ごとの素朴なGRF割当。Mesaと公開Intel PRMを参照し、命令encoding表は出典付き`.inc`へ転記、論理は新規実装）。
- Gen12 3Dパイプラインのstate emission（`3DSTATE_*`、URB、binding table、sampler state、render target、depth）。
- command buffer（`vkCmd*`）→GENバッチ変換と、WS029のRCS0 execlists/LRC/request経路での実行。RCSの3D利用を有効化する。
- fence/semaphore/query/timelineの同期を`drv_gpu_complete`へ接続。
- KMS/modeset + scanout（display plane、EDID/mode）。native WSI（`VK_KHR_display`/`swapchain`）を実表示へ接続。WS029のframebuffer保持work（[ws029-f003](https://github.com/awemorris/zedBSD/issues/386)）の延長。

最適化・完全なVulkan適合・全shader機能・複数engine並列は目標にしない。段階は増分A（三角形1枚、texture無し・定数色）→増分B（texture＋depth）→増分C（`vkdemo`の実vertex/fragment shader）。受け入れは実機ネイティブでの各増分の描画とscanout、および独立oracleでの画像照合とする。GuC/HuC不使用、HAL/UAPI不変を維持する。

## 外部設計と委譲構造

本WSは**外部設計（WS単体で自足）**として、全体アーキテクチャ・モジュール分解・モジュール間インタフェース契約の大枠・ファイル境界を [external-design.md](external-design.md) に規定する。実装は**Phase = 1 モジュール**で下位モデルへ委譲する。各 Phase の詳細設計（確定インタフェース、内部関数ガイド、触れる/触れないファイル、依存する前段 doc、受け入れ）は、その Phase の計画時に `plan/ws031/phaseNNN/phase.md` へ書く（テンプレは external-design.md §10）。前段 Phase doc が確定インタフェースの正本で、後段はそれを参照する。設計の背景・段階は [native-vulkan-design.md](native-vulkan-design.md)。

## Phase registry

各 Phase はモジュール単位。実装する公開インタフェースの大枠は external-design.md §4、委譲仕様（触れる/触れないファイル・依存・受け入れ）は §8。

| Combined ID | Phase / Module | Status | 見積 |
| --- | --- | --- | --- |
| ws031-p001 | 設計固め: 外部設計確定・`vk/*.h`枠・ライセンス監査・capset方針（[phase001](phase001/phase.md)） | cleared | 240 分 |
| ws031-p002 | top+cmd: 入口・wire decoder・object/handle table・dispatch（[phase002](phase002/phase.md)） | cleared | 300 分 |
| ws031-p003 | res: memory/buffer/image/sampler/descriptor→i915資源・surface/sampler state（[phase003](phase003/phase.md)） | cleared | 300 分 |
| ws031-p004 | spirv: SPIR-Vパーサ→baseline IR（[phase004](phase004/phase.md)） | cleared | 240 分 |
| ws031-p005 | eu: Gen12 EU命令エンコーダ（出典付き`.inc`＋論理新規）（[phase005](phase005/phase.md)） | cleared | 300 分 |
| ws031-p006 | compile: IR→GEN baseline codegen（素朴レジスタ割当、sampler send）（[phase006](phase006/phase.md)） | cleared | 360 分 |
| ws031-p007 | pipe: Gen12 3Dパイプラインstate emission（`3DSTATE_*`、URB、binding、sampler、RT、depth）（[phase007](phase007/phase.md)） | cleared | 360 分 |
| ws031-p008 | cmdbuf: command buffer→GENバッチ変換・draw・WS029 RCS0投入（[phase008](phase008/phase.md)） | cleared | 300 分 |
| ws031-p009 | sync: fence/semaphore/query/timeline→completion接続（[phase009](phase009/phase.md)） | cleared | 180 分 |
| ws031-p010 | wsi: KMS/modeset・scanout・swapchain present（`VK_KHR_display`/`swapchain`）（[phase010](phase010/phase.md)） | cleared | 300 分 |
| ws031-p011 | 統合: build-passing 達成／実機描画はビッグバンテスト（[phase011](phase011/phase.md)） | big-bang待ち | 360 分 |
| ws031-p012 | レビュー: 静的解析・規約全文確認・回帰・制限整理（[phase012](phase012/phase.md)） | cleared | 180 分 |
| ws031-p013 | Wayland モデルビューア（Venus）: FBX 変換・zwl の seat/pointer/keyboard・mview（[phase013](phase013/phase.md)） | cleared（Venus） | 3 日 |
| ws031-p014 | モデルビューアを i915 で: compiler/executor の拡張（[phase013 §p014](phase013/phase.md)） | planning | 未見積 |

段階的な受け入れの単位は Phase 境界と一致しない。増分A（三角形）は複数モジュールの最小経路を横断する最初の実機到達点で、Phase 計画時に「増分Aで必要な関数」を先行実装対象として明示する。増分B・Cで texture/depth・実shader を足す。

## 適用規約・実行境界

[Guardrail](https://github.com/awemorris/zedBSD/issues/363)とローカル`plan/coding-style.md`の全文を実装前に読む。HAL責務/`hal.h`の変更は別途適用承認が必要。UAPI（`include/drivers/gpu.h`のlayout/ioctl番号）は不変を既定とし、必要になれば事前に提示して承認を得る。aggregate make checkは禁止、対象buildは`make -j16`と意味のある限定確認を用いる。無関係な変更を保護する。

Mesaを参照するファイルは、実装前にライセンス（MIT系）を機械監査し、値・テーブル・命令encodingの転記は出典・SHA・変換規則を付けた`.inc`へ分離する（WS029の`gen-inc.py`と同じ規律）。ロジックはzedBSD規約で新規実装する。Intelの公開PRM（GEN命令encoding/3Dパイプライン）も出典として使う。firmwareは不要（GuC/HuC不使用）だが、必要になれば`userland/firmware/<機種>/`へ置く。

本WSは計画のみ。有限Queue、実行範囲・調査上限は未選択。コード実装/build/実機は未実行。source/docのgit add/commit/pushはユーザーが行う。GitHub公開（Issue/Project同期）は別途の指示による。

## p001 完了: 設計固め・header 契約枠（2026-09-14）

外部設計（[external-design.md](external-design.md)）を正本化し、`src/drivers/gpu/i915/vk/` に `vk-internal.h` と 11 モジュール header（vk/cmd/res/spirv/eu/compile/pipe/cmdbuf/sync/wsi/display）を**契約枠として作成、全て単体コンパイル確認**。p001 決定は [phase001/decisions.md](phase001/decisions.md)（capset は libvulkan 無改造で整合、in-kernel compiler 制約＝FPU 不使用・kern_calloc・SIMD8・spill 最小、tiling は当初 linear、対象 subset）。実行承認は [phase001/approval.json](phase001/approval.json)、Queue は [queue-ws031.md](queue-ws031.md)。

運用調整（設計変更の一言記録）: ライセンス監査は外部設計で p001 実行想定だったが、Mesa 転記が p005/p007/p010 で発生するため、機械監査は各転記 Phase が `.inc` 生成直前に実行する方針へ変更（p001 は方針・参照範囲の確定に留める）。source は未 build 配線（p002 から `vmunix.mk` へ追加）。git add/commit/push はユーザー。

## p002 完了: top+cmd（2026-09-14）

native 実行器の入口・コマンドフレームワーク・drv_gpu 統合を実装・検証。`vk/cmd.c`（object table、LE wire reader/writer、opcode レンジ routing、builtin）、`vk/vk.c`（attach/detach/open/close、capset、command 入口、errno）、モジュール dispatch stub（p003+ で置換）。kernel 統合: `internal.h` に `i915_device.vk`/`i915_session.vk`、`i915.c` で vk attach/detach、`i915_open/close` で vk session、drv_gpu ops に `get_capset` 配線＋`GPU_CAP_CAPSET`、`i915_command`/`i915_command_submit` で native magic 0x31394958 を見て非 native を vk 実行器へ routing（submit は同期完了）。`vmunix.mk` に vk 7 source。検証: vk cmd host fixture（通常＋ASan/UBSan）PASS、i915 kernel build PASS（vmunix check、warning 0、FPU 不使用制約クリア）、WS029 host fixture 全 PASS（fixture に vk stub と capability assert 更新＝WS031 統合の最小変更）。残: libvulkan の完全 open には `GPU_CAP_BLOB|MAPPING` と blob_create/resource_map が必要で、これは res（p003）が memory/blob と共に提供。per-command handler は各モジュール Phase で肉付け。

## 引き継ぎ（2026-09-18）: Gen12 EU スレッド実行ハングの調査を専門家へ

p011 の実機ビッグバンで残った **EU スレッド実行ハング**（PS/compute 共通、Linux i915 では同一 GPU・同一バイトで完走）について、Linux 6.8.12 の通常初期化を parity 経路として完走させた上でも同一署名で再現した（台帳 E-97）。原因特定と問題箇所の修正を新規の専門家に引き継ぐ。**着手に必要な情報はすべて [handover/README.md](handover/README.md) にまとめてある**（問題定義・署名・除外済み事項・残る候補・コード地図・ビルドと QEMU 起動パラメータ・ログの読み方・再現手順・資料索引・規約）。

- 時系列の全記録: [results-ws031.md](results-ws031.md)（E-16〜E-30 が big-bang 期の EU 調査、E-31〜E-97 が parity 移植と EU 試験）
- 前任専門家の指示書と進捗報告: [handover/expert-reports/](handover/expert-reports/)
- 設計メモ・増分結果・生成ツール・Linux 陽性対照 VM 資材: [handover/notes/](handover/notes/), [handover/increment-results/](handover/increment-results/), [handover/tools/](handover/tools/), [handover/linuxvm/](handover/linuxvm/)

修正後は元の担当（Claude）に戻し、parity の残作業（DRM object model 要の部分、runtime suspend/resume、描画）を継続する。git add/commit/push はユーザ。
