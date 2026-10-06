# Keiland — Mono / Regular / Bold

2026-10-06、userが承認したfont1 Regular 0.202を**Keiland Mono**へ改名し、同じ字形をもとに可変ピッチの**Keiland Regular**、初案Boldの線幅に合わせた**Keiland Bold**を作成した。3書体ともTrueType **0.300**、Unicode U+0020〜U+007EのASCII 95文字、2048 units/em。

| ファイル | full name | アプリでのfamily / style | 送り幅・ウェイト |
| --- | --- | --- | --- |
| [Keiland-Mono.ttf](Keiland-Mono.ttf) | Keiland Mono | Keiland Mono / Regular | 等幅1229 units、weight 400 |
| [Keiland-Regular.ttf](Keiland-Regular.ttf) | Keiland Regular | Keiland / Regular | 可変ピッチ、weight 400 |
| [Keiland-Bold.ttf](Keiland-Bold.ttf) | Keiland Bold | Keiland / Bold | 可変ピッチ、weight 700 |

TTFをインストールすると、アプリでは`Keiland Mono`と`Keiland`を選べる。`Keiland`のRegular/Boldは同じfamilyにまとめ、通常の太字切り替えで選べるようにした。日本語・非ASCII・制御文字は収録していない。

## 字形と間隔

- **Mono**: 承認済みの字形・送り幅を全96グリフ（.notdefを含む）で保持し、名前と関連metadataを変更。aの丸み、復元したg・s、調整済みのg・pの縦位置を保持。
- **Regular**: Monoの各輪郭を横方向へ平行移動して配置。字形の幅・高さ・線幅・縦位置を保ち、文字ごとの送り幅を設定した。標準の左右の余白は78 unitsずつ、SPACEは614 units、数字0〜9は同じ送り幅。一般的な34組にGPOS kerningを設定。
- **Bold**: Regularと共通の形をもとに輪郭を太くした。保存してある[初案Bold](../font1/bold/README.md)のHの線幅/大文字の高さを参照し、約9.593pxの輪郭拡張と約0.9633の大きさの正規化を行った。大文字の高さとbaselineを参照版に揃えている。`%`の丸を左右に少し離し、太くした後も斜線とつながらないように調整した。
- RegularとBoldの**送り幅・kerningは共通**。ウェイトを切り替えても同じ文字列の描画幅を保つ。

Regular/Monoは承認済みのTTFが生成元。Boldの編集可能な塗り輪郭は`bold/svg/`にASCII 95字と`.notdef.svg`を保存した。初案Boldの字形そのものを入れ替えたものではなく、現行Regularの字形から線幅を調整した版。以前のfont1の画像・SVG・TTFは生成履歴としてそのまま保存している。

## 見本と記録

- [3書体の文章見本](keiland-specimen.png): 納品するTTFを直接FreeTypeで描画。
- [MonoのASCII一覧](Keiland-Mono-ascii.png)、[RegularのASCII一覧](Keiland-Regular-ascii.png)、[BoldのASCII一覧](Keiland-Bold-ascii.png)。
- `font-build.json`: 元フォントと生成TTFのSHA256、Boldの調整値、kerning、BoldのSVGのSHA256。
- `font-verification.json`: 名前・Unicode・pitch・輪郭・線幅・描画の確認結果。

## 再生成

リポジトリの先頭から実行。Python 3、fontTools 4.57.0、Pillow 11.1.0、NumPy 2.2.4、pycairo/Cairo、Potrace 1.16を使用。確認にはSciPy 1.15.3とFontconfig、見本のラベルにはホストのDejaVu Sansを使用する。

```sh
# 現行font1の字形からBoldのSVGと3書体のTTFを生成する。
python3 fonts/keiland/build-fonts.py

# BoldのSVGを直接編集した場合、その編集を保ってTTFだけ再生成する。
python3 fonts/keiland/build-fonts.py --reuse-bold-svg

python3 fonts/keiland/preview-fonts.py
python3 fonts/keiland/verify-fonts.py
```

BoldのSVGは元と同じ1024×1024座標系のflatなpathで編集できる。全体の生成はBoldのSVGを上書きするので、直接編集後は`--reuse-bold-svg`を使う。MonoとRegularの字形の改訂は[font1のSVG生成手順](../font1/regular/README.md)から行う。

## 確認結果

- 3書体の名前・family/style・weight・太字flags・checksum・95個のUnicode対応・96グリフ・SPACEを確認。FontconfigでもMonoは等幅、Regular/Boldは通常の可変ピッチのTrueTypeとして認識。
- Monoの全輪郭・送り幅は承認済み0.202と一致。Regularの全輪郭はMonoから横方向へ平行移動しただけであることを照合。
- 全文字の文字枠・ascent/descentへの収まりと、非空文字の描画を確認。
- Regular/Boldの全94非空文字で連結部分・穴の数が一致。数字・英字・記号の穴と独立した点を保持。
- TTFから768pxで描画したHの縦線幅はMono/Regularが64・64px、高さ503px、Boldが80・80px、高さ502px。Boldの線幅/高さは**15.936%**、初案Boldの参照値は**15.900%**。
- Pillow/RAQMでMonoの`iii`と`MMM`は同じ幅、Regularは`iii`の方が狭くなること、`AV`のkerning、Regular/Boldの文章の描画幅の一致を確認。
- SVGを保持した再ビルドで3書体のTTFのSHA256が一致。全ASCII一覧と文章見本を目視確認。

TrueTypeの手作業によるhintingは未実施。アプリでの使用感はユーザーが確認する。
