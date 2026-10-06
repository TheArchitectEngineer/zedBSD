# JetBrains Mono Regular — ASCII デザイン用画像

リポジトリ内の `userland/desktop/fonts/JetBrainsMono-Regular.ttf`を直接描画したPNG。印字可能なASCIIの **U+0020〜U+007E、95文字**だけを展開。SPACEは空の透明画像。制御文字とDELは対象外。

- `glyphs/U0020.png`〜`glyphs/U007E.png`: 文字ごとの1024×1024 RGBA PNG。黒い字形、透明背景。
- `ascii-overview.png`: 全95文字の一覧。コードポイント、青いベースラインと薄い送り幅ガイド付き。
- `glyphs.json`: 文字とファイルの対応、元フォントのglyph名・送り幅・左サイドベアリング、字形範囲、入力のSHA256と出典。
- `LICENSE.txt`: 元フォントのライセンス。

768pxの文字サイズ、左の原点x=282、ベースラインy=745を全画像で統一。Monacoの個別PNGと同じ描画設定。字形を個別に拡大縮小・中央寄せ・切り抜きしていない。元フォントは等幅。字形画像にはガイドやラベルを入れていない。

Copyright: Copyright 2020 The JetBrains Mono Project Authors (https://github.com/JetBrains/JetBrainsMono)

Pillow 11.1.0 / FreeTypeで元フォントを直接描画。フォントの字形は変更していない。95文字のcmap対応、画像数・サイズ・透明度、SPACE以外の字形と画像端で切れないことを確認。一覧を目視確認。
