<!-- awesome-plan project=zedbsd record=ws274-p005 -->

# ws274-p005 S6 swap.c を vm/ へ移して 4 つに分ける

- 親: [ws274](../ws.md)、設計: [design.md](../design.md) §1.4・§2・§3 の S6（判断③ ★(a)）
- Status: in-progress（q1298 / q1298-i01、P2、2026-10-19）。T1 の起動の回帰の結果を Q1 が判定するまで cleared にしない。
- 規約: ユーザーの決定（2026-10-19 17:43、design.md §6 は既定）により、移動の段は WS106 と同じ「配置の変更だけの例外」（[ws106-relocation.md](../../standards/ws106-relocation.md) の考え）。関数の本文・評価順・lock の順は変えない。規約の全文は p007。

## 行った事（worktree `/home/awe/zedBSD-worktrees/p2`、agent/p2）

`git mv src/kern/swap.c src/kern/vm/swap.c` の後、元の file を機械的に（関数ごとの注記・本文をそのまま）4 つに分けた。

| file | 中身（元の swap.c の行） | 行数 | 関数 |
| --- | --- | --- | --- |
| src/kern/vm/swap.c | slot store・manager・I/O（169-1144）と static の swap_manager_enable_transition・swap_manager_disable_empty・swap_io・source_find_free_locked、変数 system_backend・swap_lock | 1267 | 26 |
| src/kern/vm/swap-control.c | control と boot の準備（1153-1470）と static の is_boot_reference・selector_*・control_*、変数 control_registration・control_registered・control_busy・control_lock、extern thread_current | 683 | 16 |
| src/kern/vm/swap-header.c | header の codec（1476-1673）と static の get32・get16・get64 | 286 | 7 |
| src/kern/vm/swap-source.c | source と source set（1679-2830）と残りの static（extent・file/raw の backend・identity・source_set_*）、型 fat_swap_extent・file_swap_data・raw_swap_data・swap_disk_range、変数 active_extent_count・swap_source_lock、ops の 2 表、weak extern 6 つ | 1784 | 40 |

- **static を 1 つも外に出していない。** 4 つの群の間の参照は全部、公開の関数（kern/swap.h・kern/swap-source.h などに宣言済み）だった。そのため内部の header は作っていない（design.md §2 の swap は内部 header の対象外）。
- forward 宣言と weak extern は、その関数を定義・使用する file へ元の順のまま写した（coding-style.md の「移す時にこの block を落とすな」）。
- include の block（元の 17-46 行、重複を含む 30 行）は 4 つの file に同じ物をそのまま写した。使わない include の整理は規約の全文の段（p007）へ。
- 各 file の冒頭の注記は、元の「Swap.」の注記を 4 つに分けて書いた（注記だけ）。
- Makefile の `KERN_VM_SOURCES` の先頭に 4 行（P1 の p002 が触る末尾と離した）。13 の platform の一覧から `src/kern/swap.c`（pcat・pc98 は `$(BUILD)/src/kern/swap.o`）を外した。
- docs/reference/kernel-boot-parameters.md:40-41 の path を swap-control.c・swap-source.c・swap-header.c に、userland/base/mkswap/swap-codec.c:9 の注記を swap-header.c に。
- 振る舞いの違い: HAL_FATAL の `__FILE__`（swap.c の 1 箇所、swap-source.c の 9 箇所）の文字列が新しい path になる。link の順で swap の object が vm の群の先頭へ動く（address だけ）。ほかは無い。

## 確かめ（host）

commit 469702ce8（code・Makefile・13 の platform・docs・mkswap の注記）。base は main の 4660a2fb5。

- build: `make -j16 ZEDBSD_CONFIG=<config> BUILD=build/ws274-p005-<p> vmunix`、config は config/ci/config-amd64.mk（LTO）、config/ci/config-pcat.mk（object の一覧）、plan/tools/ports/config-sun4u.mk（gcc、LTO 無し）。base（`ws274-p005-base-<p>`）と分割の後の全部で exit 0、`grep -c 'warning:'` 0。3 つとも log に vm/swap.o・swap-control.o・swap-header.o・swap-source.o が出る。
- 照合: `python3 -I plan/ws274/tests/body-compare.py 4660a2fb5 src/kern/swap.c -- src/kern/vm/swap.c src/kern/vm/swap-control.c src/kern/vm/swap-header.c src/kern/vm/swap-source.c` → functions 89 names 89 differences 0、PASS。加えて空行を除いた行の多重集合を元と比べ、違いは include の重複・著作権の頭・冒頭の注記だけ。
- nm: `build/llvm/bin/llvm-nm vmunix` の大文字の型の symbol（amd64 56・pcat 58・sun4u 1442）が base と同じ（diff 0、増えた global は無い）。小文字の local では amd64 に `t kern_swap_source_init`・`r swap_header_uuid_format.digits` が増え、sun4u で `t kern_swap_source_init.part.0` が消えた（翻訳単位が分かれて inline・分割の判断が変わっただけ）。vmunix の大きさは amd64 −120・pcat +120・sun4u +64 byte。
- host の試験: swap に関わる登録済みの host の試験は無い（master.md の Tools・tests/ のシナリオ）。ws073 の swap-slot-host.sh は削除の候補（下）なので流していない。
- 未実施: 残り 10 の platform の build（p007 で全部）、QEMU の起動（下の T1 の依頼）。

## T1 への依頼（WS274 p005、design.md §5 の一部）

worktree agent/p2 の 469702ce8（または Q1 が merge した main）で、振る舞いを変えない swap.c の移動・分割の後の起動と swap の回帰。p001・p002 の依頼とまとめて 1 つの QEMU で流してよい。

1. `plan/tools/boot-test.sh` の uefi-usb（amd64、8 GiB）と bios-ide（pcat）で login prompt（どちらも boot の swap file を使う）。
2. `plan/tools/pc98-boot.py` で pc98 の起動。
3. `plan/tools/ports/` の sun4u（sparcv9、gcc で LTO 無し）の起動の回帰。
4. 任意（swap の control の道）: amd64 の guest で `swapoff` → `swapon`（boot の swap の source の外しと付け直し）が成功し、`swapon` の一覧に戻ること。
- 合格: どれも login prompt（または各道具の PASS）。PNG を返す。

## 残り

- 完了の WS の登録の無い試験で旧 path を使う物は直さず削除の候補（Q1 へ）: `plan/ws073/tests/swap-slot-host.sh`（WS073 は completed、master.md の Tools・試験の一覧・tests/ のシナリオ・未完了の WS のどれからも参照されない。src/kern/swap.c から source_find_free_locked を awk で切り出す）。
- 履歴の記録（plan/ws035・plan/ws073 の phase.md・BUG-073）の旧 path は履歴なので直さない。
- 自分の build の出力 build/ws274-p005-{amd64,pcat,sun4u}/ と .log、build/ws274-p005-base-*/ は Q1 が消してよい。
