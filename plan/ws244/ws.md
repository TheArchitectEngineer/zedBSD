<!-- awesome-plan project=zedbsd record=ws244 -->

# WS244: i915 の Linux 由来の code の書き直し（データだけを引用で残す）

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-15 作成、p001 を P7 が途中まで実施して中断。2026-10-18 ユーザーの指示で P1 が再開、q1250）
Primary Milestone: MG006
Related Milestones: MG002
Parent: [Master](../master.md)
Related: [Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」
Resume point: p001 の棚卸しは途中（plan/ws244/inventory.md、P7 5a352c47c）。**2026-10-15 ユーザーの分割**: Gen12（5330、後に「5330を特別扱いはしません」で display 12・13 の全機種）の display は書き直し → [WS245](../ws245/ws.md) に移した。この WS の残りは (1) **Gen9 の display（display/gen9-*.c）を guardrail に沿って直す**（file の先頭の「code は自前、順序と表は Linux」などの出典の宣言を消し、表・順序の値は block ごとに「Value(s) referenced: …」、参考にした手順は「Procedure referenced: …」を付ける。code が写しなら書き直す）、(2) GT（gt-power.c・workarounds.c・verify-workarounds.c の「Follows Linux …」の関数）、(3) compiler・render（Mesa の手順の言及）、(4) intel/ の header（データは block ごとの引用に、file の先頭の MIT の表示と出典を消す）、(5) display の自前の file の言及の確認、(6) 最後に license の package（userland/base/licenses/i915-driver/）の整理。
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-15）

「code・コメント・構造:については、MITであっても写してはいけません。もしかして写した箇所があるんですか？」
「範囲を全部の WS に広げます。」
「i915はWSを立てて、すぐに対処してください。」
「Gen12（5330）のディスプレイは書き直します。WSを立ててください。Gen9 のディスプレイはガードレールにそって修正してください。」（→ Gen12 は WS245）
「ガードレイルに、ファイル上部には参考にしたことや引用したことを一切書かず、該当箇所にブロック単位で記載することを明記してください。」

## 背景

`src/drivers/gpu/i915/display/` のほぼ全部と `src/drivers/gpu/i915/intel/` の header・表（約 50 file）は、先頭に「Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/…)」と Intel などの MIT の表示を持ち、`userland/base/licenses/i915-driver/LICENSE` も派生の code を含むと書く。新しい規則（code・comment・構造は MIT でも写さない、定数・表はデータとして block ごとに引用元を示して使う）に合わない。

## 目的と完了の条件

- i915 の tree（display・intel・その他の i915 の file）に、Linux・Mesa から写した code・comment・構造が残らない。register の offset・bit・表（DDI の buf trans、PLL の表、forcewake の範囲など）は、データとして block ごとに引用元（Linux の版・file・表の名前）を示して残す。
- 外部の MIT の著作権表示は、データだけになった file から外す。`userland/base/licenses/i915-driver/` は内容に合わせて直すか無くす。
- 動作は変えない: 今動いている display 9・12・13 の全機種（例 5330・SV7・7th/8th gen。2026-10-15 ユーザー「Skylake～Raptor Lake Refreshまですべてサポートします。」）の display が同じに動く（実機の確かめはユーザー、試験の一覧に載せる）。
- 書き直しの手順は p001 で決め、design-reviewer を通す。書き直しは Intel の公開の PRM（Programmer's Reference Manual）と hardware の振る舞いから書き、Linux の code を行ごとに言い換えない。

## Phase（案、p001 で確定）

| ID | 内容 | 状態 |
| --- | --- | --- |
| p001 | 棚卸し（[inventory.md](inventory.md)）と方針・順序・試験の道 | cleared（2026-10-18 Q1） |
| [p002](phase002/phase.md) | intel/ の Gen9 の display の header（display-regs-gen9*.h）と bits.h: 先頭の表示を外し block ごとの referenced、`I915_*` に改名、使う 24 file を追従 | cleared（2026-10-18 Q1、merge f6b4952b0） |
| p003 | intel/ の GT の header・表（gt-regs.h・commands.h・gt-power.h・lrc-offsets.h・mocs*・workarounds.h・mcr-ranges・engine-table*・forcewake-ranges*・pci-ids.h）: 同上、使う 70 file を追従 | cleared（2026-10-19 Q1） |
| p004 | GT の core の小さい物の書き直し: power.c（PCODE）・reset・tlb・device-info・ggtt・ppgtt・memory・irq・dma.h ほかの言及 | cleared（2026-10-19 Q1） |
| [p005](phase005/phase.md) | GT の submit: context・request・submit・engine の書き直し（2026-10-19: defaults.c・defaults.h・worker.c の言及を p004 から移した） | cleared（2026-10-19 Q1） |
| [p006](phase006/phase.md) | GT の電源と WA: gt-power.c・workarounds.c・verify-workarounds.c の書き直し | cleared（2026-10-19 Q1） |
| [p007](phase007/phase.md) | compiler・render: genxml*・eu-encoding-*・null-state-gen9 の header（判断 1 に従う）と eu.c・compile.c・*.inc・render の Mesa の言及を Procedure/Values referenced に | planned（2026-10-19 P1 が規模と再開の手順を phase.md に書いた） |
| p008 | display の残り: internal.h の古い comment、xd-dp.h ほかの言及の掃除 | planned |
| p009 | license の package（userland/base/licenses/i915-driver/）の整理（判断 2）、規約の全文の見直し、T1 への実機の試験の依頼 | planned（p002〜p008 の後） |

## ユーザーの決定（2026-10-18、クリック）

1. Intel の PRM に載る command・opcode の名前は「PRM の名前は使ってよい」（WS039 の ISA の mnemonic と同じ扱い）。Linux・Mesa にだけある独自の macro 名は zedBSD の名前に変える。値は block ごとに引用元を示す。
2. 書き直しの後、`userland/base/licenses/i915-driver/` は「外す」（p009）。
- 決定（2026-10-19 ユーザー、クリック）: p003 の出典の照合の Linux v6.19 の source は「取ってよい」（kernel.org の tarball を署名・SHA で確かめ、担当の worktree の build/ の中に置き読むだけ）。
