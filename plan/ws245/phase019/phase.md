<!-- awesome-plan project=zedbsd record=ws245-p019 -->

# ws245-p019: AUX の transaction（xd-aux）

Status: planned（q1225-i01 で P1 が受けたが、コンテキストが重くなったので調べだけで返却した: 2026-10-16 Q1「コンテキストが重くなったら安全な区切りで返却してよい」。code は未着手）
Parent: [WS245](../ws.md)
Queue: q1225 / q1225-i01
設計: [design.md](../design.md) §6.2 の p019 の行（改訂 6）、§2.9・§3.2 の xd-aux

## 再開のための調べ（2026-10-16、main 1c5a9e91f の `display/aux.c` 957 行）

- 継ぎ目: `aux.c` の `i915_dp_aux_transfer`（`drm_dp_aux.transfer`、`drv_i915_dp_aux_init`（`aux.c:119`、`dp-sink.c:1753` が呼ぶ）が結ぶ）→ `i915_dp_aux_xfer`（`:591`。AUX の power domain・PPS lock・VDD を取って返す: **これは殻に残す**。design のとおり well・DC off・VDD は呼ぶ側）→ `i915_dp_aux_xfer_locked`（`:489`）→ `i915_dp_aux_send`（`:415`）。xd-aux に替えるのは `_xfer_locked` と `_send`、`_wait_done`（`:264`）、`_pack/_unpack`（`:221`・`:243`）、`i915_skl_get_aux_send_ctl`（`:358`）・`_fw_sync_len`（`:334`）・`i915_skl_get_aux_clock_divider`（`:297`）、`i915_tgl_aux_ctl_reg/_data_reg`（`:827`・`:858`）。
- 今の振る舞い（xd-aux が保つ物）:
  - 前の AUX の busy: CTL を 3 回、1 ms おきに読み、SEND_BUSY が残れば EBUSY（status が前回と違う時だけ warn）。
  - 送る大きさ・受ける大きさが 20 を超えれば E2BIG。
  - clock divider の list（SKL 以降は 1 つ、0 で終わり）ごとに最大 5 回: data の register に pack して書き、CTL に送る語を書き、done を待ち、DONE・TIME_OUT_ERROR・RECEIVE_ERROR を書いて消す。timeout は即次、receive error は 400〜500 µs 待って次、DONE なら終わり。
  - 結果: DONE が無い → EBUSY、receive error → EIO、timeout → ETIMEDOUT（debug の log だけ）、受けた大きさが 0 か 20 超 → EBUSY、受け手の room まで unpack。
  - 送る語（`i915_skl_get_aux_send_ctl`）: SEND_BUSY・DONE・INTERRUPT・TIME_OUT_ERROR・TIME_OUT_MAX・RECEIVE_ERROR・MESSAGE_SIZE・FW_SYNC_PULSE（fast wake の SYNC: precharge + preamble、DPCD の quirk QUIRK_FW_SYNC_LEN で変わる）・SYNC_PULSE（precharge + preamble）、TBT-alt の Type-C port は TBT_IO。XELPDP の POWER_REQUEST は display 14 以降で範囲外。
  - CTL・DATA の番地: AUX A・B・C・USBC1〜6 は `DP_AUX_CH_CTL(ch)`、他は A に落とす（Linux `intel_dp_aux.c` の tgl の表）。
  - 外側の再試行（`dp-sink.c:1830-1840` の native の 32 回、`:2230-2238` の I2C の defer）は `aux->transfer` の外で、p031・p048 まで旧（design の m13）。
- R11: `aux.c` の開発の管理情報は 1 箇所（触る時に消す）。
- 試験: own の register の model（`tests/display/xd-fake-regs.c`）に AUX の応答（ACK/NACK/DEFER/timeout/receive error）を足し、`dp-fake-hw.c` の期待値（列と回数）を写す（design §6.3）。TC の IO select（TBT_IO）の語も（R9）。
- 5330 の回帰は T1 に依頼（2026-10-16 ユーザー「P1のテストはT1にオフロード」）: 手順は `phase053/phase.md`・`plan/ws245/tests/5330-bootcycle.sh`。見る物は design の p019 の行（訓練が通る、DPCD の読みの log が同じ、AUX の error の log が増えない）。
