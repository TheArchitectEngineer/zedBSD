<!-- awesome-plan project=zedbsd record=ws293 -->

# WS293 i915 の Vulkan の executor を render/ から vk/ に揃える

- Master: [plan/master.md](../master.md)（2026-10-27 00:06 作成）。Primary Milestone: MG006。Related: [WS031](../ws031/ws.md)（i915 native Vulkan 実行器）。
- Status: planned（2026-10-27 00:06 新設）
- 出典: 2026-10-27 00:06 ユーザー「drivers/gpu/i915には他のGPUドライバと違ってvk/ディレクトリがないですね」→（Q1: render/ が同じ役）→「vk/に揃えたいです。」

## 単一の到達目標

`src/drivers/gpu/i915/render/` を `src/drivers/gpu/i915/vk/` に移し、file 名と内部の名前の付け方を他の driver（amdrdna・nvrtx・powervr・bcm2711 の `vk/`、`vk-*.c`）に揃える。動作は変えない。

## 受け入れ

- `render/` が無く、build の規則・host 試験・execution の試験・文書の参照（WS031 などの未完の Phase の `render/` の列を含む）が新しい path を指す。
- build（既定と I915_TESTS=y）の warning 0、i915 の host 試験 PASS、5330 の素の起動の回帰 PASS（P4 の plan/ws245/tests/5330-run.sh）。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 改名（git mv、file 名・include・build の規則・試験・文書の参照）と回帰 | P4 の WS252・q1601 の後（i915 の source を同時に触らない） | planned |
