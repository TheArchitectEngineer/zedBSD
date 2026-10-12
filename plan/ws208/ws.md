<!-- awesome-plan project=zedbsd record=ws208 -->

# WS208: arm64 の HAL の SMP（Raspberry Pi 4 の 4 core）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-11 Q1 作成。q939 P2 が p001 の設計の第 1 版、判断 D1〜D6 待ち）
Primary Milestone: MG008
Related Milestones: MG006
Parent: [Master](../master.md)
Queue: [P2 の lane](../agents/P2/queue.md)
Resume point: p001 の判断 D1〜D6（[設計](phase001/phase.md) §4）の答えの後、p002（下地）から。
<!-- awesome-plan-current:end -->

## 由来

2026-10-11 ユーザー:「お、arm64のHALはマルチコア非対応なんですか？SMPを実装したらどれくらいの見積もりでしょうか。」→ Q1 の見積もり（約 16〜22 LW）の後、「arm64 の HAL の SMP（WS208）はP2にアサインします。現在の処理の後です。」

## 今（2026-10-11 Q1 の確認）

arm64 の HAL は UP だけ: `src/hal/arm64/locore.S` は core 0 以外を `wfe` で止めたまま、`space.c` は `hal_cpu_count()!=1` で止まる、`task.c` の `running_task` は全体で 1 つ。kernel の MI の部分（`entry.c` の secondary の起動、scheduler、`clock.c` の CPU 間の通知の確かめ）と hal.h の SMP の API は既にあり amd64 で動いている。作業は `src/hal/arm64/` の実装だけ（hal.h は不変、承認不要）。

## 目標

Pi 4 で 4 core を起こし、kernel が 4 CPU で安定して動く。

## 範囲（見積もり、LW）

- core 1〜3 の起動（firmware の spin-table、device tree の cpu-release-addr、CPU ごとの stack、EL2→EL1、MMU）2
- CPU ごとの領域（TPIDR_EL1）、hal_cpu_current、running_task・FPU・debug の状態を CPU ごとに 2
- GIC-400 の CPU ごとの初期化、SGI による hal_cpu_notify・_mask、割り込みの宛先 1.5
- CPU ごとの generic timer 0.5
- page table・MAIR の inner-shareable、TLBI の IS と DSB ISH、ASID、cache・I-cache の同期の broadcast 3〜4
- host・build・全文規約 1、Pi 4 の実機の debug 4〜6、driver（V3D・GENET・xHCI・SDHCI）の競合の直し 2〜4

## 完了の条件（仮、p001 で確定）

Pi 4 の起動の log が `CPUs ready: 4`、desktop・network・SSH が動き、reboot 10 回で起動が安定する（実機）。QEMU raspi4b の -smp 4 での起動（T1）。amd64 の回帰が無い。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| [p001](phase001/phase.md) | 調査と設計 | in-progress（設計 第 1 版、判断待ち） |
| p002 | 下地（percpu・TPIDR_EL1・lock・task の切り替えの手順、まだ 1 CPU） | planning |
| p003 | secondary の起動（spin-table・GIC・timer・TLB・`cpus=N`）、QEMU raspi4b（4 core 固定） | planning |
| p004 | 負荷と driver の競合（QEMU） | planning |
| p005 | Pi 4 の実機（4 core・reboot 10 回） | planning |
| p006 | 規約の全文の見直し | planning |

## 2026-10-11 ユーザー「全部承認です。」（Q1 が示した推しを全て承認）

p001 の推しを全て採用: D1 外の割り込み（SPI）は全部 CPU 0 のまま（分散は Future Work）D2 ASID は使わない（space の切り替えは自分の CPU だけ flush、page table の変更は broadcast、ASID は Future Work）D3 cmdline.txt の `cpus=N`（既定は全部）D4 規約は触る関数だけ全文で D5 WS141 の GPU driver の SMP の競合は P2 が再現と log を Q1 へ、直しは WS141 D6 secondary が来なければ FATAL で止め、画面に cpus=1 で起動できると出す。

## 2026-10-12 ユーザーの方針: SMP は最後

ユーザー「もたつきはSMPを入れたら少し改善するかもしれないとも思いますが、それは最後にして、できるところまで最適化するのがよさそうですね。」→ WS208（arm64 の SMP、agent/p2 の ea8ecb4cf まで、main に未統合）の merge と Pi の実機の確かめ（p005）は、WS141 の描画の最適化（P3）と WS210（P1）の後に回す。単一の CPU のまま最適化の効果を測る。
