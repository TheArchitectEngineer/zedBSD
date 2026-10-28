<!-- awesome-plan project=zedbsd record=ws239 -->

# WS239: Wayland（compositor）の起動の失敗時に text console の login へ戻す

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001 の実装は main。残りは p002 の QEMU の確かめだけ）
Completed: 2026-10-27（q1608）
Primary Milestone: MG002
Related Milestones: なし
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

graphical boot（login=graphical、kmsg=quiet）で compositor が起動に失敗した（起動しない・すぐ終わる・繰り返し失敗する）時に、(1) `/dev/system` を通して kernel の message の出力を console へつなぎ直し、(2) graphical boot を解除し、(3) tty の login（getty）を表示する。正常な graphical boot は変えない。

## ユーザーの決定

- 2026-10-15 ユーザー「あと、Waylandが起動に失敗したとき、/dev/systemを通じて、カーネルメッセージをコンソール出力につなぎ直し、グラフィカルブートを解除してttyのログインを表示してほしいです。」
- `/dev/system` の `KERN_SYSTEM_SHOW_CONSOLE` の追加は、ユーザーの指示の範囲として Q1 が承認した（2026-10-15）。

## 結果

- main c17de52b2: `include/uapi/system.h` の `KERN_SYSTEM_SHOW_CONSOLE`、`src/drivers/generic/system-device.c`、`userland/desktop/sessiond/main.c` と新しい `userland/desktop/sessiond/fallback-rules.{c,h}`。
- 失敗の定義（fallback-rules.c）: greeter が READY の前に終わる、起動から 60 秒以内に READY を言わない、READY の後でも 20 秒以内に error・signal で終わる。続けての失敗が 6 回、または 2 回以上で最初の失敗から 90 秒を過ぎたら console に戻す（すぐ終わる compositor は約 30 秒、固まる compositor は約 2 分）。
- host 試験（2026-10-15）: fallback rules の host 試験 PASS（plain、ASan/UBSan）。build は warning 0。

## 制限・移管

- p002 の QEMU の確かめ（失敗を模した起動で kernel の message と tty の login が出る、T1-655 の case A〜D）は未実施（T3 が途中で停止、結果なし）。2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」の扱いで WS を閉じた。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws239-p001 | 調査・実装・host 試験・T の手順（`KERN_SYSTEM_SHOW_CONSOLE`、sessiond の失敗の定義と console への戻し） | in-progress のまま WS を閉じた（実装は main c17de52b2、host 試験 PASS） |
| ws239-p002 | QEMU での確かめ（T） | 未実施のまま WS を閉じた（2026-10-27 の q1608 の扱い） |

## 記録の所在

Phase の計画・結果・T の手順と試験（`plan/ws239/phase001`、`plan/ws239/tests/`）は削除する。git の commit `fb1f6afee` の `plan/ws239/` にある。Queue の履歴は [plan/history](../history/index.md)。
