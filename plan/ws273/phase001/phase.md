<!-- awesome-plan project=zedbsd record=ws273-p001 -->

# ws273-p001 RG350（Ingenic JZ4770）の移植の設計

- 親: [ws273](../ws.md)、Queue: q1317 / q1317-i01（P2、2026-10-19）
- Status: in-progress（設計の文書が出来た、design-reviewer 待ち）

## 目的

Anbernic RG350 の実機で zedBSD を起動する道を決め、`plan/ws273/design.md` に書く。コードは書かない。

## 受け入れの条件

1. 公開の資料（JZ4770 の programming manual、Linux mainline の `ingenic` の DT と driver、OpenDingux の起動の道、RG350 の基板の情報）を調べ、CPU（MIPS32r2 の XBurst、FPU の有無、cache・TLB）・endian・memory・起動（SD の boot loader、U-Boot の有無）・LCD（LCDC と panel）・SD（MSC）・入力（GPIO のボタン、ADC）・UART の取り出しを出典付きで書く（未確認は未確認と書く）。
2. WS271 の MIPS32 malta の HAL（src/hal/mips32/）との差（BSP の分け方、boot の handoff、早期の console）と、HAL の API の変更の要否（要るなら差分の案を proposed/ に置くだけ）。
3. 実装の Phase の分割（依存・LW の見込み・実機の確認の点。実機はユーザーが画面で目視）と、ユーザーの判断が要る点（D1…）。
4. GPL の source は手順の参照だけで code を写さない。

## 結果（2026-10-19 P2）

- 設計: [design.md](../design.md)（版 1）。出典は Linux mainline の jz4770.dtsi・gcw0.dts・cpu-probe.c・panel-simple.c、OpenDingux の jz-6.1 の rg350.dts の連鎖、UBIBoot（`gh api` で取得、GPL は事実だけを読み code は写さない）。JZ4770 の Programming Manual は公開の所在が見つからず未確認。
- 要点: XBurst は CP0 Count が無い（timer を bsp へ移す）、PageCtrl で HPTLB を切る、I cache が VTAG。起動は UBIBoot の `UZIMAGE.BAK`（X を押して起動）。HAL の API の変更は不要（proposed/ は作らない）。
- Phase の案 p002〜p006、ユーザーの判断 D1〜D6（design.md §5）。
- 未実施: design-reviewer、実機の確認（全部）。
