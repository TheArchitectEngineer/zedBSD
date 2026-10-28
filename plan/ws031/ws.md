<!-- awesome-plan project=zedbsd record=ws031 -->

# WS031: i915ネイティブVulkan実行器

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 00:09 ユーザー「WS031をcompletedにしてください。」。cleared の候補の p019・p020・p032・p033・p039 は cleared、残りの未完の Phase は取り下げ。WS の受け入れの全部の確認ではなくユーザーの判断による完了）
Completed: 2026-10-27（ユーザーの判断）
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

WS029 の i915 native GPU driver の上に、GPU driver の中の native Vulkan 実行器を作る。libvulkan（WS030）が drv_gpu UAPI へ送る Vulkan の command（Venus の wire 形式の直列化）を i915 の中で decode し、host（QEMU・Venus・Mesa ANV）に依らず GEN の command として Dell Latitude 5330（Alder Lake-P、Gen12 Xe-LP、`8086:46a8`）で実行・表示する。到達点は実機の native の `/bin/vkdemo`（textured rotating cuboid）の描画と scanout。in-kernel の SPIR-V→GEN の baseline compiler、Gen12 の 3D pipeline の state、command buffer の変換、同期、KMS・scanout と native WSI を含む。GuC/HuC は使わず、HAL・UAPI は変えない。

## ユーザーの決定

- 2026-09-23: WS031 は閉じない（ユーザーの判断、原文の記録なし）。p015〜p018 は後回し、着手はユーザーの指示で。
- 2026-09-26（p050）: session の close で残った Vulkan の object を解放、descriptor pool の破棄でその set を解放（ユーザーの指示）。
- 2026-09-27: 「OpenGL 3.2が問題なければ、それ以降のOpenGLはいったん保留して、i915の高度化に進んでください。」（main の中継）→ i915 の高度化は新しい WS [WS075](../ws075/ws.md) に置き、p027・p030・p031・p034・p035・p038・p040・p041・p044・p045 を移した。
- 2026-10-07: 範囲の変更。p033 は image view の format の読替え・swizzle（usage の照合は変えない）、p039 は 16 bit の整数（64 bit は p051、Float64・Float16 は Future Work F-080）。
- 2026-10-13: 「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」（p025 を cleared）
- 2026-10-27 00:09: 「WS031をcompletedにしてください。」

## 結果

- 外部設計（[external-design.md](external-design.md)、背景は [native-vulkan-design.md](native-vulkan-design.md)）と `src/drivers/gpu/i915/vk/` の module の header（p001）、入口・wire decoder・object table（p002）、資源（p003）、SPIR-V の parser（p004）、Gen12 EU の encoder（p005、Mesa の値は出典付きの `.inc`）、baseline の codegen（p006）、3D pipeline の state（p007）、command buffer→GEN の batch と RCS0 への投入（p008）、同期（p009）、KMS・scanout・swapchain の present（p010）、静的解析と規約の見直し（p012）。
- Wayland の model viewer（p013、Venus）と、mview を i915 で（p014、executor の索引描画・mip・push・blend と compiler の制御 flow・discard・行列・UBO）。5330 の passthrough で vkdemo の offscreen の hash、LCD への表示、Wayland、mview の描画が通った（記録は [results-ws031.md](results-ws031.md) の E-ledger、[regression-baseline.md](regression-baseline.md)）。
- p011 の big-bang の間の Gen12 EU の thread の hang は、Linux の初期化の parity の移植で解けた（[results-ws031.md](results-ws031.md) E-16〜E-97、[handover/](handover/README.md)）。
- 後の追加: 異常系の確認（p025）、blend の logic op・dual source（p032）、image view の format・swizzle（p033）、16 bit の整数（p039、設計 p020）、executor の設計（p019）、session の close での object の解放（p050）。
- i915 の再構築の記録: [i915-rebuild-plan.md](i915-rebuild-plan.md)・[i915-rebuild-rules.md](i915-rebuild-rules.md)・s1〜s5 の報告・[i915-rebuild-coverage.md](i915-rebuild-coverage.md)、refactoring の設計（[i915-refactoring-design.md](i915-refactoring-design.md) ほか）、license の監査（[i915-vk-license-audit.md](i915-vk-license-audit.md)、[license-inventory.md](license-inventory.md)、[provenance-ledger.md](provenance-ledger.md)、[parity-notice-map.md](parity-notice-map.md)）。

## 制限・移管

