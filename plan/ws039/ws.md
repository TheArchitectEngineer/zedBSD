<!-- awesome-plan project=zedbsd record=ws039 -->

# WS039: AMD RDNA GPU対応

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-15 開始・一時停止の後、同日夕にユーザーの指示で再開。p001 は P3 が q1202、p007（P5）は停止のまま）
Primary Milestone: MG006
Related Milestones: なし
Objectives: O2
Parent: [Master](../master.md)
Queue: q1202（P3、ws039-p001、2026-10-15 夕）
Resume point: P3 が q1202 で p001 を再開。[p001](phase001/phase.md) の「再開の点」（調べの note は worktree /home/awe/zedBSD-worktrees/p3 の plan/ws039/temp/notes/、約 600 MB の source と note は消さない）と [p007](phase007/phase.md) の 7.2（Mesa の tarball は /home/awe/zedBSD-worktrees/p5/build/mesa、消さない）。
Target: 未定（2026-10-15 に開始・一時停止。以前の扱い: **ベータ4 以降**（2026-10-05 user「WS037, WS044,WS048,WS141, WS112, WS118, WS124, WS125, WS126, WS119, WS096, WS097, WS039, WS038, WS144, WS143, WS146,WS147, WS152,  WS119, WS080, は、ベータ4以降としてください。…WS027, WS015, WS047, WS028, WS017,  WS077, はキャンセルします。」））
<!-- awesome-plan-current:end -->

## 単一目標

AMD RDNA世代のGPUのドライバを実装する。

## 担当

2026-10-15 から driver は P3、compiler は P5（Q1 の割当、ユーザー「担当を 2 人（driver と compiler）で並行してよいです。」）。以前の記述（2026-09-23 別契約のエージェント、番号の予約）は 2026-10-15 の開始で置き換えた。

## 前提（参考）

- GPUの共通層は `src/drivers/gpu/`（drv_gpu）、既存のbackendはVenus（WS014）とi915（WS029・WS031）。
- 標準Vulkan libraryはWS030（`userland/desktop/libvulkan/`）。
- 規約とGuardrailは `plan/guardrail.md`、運用は `plan/master.md`「実行体制とQueue運用方針」。
  デバイスドライバには設計Phaseを入れる。HALの変更は差分ごとの事前承認が要る。

## Phase一覧

下の「再開と計画」の表（案）。作った Phase: [p001](phase001/phase.md) 調べと設計（in-progress、一時停止）、[p007](phase007/phase.md) GFX10 の shader の compiler（in-progress、一時停止）。

## 要検討・ブロック（2026-10-05）

ユーザーの指示で要検討の状態にしてブロックする。ユーザーと方針を決めるまで Queue に入れない。（2026-10-15 のユーザーの決定で解除して開始、同日一時停止）


## 再開と計画（2026-10-15 ユーザー、Q1 の案、まだ実装しない）

ユーザー（原文）:「今日この後、AMD RDNAのGPUが届きます。amdrdnaドライバを作成します。すでにWSはあるはずです。実機が届くまで、NVIDIAのときと同じような手順でMesaベースでコードを作成し、動作確認なしで書き進めます。GPUが届いたらテストを開始します。GPUはRX 5600 XTです。この作業はまず計画を立ててください。NVIDIAのときの作業を参考に計画を立てます。まずは計画だけお願いします。」

### 対象
- driver の名前 **amdrdna**（`src/drivers/gpu/amdrdna/`）。最初の対象は **RX 5600 XT = Navi 10（RDNA1、GFX10.1、gfx1010）**。RDNA2・3 は後（chip の表で足せる形にする）。
- 手本の source（Q1 の知識、file ごとに確かめる）: Mesa の RADV（Vulkan）・ACO（shader の compiler）・`src/amd/common`・`src/amd/registers`（MIT）、Linux の amdgpu（`drivers/gpu/drm/amd/` の多くは MIT の X11 系の license、file ごとに確かめる）、AMD の公開の ISA の文書（RDNA1 ISA）。

### NVIDIA（WS037・nvrtx）との違い（設計の要点）
- GSP のような「RM が全部やる」firmware は無い。driver が PSP（security processor）に firmware を渡し、SMU（電源・clock）、GMC（memory の制御・VM の page table）、IH（割り込み）、GFX・compute の ring（CP の PFP・ME・CE・MEC、KIQ）、SDMA（copy）を自分で初期化する。nvrtx より driver の仕事は多いが、仕組みは公開されている。
- 命令の投入は PM4 の packet（CP）と SDMA の packet。shader は GFX10 の ISA（wave32・wave64、SOP・VOP・SMEM・VMEM などの encode）。
- display は DCN 2.0（Navi10）。firmware（GOP）の画面を引き継ぎ、flip・cursor を足す（R8 と同じ順で）。
- Navi10 は VFIO の passthrough で reset の問題が知られている（要確かめ）ので、実機の試験は素の起動（方法 A に当たる）を主にする見込み。

