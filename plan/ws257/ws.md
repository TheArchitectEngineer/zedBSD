<!-- awesome-plan project=zedbsd record=ws257 -->

# WS257 i915 を Haswell・Broadwell（Gen7.5・Gen8、Core の第 4・5 世代）に対応

- ID: ws257、Master: [master.md](../master.md)、Primary Milestone: MG006（関係: MG003）
- Status: planning（2026-10-18 Q1 作成。今は取り組まない）
- 起点（2026-10-18 ユーザー）:「Haswell, Broadwellへの対応はWSを作っておいて、今は取り組みません。」
- 経緯: Intel MacBook Air（A1466、Core i5 1.5GHz）で zedBSD は起動したが、i915 が Skylake より前の GPU を知らないので display が無く、sessiond が「there is no greeter or no display」で console の login に戻った（2026-10-18 ユーザーの写真）。A1466 は 2012〜2017 年の 13 インチで、Ivy Bridge・Haswell・Broadwell のどれか（年式は未確認）。
- 担当: 未割当。

## 完了の条件（案、着手の時に確定する）

1. Haswell（Gen7.5）と Broadwell（Gen8）の iGPU で、i915 が device を認識し、display（内蔵の eDP）を出し、Keiland の desktop が動く。
2. GT・EU（Vulkan の実行）の範囲は着手の時に決める（WS229 の Gen9 と同じく、GOP のままの display から始める案もある）。
3. 実機（A1466 の MacBook Air など）で確かめる。

## Phase

未定（着手の時に設計の Phase から作る）。

## 未決

- 対象の機種と年式（A1466 の年式、Ivy Bridge を含めるか）。
- 着手の時期（ユーザーの指示まで止める）。
