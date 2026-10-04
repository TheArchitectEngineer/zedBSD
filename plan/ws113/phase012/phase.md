<!-- awesome-plan project=zedbsd record=ws113-p012 -->

# ws113-p012: native の power と refresh の境界（VK_EXT_display_control の下層）

Parent: [WS113](../ws.md)
Status: planned（2026-10-05 q702 の計画で足した。共有の UAPI の追加は Q1 の許可が要る: C2）
Disposition: normal
Primary Milestone: MG006（WS から継承）
Queue: none
目安: 3h

## 目的

`VK_EXT_display_control` を全部実装するのに要る native の操作を、GPU の display の UAPI に 2 つ足す（[契約の確定](../phase001/contracts-beta2.md) D-EXT・D-UAPI、[native capability](../phase001/native-contract.md) §2・§3）。HAL は変えない。

## 範囲

- `include/uapi/gpu-display.h`: `GPU_DISPLAY_POWER`（version・size・display_id・generation・state ON/OFF/SUSPEND・reserved。lease の持ち主の衝突・旧 generation・切断を副作用の前に拒む）と `GPU_DISPLAY_REFRESH`（version・size・display_id・generation・cursor・timeout_ns → refresh の連番と時刻。lease 不要、pipe が止まっていれば境界を作らず timeout、旧 generation は ESTALE、GPU の offline は ENODEV）。GPU の core（`src/drivers/gpu/gpu.c`）の dispatch と driver の ops の口。
- i915: pipe の vblank の割り込み（`vblank.c`）で出力ごとの refresh の連番を短い lock で publish し waiter を起こす。power は出力の worker で serialize（OFF/SUSPEND は present を止め buffer を退役してから pipe を止める、ON は検証してから戻す）。power の変更で topology の sequence を進めない。
- Venus: refresh は今の仮想の clock の境界（仮想と log・capability で明示）、power は scanout の停止と再開。
- 0 の capability を値 0 と取り違えない。fake の 60Hz を作らない。

## 受け入れ

host の試験（GPU の core の dispatch・検査、i915 の vblank の連番の publish と wait の race、power の状態の遷移）、QEMU（T1、Venus）で refresh の wait が進み power OFF・ON で scanout が止まり戻る（native の probe）、実機（5330）で eDP の refresh の wait が 60Hz 程度で進む（p008 にまとめてよい）。build warning 0（vmunix の kernel include check まで）、規約。

## 依存と衝突

依存: p002、C2 の許可。p003 がこれを使う。衝突: GPU の core（`gpu.c`）を変える他の WS の Phase。