### Phase（案、NVIDIA の順に倣う。見積もりは粗い）
| ID | 内容 | 段の印 | 見込み |
| --- | --- | --- | --- |
| ws039-p001 | 調べと設計: 初期化の順（PCI・MMIO・GOP の画面の保持・PSP・SMU・GMC・IH・CP・SDMA）、firmware の一覧、display（DCN 2.0）、PM4、GFX10 の ISA、license の監査の表、試験の道（素の起動・passthrough の可否） | — | 3〜5 LW |
| ws039-p002 | 骨格と認識: PCI の attach、RDNA1〜3 の全 chip の device id の表（Navi10・12・14、Navi2x、Navi3x）、MMIO、chip の表、GOP の画面を保つ、段の印を画面に出す | A0・A1 | 2 LW |
| ws039-p003 | firmware の package（navi10 の psp・smc・pfp・me・ce・mec・mec2・rlc・sdma・sdma1・display、upstream の path・大きさ・sha256、license の同意）と PSP の起動・firmware の load | A2 | 3〜5 LW |
| ws039-p004 | SMU・GMC（VM の page table、VRAM・GART の窓）・IH・fence の割り込み | A3・A4 | 4〜6 LW |
| ws039-p005 | CP（GFX・compute の ring、KIQ）と SDMA、最初の copy と fence | A5 | 3〜5 LW |
| ws039-p006 | display（DCN 2.0）: GOP の画面の引き継ぎ、mode set、flip、cursor、GPU device の登録（nvrtx の P8a〜P8c に当たる） | A6〜A8 | 6〜10 LW |
| ws039-p007 | shader の compiler: SPIR-V → GFX10 の ISA（既存の IR の front を使い、ACO を手本に lowering・encode・scheduling）、host の interpreter と desktop の全 shader の差分の試験（nvrtx の SM75 と同じ形） | — | 8〜14 LW |
| ws039-p008 | Vulkan の executor: memory・image・pipeline・render pass・vkCmd（compositor が使う 15 種）を PM4 で、renderer の node（RENDER の role、WSI、swapchain） | A9 | 8〜12 LW |
| ws039-p009 | desktop: compositor が amdrdna で描く（nvrtx の R9k に当たる）、reboot の時の後片付け | — | 2〜4 LW |
| ws039-p010 | 規約の全文の見直し | — | 1〜2 LW |

計 約 40〜65 LW。実機が届くまでは p001〜p008 を host 試験と build まで書き進め、届いたら p002 の段から実機で順に確かめる（NVIDIA の R0〜R9 と同じ進め方）。描画の速さ（WS241 の教訓）: 投入・fence の待ちを最初から lock の外・spin＋sleep の形にする。

### 担当の案
- driver（p002〜p006・p008・p009）と compiler（p007）を 2 担当で並行（NVIDIA の P2・P4 と同じ分け方）。設計（p001）の後に design-reviewer。

### ユーザーに決めてほしいこと
1. 参考の source の扱い: amdgpu（Linux）は多くが MIT だが GPL の file もある。WS037 と同じ規則（値は事実として可、名前・構造・comment は写さない）をこの WS にも広げるか。
2. RX 5600 XT を挿す機械（RTX 機と入れ替えか、別の機械か）と、試験の道（素の起動だけか、passthrough も試すか）。
3. 最初の到達点: NVIDIA と同じく「desktop が描ける」までか。動画（VCN）は後でよいか。
4. 対象の世代: まず RDNA1（Navi1x）だけを動かし、RDNA2・3 は認識（device id）だけ先に入れるか。
5. 担当の数（2 担当で並行してよいか）。

### ユーザーの追加の決定（2026-10-15 夕、原文）
「もう1つサブエージェントを立てます。AMD RDNAドライバを書く既存WSの作業をします。このWSにも参照元などに関するガードレールをしっかり適用していきます。値の引用は明示すればOK、コード・シンボル名の引用は不可、ハードウェア操作手順の引用は明示すればOK、GPLなコードを引用しても、引用ならライセンスは影響しない、です。」
→ [Guardrail](../guardrail.md) の「MIT・GPL の出典のデータの引用」の WS039 の項。

### ユーザーの決定（2026-10-15、原文）
1.「参考の source の扱いですが、値は事実として使い、名前・構造・comment は写さない、をこの WS にも広げます。」
2.「RX 5600 XT を挿す機械と、試験の道は、ベアメタルでOKです。」
3.「最初の到達点を「デスクトップが描ける」までにします。動画（VCN）は後にします。」
4.「まず RDNA1 だけを動かし、RDNA2・3 は認識だけ先に入れます。」
5.「担当を 2 人（driver と compiler）で並行してよいです。」
→ 開始（Q1）: driver（p001 の設計 → p002〜p006・p008・p009）は P3、compiler（p007）は P5。設計（p001）の後に design-reviewer。
