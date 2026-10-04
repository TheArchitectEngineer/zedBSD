<!-- awesome-plan project=zedbsd record=ws052p009 -->

# ws052-p009: i915 の suspend・resume

Phase ID: `ws052-p009`
Parent: [WS052](../ws.md)
Status: in-progress（2026-10-05 P1 generation17。設計は Q1 が §6 を判断（design の末尾）。段 (a) を実装、(b)・(c) は未着手）
Phase disposition: normal
Queue: Q1 の 2026-10-05 の指示（p004 から i915 を分けた: QEMU で試せず規模が大きい。設計から、検証は 5330 の UAT）

## 範囲

display の suspend（窓を出る）、request worker の park、GT（forcewake・RPS・RC6・engine）、割り込み、display の power（DC9、DMC の再 load、display の
core の再初期化）、GGTT、resume で suspend の前の出力先（panel か HDMI、Keiland の lease はそのまま）へ戻す。HAL に依らない。

## 受け入れ

- 設計（Q1 のレビュー済み）、build（vmunix の link、warning 0）、host の試験（worker の park の状態機械、DC9 の前提と書き順）。
- 5330 の UAT（p004 の `sleepctl devices`）: 画面が消えて同じ出力先に戻り、GPU の描画が続く。dmesg に RC6 と DC9。

## 設計

[design-p009-i915.md](../design-p009-i915.md)（§6 に判断の点: GGTT の書き直し、DC9 の前提、park 中の request、出力先の記憶、commit の分け方）。

## 依存

- p004（PCI の口、`KERN_SYSTEM_SLEEP` の devices だけの mode）。
- HAL は不要（H1〜H4 の承認を待たない）。

## 段 (a): worker の park と窓の suspend の要求（2026-10-05）

- `src/drivers/gpu/i915/park.c`・`park.h`（新規、amd64 の vmunix に追加）: park の状態と判断（device の IRQ lock の下で呼ぶ、眠らない）。
  `drv_i915_park_begin`（二重は EBUSY、generation を進める）、`drv_i915_park_action`（park が無ければ serve、stop が勝つ、窓の中なら窓を出る、
  外なら park）、`_entered`・`_stays`・`_left`・`_end`（reach 済みかを返す）・`_reached`（generation で自分の park か）。
- `worker.c`: loop の各回の頭で `drv_i915_park_action` を見て、窓の中は `I915_WORKER_SERVE_LEAVE_DISPLAY`（resident run が reference の stop path で
  出力を消す。lease は残る）、外は新しい `I915_WORKER_SERVE_PARK`。眠りの条件に park を足し、起きたら頭で判断する。serve の loop は PARK で
  `i915_worker_park_here`（GT を idle に → parked を立て sync_done を起こす → park が終わるか stop まで眠る → GT を戻す）。待っている request は
  queue に残る（Q1 の判断 3）。`drv_i915_worker_park(device, timeout_ms)`（reach まで待つ、時間切れは park を取り消して EBUSY、serving でなければ
  ENODEV）、`drv_i915_worker_unpark(device)`。
- `device.c`: `drv_i915_device_park_gt`（RPS を止め、全 forcewake を放して `gt->forcewake_parked` に覚える。GT が RC6 に入れる）、
  `drv_i915_device_unpark_gt`（forcewake を取り直し RPS を始める）。`gt.h` に `forcewake_parked`。
- まだ suspend の op には結線していない（段 (c)）。

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| park の host の試験（ASan/UBSan） | `make -C plan/ws052/tests i915-park && build/ws052/host/i915-park` | PASS（窓の中→窓を出る→park→reach→resume→serve、窓の外は直ちに park、二重は EBUSY、stop が勝つ・park 中の stop で出る、取り消し後の遅い worker は park しない、generation の取り違え無し） |
| vmunix の link | `make vmunix` | PASS、warning 0 |
| 規約 | `style-check.py`（park.c・park.h・試験、worker.c・device.c は新しい指摘 0） | PASS |
| 実機 | — | 未実施（段 (c) の後、5330 の UAT） |

残りと危険: 同じ lease のまま窓を出て入り直す道は今まで無かった（hold が切れて次の lease が入る道はある）。panel の buffer の map は窓を出る時に
外し、frame ごとに map し直す作りなので通る見込みだが、段 (c) の UAT で確かめる。
