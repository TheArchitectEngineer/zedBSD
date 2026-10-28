<!-- awesome-plan project=zedbsd record=ws219 -->

# WS219: Venus の protocol の op の流用の整理（定数名の付け直し、Venus の著作権表示を外し、引用の明記に留める）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。p001 の監査（表現の写し無し）、p002 は実装・host 試験済みで T1-572 PASS（beta2.md の記録）、p003 cleared）
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: なし
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

Venus（virtio-gpu の Vulkan の protocol）の op を使っている所（`src/drivers/gpu/venus/`、`userland/desktop/libvulkan/`、`include/uapi/gpu-op.h` ほか）について、事実（op の番号・値）と表現の写しを分けて記録し、Venus の著作権表示を外し、引用元の明記を引用の箇所に残す。

## ユーザーの決定

- 2026-10-13（原文）「WSを作ります。VenusのOPを流用しているのですが、定数名を変えて、Venusの著作権表示を外し、引用を明記するに留めます。」
- 2026-10-13（p001 の後）「はい、推奨でいいです。」→ p002 は出典の記入、README:164 と API-PROVENANCE（出典を Khronos に）の直し、gpu-op.h の名前の無い数値 3 つに名前、に縮める。zedBSD 自身の "venus" の名前は付け直さない。comment の中の Mesa の command の名前は引用として残し出典を書き足す。capset の配置は値だけを使い出典を書き、license の表示はしない。
- 2026-10-13（p002 の後）「コメントです。gpu-op.hでは、OPごとに詳細な説明をいれてください。でないと、Venusの翻案になってしまいます。」→ p003。

## 結果

- p001: 監査。表現の写しは無し。Google の表示は WS167 で既に除去済み。
- p002: 出典の記入、README・API-PROVENANCE の直し、名前の無い数 3 つに gpu-op.h の名前。build warning 0・host 試験 PASS、QEMU の Venus の graphical の起動は T1-572 PASS（2026-10-13 Q1 判定、QEMU のみ。plan/agents/T1/requests.md、Phase の記録には未反映）。
- p003: `include/uapi/gpu-op.h` の 159 の entry に op ごとの説明（用途・引数・reply・送り手と受け手・順序・未使用）と file の頭の注記（翻案ではない旨）。名前と値の不変、build warning 0、host 試験 PASS。
- p003 の発見（libvulkan が送るのに i915・bcm2711 が解かない op が 29）は [WS226](../ws226/ws.md) の入力。

## 制限・移管

- 無し（規約の全文の Phase は持たない）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws219-p001 | 監査: Venus 由来の物の洗い出し、事実と表現の写しの区別 | in-progress のまま閉じた（監査は済み、ユーザーの決定で p002 へ） |
| ws219-p002 | 出典の記入、README・API-PROVENANCE の直し、名前の無い数に名前 | in-progress のまま閉じた（build・host 試験 PASS、T1-572 PASS） |
| ws219-p003 | gpu-op.h の op ごとの詳しい説明 | cleared |

## 記録の所在

Phase の計画・結果は削除する。git の commit `fb1f6afee` の `plan/ws219/` にある。Queue の履歴は [plan/history](../history/index.md)。
