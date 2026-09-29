<!-- awesome-plan project=zedbsd record=ws074-p089 -->

# ws074-p089: 百分率の高さ

Status: cleared（2026-09-30）
Disposition: normal
Parent: [WS074](../ws.md)
Queue: q501-i01

## 目的

Amazonトップのcard内で`height`・`max-height`の百分率を包含blockの確定した高さから解決し、Chromiumと同じ高さにする。float、inline-block、gridの`1fr`内を含む。

## 受け入れ条件

- 確定した高さを持つ包含blockでは、子の百分率の`height`と`max-height`をその高さから解決する。
- 高さが未確定の包含blockでは、百分率を誤ってviewportや0へ解決しない。
- 小さいhost回帰試験を追加し、ASanを通す。
- 固定captureのtopで2番目のcardがChromiumと同じ縦幅になり、p094の画素・ink一致率が低下しない。

## 初期証拠

p094の固定比較でtopは画素82.96%、ink 79.01%。Chromiumでは2番目の緑のcardが約490 px高いが、browserでは約200 pxで終わる。HTMLのcardは`height:100%`、内部にgridとpercentage heightが連鎖している。footer開始位置には約24 pxの差もある。

## 手順

1. 対象cardのlayout dumpとcomputed styleを最小fixtureに落とし、percentage heightを失うboxを特定する。
2. containing blockの高さの確定性をlayoutへ渡して解決する。
3. fixture、host ASan、固定captureを順に確認する。

## 実装

- `layout_tree`に現在のcontaining blockの高さと、その高さが確定しているかを持たせた。`height`・`min-height`・`max-height`とreplaced elementの百分率を、確定した時だけ解決する。
- block childを持つinlineを内部でblock化したwrapperはcontaining blockを作らないため、直近のblockの確定高さを通す。
- flexのstretch itemと確定した百分率高さ、grid areaのstretch itemを確定した高さでもう一度layoutし、百分率高さを子孫まで伝える。
- 確定した高さのgridでは、固定・auto行の残りを`fr`へ配分する。`repeat(2, 1fr)`のAmazonの商品gridは2行を等分する。
- 固定captureの複数行の`srcset`も確実に除き、browserとChromiumが同じ`src`を使うようにした。

## 検証と結果

- `percentage-height.html`とlayout goldenを追加。固定200 pxの50%、block化したinline wrapper、flex stretchの子孫、240 pxのgridの2つの`1fr`行を確認した。
- host plain buildはwarning 0。既存を含むlayout golden 20/20。
- ASan・UBSanの`host-relayout`はfixture、Amazon top、searchの21検査がすべてPASS。fixtureのASan renderもPASS。
- 固定比較: topは画素82.96%→84.06%、ink 79.01%→80.56%。searchは75.85%→76.32%、ink 32.97%→34.48%。2番目のcardは約490 pxを使い、4商品の2x2 gridを描画する。
- hostだけで検証。guest・bootはこのlayout Phaseでは未実施。GitHubへは未公開。
