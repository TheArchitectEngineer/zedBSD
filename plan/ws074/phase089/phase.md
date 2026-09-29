<!-- awesome-plan project=zedbsd record=ws074-p089 -->

# ws074-p089: 百分率の高さ

Status: in-progress（2026-09-30）  
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
