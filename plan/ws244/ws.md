<!-- awesome-plan project=zedbsd record=ws244 -->

# WS244: i915 の Linux 由来の code の書き直し（データだけを引用で残す）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27。p001〜p009 は全部 cleared（最後の p009 は 2026-10-26 23:45 Q1、5330 だけ、SV7 は試さない）。Gen12 の display は 2026-10-15 にユーザーの分割で [WS245](../ws245/ws.md) に移し、そちらも completed）
Primary Milestone: MG006
Related Milestones: MG002
Parent: [Master](../master.md)
Related: [Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」、[WS245](../ws245/ws.md)
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-15）

「code・コメント・構造:については、MITであっても写してはいけません。もしかして写した箇所があるんですか？」
「範囲を全部の WS に広げます。」
「i915はWSを立てて、すぐに対処してください。」
「Gen12（5330）のディスプレイは書き直します。WSを立ててください。Gen9 のディスプレイはガードレールにそって修正してください。」（→ Gen12 は WS245）
「ガードレイルに、ファイル上部には参考にしたことや引用したことを一切書かず、該当箇所にブロック単位で記載することを明記してください。」

## 結果

- i915 の tree（Gen9 の display `display/gen9-*.c`・GT・compiler・render（今は `vk/`）・`intel/` の header）から、Linux・Mesa から写した code・comment・構造と file の先頭の出典の宣言・MIT の表示を外した。register の offset・bit・表は、データとして block ごとの「Value(s) referenced:」で残し、参考にした手順は「Procedure referenced:」で示す（p002〜p008）。
- PRM にある command・opcode の名前は使い、Linux・Mesa にだけある独自の macro 名は zedBSD の名前（`I915_*`）に変えた（2026-10-18 ユーザーの決定 1）。
- `userland/base/licenses/i915-driver/` を外し、license の inventory から i915-intel の項目を外した（p009、決定 2）。log・trace の文字列と comment の Linux の関数名も外した。
- 確かめた事: build は既定と `I915_TESTS=y` の組で warning 0。p003〜p006 の出典の照合（`tests/*-equiv`、Linux v6.8.12・v6.19 の source との値の比べ）。5330 の実機の素の起動の回帰 PASS（p009、2026-10-26。証拠は `plan/ws245/tests/evidence/`）。

## 制限と移管

- SV7（Gen9.5）・7th/8th gen の Gen9 の display の実機は試していない（2026-10-26 ユーザー「SV7は試しません。clearedの条件は5330のみにします。」）。Gen9 の display の書き直しは、comment と表の引用の形だけで、動作の変更は無い。
- plan/ws129/ の license の写しの記録には i915-intel の行が残る（WS129 の記録、p009 で Q1 の判断に回した）。
- 棚卸し [inventory.md](inventory.md) は記録として残す。Phase の dir と WS だけの試験（`tests/*-equiv`・`v619-check`・`inventory.py`）は削除を Q1 に依頼した（2026-10-27）。

## Phase

| ID | 内容 | 状態 |
| --- | --- | --- |
| p001 | 棚卸し（[inventory.md](inventory.md)）と方針・順序・試験の道 | cleared（2026-10-18 Q1） |
| p002 | intel/ の Gen9 の display の header（display-regs-gen9*.h）と bits.h: 先頭の表示を外し block ごとの referenced、`I915_*` に改名、使う 24 file を追従 | cleared（2026-10-18 Q1、merge f6b4952b0） |
| p003 | intel/ の GT の header・表（gt-regs.h・commands.h・gt-power.h・lrc-offsets.h・mocs*・workarounds.h・mcr-ranges・engine-table*・forcewake-ranges*・pci-ids.h）: 同上、使う 70 file を追従 | cleared（2026-10-19 Q1） |
| p004 | GT の core の小さい物の書き直し: power.c（PCODE）・reset・tlb・device-info・ggtt・ppgtt・memory・irq・dma.h ほかの言及 | cleared（2026-10-19 Q1） |
| p005 | GT の submit: context・request・submit・engine の書き直し（2026-10-19: defaults.c・defaults.h・worker.c の言及を p004 から移した） | cleared（2026-10-19 Q1） |
| p006 | GT の電源と WA: gt-power.c・workarounds.c・verify-workarounds.c の書き直し | cleared（2026-10-19 Q1） |
| p007 | compiler・render: genxml*・eu-encoding-*・null-state-gen9 の header（判断 1 に従う）と eu.c・compile.c・*.inc・render の Mesa の言及を Procedure/Values referenced に | cleared（2026-10-19 Q1） |
| p008 | display の残り: internal.h の古い comment、xd-dp.h ほかの言及の掃除 | cleared（2026-10-19 Q1） |
| p009 | license の package（userland/base/licenses/i915-driver/）の整理（判断 2）、規約の全文の見直し、T1 への実機の試験の依頼 | cleared（2026-10-26 23:45、5330 の実機 PASS、SV7 は試さない（ユーザー）） |

## ユーザーの決定（2026-10-18、クリック）

1. Intel の PRM に載る command・opcode の名前は「PRM の名前は使ってよい」（WS039 の ISA の mnemonic と同じ扱い）。Linux・Mesa にだけある独自の macro 名は zedBSD の名前に変える。値は block ごとに引用元を示す。
2. 書き直しの後、`userland/base/licenses/i915-driver/` は「外す」（p009）。
- 決定（2026-10-19 ユーザー、クリック）: p003 の出典の照合の Linux v6.19 の source は「取ってよい」（kernel.org の tarball を署名・SHA で確かめ、担当の worktree の build/ の中に置き読むだけ）。
