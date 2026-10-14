<!-- awesome-plan project=zedbsd record=ws074-p233 -->

# ws074-p233: yahoo.co.jp の feed の画像まわりの枠（transform: scale の hairline）

B1 (2026-10-14)。ユーザーの指摘（原文「画像のブロック要素でしょうか、なにか画像が表示されなかったときみたいなaltの表示がある枠が、ニュースフィードに出ています」）。Status: host の確認まで cleared 相当（QEMU・実機は未実施）。p232 の続き。

## 原因（観測した順）

1. feed の画像はすべて取れて描かれていた（`BROWSER_NET_DUMP` の body・画像の欄を確認。alt を描く経路は engine に無い）。ours にだけある物は、各記事の画像の周りに出る **細い灰色の長方形の枠**（画像の 2 倍の 160x160 と、大きい画像では 400 幅）で、Chromium には無い。
2. 枠の正体は bundle の CSS の hairline の技法: `span:before{position:absolute;top:0;right:-100%;bottom:-100%;left:0;content:"";border:1px solid rgba(0,0,0,.1);transform:scale(.5);transform-origin:0 0}`。200% x 200% の箱を `scale(.5)` で等倍に戻し、0.5px の線にする。この engine は `transform` の **translate だけ**を適用し scale を無視していたため、倍の大きさの枠がそのまま描かれていた。

## 実装

- `css/values.c`: `transform` が `scale`・`scaleX`・`scaleY`・`scale3d`（数と百分率、掛け合わせ）を読み、`transform-origin`（`-webkit-` 付きも、1〜3 成分、left/right/top/bottom/center・長さ・百分率）を新しい shorthand として読む。`inherit`/`initial` は 4 つの longhand に及ぶ。
- `css/internal.h`・`css/css.h`・`css/cascade.c`: longhand `CSS_PROP_TRANSFORM_SCALE_X/Y`・`CSS_PROP_TRANSFORM_ORIGIN_X/Y`、style の `transform_scale[2]`・`transform_origin[2]`（初期値 1・50%）、継承・initial。
- `layout/box.c` `box_apply_scale`: layout の最後に、**子も行も持たない**箱（上の枠のような空の箱と replaced）だけを、origin を中心に縮める・拡げる。border は 1px 未満にならない。子のある箱の scale（subtree 全体の拡縮）は未実装（従来どおり無視）。
- 試験: [transform-scale-hairline.html](../tests/transform-scale-hairline.html)（pseudo と実 element の二通り。直す前は 160x160 の枠が出る、後は 80x80 に重なる）。

## 確かめ（host）

- yahoo.co.jp live（1024x3400、feed の記事の周り）。前: [before](../../../build/review/b1-yahoo-hairline-before-crop.png)、後: [after](../../../build/review/b1-yahoo-hairline-after-crop.png)（全体 [前](../../../build/review/b1-yahoo-hairline-before.png)・[後](../../../build/review/b1-yahoo-hairline-after.png)、左 ours・中 Chromium・右 差）。画像まわりの余計な枠は無くなり、Chromium と同じ見た目。広告が request ごとに違うので全体の数は動く（前 ink 37.42%・後 31.40% は広告の違いを含み、枠の効果だけの数ではない）。
- build warning 0（`sh plan/ws074/tests/host-build.sh plain`）、host JS 17/17、DOM 23/23、http 15/15、loader 11/11、fetch `--async` 38/38、CSS rule model 51/51、Grid の test-grid-*.py 23/25（`abspos-static`・`auto-track-css` は p232 から失敗、この変更の前の binary でも同じ）。Acid3 score 100・差 0、Acid2 差 873（不変）。固定 WPT 1,455 ID: 前（HEAD の binary）483 pass・callback 10,983、後 483 pass・callback 10,983、ID 単位の差 0。
- 未実施: QEMU・実機。google.com の再確認は scale を使わない変更なので host の試験のみ（live は未再測定）。

## 残り

- alt の text と壊れた画像の icon は engine に描く経路が無い（画像が取れない時は何も出ない。Chromium は寸法がある時だけ icon と alt を出す）。今回の指摘の原因ではなかったので未着手。
- 子を持つ箱の `scale()`、rotate・skew などは未実装。
