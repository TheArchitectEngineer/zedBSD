<!-- awesome-plan project=zedbsd record=ws074-p095 -->

# ws074-p095: Amazon検索欄の文字の位置

Status: cleared（2026-09-30）
Disposition: normal
Parent: [WS074](../ws.md)
Queue: q504-i01

## 目的

Amazon.co.jpの検索欄で、入力欄とplaceholderの縦横の位置をChromiumへ合わせる。検索欄だけの補正ではなく、flex itemのbox sizingとform controlの`text-indent`を一般のCSS指定として扱う。

## 受け入れ条件

- `box-sizing:border-box`、`height:38px`、上下paddingを持つflex itemのinputが38pxのborder boxになる。
- inputの値とplaceholderへ`text-indent`を適用し、caretとclickから求める文字位置も同じ原点を使う。
- 固定したAmazon topで検索文字の範囲がChromiumと縦横とも一致する。
- Chromiumと共通の回帰、plain・ASanのhost回帰、規約とdiffの検査を通す。

## 前提・規約

- 前提はform control（p032）、flexbox（p035）、固定したAmazon比較と後続script（p092・p094）。いずれもclearedで、未決の判断は無い。
- [Guardrail](../../guardrail.md)と[コーディング規約](../../coding-style.md)を適用する。HAL、公開ABI、vendor sourceは変更しない。

## 初期証拠

- `build/ws074-compare-p092-stack/top-ours.png`では検索文字が`x=427..566, y=31..45`、同じ入力のChromiumは`x=435..574, y=22..36`だった。
- Amazonのinputは`box-sizing:border-box; height:38px; padding:7px 10px 10px 0; text-indent:8px`。browserではflexの最終配置がbox sizingを一時的にcontent boxへ変え、cross axisの高さまで38+17=55pxにしていた。
- 高さを直した中間画像では`y=22..36`になったが、`text-indent`を未実装のため`x=427..566`のままだった。

## 実装と結果（2026-09-30）

- `flex_lay_sized()`はitemの元のbox sizingを保つ。flex algorithmのmain axisのcontent sizeをborder-boxのstyleへ渡す場合だけ、その軸のborderとpaddingを加える。これでcross axisの明示的な38pxをborder boxとして解釈する。
- CSSへ`text-indent`のlength・percentage、継承、`getComputedStyle()`を追加した。form controlは値とplaceholderの描画原点、scroll、caret、clickの座標に同じindentを使う。
- Chromiumと共通のgeometry fixtureでinputは`300 200 200 38`、computed styleは`textIndent == "8px"`。form fixtureは8px移動したplaceholderのpaint座標も検査する。
- 最終画像`build/ws074-compare-p095-final/top-ours.png`の検索文字は`x=435..574, y=22..36`でChromiumと一致した。比較は72.88%/ink 65.76%、Uncaught 4。
- host buildはplain・ASanともwarning 0。DOM 22/22、form 29 checks、position 22 checksをplain・ASanで通した。`layout/flex.c`ほか変更行のstyle-checkに新しい指摘0、`git diff --check` 0。guest・bootはこの表示修正では実施していない。GitHubへは未公開。
- 実装と回帰はcommit `3327f8ef`（`Browser`）。
