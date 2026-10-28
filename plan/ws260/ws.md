<!-- awesome-plan project=zedbsd record=ws260 -->

# WS260 i915 の Gen9 の display（gen9-*.c）を xd と同じく自前で書き直す

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001 cleared、p002〜p015 は実装済み（gen9-*.c を xd に書き直し）。残りは T1-710 の SV7・5330 の実機の回帰だけ（U24・U25） → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」）
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: なし
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

i915 の display 9（Skylake・Kaby Lake・Coffee Lake・Comet Lake）の `src/drivers/gpu/i915/display/gen9-*.c`・`*.h`（22 file）を、[WS245](../ws245/ws.md) の xd と同じく Intel の PRM と hardware の振る舞いから自前で書き直し、xd の部品に display 9 を足す（Linux の行ごとの言い換えをしない。値は block ごとに `Values referenced:`、手順は `Procedure referenced:`）。Gen9 の機種で起動・内蔵の画面・backlight・desktop を今と同じに保つ。設計は [design.md](design.md)。

## ユーザーの決定

- 2026-10-18 ユーザー「i915由来のdisplayのソースコードをすべて書き直す件は、どうなったんですか？」→ Q1 の報告 → ユーザー（クリック）「xd と同じく書き直す」（2026-10-15 の決定「Gen9 のディスプレイはガードレールにそって修正してください」を書き直しに広げた）。
- 2026-10-18（クリック）: 1. 形は「案 A: xd に display 9 を足す」。2. 実機の確かめは「最後に 1 回」（全部の段の後に T1 が SV7 と 5330 で 1 回、壊れていれば段の境で二分）。3. Gen9 の log の文言は xd の形に「揃える」。4. 起動の語 `i915.gen9.display=` は「xd の語にまとめる」→ 中身は「語を廃止、常に full」（切り分けは `i915.debug=display` と `i915.start=manual` だけ。src/kern/boot.c・include/kern/boot.h の変更を P1 に許可）。5. SV7 に外の display は「繋げない（host 試験だけ）」。
- 2026-10-18（クリック）: 実機（SV7・5330、T1-710）は「実機は残して待つ」。

## 結果

- `display/xd9-*.c/h`（12 file）と xd の部品（xd-output・xd-pipe・xd-dbuf・xd-hpd・xd-ddi・xd-clock・xd-power・xd-panel・xd-takeover・xd-aux・xd-gmbus・xd-dp・xd-platform）の display 9 の分、`intel/display-regs-xd9.h`・`display-tables-xd9.h`。gen9-*.c は全部消え（今の tree に 0 file）、caller（bootfb.c・i915.c・device.c・display.c）は xd に付け替えた。p002〜p015 は q1253（P1）。
- 試験: golden の書き込みの列と数の計算の表（今の code で取った基準）と、display 9 の host 試験（ASan/UBSan）。p015 の時点で golden 4 つが same。
- 写しの点検（p015、Linux 6.8.12 の i915・display と比べた）: comment の 7 語の並び 12,900 個のうち一致 0。code の 5 token の並び（識別子 3 個以上）の一致は file ごとに 0〜2 %（決まった形）、行の写しは無い。`Procedure referenced: Linux` は 2 つ（PRM に載らない workaround: Display WA #1143・#0868）。
- 規約の全文の見直し（p015）: 機械の点検と書いた時の確かめ。全項目を 1 行ずつ人が読んだのではない（p015 の記録の制限）。

## 制限・移管

- T1-710（SV7 の起動・graphical の login・dmesg の xd9 の行、5330 の段ごとの回帰）は未実施。
- 外の display の実機（SV7 に繋がない）は未実施（ユーザーの決定 5）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws260-p001 | 設計: gen9-*.c の棚卸し（写しの点検）、xd の部品の共有、段の分け方と回帰の方法 | cleared（2026-10-18、design.md、判断 5 点はユーザーが回答） |
| ws260-p002 | 試験の土台: golden と gen9-fake-hw を xd-io の backend に | in-progress のまま WS を閉じた（実装は main） |
| ws260-p003 | xd-platform に display 9 の記述 | in-progress のまま WS を閉じた（実装は main） |
| ws260-p004 | 数の計算（CDCLK・DP の link と M/N・DPLL の語・buf trans） | in-progress のまま WS を閉じた（実装は main） |
| ws260-p005 | WM・DDB の計算を xd-dbuf へ | in-progress のまま WS を閉じた（実装は main） |
| ws260-p006 | AUX・GMBUS・EDID を xd-aux・xd-gmbus・xd-edid へ | in-progress のまま WS を閉じた（実装は main） |
| ws260-p007 | backlight（SPT/KBP と CNP/CMP の形）を xd-panel へ | in-progress のまま WS を閉じた（実装は main） |
| ws260-p008 | power well を xd-power へ | in-progress のまま WS を閉じた（実装は main） |
| ws260-p009 | DPLL の enable/disable と DDI clock の選択を xd-clock へ | in-progress のまま WS を閉じた（実装は main） |
| ws260-p010 | DDI buffer・signal level・iboost・DP_TP と link training を xd-ddi・xd-dp へ | in-progress のまま WS を閉じた（実装は main） |
| ws260-p011 | transcoder・pipe・plane の点灯と消灯を xd-pipe へ | in-progress のまま WS を閉じた（実装は main） |
| ws260-p012 | WM・DDB の書き込みと順序、hotplug の sense を xd-dbuf・xd-hpd へ | in-progress のまま WS を閉じた（実装は main） |
| ws260-p013 | firmware の点検と flip を xd-takeover・xd-screen へ | in-progress のまま WS を閉じた（実装は main） |
| ws260-p014 | 外の display を xd-screen・xd-display の口へ、caller の付け替え、殻を全部消す | in-progress のまま WS を閉じた（実装は main、gen9-*.c は 0 file） |
| ws260-p015 | 規約の全文の見直し、写しの点検、T1 に SV7 と 5330 の実機の回帰 | in-progress のまま WS を閉じた（見直しと点検は済み、T1-710 は未実施） |

## 記録の所在

Phase の計画・結果・試験（`plan/ws260/phase001`〜`phase015`、`plan/ws260/tests/`）は削除する。git の commit `fb1f6afee` の `plan/ws260/` にある。Queue の履歴は [plan/history](../history/index.md)。
