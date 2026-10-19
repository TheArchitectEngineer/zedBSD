<!-- awesome-plan project=zedbsd record=ws269-p013 -->

# ws269-p013 USC の encoder の広げ（出力 O-ENC）

- 親: [ws269](../ws.md)、設計: [design-p010.md](../design-p010.md) 版 2 §5・§5.4、レビュー: [design-p010-review-2.md](../design-p010-review-2.md)（M-6 (c)・結論 5）
- 担当: P2（`src/drivers/gpu/powervr/usc-encode.c`・`usc-encode.h` の持ち主。p010 の側は encoder を直接変えず、要る命令をこの Phase に依頼する）
- Queue: 最初の試行は q1363 / q1363-i01（P2、2026-10-20、[p010-compiler](../phase010-compiler/phase.md) として記録。この Phase の範囲を含む）。q1369 / q1369-i01 でこの phase.md を作った
- Status: in-progress（2026-10-20 P2: q1363 で compiler が使う命令を足した。§5.4 の固定の program の命令は一覧の確定待ち）
- 依存: p008（encoder の土台、host 試験）
- 見積もり: 残り 6〜10 LW（命令の一覧による。推測）

## 目的と完了の条件

1. encoder を SMP（texture）・FITR/FITRP（補間）・EMITPIX と、vertex・fragment の compiler（[p014](../phase014/phase.md)）・EOT・SR/PT・transfer の固定の program（design-p010.md §5.4）が要る命令に広げる。
2. 広げた命令ごとに、Mesa 26.2.5 の `pco_isa.py`・`pco_map.py` の field から手で組んだ bytes と比べる host 試験（`plan/ws269/tests/p008/`・`p010-compiler/`）が PASS。
3. build（VF2・riscv64-virt・amd64）warning 0。
4. **O-ENC**: 上の encoder と host 試験が main に merge 済み。

**O-ENC の完了の条件は暫定**: 固定の program（§5.4）の命令の一覧は P1 の design-p010 版 2.1 で決まる（review 2 の M-6 (c)・結論 5）。一覧が届くまで、この条件の「要る命令」は compiler が使う命令と下の既知の物だけを指す。一覧が届いたら、この節を確定の形に書き直す。

## 最初の試行（q1363 / q1363-i01、2026-10-20 P2）

詳細の記録は [p010-compiler](../phase010-compiler/phase.md)（境・確かめ・残り）。この Phase に当たる部分:

- 足した命令: `smp`（brief）、`ditr`/`ditrp`（BXE の usc_itrsmp_enhanced の補間）、`emitpix`、`upck`、`uvsw`（write・emit.endtask）（p010-compiler の残り 3 の記述）。fitr/fitrp（back end の形）は BXE で使わないので足していない。
- 確かめ: p008 の host 試験 encode 91/0（encoder の照合は p008 の手で組んだ期待と、開発中の Mesa の generator との突き合わせ）、p010-compiler の group host 試験 20/0（group の field と phase の bytes）。build VF2・riscv64-virt・amd64 warning 0。
- 結果: 未判定（O-ENC は一覧の確定待ち）。

## 残り

1. P1 の design-p010 版 2.1 の命令の一覧（p010n・p010e・p010f の SR・PT・clear・EOT・pixel event・transfer）を Q1 経由で受け、足りない命令を足す。
2. 足した命令の host 試験（手で組んだ bytes）。
3. 完了の条件を確定の形に直し、O-ENC の merge を Q1 に依頼する。
