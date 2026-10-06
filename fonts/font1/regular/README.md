# Font1 Regular — ASCII デザイン案 03 / TrueType 0.200

## 小文字の調整（2026-10-06、0.200）

user「大文字はOK」「i,lは水平方向のバーを短く」「小文字はすべて、ストロークを一定にする」により、Regularの小文字a〜zの画像・SVG・TTFを更新した。a・gの一階建ての形と字形の概略の比率を保ち、曲線・縦線・横線・斜線を共通の**62px**のストロークで組み直した（1024×1024のグリフ画像座標）。接合部分ではストロークが合流する。i・jの独立した丸い点は直径78px。

- `lowercase-strokes.json`: 小文字26字の中心線Bezier pathと共通線幅。a・g・qの斜めの丸い部分も一定の太さで作成。
- `refine-lowercase.py`: Cairoで同じ幅のストロークを展開し、Potraceで塗り輪郭へ変換。最終SVGからPNGを描画し、画像とSVGの形を揃える。
- `lowercase-review.png`: 0.100と0.200のTTFを同じサイズ・送り幅で描画した変更前後の見本。
- `glyphs/`・`svg/`: 小文字26字を更新。大文字・数字・記号・SPACEのPNG/SVG、Boldの全ファイルは変更前のSHA256と一致。TTFも小文字以外の輪郭・送り幅が変更前と一致。
- `Font1-Regular.ttf`: 更新したSVGから再ビルドした0.200。UnicodeのASCII 95文字・等幅の設定は共通。

`generated-atlas.png`と`generation-prompt.txt`は初案の生成履歴として保存。現在の小文字の正本は編集用の`svg/`、その構成用データは`lowercase-strokes.json`で、原画像には今回の編集を反映していない。個別画像と各見本は更新済み。詳細な確認値は`font-verification.json`を参照。

```sh
# 中心線と線幅を編集した後、小文字のPNG/SVGを再生成する。
# 小文字のSVGへ直接加えた編集は上書きされる。
python3 fonts/font1/regular/refine-lowercase.py
python3 fonts/font1/regular/build-font.py
python3 fonts/font1/regular/preview-font.py
```

小文字の生成はPython 3・fontTools・Pillow・NumPyに加えてpycairo・CairoとPotrace 1.16を使用。見本のラベルにはホストのDejaVu Sansを使用。`preview-font.py --before-font /path/to/previous.ttf`で変更前後の見本も更新できる。今回の比較元TTFはcommit `ef9c3295` の0.100。

確認ではiの上側の横棒が164→104px、下側が165→126px、lの上側が180→113px、下側が183→135pxになった（横棒の中間の行、alpha≥128）。直線部分の代表値はi・l・h・n・mがすべて62px。全94非空文字のPNG対TTF描画のIoUは平均98.78%、最小97.52%、連結部分の数は全文字一致。小文字以外の70グリフ（.notdefを含む）のTTF輪郭と送り幅も変更前と一致した。

## 初案と0.100の履歴

user「これはBoldとして保存して、Monacoと同程度の細さにダウンしたバリエーションを生成してほしいです。」によるRegularの初案。

編集対象は `../bold/generated-atlas.png`、線幅の参照は `../../monaco-ascii/ascii-overview.png`。組込み `image_gen` へ、字形の骨格・文字対応・幅・高さ・記号の形を保ちながら線を細くするよう指定した。最終プロンプト全文は `generation-prompt.txt`。

- `glyphs/U0020.png`〜`glyphs/U007E.png`: ASCII 95文字、1024×1024、黒い字形・透明背景。SPACEは空。
- `ascii-overview.png`: 個別PNGから作成した全字形の一覧。
- `specimen.png`: 英数字、記号、コード、文章と似た文字の見本。
- `comparison.png`: 元の3書体との比較。ウェイトを同じ大文字の高さで比べる場合は `../weights-comparison.png` を参照。
- `generated-atlas.png`: 初案の最終生成原画像。1254×1254・透明背景（現在の小文字の編集前）。
- `glyphs.json`: 文字・切り出し範囲・描画配置・出典とSHA256。

生成原画像から連結した字形をまとめて切り出し、Qや小文字の下部が行境界で切れないようにした。全字形に同じ拡大率を適用してBoldのHと大文字の高さを合わせ、各文字の字面の中心をBoldの位置へ合わせた。比較用の送り幅460.875px、ベースラインy=745はBoldと共通の仮配置。画像内の字形をグレーにしたり透明度を落として細くしたものではない。

95文字のファイル数と対応、SPACE以外の非空字形、保存PNGのサイズと透明背景、画像端で切れないことを確認。一覧と文章の見本を目視確認。これが下記SVGとTrueTypeの変換元。

## SVGとTrueTypeへの変換（2026-10-06）

user「細い方をグリフごとにSVG化してください。そのあとTrueTypeフォントにしてください。TrueTypeフォントはiso10646-1 (Unicode) のフォントにしたいです。」により、Regularの画像をベクター化してTTFを作成した。

- `svg/U0020.svg`〜`svg/U007E.svg`: ASCII 95文字のSVG。1024×1024の元画像と同じ座標系。実際の輪郭pathだけで構成し、画像の埋め込みはない。SPACEにはpathがない。
- `svg/.notdef.svg`: 未収録文字用の四角いグリフ。Unicodeの文字には割り当てず、TTFのglyph index 0に格納。
- `Font1-Regular.ttf`: family **Font1**、style **Regular**、2048 units/em、全グリフの送り幅1229 unitsの等幅フォント。初回0.100、現行0.200。
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

0.100の確認では94個の非空文字をTTFから768pxで描画し、元PNGのalpha≥128と比較した。黒い領域のIoUは平均98.69%、最小97.09%、連結した部分の数はすべて一致。現行0.200も同じ方法で再確認し、結果を`font-verification.json`へ更新した。SVGからの再ビルドでTTFのSHA256が一致し、TTFの全95文字の一覧と12〜24pxの文章見本も目視確認した。

この版は試作フォントで、小文字以外は生成画像のトレースを保持している。小文字のカーブと線幅は今回調整したが、全体のbaseline・overshootの細部調整やTrueType hintingは未実施。検証済みのSVGとTTFは使用・編集できる状態で保存した。
