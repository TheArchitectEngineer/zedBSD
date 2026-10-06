# Font1 Regular — ASCII デザイン案 02 / TrueType 0.100

user「これはBoldとして保存して、Monacoと同程度の細さにダウンしたバリエーションを生成してほしいです。」によるRegularの初案。

編集対象は `../bold/generated-atlas.png`、線幅の参照は `../../monaco-ascii/ascii-overview.png`。組込み `image_gen` へ、字形の骨格・文字対応・幅・高さ・記号の形を保ちながら線を細くするよう指定した。最終プロンプト全文は `generation-prompt.txt`。

- `glyphs/U0020.png`〜`glyphs/U007E.png`: ASCII 95文字、1024×1024、黒い字形・透明背景。SPACEは空。
- `ascii-overview.png`: 個別PNGから作成した全字形の一覧。
- `specimen.png`: 英数字、記号、コード、文章と似た文字の見本。
- `comparison.png`: 元の3書体との比較。ウェイトを同じ大文字の高さで比べる場合は `../weights-comparison.png` を参照。
- `generated-atlas.png`: 最終生成原画像。1254×1254・透明背景。
- `glyphs.json`: 文字・切り出し範囲・描画配置・出典とSHA256。

生成原画像から連結した字形をまとめて切り出し、Qや小文字の下部が行境界で切れないようにした。全字形に同じ拡大率を適用してBoldのHと大文字の高さを合わせ、各文字の字面の中心をBoldの位置へ合わせた。比較用の送り幅460.875px、ベースラインy=745はBoldと共通の仮配置。画像内の字形をグレーにしたり透明度を落として細くしたものではない。

95文字のファイル数と対応、SPACE以外の非空字形、保存PNGのサイズと透明背景、画像端で切れないことを確認。一覧と文章の見本を目視確認。これが下記SVGとTrueTypeの変換元。

## SVGとTrueTypeへの変換（2026-10-06）

user「細い方をグリフごとにSVG化してください。そのあとTrueTypeフォントにしてください。TrueTypeフォントはiso10646-1 (Unicode) のフォントにしたいです。」により、Regularの画像をベクター化してTTFを作成した。

- `svg/U0020.svg`〜`svg/U007E.svg`: ASCII 95文字のSVG。1024×1024の元画像と同じ座標系。実際の輪郭pathだけで構成し、画像の埋め込みはない。SPACEにはpathがない。
- `svg/.notdef.svg`: 未収録文字用の四角いグリフ。Unicodeの文字には割り当てず、TTFのglyph index 0に格納。
- `Font1-Regular.ttf`: family **Font1**、style **Regular**、version **0.100**、2048 units/em、全グリフの送り幅1229 unitsの等幅フォント。
- `ttf-ascii-overview.png` / `ttf-specimen.png`: 個別PNGではなく、TTF自体をFreeTypeで描画した一覧と見本。
- `font-build.json`: 生成設定・SVGとTTFのSHA256・Unicode cmap・ツール版。
- `font-verification.json`: 文字対応・等幅・元PNGとの描画比較・再ビルド一致の確認結果。
- `build-font.py`: PNG→SVG、およびSVG→TTFの再生成手順。

Potrace 1.16でPNGのalpha≥128の輪郭を曲線として抽出し、元のPNG座標へ変換した。SVGの三次Bezier曲線はfontTools 4.57.0で二次曲線へ変換（最大近似誤差1 font unit）し、TrueTypeの`glyf`へ格納した。共通の原点x=282・baseline y=745を使用し、SVGの編集が再ビルドに反映される。画像原本とBoldの画像は変更していない。

## Unicode / iso10646-1

Unicodeの文字コード **U+0020〜U+007E** に95文字を対応付けた。これはUnicode / ISO 10646の符号位置であり、Symbolフォントや独自の8bit配列ではない。Unicodeで符号化することとUnicode全字形を収録することは別で、日本語・非ASCII・制御文字はこの版に含めていない。

`cmap`はUnicode platform 0（encoding 3 / format 4、encoding 4 / format 12）とWindows platform 3（encoding 1 / format 4、encoding 10 / format 12）を持つ。どのsubtableも同じ95文字に対応する。`iso10646-1`はX11系で使う文字集合名で、TTFでは現行のUnicode cmapとして実装した。旧ISO platform 2やSymbol encodingは使わない。[OpenTypeのcmap仕様](https://learn.microsoft.com/en-us/typography/opentype/spec/cmap)を参照した。

## 再生成

リポジトリの先頭から実行する。Python 3、fontTools 4.57.0、Pillow 11.1.0、NumPy 2.2.4を使用。PNGからのトレース時だけPotrace 1.16が必要。

```sh
# 編集済みのSVGからTTFを再生成する（SVGは保持）。
python3 fonts/font1/regular/build-font.py

# 元PNGからSVGとTTFを作り直す（SVGの編集を上書きする）。
python3 fonts/font1/regular/build-font.py --trace
```

SVGはflatなpathを同じ1024×1024座標で編集する。別のSVG編集ツールを使う場合は、group transformをpath座標に展開して保存する。`font-verification.json`と描画見本は今回の成果物に対する確認結果なので、SVG編集後には更新が必要。

## 検証と制限

fontToolsでTTFを再読込してchecksum・95個のUnicode対応・96グリフ・等幅・Regularの名前とweight 400・空のSPACEを確認。Fontconfigはfamily Font1 / style Regular / format TrueType / spacing 100（monospace）/ charset `20-7e` と認識した。SVGがベクターpathであり、PNGを埋め込んでいないことも確認。

94個の非空文字をTTFから768pxで描画し、元PNGのalpha≥128と比較した。黒い領域のIoUは平均98.69%、最小97.09%、連結した部分の数はすべて一致。SVGからの再ビルドでTTFのSHA256が一致した。TTFの全95文字の一覧と12〜24pxの文章見本も目視確認した。

この版は生成画像を自動トレースした試作フォントで、手作業によるカーブ・baseline・overshootの細部調整やTrueType hintingは未実施。検証済みのSVGとTTFは使用・編集できる状態で保存した。
