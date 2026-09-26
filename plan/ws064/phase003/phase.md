<!-- awesome-plan project=zedbsd record=ws064p003 -->

# ws064-p003: WS064 の変更の規約の適合

Phase ID: `ws064-p003`
Parent: [WS064](../ws.md)
Status: cleared
Queue: 2026-09-27 ユーザー指示（サブエージェントで WS を完了まで。main session が Queue を記録する）
Disposition: normal

## 目的

WS064 で変えた source を [coding-style.md](../../coding-style.md) の全文で見直す（2026-09-26 ユーザー「規約適合は最後でいいです」: 性能の目標の後に行う）。

範囲: `userland/base/make/`（`update.c`・`job.c`・`main.c`・`rule.c`・`make.h`）、`src/kern/lock.c`（mutex）、`src/kern/process.c`・`exec.c`・`syscall.c`（vfork）、`src/kern/vmspace.c`・`vm.c`（fork の一括、destroy、`object_link`）、`userland/base/libc/posix.c`・`signal.c`・`syscall-amd64.S`・`src/libc/crt/crt0-amd64.S`（vfork・posix_spawn）、`userland/base/sh/`（`sh_spawn`）、`include/`（`uapi/syscall.h`・`kern/process.h`・`kern/vmspace.h`・`libc/unistd.h`）、`plan/ws046/tests/cases/parallel.sh`。

## 受け入れ

style-check の指摘が 0、全文の規約で見直した記録。build（warning 0）、make の差分試験 100/100、boot test。

## 見直したこと（2026-09-27）

WS064 の history は squash されている（`1e867fbf` に他の WS と一緒）ので、差分ではなく Phase の記録にある関数を読んで直した。比べた元は `9e5d76a5`（WS064 の後、この Phase の前）。

| file | style-check（前 → 後） | 内容 |
| --- | --- | --- |
| `userland/base/make/job.c`（WS064 の新しい file） | 6 → 0 | file 全体を書き直した: file 変数に 1 つずつ説明、`struct job` の field の説明、jobserver の継承と作成を `inherit_server`・`create_server` に、行の展開を `job_next_line` に、割り込みの対象の判定を `target_changed` に。長い呼び出しは 1 行に 1 引数、直接の return を保存してから |
| `userland/base/make/update.c` | 4 → 0 | 歩き（`update_goal`・`update_target`・`update_double_colon`・`update_rule`・`update_prerequisites`・`remake`）の段落、非標準の `for` を 3 行に、`in_walk` の protocol の説明、`ignore`・`silent` を `if` で |
| `userland/base/make/main.c` | 4 → 0 | jobserver の file 変数に 1 つずつ説明、`-j` の読み、`MAKEFLAGS` |
| `userland/base/sh/exec.c` | 52 → 0 | ws065-p004 で一緒に直した（posix_spawn の `pipeline_spawn`・`pipeline_start`、`word_is_pure`（`brace_is_pure`）、`subshell_unset_trial`・`unset_trial`（`unset_trial_eval`・`unset_names_plain`・`unset_trial_restore`・`words_are_pure`）、`substitution_in_shell`・`substitution_simple`（`join_operands`）、`pipeline_inline`・`output_builtin`・`output_bound`・`parse_quietly`） |
| `userland/base/sh/jobs.c` | 0 → 0 | `sh_spawn` の段落と成功の return |
| `src/kern/lock.c` | 18 → 0 | file 全体: static 関数を公開関数の後へ、前方宣言。mutex の回転を `mutex_spin_take`、guard の下で眠って取るのを `mutex_sleep_take`（`mutex_lock_interruptible` と `mutex_wait` で共通。割り込みで諦めるのは `WAITQ_INTERRUPTIBLE` のときだけで、前と同じ）、持ち主の検査を `mutex_check_owner` に。原子操作の順序と種類は変えていない |
| `src/kern/vmspace.c` | 485 → 444 | `vmspace_fork_locked`（27 件）を `fork_region`・`fork_batch_share`・`fork_share_page`・`fork_batch_map`・`fork_batch_release` に分けた（lock を放す・取る位置、BUSY と hold の順は同じ）。`vmspace_fork` の `goto retry` を loop に、`vmspace_destroy`・`detach_vm_page_for_unmap`・`detach_region_pages_for_unmap` の段落。`struct vmspace_fork_entry` を前方宣言の前へ移し field に説明。残りの 444 件は WS064 の外の既存のコード |
| `src/kern/vm.c` | 673 → 671 | `object_link` の連結と外し |
| `src/kern/process.c` | 200 → 197 | `process_vfork`・`process_vfork_release`・`fork_process` の vfork の分、`struct process_vfork_wait` の field。残りの `goto fail` は共通の後始末への前方の jump（許される形） |
| `src/kern/exec.c`・`syscall.c` | 96 → 95、393 → 393 | vfork の解放の後と `sys_vfork_call` の成功の return |
| `userland/base/libc/posix.c` | 870 → 853 | `posix_spawn_common`（amd64 と他）・`spawn_child`・`__vfork_error`・`vfork`（他の arch）・`posix_spawn`・`posix_spawnp`（`function_result` の名前）、`struct spawn_request` の説明。`posix_spawn_fork`・`spawn_child_setup`・`spawn_exec_search`・`spawn_environment_path` は WS064 の前からの fork 版の中身（名前だけ変わった）で、手を付けていない |
| `userland/base/libc/signal.c` | 60 → 55 | `sigaction`（`__libc_caught_signals` に記録する分と、その protocol の説明） |

