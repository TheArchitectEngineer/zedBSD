<!-- awesome-plan project=zedbsd record=ws273-p001 -->

# ws273-p001 RG350（Ingenic JZ4770）の移植の設計

- 親: [ws273](../ws.md)、Queue: q1317 / q1317-i01（P2、版 1、2026-10-19）、q1320 / q1320-i01（P1、改訂 2、2026-10-19）、q1323 / q1323-i01（P2、改訂 2.1、2026-10-19）
- Status: cleared（2026-10-19 Q1 の判定: design-review-2 の結論「Major 1 件（M-a）を直せば受け入れてよい…Q1 がその差分を見れば足りる」。改訂 2.1（7c87a62d3、merge 96bb99756）で M-a を直した事を Q1 が差分で確かめた: framebuffer・descriptor を NOLOAD の .uncached へ、入口の全体の cache の操作を新しい関数に。proposed の 2 つの diff は git apply --check が通る。ユーザーの判断 D1〜D8 は未決で、p002 の前に聞く）

## 目的

Anbernic RG350 の実機で zedBSD を起動する道を決め、`plan/ws273/design.md` に書く。コードは書かない。

## 受け入れの条件

1. 公開の資料（JZ4770 の programming manual、Linux mainline の `ingenic` の DT と driver、OpenDingux の起動の道、RG350 の基板の情報）を調べ、CPU（MIPS32r2 の XBurst、FPU の有無、cache・TLB）・endian・memory・起動（SD の boot loader、U-Boot の有無）・LCD（LCDC と panel）・SD（MSC）・入力（GPIO のボタン、ADC）・UART の取り出しを出典付きで書く（未確認は未確認と書く）。
2. WS271 の MIPS32 malta の HAL（src/hal/mips/）との差（BSP の分け方、boot の handoff、早期の console）と、HAL の API の変更の要否（要るなら差分の案を proposed/ に置くだけ）。
3. 実装の Phase の分割（依存・LW の見込み・実機の確認の点。実機はユーザーが画面で目視）と、ユーザーの判断が要る点（D1…）。
4. GPL の source は手順の参照だけで code を写さない。

## 結果（2026-10-19 P2）

- 設計: [design.md](../design.md)（版 1）。出典は Linux mainline の jz4770.dtsi・gcw0.dts・cpu-probe.c・panel-simple.c、OpenDingux の jz-6.1 の rg350.dts の連鎖、UBIBoot（`gh api` で取得、GPL は事実だけを読み code は写さない）。JZ4770 の Programming Manual は公開の所在が見つからず未確認。
- 要点: XBurst は CP0 Count が無い（timer を bsp へ移す）、PageCtrl で HPTLB を切る、I cache が VTAG。起動は UBIBoot の `UZIMAGE.BAK`（X を押して起動）。HAL の API の変更は不要（proposed/ は作らない）。
- Phase の案 p002〜p006、ユーザーの判断 D1〜D6（design.md §5）。
- 未実施: design-reviewer、実機の確認（全部）。

## 結果（2026-10-19 P1、改訂 2、q1320 / q1320-i01）

- [design-review-1.md](../design-review-1.md)（Blocker B1、Major M1〜M10、Minor 1〜12、D3 の推奨の変更、D7 の追加）の全部に対応し、[design.md](../design.md) を版 2 にした。対応の表は design.md §9。
- 主な変更: 入口の Cause.IV=1 への対処（Cause の消去と 0x200 の vector、p002）、argv を使わない、tick = TCU ch0・OST = 64 bit の counter、VTAG の I cache の 3 項、L2 の操作と KSEG1 の framebuffer、ボタンの表を DT のとおり（alt key は PE28 = DT の Y）、LED・backlight の点滅の観測、共通の層の変更を全部 p002 に、p005 を分けて p002〜p007、D3 を保留に変更、D7（実機の検証の方法）・D8（mips32.h の comment）を追加。
- HAL の API の差分の案（未適用）: [proposed/mips32-h-timer-comment.diff](../proposed/mips32-h-timer-comment.diff)、[proposed/mips32-h-fpregs-fr0.diff](../proposed/mips32-h-fpregs-fr0.diff)。
- 受け入れの条件 2 の path を `src/hal/mips/` に直した（Minor 8）。
- 出典の確かめ: レビュー 1 が `gh api` で取った複写で B1・M2・M4・M5・M6・Minor 9 の根拠を読み直した。他は「確（レビュー 1）」と記した。
- 未実施: design-reviewer の差分の確認、実機の確認（全部）、build・試験（設計なので不要）。

## 結果（2026-10-19 P2、改訂 2.1、q1323 / q1323-i01）

- [design-review-2.md](../design-review-2.md)（Major M-a、Minor 1〜12）の全部に対応し、[design.md](../design.md) を版 2.1 にした。対応の表は design.md §10。
- M-a: framebuffer・descriptor を bss から外し `__bss_end` の後の NOLOAD の `.uncached` に置いて KSEG1 でだけ触れる。入口の全体の cache の操作は既存に無いので新しい関数（Config1・Config2 をその場で読み L1 D・L1 I・L2 を Index、bss の消去の後・`mips_board_init` の前）を p002 に。
- Minor 4〜12 は §3.1 の Phase の項（p002〜p006）に取り込んだ。D7 は AGENTS.md の `boot-test.sh` だけの規則の例外としてユーザーの明示の承認が要ると書いた。
- [proposed/mips32-h-fpregs-fr0.diff](../proposed/mips32-h-fpregs-fr0.diff) を作り直し、`git apply --check` が通ることを確かめた（timer-comment の diff も通る）。
- 出典は新たに取得していない（レビュー 2 の行番号の根拠は locore.S・cmain.c・vmunix.ld・mips32.h・fpregs.S を worktree で読んで確かめた。DT の RG350M の panel は「確（レビュー 2）」）。
- 未実施: Q1 の差分の確認、実機の確認（全部）、build・試験（設計なので不要）。
