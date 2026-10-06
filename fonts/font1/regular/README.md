# font1 Regular — ASCII デザイン案 02

user「これはBoldとして保存して、Monacoと同程度の細さにダウンしたバリエーションを生成してほしいです。」によるRegularの初案。

編集対象は `../bold/generated-atlas.png`、線幅の参照は `../../monaco-ascii/ascii-overview.png`。組込み `image_gen` へ、字形の骨格・文字対応・幅・高さ・記号の形を保ちながら線を細くするよう指定した。最終プロンプト全文は `generation-prompt.txt`。

- `glyphs/U0020.png`〜`glyphs/U007E.png`: ASCII 95文字、1024×1024、黒い字形・透明背景。SPACEは空。
- `ascii-overview.png`: 個別PNGから作成した全字形の一覧。
- `specimen.png`: 英数字、記号、コード、文章と似た文字の見本。
- `comparison.png`: 元の3書体との比較。ウェイトを同じ大文字の高さで比べる場合は `../weights-comparison.png` を参照。
- `generated-atlas.png`: 最終生成原画像。1254×1254・透明背景。
- `glyphs.json`: 文字・切り出し範囲・描画配置・出典とSHA256。

生成原画像から連結した字形をまとめて切り出し、Qや小文字の下部が行境界で切れないようにした。全字形に同じ拡大率を適用してBoldのHと大文字の高さを合わせ、各文字の字面の中心をBoldの位置へ合わせた。比較用の送り幅460.875px、ベースラインy=745はBoldと共通の仮配置。画像内の字形をグレーにしたり透明度を落として細くしたものではない。

95文字のファイル数と対応、SPACE以外の非空字形、保存PNGのサイズと透明背景、画像端で切れないことを確認。一覧と文章の見本を目視確認。全字形の輪郭・ストローク・字面の厳密な調整はTrueType化前に行う。