C 以外（`syscall-amd64.S`・`crt0-amd64.S`・clang の Makefile・`parallel.sh`）は規約の対象外で、読んだだけ。

## 検証（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| build（lean の amd64 image、`-Werror`、`plan/ws045/tests/config-amd64-base.mk`） | warning 0 |
| make の差分試験（host で build した make） | 100/100（posix 45、automake 15、gnu 31、parallel 9） |
| 試験用の guest image | main の guest image（clang・sshd を含む）の複写に、この tree の `vmunix`・`BOOTX64.EFI`（ESP）と `/lib/libc.so`・`/usr/lib/libc.so`・`/usr/bin/make`・`/bin/sh` を入れた（`plan/tools/guest/hybrid-image.sh`）。この tree の kernel で起動し SSH が通る |
| make の差分試験（guest、`plan/tools/guest/make-cases.sh`、`/root` で） | 100/100 |
| fork・vfork・posix_spawn（guest、`plan/tools/process/guest-vfork.sh`） | fork の copy on write 100 page、vfork の共有、vfork 2000 回、`posix_spawn`・`posix_spawnp`・無い program の `ENOENT`、4 process 同時に 300 回ずつ fork: ALL OK |
| sh の差分試験（guest、1458 件） | 1413/1458、失敗の一覧が変更前と同一 |
| expat の configure・`make -j4`・`tests/runtests`（guest、新しい kernel・libc・make・sh） | configure の生成物 9 file の cksum が前と同一、`make -j4` status 0、runtests 4932/4932 |
| boot test（`build/ws065/image`、`BOOT_MODE=uefi-nvme`） | PASS（`build/boot-test-ws064p003/login.png`） |

時間の測定は未実施（他の agent が機械を使っている。受け入れに時間は無い）。SMP の `smpstress`・`lock-stress`・swap の試験は未実施（道具が残っていない。代わりに上の同時 fork と `make -j4`）。実機は未実施。

## 結果（2026-09-27、cleared）

WS064 の新しい file と関数は style-check 0。既存の大きな file（`vmspace.c`・`vm.c`・`process.c`・`posix.c` ほか）は WS064 の関数だけを直し、指摘は増えていない（減った）。make・sh・fork の振る舞いは host と guest の試験で変わらない。
