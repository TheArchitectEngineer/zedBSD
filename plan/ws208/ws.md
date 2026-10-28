<!-- awesome-plan project=zedbsd record=ws208 -->

# WS208: arm64 の HAL の SMP（Raspberry Pi 4 の 4 core）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: 2026-10-13 朝 ユーザー「実機動作OKです」（4 core）、4 core の固まりの直し（p007）は 2026-10-13 ユーザー「修正されたことを確認しました」→ main）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG008
Related Milestones: MG006
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

Pi 4 で 4 core を起こし、kernel が 4 CPU で安定して動く。作業は `src/hal/arm64/` の実装だけ（hal.h の SMP の API は既にあり、不変）。完了の条件（仮）: Pi 4 の起動の log が `CPUs ready: 4`、desktop・network・SSH が動き、reboot 10 回で起動が安定する。QEMU raspi4b の -smp 4 の起動。amd64 の回帰が無い。

## ユーザーの決定

- 2026-10-11（由来）「お、arm64のHALはマルチコア非対応なんですか？SMPを実装したらどれくらいの見積もりでしょうか。」→「arm64 の HAL の SMP（WS208）はP2にアサインします。現在の処理の後です。」
- 2026-10-11「全部承認です。」（p001 の推し D1〜D6）: D1 外の割り込み（SPI）は全部 CPU 0 のまま、D2 ASID は使わない、D3 cmdline.txt の `cpus=N`（既定は全部）、D4 規約は触る関数だけ全文で、D5 WS141 の GPU driver の SMP の競合は再現と log を Q1 へ・直しは WS141、D6 secondary が来なければ FATAL で止め、cpus=1 で起動できると画面に出す。
- 2026-10-12「もたつきはSMPを入れたら少し改善するかもしれないとも思いますが、それは最後にして、できるところまで最適化するのがよさそうですね。」
- 2026-10-13「これ以上詰めなくていいです。SMPに進みます。」
- 2026-10-13 朝「実機動作OKです」、2026-10-13「修正されたことを確認しました」（p007 の 4 core の固まりの直し）。

## 結果

- arm64 の HAL に CPU ごとの領域（TPIDR_EL1）・running_task・FPU、spin-table による secondary の起動、GIC-400 の CPU ごとの初期化と SGI、CPU ごとの generic timer、inner-shareable の TLB と cache の broadcast、`cpus=N` を実装し、4 core の固まりの検出（stall watch）と 2 つの直しを入れた（main、b44b2420d 以降）。
- Pi 4 の実機で 4 core の動作（ユーザー「実機動作OKです」）と固まりの直し（「修正されたことを確認しました」）。

## 制限・移管

- reboot 10 回の安定（p005）と QEMU の負荷（T1）は未実施。SPI の分散と ASID は Future Work（D1・D2）。
- p006（規約の全文の見直し）は WS の外の一括の見直しへ（2026-10-08 ユーザー「規約の整形（各 WS の全文規約の Phase）はベータ3」）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws208-p001 | 調査と設計 | in-progress のまま WS を閉じた（D1〜D6 は 2026-10-11 全部承認） |
| ws208-p002 | 下地（percpu・TPIDR_EL1・lock・task の切り替え） | in-progress のまま WS を閉じた（実装は main、実機で 4 core OK） |
| ws208-p003 | secondary の起動（spin-table・GIC・timer・TLB・`cpus=N`）、QEMU raspi4b | in-progress のまま WS を閉じた（実装は main、T1-537 は一部 FAIL の判定が未記録） |
| ws208-p004 | 負荷と driver の競合、main の統合 | in-progress のまま WS を閉じた（main に統合 b44b2420d、QEMU の負荷は未実施） |
| ws208-p005 | Pi 4 の実機（4 core・reboot 10 回） | 未着手のまま WS を閉じた（4 core はユーザーの確認、reboot 10 回は未実施） |
| ws208-p006 | 規約の全文の見直し | 未着手のまま WS を閉じた（WS の外の一括の見直しへ） |
| ws208-p007 | Pi の 4 core の固まり（stall watch・直し） | in-progress のまま WS を閉じた（2026-10-13 ユーザー「修正されたことを確認しました」） |

## 記録の所在

Phase の計画・結果・試験は削除した。git の commit `fb1f6afee` の `plan/ws208/` にある。Queue の履歴は [plan/history](../history/index.md)。
