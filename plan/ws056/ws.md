<!-- awesome-plan project=zedbsd record=ws056 -->

# WS056: POSIX の試験と utility の小さな不具合を直す（BUG-034・035・037、実行中に見つけた BUG-042・043）

<!-- awesome-plan-current:start -->
Status: completed（2026-09-27）
Primary Milestone: MG002
Related Milestones: —
Objectives: O2
Parent: [Master](../master.md)
Queue: q423（p001）、2026-09-27 のサブエージェント（p002）
Resume point: —（完了）
<!-- awesome-plan-current:end -->

きっかけ: 2026-09-25 ユーザー「報告してくれた未修正の問題と、新しいバグについて、解決に取り組んでください。」

## 結果

| 項目 | 結果 |
| --- | --- |
| BUG-034 | `include/uapi/limits.h` の `RTSIG_MAX` を 33（`SIGRTMAX - SIGRTMIN + 1`） |
| BUG-035 | `posix-r2-remaining.c` の `struct atomic_record` を 8 byte に（lock-free の `_Atomic`） |
| BUG-037 | pax の読み手に typeflag `x`・`g`（pax の拡張 header）と `L`・`K`（GNU の長い名前）。GNU tar の `--format=pax`・`gnu`・`ustar` の coreutils の source を guest で展開し host と一致 |
| BUG-042 | kernel の `thread_create(2)` が作った thread の signal mask を継承しなかった（kernel と libc で修正） |
| BUG-043・044 | 試験の前提の誤りを直した |
| BUG-046 | 置き換えの signal mask（`SIG_SETMASK`・`sigsuspend`・`ppoll`・`pselect`）が libc の予約の wake signal 63 を unblock していた。libc で保つ（p002、EINTR 29〜155/300 → 0/300） |
| 受け入れ | `POSIX-R2-REMAINING.ELF` status 0（`R2R:01-12:PASS`）。`POSIX-R2.ELF` は console で 10 回続けて status 0（BUG-068・BUG-069 を WS073 の p007・p008 で直した後。2026-09-27 ユーザーが閉じる判断を main に委ね、main が clear） |
| 回帰 | guest の make の差分試験 91/91、sh の差分試験 1388/1425（落ちる 37 件は q422 と同じ集合）、boot test PASS |
| 規約 | 新しい・変えた code は style-check で増やしていない（p001・p002 で確認） |

QEMU だけ。実機は未実施。

## 制限・移管

- 試験（console の `POSIX-R2.ELF`、SIGEV_THREAD の mask、posix_spawn の probe、pax の archive）は [plan/tools/posix/](../tools/posix/) へ移した（Tools 節）。
- console の停止の残りは [BUG-068](../bugs/BUG-068.md)（execve と thread の回収）・[BUG-069](../bugs/BUG-069.md)（tty の EAGAIN）として WS073 で直した。

## Phase 一覧（Phase のディレクトリは削除、git の履歴に残る）

| Phase | 内容 | Status |
| --- | --- | --- |
| ws056-p001 | BUG-034・035・037（と 042・043・044）の修正と試験、規約の確認 | cleared（2026-09-27。q423-i01 は uncleared で終わり、残りは console の `POSIX-R2.ELF` の status 0 だった。BUG-046・068・069 の修正の後の console の 10 回連続 status 0 で clear。ユーザーの委任で main が判断） |
| ws056-p002 | console での `POSIX-R2.ELF` の EINTR の経路の特定と修正（BUG-046） | cleared（2026-09-27、サブエージェント） |
