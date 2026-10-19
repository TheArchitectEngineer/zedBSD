<!-- awesome-plan project=zedbsd record=ws273-p001 -->

# ws273-p001 RG350（Ingenic JZ4770）の移植の設計

- 親: [ws273](../ws.md)、Queue: q1317 / q1317-i01（P2、2026-10-19）
- Status: in-progress

## 目的

Anbernic RG350 の実機で zedBSD を起動する道を決め、`plan/ws273/design.md` に書く。コードは書かない。

## 受け入れの条件

1. 公開の資料（JZ4770 の programming manual、Linux mainline の `ingenic` の DT と driver、OpenDingux の起動の道、RG350 の基板の情報）を調べ、CPU（MIPS32r2 の XBurst、FPU の有無、cache・TLB）・endian・memory・起動（SD の boot loader、U-Boot の有無）・LCD（LCDC と panel）・SD（MSC）・入力（GPIO のボタン、ADC）・UART の取り出しを出典付きで書く（未確認は未確認と書く）。
2. WS271 の MIPS32 malta の HAL（src/hal/mips32/）との差（BSP の分け方、boot の handoff、早期の console）と、HAL の API の変更の要否（要るなら差分の案を proposed/ に置くだけ）。
3. 実装の Phase の分割（依存・LW の見込み・実機の確認の点。実機はユーザーが画面で目視）と、ユーザーの判断が要る点（D1…）。
4. GPL の source は手順の参照だけで code を写さない。
