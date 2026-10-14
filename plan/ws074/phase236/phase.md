<!-- awesome-plan project=zedbsd record=ws074-p236 -->

# ws074-p236: `<button>` の中身を縦に中央に置く（google.com の「AI モード」）

B1 (2026-10-15)。ベータ2の目標「実 site を Chrome に近づける」。Status: host の確認まで cleared 相当（QEMU・実機は未実施）。p235 の続き。

## 観測

google.com の live を Chrome と並べた（1280x2400、[画像](../../../build/review/b1p236-google.com-top.png)）。ほぼ一致する中で、検索 box の右の「AI モード」の pill（`button.plR5qb`、`height:36px`、中に flex の `div.u4Uk3c`）の icon と文字だけが Chrome より約 6px 上に出ていた。

## 原因

`<button>` は engine では ua の inline-block にすぎず、`height`・`min-height` が中身より大きいとき、中身は上に詰まっていた。Chrome は button の中身を余白の中央に置く。最小の再現: [pages/button-center.html](../tests/pages/button-center.html)（高さ 36px の button に、abspos の背景＋flex の中身／flex の中身／inline-block／文字だけ）。

## 実装

`layout/block.c` `block_center_button`: HTML の `<button>` の最終の高さが中身の高さより大きいとき、差の半分だけ lines と in-flow の子の y、out-of-flow の子の static_y を下げる（abspos の `inset:0` の背景は動かない）。

## 確かめ（host）

- 再現頁 4 箱とも Chrome と縦位置が一致（[ours | Chrome](../../../build/review/b1p236-bc-both.png)）。google.com live: ink 79.14%→80.68%、全体 99.87%→99.88%（[前後](../../../build/review/b1p236-google-after.png)、AI モードは Chrome と同位置）。
- 固定 WPT reftest の前後（同じ revision、前は main の binary）: css-forms 3/13・css-flexbox 607/945・css-ui 65/116・css-align 23/61 とも前後同数、PASS↔FAIL の差 0。css-grid 406/1509 も前後同数（差 0）。
- build warning 0。host-css-rule-model・host-sticky ok、Acid3 score 100・差 0、Acid2 差 873（不変）。
- 未実施: QEMU・実機。

## 残り

- google.com: 「Google 検索」「I'm Feeling Lucky」の button が Chrome より約 2px 高さが違う、caret（入力欄の点滅の線）が静止画に出ない。
- yahoo.co.jp: 差の大半は広告・時刻・popup の違い。
- 前の候補: inner scroll container、子つき rotate・skew、flex item の aspect-ratio、GPU 経路の sticky。
