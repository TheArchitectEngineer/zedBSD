<!-- awesome-plan project=zedbsd record=ws274-p001 -->

# ws274-p001 S0 準備・S1 移動・S2 commit と metadata lock の切り出し

- 親: [ws274](../ws.md)、設計: [design.md](../design.md) §3 の S0〜S2
- Status: in-progress（q1296 / q1296-i01、P1、2026-10-19）。T1 の起動の回帰の結果を Q1 が判定するまで cleared にしない。
- 規約: ユーザーの決定（2026-10-19、design.md §6 は既定）により、移動の段は WS106 と同じ「配置の変更だけの例外」（[ws106-relocation.md](../../standards/ws106-relocation.md) の考え）。関数の本文・評価順・lock の順は変えない。規約の全文は p007。

## 行った事（worktree `/home/awe/zedBSD-worktrees/p1`、agent/p1）

| 段 | commit | 中身 |
| --- | --- | --- |
| S0 | fd0c9bb51 | Makefile に `KERN_VM_SOURCES`・`KERN_VM_OBJS`（KERN_NET と同じ形）。source の一覧を持つ 11（amd64・arm64・m68k-virt・mips32-malta・mips64-malta・ppc32-mac99・ppc64-pseries・riscv32・riscv64・sparcv9・x68k）と object の一覧の 2（pcat・pc98）を変数へ。design.md の後に mips32/64-malta が増えていたので 13 の platform。照合の script [tests/body-compare.py](../tests/body-compare.py) |
| S1 | 32c1cde14 | `git mv` で vm.c → vm/vm.c、vmspace.c → vm/vmspace.c、vm-device.c → vm/device.c。試験の path（ws213 の kd-host-test・pin-host-test、ws113 の host-display-control、ws214 の display-p003a-host）と docs/architecture/kernel-and-hal.md:16 |
| S2 | cd04d7e82 | commit（H）を vm/commit.c、metadata lock（I、static helper の metadata_lock_measured・metadata_hold_end を含む）を vm/metadata-lock.c へ。static は 1 つも外に出していない。vm.c の冒頭の注記を直した。kd-host-test の切り出し元を metadata-lock.c に |

- 振る舞いの違い: HAL_FATAL の `__FILE__`（commit.c の 4 箇所、metadata-lock.c の 3 箇所）の文字列が新しい path になる。ほかは無い。
- 区切りの見出しの注記（`/* Commit */`・`/* Metadata */`）は file が分かれたので新しい file に写していない（注記だけ）。

## 確かめ（各段、host）

- build: `make -j16 ZEDBSD_CONFIG=<config> BUILD=build/ws274-<段>-<p> vmunix`、config は config/ci/config-amd64.mk（LTO）、config/ci/config-pcat.mk（object の一覧）、plan/tools/ports/config-sun4u.mk（gcc、LTO 無し）。base（S0 の commit）・S1・S2 の全部で exit 0、warning 0。
- 照合: `python3 -I plan/ws274/tests/body-compare.py fd0c9bb51 src/kern/vm.c src/kern/vmspace.c src/kern/vm-device.c -- src/kern/vm/*.c` → functions 345 names 343 differences 0、PASS（S1・S2）。
- nm: `build/llvm/bin/llvm-nm vmunix` の大文字の型の symbol（amd64 56・pcat 58・sun4u 1442）が base と S1・S2 で同じ（増えた global は無い。S2 は static を外に出していない）。
- host の試験: `sh plan/ws213/tests/kd-host-test.sh` PASS、`sh plan/ws213/tests/pin-host-test.sh` PASS（S1・S2）。
- 未実施: 残り 10 の platform の build（p007 で全部）、QEMU の起動（下の T1 の依頼）。

## T1 への依頼（WS274 p001、design.md §5 の一部）

worktree agent/p1 の cd04d7e82（または Q1 が merge した main）で、振る舞いを変えない VM の file の移動の後の起動の回帰。1 つの QEMU でまとめて流す。

1. `plan/tools/boot-test.sh` の uefi-usb（amd64、8 GiB）と bios-ide（pcat）で login prompt。
2. `plan/tools/pc98-boot.py` で pc98 の起動。
3. `plan/tools/ports/` の sun4u（sparcv9、gcc で LTO 無し）の起動の回帰。
- 任意: `plan/tools/process/guest-vfork.sh`（fork の COW）。
- 合格: どれも login prompt（または各道具の PASS）。PNG を返す。

## 残り

- ws141 の `plan/ws141/tests/host-display-cursor.sh:22` の `src/kern/vm-device.c` → `src/kern/vm/device.c`（別の担当が直す）。
- 完了・closed の WS の登録の無い試験で旧 path を使う物は直さず削除の候補（Q1 へ）: plan/ws073/tests/region-index-host.sh、plan/ws014/tests/run-venus-remote.py、plan/ws014/tests/run-gpu-*.sh（vm-device.c を参照する 9 本）。
- 自分の build の出力 build/ws274-{base,s1,s2}-{amd64,pcat,sun4u}/ と .log は Q1 が消してよい。
