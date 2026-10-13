<!-- awesome-plan project=zedbsd record=ws230 -->

# WS230: i915 の GuC 対応（GuC の firmware の読み込み・投入・電源の管理）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-14 Q1 作成）
Primary Milestone: MG006
Related Milestones: なし
Objectives: O1
Parent: [Master](../master.md)
Queue: 未割当
Target: ベータ3（2026-10-14 ユーザー「GuC対応はベータ3で入れます。」）
Resume point: p001（調べと設計）を Q1 が投入する。
<!-- awesome-plan-current:end -->

## 原文（2026-10-14 ユーザー）

「GuC対応はベータ3で入れます。」（Q1 の説明: Linux の xe は GuC の投入だけが製品の経路で execlists は試験用、i915 は Alder Lake-P 以降で GuC の投入が既定。今の zedBSD の i915 は execlists で、firmware は DMC だけ）

## 目標（案、p001 で決める）

- GuC の firmware（と HuC の認証）の読み込み、GuC の投入（CT の buffer、context の登録、doorbell）、SLPC の電源の管理。
- execlists の経路は残す（Gen9 から Gen12 まで）。Meteor Lake 以降（Xe-LPG）の対応の前提になる。
- firmware の配布の license の監査。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| p001 | 調べと設計（Linux の i915 の uc と xe の guc、firmware の版と ABI、execlists との切り替え） | planned | — |
