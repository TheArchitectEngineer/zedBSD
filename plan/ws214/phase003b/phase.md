<!-- awesome-plan project=zedbsd record=ws214-p003b -->

# ws214-p003b: kernel の段（UAPI の承認の後）: COMMIT・OVERLAY_INFO・WAIT を眠る形に、overlay の plane

Parent: [WS214](../ws.md)
Status: in-progress（2026-10-12 P2 開始。UAPI は 2026-10-12 ユーザーの承認、Q1 の指示で p003a の merge の後に開始）
Phase disposition: normal
Queue: P2

## 範囲

設計は [p001 第 3 版](../phase001/phase.md) の 4.2（UAPI の差分の案、そのとおりに適用）と 4.3 (2)(4)(5)(6)(7)(9) の p003b の分（7 節の表）。HAL の API・toolchain は変えない。i915・Venus の WAIT と present は変えない。

- UAPI（`include/uapi/gpu-display.h`）: `GPU_DISPLAY_HAS_OVERLAYS`、`GPU_DISPLAY_COMMIT`（'G' 44）・`GPU_DISPLAY_OVERLAY_INFO`（'G' 45）と struct・定数、`struct gpu_display_wait` の文（眠る、番号は controller の 1 本）。大きさを `_Static_assert` で確かめる（plane_state 96、commit 552、overlay_info 56）。
- core（`gpu.c`、`gpu-display.h`）: `commit`・`overlay_info` の op（両方か無し、`commit` は UNGATED を要る）、COMMIT の検査（4.2 の EINVAL の場合: plane の重複、COMMIT_WAIT なしの signal_fd、COMMIT_WAIT は primary を名指す時だけ、未定義の flag、count）、plane ごとの wait の fence は lookup の前、IMAGE の resource は pin、COMMIT_WAIT は pin を放した後に採用を待つ、signal の fence。fence は display の node が `GPU_CAP_FENCE` を持つ時だけ（無ければ wait_fd・signal_fd は EOPNOTSUPP）。
- bcm2711: overlay の plane（K=2）の状態と list の符号（unscaled、src の clip、位置）、plane ごとの serial と DROPPED、SHOWN・IMAGE の扱い、primary の状態が無い間の overlay は EINVAL、帯域の予算（仮 1.5 GB/s）と能力を外れた時の ENOSPC（何も変えない）、clock の見積もりに overlay、lease の始めと終わりで overlay を隠し serial を 0 に、外に見せる番号を `state_serial` に（PRESENT・COMMIT・WAIT）、WAIT は `adopted_serial` まで眠る（timeout_ns、1 秒まで、EINTR、失効で EBUSY）、OVERLAY_INFO、`plane_count` を 1＋K、計数（`pair`、overlay の採用の時刻の輪）。
- scaled の plane（PPF・TPZ・LBM、幅 2048 の制限）: HVS5 の scaled の word の値の出典が Linux の vc4 だけなので、WS141 の 2026-10-04 のユーザーの決定（register の値は事実として使い、名前は独自、配置・comment・構造は写さない）の当てはめを Q1 に確かめてから入れる。それまで OVERLAY_INFO は拡大縮小の flag を出さない（unscaled だけ）。
- 終わりに: 値の出典の表（[../phase003/hvs-values.md](../phase003/hvs-values.md)）、GPL との類似の監査、Pi の帯域の実測の道具 `hvs-load`（`plan/ws214/tests/`）。実測そのものは Pi で T1・Q1 経由。

## 受け入れ

1. host: overlay の list の符号（位置・clip・順）、予算と ENOSPC、COMMIT の原子性と一部 DROPPED、COMMIT_WAIT と WAIT の眠り・EINTR・失効、番号の意味（後の記録の番号が大きく、WAIT(n) は n 以下が全部採用済みで返る）、UAPI の struct の大きさ、ASan・UBSan。既存の display の回帰（p003a の `display-p003a-host.sh`）。
2. build: rpi4・amd64 の vmunix が warning 0。
3. T1: i915・Venus の回帰（WAIT が変わらない）。Pi（T1・Q1 経由）: `hvs-load` で plane の組み合わせごとの underrun（予算と能力を決める）。3 は Q1 が判定。

## コマンドと上限

- host: `timeout 900 sh plan/ws214/tests/display-p003a-host.sh <out>`（p003b の試験もここに足す）。
- build: p003a と同じ。QEMU は起動しない。

## 結果

（実行中）
