<!-- awesome-plan project=zedbsd record=ws029 -->

# WS029: i915ネイティブGPU実装

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2, O4
Parent: [Master](https://github.com/awemorris/zedBSD/issues/1)
Queue: none
<!-- awesome-plan-current:end -->

## 単一目標

WS014で検証・整理したGPUフレームワークを使い、選定したIntel実機でzedBSDのi915 driverによる描画・表示を成立させる。ホストLinuxのi915を使うだけのQEMU/Venus実行とは別の成果。終了したWSを再利用せず、新しいWSとして持つ。

## 順序・依存

ユーザー指示の順序はGPUフレームワークのみ→QEMU＋Venusでのデバッグ/API改善→i915実装。前段は[WS014](https://github.com/awemorris/zedBSD/issues/15)が所有する。WS014最終contract・規約/検証結果を受け取り、i915で判明した不足も記録して整理する。

## 範囲・受け入れの具体化

struct drv_gpu_interfaceのstatic callback実装とPCI経由のGPU登録を用いる。対象GPUは従来のLatitude 5320を候補とし、PCI ID/世代、firmware、memory/submit/display/resetの要件、ユーザー空間driverとの分担、ライセンス境界を実装前に確定する。Linux i915コードの全面移植を今回決定したとは扱わない。

実機で合意した描画・表示テスト、console fallback、必要な同期/資源回収を確認することを完了方向とする。正確なAPI profileと受け入れは前段成果・実機情報で具体化する。PPCや他GPUを同居させない。

## Phase registry

前段成果と対象実機を確認してから有限Phaseを作成する。今はplanningで実行Phase/Queueなし。コード実装の終盤には適用規約全文と最終ソースの整合確認Phaseを必ず含める。

## 適用規約・実行境界

[Guardrail](https://github.com/awemorris/zedBSD/issues/363)とローカルplan/coding-style.mdの全文を実装前に読む。HAL責務/hal.hの変更は別途適用承認が必要。既存PCI/VFS/VMの責務を確認し、大規模refactor前の配置を仮定しない。aggregate make checkは禁止。必要な対象buildはmake -j16と意味のある限定確認を用いる。無関係な変更を保護する。

ユーザーは計画・GitHub公開を指示した。まだ有限Queue、実行範囲と調査上限は選択していない。コード実装/build/QEMUは未実行。資料のgit add/commitはユーザーが行うためエージェントはadd/commit/pushしない。

## q310開始: GPUレビュー改善p007（2026-09-13）

ユーザーの新Phase作成・実行指示により、[WS014 p007](https://github.com/awemorris/zedBSD/issues/394)全体を単一項目q310-i01として実行する。直接VK_KHR_displayにもGPU copy/blit→共有linear画像→BLOB scanoutを使い、通常vkdemoのCPU readbackを除く。完了通知・command batch/reply mmap・controller排他の短縮・非同期present・同一open並行性・安全なtransport回復・buffer/optimal allocation共有を改善し、実QEMUで描画/寿命と転送数を検証する。

ユーザーはzwlの1パス1surface同期presentとlibwaylandの限定protocolをテストドライバとして承認した。一般Wayland環境、複数window合成・入力・既存Toolkit対応・常駐化は本Phaseへ入れない。reviewの推奨は仕様と照合し、送信受理、Venus decoder応答、VkFence完了、scanoutを区別する。以前のp006でGPU内表示を実証したのはWayland経路であり、直接表示にCPU経路が残った対応不足を訂正する。

p006/q309は実際の受入範囲のcleared/finishedと証拠を保持する。順序はp006 cleared → p007 in-progress → p004 planning/未queue → WS029 native i915。WS030 completed、p001の未決定と既存clearanceは維持。q310は720 active minutes見積/120分レビューの有限項目。HAL追加変更、VFIO/ホスト表示停止、git add/commit/pushは含めず、private image/source転送とGitHub計画同期の既存承認を使用する。開始時点では新実装・試験の成功を主張しない。

## q310最終引継ぎ

q310 finished、[WS014 p007](https://github.com/awemorris/zedBSD/issues/394) cleared。BLOB直接表示・標準OPAQUE memory/fence・同期/batch・topology/placementを受入済み。最終APIは170 commands / drv_gpu_ops v6。active Queueなし、p004とWS029は未queue。optimal共有には隔離したpaired renderer差分を使用。source/docのgit公開はユーザー担当。
