<!-- awesome-plan project=zedbsd record=ws064 -->

# WS064: base の make の並列（`-j`）

<!-- awesome-plan-current:start -->
Status: completed
Primary Milestone: MG002
Related Milestones: MG001
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: —（2026-09-27 完了）
<!-- awesome-plan-current:end -->

## 目標

2026-09-26 ユーザー指示: 「makeは-jに対応させて、並列makeの実行時間もホストと同等以上にしてください。」（[F-016](../future-work.md) の昇格）

base の make（`userland/base/make/`）で `-j N` の job を並列に走らせ、再帰の make と job の数を分け合い（jobserver）、guest の expat の `make -j4` を host の `make -j4` と同等以上にする。

## 結果

- make: `-j N`・`-j`・`--jobs`、GNU make 互換の jobserver（`MAKEFLAGS` の `-jN --jobserver-auth=R,W`、pipe の token）、`.WAIT`・`.NOTPARALLEL`、失敗の後の待ちと `-k -j`（p001）。
- 並列の費用（p002）: kernel の mutex の短い回転と 3 状態の速い道、system call `vfork` と libc の `vfork`・`posix_spawn`（amd64 は専用 stack の vfork）、make と sh の外部 command の `posix_spawn`、fork の private page の 32 page ずつの共有、`vmspace_destroy` の解放を lock の外へ、object page の逆写像の O(1) の外し。
- sh の process の生成（p004）: pipeline の要素・command substitution・`unset` の試しの subshell を fork せずに。fork は expat の make で 1417 → 214、configure で 966 → 423。
- 計測（p002・p004、QEMU 8 GiB 4 vCPU NVMe）: expat の `make -j4` 4.18〜4.38 秒（host の GNU make 5.14〜5.18 秒）、configure 7.1 秒（host 10.7 秒）、直列の make 10.7 秒（host 15.5 秒）。
- 規約（p003、2026-09-27）: WS064 の新しい file と関数は style-check 0。make の差分試験 host・guest 100/100、guest の fork・vfork・posix_spawn の試験、sh の差分試験、expat の configure・`make -j4`・runtests が変わらないことを確かめた。

## 制限・移管

- `make check` の試験の driver は bash を要り、guest では走らない（WS046 の既知の制限）。`tests/runtests` は 4932/4932。
- p003 は時間を測っていない（受け入れの時間は p002・p004 で達成済み）。`smpstress`・`lock-stress`・swap の試験は道具が残っておらず未実施。
- 既存の大きな file（`vmspace.c`・`vm.c`・`process.c`・`posix.c`・`syscall.c`）の WS064 の外の指摘は残る（WS064 の関数だけを直した）。
- 実機は未実施（QEMU だけ）。

## Phase 一覧

Phase の記録は完了に伴い削除した（git の履歴に残る）。

| Phase | 内容 | Status |
| --- | --- | --- |
| ws064-p001 | 設計と実装: job の並列、jobserver、失敗と `-k`、差分試験の `-j` の case | cleared（q448-i01） |
| ws064-p002 | 並列の make の性能（mutex、vfork・posix_spawn、fork・destroy の lock） | cleared（q449-i01） |
| ws064-p003 | 規約の適合 | cleared（2026-09-27、サブエージェント） |
| ws064-p004 | `/bin/sh` の process の生成を posix_spawn（vfork）に広げる | cleared（q450-i01） |

## 試験と道具

- make の差分試験は `plan/ws046/tests/make-diff.py`（`cases/parallel.sh` を含む 100 件）。
- p003 で `plan/tools/` に置いた: `guest/hybrid-image.sh`（main の guest image の複写にこの tree の kernel・libc・make・sh を入れる）、`guest/make-cases.sh`（guest で make の差分試験）、`process/vfork-test.c`・`process/guest-vfork.sh`（fork の copy on write、vfork、posix_spawn、同時の fork）。