- 正式な Vulkan の適合・全 shader 機能・最適化は目標の外。
- [WS075](../ws075/ws.md) へ移した: present mode（p027）、mip level・layer への描画（p030）、MRT（p031）、sampler（p034）、descriptor 配列（p035）、整数 varying（p038）、local の配列・構造体（p040）、OpSwitch・関数呼出し（p041）、完了待ちの割込み化と非同期の実行器（p044・p045）。
- 取り下げ（2026-10-27 の完了で）: p011 の big-bang の統合（実機の描画は p014 以降で到達）、p015〜p018（分割の元）、p021〜p024・p026・p028・p029・p036・p037・p042・p043・p046〜p048・p051。p049 は 2026-10-07 に取り下げ（対象の試験が tree に無い）。
- Future Work: [F-080](../future-work.md)（shaderFloat64・Float16）。
- 試験は `tests/` に残して `plan/tools/` へ移す（5330・SV7 の実機の試験の土台、`plan/tools/hw5330/README.md` が参照。移す先は Q1 の OK の後に記録する）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws031-p001 | 設計固め: 外部設計・`vk/*.h` の枠・license の監査・capset の方針 | cleared |
| ws031-p002 | top+cmd: 入口・wire decoder・object table・dispatch | cleared |
| ws031-p003 | res: memory・buffer・image・sampler・descriptor→i915 の資源 | cleared |
| ws031-p004 | spirv: SPIR-V の parser→baseline IR | cleared |
| ws031-p005 | eu: Gen12 EU の命令の encoder | cleared |
| ws031-p006 | compile: IR→GEN の baseline codegen | cleared |
| ws031-p007 | pipe: Gen12 の 3D pipeline の state | cleared |
| ws031-p008 | cmdbuf: command buffer→GEN の batch・draw・RCS0 への投入 | cleared |
| ws031-p009 | sync: fence・semaphore・query・timeline | cleared |
| ws031-p010 | wsi: KMS・scanout・swapchain の present | cleared |
| ws031-p011 | 統合: 増分 A→B→C の実機の描画（big-bang） | 取り下げ（2026-10-27 の完了。実機の描画は p014 以降で到達） |
| ws031-p012 | レビュー: 静的解析・規約・回帰・制限 | cleared |
| ws031-p013 | Wayland の model viewer（Venus） | cleared（Venus） |
| ws031-p014 | mview を i915 で、shader を一通り | cleared（A0〜E3・性能の第 2 回） |
| ws031-p015〜p018 | 準正常・異常系／executor／compiler／性能の構造 | 取り下げ（p019〜p048 に分割、2026-09-23） |
| ws031-p019 | 設計: executor の未実装の機能 | cleared（2026-10-27 の完了でユーザーの判断） |
| ws031-p020 | 設計: compiler の 16 bit の整数 | cleared（2026-10-27 の完了でユーザーの判断） |
| ws031-p021 | 設計: 性能の構造 | 取り下げ（2026-10-27） |
| ws031-p022 | 画像・sampler の境界の確認 | 取り下げ（2026-10-27） |
| ws031-p023 | blend・UBO の境界の確認 | 取り下げ（2026-10-27） |
| ws031-p024 | compiler の境界の確認 | 取り下げ（2026-10-27、host の分と T1-190b の vke2 まで実施） |
| ws031-p025 | 異常系（範囲外の index・65536 超・descriptor の上限・終わらない loop） | cleared（2026-10-13 ユーザー「UATの結果待ちのものは…」） |
| ws031-p026 | 小さな欠落（uint8 index ほか） | 取り下げ（2026-10-27、host の分は実装と host 試験まで） |
| ws031-p027 | present mode と vsync | WS075 p008 へ移した |
| ws031-p028 | 入力と mview | 取り下げ（2026-10-27） |
| ws031-p029 | 統合回帰 | 取り下げ（2026-10-27） |
| ws031-p030・p031・p034・p035 | executor: mip・layer・MRT・sampler・descriptor 配列 | WS075 p005・p006 へ移した |
| ws031-p032 | executor: blend の logic op・dual source | cleared（2026-10-07 Q1、host の fixture と build） |
| ws031-p033 | executor: image view の format の読替え・swizzle | cleared（2026-10-07 Q1、host の fixture と build） |
| ws031-p036・p037 | executor: UBO の dataport の読み・tiling | 取り下げ（2026-10-27） |
| ws031-p038・p040・p041 | compiler: 整数 varying・local の配列・OpSwitch・関数呼出し | WS075 p004 へ移した |
| ws031-p039 | compiler: 16 bit の整数 | cleared（2026-10-07 Q1、host の fixture） |
| ws031-p042・p043 | compiler: SWSB と並べ替え・spill の改善 | 取り下げ（2026-10-27） |
| ws031-p044・p045 | 性能: 割込みの完了待ち・非同期の実行器 | WS075 p008 へ移した |
| ws031-p046・p047・p048 | 性能: frame copy の削減・scheduler の wakeup・最終の確認 | 取り下げ（2026-10-27） |
| ws031-p049 | 失敗する GPU の host 試験 3 件 | 取り下げ（2026-10-07、対象の試験が tree に無い） |
| ws031-p050 | session の close で Vulkan の object を解放 | cleared（q467-i01、2026-09-26） |
| ws031-p051 | compiler: 64 bit の整数 | 取り下げ（2026-10-27） |

## 記録の所在

Phase の計画・結果は削除する。git の commit `fb1f6afee` の `plan/ws031/` にある。設計・台帳・handover・Linux の parity の資料は、この directory に残す。Queue の履歴は [plan/history](../history/index.md)。
