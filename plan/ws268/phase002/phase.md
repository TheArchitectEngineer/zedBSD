<!-- awesome-plan project=zedbsd record=ws268-p002 -->

# ws268-p002 規約の全文の見直し

Status: cleared（2026-10-19 Q1 判定: m68k の task.c・space.c・bsp-x68k の irq.c・timer.c・x68k.c を規約の形に、動作は不変（P2 が旧と並べて読んだ）、x68k の image と検査 PASS（P2）、Q1 が merge 96c54eb31 の後に x68k の vmunix を build して warning 0・check PASS）（旧: in-progress（2026-10-19 P2 実施済み、Q1 の判定待ち））

- 親: [WS268](../ws.md)。範囲: p001 が変えた C（`src/hal/m68k/` の task.c・space.c・runtime.c・defs.h、`bsp-x68k/` の boot.c・handoff.c・bsp.h・cmain.c・console.c・irq.c・pmem.c・timer.c、`src/kern/platform/x68k.c`、`src/drivers/platform/x68k/x68k-console.[ch]`、`bootloader/x68k/handoff.h`）。

## したこと

- task.c・space.c・irq.c・timer.c を全体で書き直した（p001 では関数の API の差だけを直していた）: 関数の定義の形、前方宣言、file-scope の変数と型の役割の comment、宣言の位置、条件の中の関数の呼び出しを変数に、複合の条件の分け、成功の return を最後に、comment の位置と空行。動作は変えていない（各関数を旧と並べて読んだ: 条件の順、走査、flush の位置、rollback、U/M の bit の扱い、timer の分周の選び方）。使われていなかった tick の数の変数（timer.c）を外した。
- x68k.c: 走査の中の条件の関数の呼び出しを変数に。
- 機械の走査（plan/coding-style.md §14）: 条件の中の関数の呼び出し 0 件、関数の結果の直の return 0 件。

## 結果

- `build/ws268/zedbsd-x68k.hd` rc=0、warning・error 0、`X68k image check: PASS`。`check-disk-image`・`x68k-user-abi-check`・`x68k-target-audit` rc=0。`git diff --check` 差なし。
- 範囲外のまま: 変えていない m68k の file（cache.c・exception.c・trap.c・io.c・atomic.c・machine.c・memory-map.c・mmu030.h・assembly）。
- 動作の確かめ: 起動できる emulator の手順が無く未実施（p001 の MAME の記録）。
