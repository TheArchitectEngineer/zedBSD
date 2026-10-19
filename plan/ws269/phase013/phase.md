<!-- awesome-plan project=zedbsd record=ws269-p013 -->

# ws269-p013 USC の encoder の広げ（出力 O-ENC）

- 親: [ws269](../ws.md)、設計: [design-p010.md](../design-p010.md) 版 2 §5・§5.4、レビュー: [design-p010-review-2.md](../design-p010-review-2.md)（M-6 (c)・結論 5）
- 担当: P2（`src/drivers/gpu/powervr/usc-encode.c`・`usc-encode.h` の持ち主。p010 の側は encoder を直接変えず、要る命令をこの Phase に依頼する）
- Queue: 最初の試行は q1363 / q1363-i01（P2、2026-10-20、[p010-compiler](../phase010-compiler/phase.md) として記録。この Phase の範囲を含む）。q1369 / q1369-i01 でこの phase.md を作った
- Status: in-progress（2026-10-20 P2: q1363 で compiler が使う命令を足した。q1372 で §5.4 の残り 1 点（SR の shared register の burst）が今の encoder で書けることを host 試験で確かめた。O-ENC の判定は Q1）
- 依存: p008（encoder の土台、host 試験）
- 見積もり: 残り 6〜10 LW（命令の一覧による。推測）

## 目的と完了の条件

1. encoder を SMP（texture）・FITR/FITRP（補間）・EMITPIX と、vertex・fragment の compiler（[p014](../phase014/phase.md)）・EOT・SR/PT・transfer の固定の program（design-p010.md §5.4）が要る命令に広げる。
2. 広げた命令ごとに、Mesa 26.2.5 の `pco_isa.py`・`pco_map.py` の field から手で組んだ bytes と比べる host 試験（`plan/ws269/tests/p008/`・`p010-compiler/`）が PASS。
3. build（VF2・riscv64-virt・amd64）warning 0。
4. **O-ENC**: 上の encoder と host 試験が main に merge 済み。

**O-ENC の中身（確定、design-p010 版 2.1 §5.4、2026-10-20 Q1 が受け入れ）**: §5.4 の表の「今の encoder」の列の関数が main に merge 済みで host 試験 PASS、加えて SR（context の store・resume）の「shared register を宛先・元にする `ld`・`st` の burst（256・1024 語）」を host 試験で確かめる（書けなければ encoder に足す）。これより広い命令を後から足さない。

## 最初の試行（q1363 / q1363-i01、2026-10-20 P2）

詳細の記録は [p010-compiler](../phase010-compiler/phase.md)（境・確かめ・残り）。この Phase に当たる部分:

- 足した命令: `smp`（brief）、`ditr`/`ditrp`（BXE の usc_itrsmp_enhanced の補間）、`emitpix`、`upck`、`uvsw`（write・emit.endtask）（p010-compiler の残り 3 の記述）。fitr/fitrp（back end の形）は BXE で使わないので足していない。
- 確かめ: p008 の host 試験 encode 91/0（encoder の照合は p008 の手で組んだ期待と、開発中の Mesa の generator との突き合わせ）、p010-compiler の group host 試験 20/0（group の field と phase の bytes）。build VF2・riscv64-virt・amd64 warning 0。
- 結果: 未判定（O-ENC は一覧の確定待ち）。

## q1372 / q1372-i01（2026-10-20 P2）: SR の burst

- 結論: **今の encoder で書ける**（encoder の変更なし）。Mesa 26.2.5 の SR（`pco/usclib/common.cl` の `load_store_sr`）は `dma_ld_shregs_pco`・`dma_st_shregs_pco` を使い、`pco_trans_nir.c` はそれを「offset を index register 0 へ mov」と「index 0 を通した shared register 0 を宛先（ld は s3）・元（st は s0）にする `ld.regbl`・`st32.regbl`」に落とす（`pco_map.py` の O_LD_REGBL・O_ST32_REGBL）。burst の長さは register から（1024 語は 0）。
- `powervr_usc_load/store` の `burst = 0`・`burst_source` が regbl の形（op の bytes は p008 の既存の試験）。index の bank は `POWERVR_USC_BANK_INDEXED_0`（6）、register の番号は F_IDXBANK と offset<<3（index register 自身は idx 0b101 で 5、shared は 0b011 で 3）を 11 bit の形で（`register_bits`）。
- host 試験（`plan/ws269/tests/p008/encode-host-test.c`、手で組んだ bytes と比べる、+3）: index register 0 への mov（宛先 0x85 0x0c）、shared register の burst の ld（s3 = 0x83 0x0c 0x00）、st（s0 に index 0 の shared、s2 に burst、s3 に address）。p008 encode 94/0。
- 注: Mesa の SR の store は `idf` の flag を付ける（`pco_ops.py` の OM_IDF）が、op の bytes には field が無い（schedule の側の扱い）。SR の program（p010n）で store の後に fence（`powervr_usc_fence`）を置くかは p010n で Mesa の出力と照らす。
- 結果: O-ENC の命令は §5.4 の全部が書ける。merge の依頼は Q1 へ（この commit）。

## 残り

1. （済み、q1372）P1 の design-p010 版 2.1 の命令の一覧（p010n・p010e・p010f の SR・PT・clear・EOT・pixel event・transfer）を Q1 経由で受け、足りない命令を足す。
2. 足した命令の host 試験（手で組んだ bytes）。
3. 完了の条件を確定の形に直し、O-ENC の merge を Q1 に依頼する。
