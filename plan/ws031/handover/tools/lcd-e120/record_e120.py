import sys
root = sys.argv[1].rstrip("/") + "/"
L = root + "plan/ws031/results-ws031.md"; B = root + "plan/ws031/regression-baseline.md"
led = """

## p011 増分E-120 (2026-09-19): E-119 の境界確認と native 受入の準備（ベアメタル起動の直前まで）
- E-119 固定: increment-results/e119-freeze.md（2bf790a4＋e97-e119-changes.patch、各 build の sha256）。報告の訂正: buffer をまたぐ同一性は E-119 では未確認だった（E-120 で確認）。`event_rc` 表記。TLB の done は 0 に戻れば完了。
- TLB: 実装は正本どおり mask=done, value=0 を待つ。fake は受け付け後 3 read で clear（TLB-POLL）、stuck は bit が残る。seqno は成功時のみ +2。意図的な fault の log に backend／test／expected_fault と ktest の区間の目印。
- flip の event: **欠落を修正** — TIMEOUT した event の vblank 参照が停止で返っていなかった。intel_crtc_vblank_off → parity_lcd_ms_vblank_off（cancel_event＋put 1 回）。event 本体は thread 専有、IRQ は lock 下の計数だけを更新（no-op にしている lock の根拠）。host J1。
- evasion sleep: model J2（範囲内 → sleep 1 → DONE、IRQ 有効）／J3（vblank なし → 有限に終わり、完了扱いにしない）。**実機 REACHED**（scanline 1074、遅れ 0 line）。最初の固定先行量 2〜30 line の試行は NOT-REACHED（log 保存）。
- LCD-D: 順序 A0 B1 A2 B3 A1 B2 A3 B0、buffer をまたぐ hash 4/4 一致、写真一致。trace 上限 768→2048（probe 分の flip で記録が欠けないように）。
- **N0**（native_precheck.c／native_decide.c／opregion_vbt.c）: P3.4 と P3.6 の間、読むだけ。OpRegion→VBT（2.0 は物理、2.1 以上は相対の RVDA、なければ mailbox#4）、GFXVTBAR（MCHBAR mirror 0x145400）の VER／GSTS／PMEN、fb の handoff と GGTT の重なり、pipe の電源は well の STATE bit で判定（init_hw 前の hw_enabled は使わない）。判定: active pipe／重なり／VT-d が読めない・TES・PMR → 書込み前に STOP（BLOCKED where=native-precheck）。
- 対象機の native 側の事実: ASLS 0x614e5018、OpRegion 2.1、RVDA 0x2000／RVDS 8704（VBT の sha は明示 pin と同一）、DMAR flags 0x05（platform opt-in）、GPU の DRHD 0xfed90000、efifb 0x4000000000（GMADR 先頭、1920×1080）→ **native の初回は active pipe A で STOP の見込み**。
- 試験: host lcd-modeset 123/0、lcd 56/0、dp 72/0、opregion 11/0（新）、native-decide 8/0（新）。ktest 536/0。SWEEP_LINE
- 未解決: N1（active な pipe の readout と crtc_disable_noatomic）、intel_opregion_register、同じ context を再利用する renderer（未着手）。native の log は画面の写真＋/var/log/messages（ring 32 KiB）。
- 報告 = handover/expert-reports/report-e120-native-prep.md、review diff = e120-review-changes.patch（E-119 固定版比）、累積 = e97-e120-changes.patch。
"""
base = """

## E-120 (2026-09-19)
- host: lcd-modeset **123/0**、lcd 56/0、dp 72/0、opregion 11/0、native-decide 8/0。GPU-free ktest **536/0**。
- どの mode も probe で N0 を通る（VM では `N0 decision: PROCEED`）。LCD-D の合格条件に、buffer をまたぐ比較 4/4、sleep 入口での IRQ off 0、evasion probe の結果 ≥ 0 を追加（REACHED／NOT-REACHED は記録のみ）。
- 意図的な TLB fault の log は `backend=MODEL test=… expected_fault=1`。集計では `expected_fault=0`（HW）の行だけを異常として数える。
- 回帰 sweep: `handover/tools/sweep_e120.sh`（13 mode）。SWEEP_LINE
"""
line = "回帰 sweep 13/13 PASS（sweep_e120.sh、最終 source、各 ktest 536/0、全 run で N0 PROCEED）。"
s = open(L).read(); assert "増分E-120" not in s; open(L, "w").write(s.rstrip("\n") + led.replace("SWEEP_LINE", line))
s = open(B).read(); assert "## E-120" not in s; open(B, "w").write(s.rstrip("\n") + base.replace("SWEEP_LINE", line))
print("recorded")
