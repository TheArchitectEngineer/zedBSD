<!-- awesome-plan project=zedbsd record=ws290-p004 -->

# ws290-p004 core

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p004（版 4.5、ghci.md cb937e6）。
- 状態: cleared（2026-10-28 P3 q1642-i01。受け入れ: amd64・riscv64 の build の warning 0、p004 の場面 PASS（driver 377）、build に入る file に 1.1 の名前が無い（grep PASS））
- 依存: p003

## 範囲

core: pci-attach（match・Bus Master の順）、identify・caps・CP_HEAP（flat は 16 KiB）・ring・fence・IRQ・poll・watchdog、fault（FAULTED に依らず reset、犯人の記録、U5）・reset・LOST、detach。1.1 の file（firmware・native・state・blit・frame・packets・hcr）と vk/・display・compiler を build から外し、呼ぶ所を消す。capability は p004 で callback を全部実装した物だけを出す（`gpu.h` の「Optional capabilities require their complete callback pair」。resource・blob・map は p005 で足す。R17）

詳細は design の該当の節（§4〜§16）と §14 の p004 の場面。

## 受け入れ

- amd64・riscv64 の build の warning 0、p004 の場面 PASS、build に入る file に 1.1 の名前が無い（grep）
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 22 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

無い（design §19: 仕様の残りは無い。正本は ghci.md d9252e4）。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
- 2026-10-28 P3（q1642-i01、worktree `/home/awe/zedBSD-worktrees/p3-ws290f`、branch agent/p3-ws290f）: 実装。
  - 144e44106: core の書き直し。`ghci.h`（device・ticket・fault の記録・reset の世代の履歴・bus の ops）、`ghci.c`（identify・reset と待ちの上限・SCRATCH・caps の読み（1 KiB の写し）・executor の判定と isolation の log・登録（capability 0、open・close・get_info だけ）・detach）、`cp.c`（CP_HEAP（flat は 16 KiB、他は `ghci_heap_device_bytes` を半分ずつ減らす）・ring 256 KiB・fence page・割り込みは reset の後に bus の ops で取る・WDT_CYCLES・IRQ_STATUS の W1C・Bus Master（firmware が立てていれば触らない）・RUN・最初の fence と割り込み（liveness）、門 `ghci_cp_enter`（OPEN・RETRY・LOST、待つ時は外の lock を全部離す）、置く・doorbell・fence・submit（BIND_SESSION(s)・packet・FENCE・BIND_SESSION(0)、flat は bind 無し、ring より大きい物は packet の境で doorbell して待つ、空きの待ちは RPTR か recovering でだけ抜ける）、ticket（世代の履歴で古い ticket を判定）、handler（FAULT・WATCHDOG を先に記録、FENCE は fence page の後に IRQ_STATUS を読み直し、recovering の間は completed を進めない））、新 `recover.c`（service thread・poll thread・hang の判定・回復の 1〜9 段（session 0 だけ）・嵐（犯人の無い fault 3 回、MMU_OOM 2 回 / 60 s）・確かめの 3 回の失敗・LOST）、`memory.c`（host の連続の run、4 GiB と HOST_LIMIT 未満で local の範囲の外、uncached の view、quarantine）、`pci-attach.c`（class 03h/02h・80h と KC705 10EE:7024・5441:4748 の ID、BAR0 は 4 KiB を map、割り込みと Bus Master は start から）、`check.c` は ring の計画だけ、`ghci-words.h` から 1.1 の宣言を外した。試験: `plan/ws290/tests/ghci-driver-host-test.c`（新）、model に `scratch_stuck`・`halts`、stub に `drv_gpu_report_error`、waitq の stub が待ちの間 spinlock を離す直し、`ghci-host-test.sh` に driver の試験と 1.1 の名前の grep、`config-amd64-ghci.mk`・`config-visionfive2-ghci.mk`（WS290 の build の config）。
  - 06d9a97eb: `platform/amd64/vmunix.mk` の AMD64_GHCI_SOURCES と `platform/riscv64/vmunix.mk` の RISCV64_PCI_DEVICE_SOURCES の GHCI の block だけ（ghci・pci-attach・cp・recover・service・memory・check・caps・cmd の 9 file）。vk/・display・compiler・share・range・jobs・capset・isa は build の外（後の Phase で戻す）。
  - 614d315f5: WS288 の 1.1 の試験 12 file の git rm（Q1 の許可 q1641）と design の参照の直し。3749b89f8: `firmware.c`・`native.c`・`blit.c`・`state.c`・`frame.h` の git rm（Q1 の許可 q1642）。`hcr.h`・`packets.h` は残す isa.c・display*.c・adv7511.c が include するので、その Phase で消す。
  - review: design-reviewer（Opus 5.5）が 144e44106 を読み 13 件（H 2・M 5・L 6）。全部を直すか試験を足した（design §24 の「p004 の code の review」）。直しの commit は e0abaf351（GHCI 有りの amd64・VF2 の vmunix を直しの後に build し直して warning 0）。
  - 確かめた事（直しの後）: `plan/ws290/tests/ghci-host-test.sh` PASS（words 141・ref 56・model 254・driver 377・1.1 の名前の grep（build に入る 9 file と 4 header））。mutation の確かめ（handler が recovering の間も completed を進める形に変えると「fence before fault」が FAIL）。build: GHCI 有りの amd64 vmunix（build/ws290-p004-amd64）・VF2 vmunix（build/ws290-p004-vf2）、GHCI 無しの config/ci/config-amd64.mk・config-visionfive2.mk の vmunix（build/ws290-p004-*-ci）、全部 exit 0・warning 0。
  - driver の場面（design §14 の p004）: 版 0x00010000・0x00010003 を受け 0x00020000・0・MAGIC 違いを拒む、SCRATCH・MEM_READY（遅れ・来ない）・reset（遅れ・終わらない）、WDT_CYCLES と reset の待ち 8500 ms、register の順（割り込みは reset の後、Bus Master は ring と CP_HEAP の後で RUN の前）、最初の fence が来ない → EIO で undo、FAULTED・RUNNING で残った device、kc705・no-su・flat（CP_HEAP 16 KiB、bind 無し）・prot、1000 回の submit で ring が 3 回以上回る、ring より大きい submit、4 KiB の ring で 1000 回、poll、装置の fault → 門 → reset → 古い ticket が EIO → 次が動く、session 0 の watchdog、PROT の session の fault（CP が動いたまま）は HALT してから reset、fence が書かれ IRQ が未処理のまま fault（その ticket は OK）、古い世代の記録を捨てる、hang と fence だけの hang、RPTR が進む長い submit は hang にしない、空きの待ちは時計が進んでも抜けず fault で抜ける、門で device・display の lock を離して待つ、確かめの 3 回の失敗で LOST（門が LOST を返す、open・submit が EIO、core に報告）、reset が終わらず即 LOST、嵐・MMU_OOM 2 回で LOST・60 s を超えて散る fault は回復、前の kernel の WDT_CYCLES での reset の待ちと GCLK_HZ = 0、最後の確かめの失敗（FAULT_INFO が残る → 2 周目、毎周 → LOST）、detach（session が有れば EBUSY、reset → IRQ_ENABLE = 0 → 割り込みの解除 → Bus Master の順、解除の EBUSY で memory を残し再試行で終わる、firmware の Bus Master は残す、回復の途中の detach は回復の後）、全場面で lock の順・待ちの規則・二重の lock の違反 0。
  - 未実施: 実機（p014）。client の session・犯人・再生・recovery ops は p005・p007（design のとおり）。
