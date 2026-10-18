<!-- awesome-plan project=zedbsd record=ws263-p003 -->

# ws263-p003 規約の全文の見直し（書き方だけ）

Status: in-progress（2026-10-19 P1、q1257。cleared は Q1 の判定）

- 親: [WS263](../ws.md)。
- 範囲: この WS の新しい C（src/hal/riscv32/、platform/riscv32/、include/hal/arch/riscv32.h）を plan/coding-style.md の全文と照らし、書き方だけを直した（動作は変えない）。q1257 の 4 WS（WS263・WS264・WS265・WS266）でまとめて行った。
- 照らし方: 規約の §14 の checklist を機械で見られる所は scratchpad の検査の script（関数の forward declaration、条件の中の関数の呼び出し、3 個以上の節を 1 行に持つ条件、閉じ括弧の後の空行と段落の comment、関数の結果をそのまま返す return、初期化子の中の呼び出し、file scope の変数の comment、関数の頭の comment）で全部の file を見て直し、残りは読んで直した。基準として riscv64 の HAL に同じ script を当てると、閉じ括弧の後の空行が 34 件ほど出る（WS256 の範囲なので触っていない）。
- 直した主な物: 条件の中の呼び出しを変数に受けてから判定（`valid_user`・`space_lock_handle`・`*_range_is_ram`・`hal_cpu_mask_test`・`name_is`・`getprop`・`slot_read`・`virt_clock_ticked` など）、複合の条件の分割、閉じ括弧の後の空行と段落の comment（約 120 箇所）、`hal_space_prot` などの呼び出しの結果を直に返す所、`hal_printf` の段落、atomic の enter の条件演算子、file scope の変数の comment、static 関数の forward declaration。
- 残した物（規約の内）: 短く対称な条件演算子（hash 関数の primary と secondary の選び）、critical section の中の if（lock の後の空行の規則どおり）、byte 列から語を組む accessor の return（`__builtin_bswap32`・`read_be32`）。
- kernel の platform の binding: 4 port で共有していた `src/kern/platform/riscv-virt.c` を arch に依らない名前の `src/kern/platform/hal-console.c`（内容は同じ、file の説明だけ一般化）にし、riscv64・riscv32・m68k-virt・ppc32-mac99・ppc64-pseries の vmunix.mk と `src/kern/platform/README.md` を直した。古い `riscv-virt.c` はどこからも使われなくなったので Q1 に削除を依頼する。
- 確かめた事（2026-10-19）: `make … build/<platform>/vmunix` を riscv64-virt・riscv32-virt・m68k-virt・ppc32-mac99（vmunix.bin）・ppc64-pseries で流し、全部 exit 0、error・warning 0（log `build/tmp/conf-<platform>.log`、worktree p1）。`git diff --check` 0。解析の短い QEMU（QMP の screendump、判定ではない）で 4 port とも見直しの後も `HAL READY … TIMER TICK` と `VFS initialization failed` まで出る事を見た。
- 未実施: T1 の再判定（見直しで動作は変えていない。必要なら Q1 が判定に含める）。
