# font1 — Bold / Regular

**現行の配布TTFは [Mahora Mono / Mahora Regular / Mahora Bold](../keiland/README.md)。** 2026-10-06 userの命名指示により、承認済み0.202をKeiland Monoとし、可変ピッチのRegular/Boldを追加した。実使用の確認後、配布名をMahoraへ変更した。このdirectoryは元の字形と生成履歴を保持する。

2026-10-06 userの指示で、承認された最初の字形を **Bold** として保存し、Monaco Regularと同程度の線幅を目標に **Regular** の画像案を生成した。

- [Bold](bold/README.md): 元の95文字と原画像を変更せず保存。初案のcommitは `aad48d2a`。
- [Regular](regular/README.md): 同じ骨格を参考に線を細くした95文字の案。
- [ウェイト比較](weights-comparison.png): Monaco / font1 Regular / font1 Bold を同じ大文字の高さで比較。
- [線幅の比較値](weight-measurements.json): 保存した個別PNGのHを同じ方法で測定した値。
- [Font1-Regular.ttf](regular/Font1-Regular.ttf): Unicode対応・等幅のTrueType試作版0.203。収録はASCII 95文字。
- [RegularのSVG](regular/svg/): グリフごとのベクター輪郭。PNGの埋め込みではない。
- [TTF描画見本](regular/ttf-specimen.png): 完成したTTFをFreeTypeで描画した見本。
- [小文字の変更前後](regular/lowercase-review.png): g・s以外の小文字を共通の線幅に整理し、i・lの横棒を短縮した比較。
- [g・pの縦位置の比較](regular/vertical-position-review.png): 実使用のレビューを受けて縦位置を下げた変更前後。

各ウェイトに `glyphs/`（1024×1024・黒・透明背景、ASCII 95文字）、`ascii-overview.png`、`specimen.png`、`generated-atlas.png`、`glyphs.json`、`generation-prompt.txt` がある。SPACEは空の透明画像。

Regularの生成には組込み `image_gen` を使用。承認されたBoldの原画像を編集対象、Monacoを線幅の参照として渡した。薄い最初の候補を採用せず、同じ大文字の高さに対する縦線幅でMonacoに近い候補を選んだ。Hの縦線の幅/高さ（原画像、alpha>128で測定）は Bold 約15.5%、Regular 約12.5%、Monaco約13.0%。この数値はHの比較であり、全字形の線幅が一致するという意味ではない。

Boldの全99 PNGを移動前後のSHA256で照合し、変更がないことを確認。両方95文字の対応・非空字形・透明SPACE・画像端での欠けを確認し、一覧と文章の見本を目視確認した。

Regularはユーザーの次の指示によりSVG化し、Unicode対応のTrueType試作版へ変換済み。0.200では小文字26字のストロークを一定にし、i・lの横棒を短縮。0.201ではユーザーのレビューによりg・sを調整前の字形へ戻し、0.202では実使用のレビューによりg・pの縦位置を下げた。0.203ではqの縦位置も下げ、Keiland 3書体へ反映した。承認されたほかの字形とBoldは保持している。画像生成では細部や比率にわずかな変動があるため、RegularはBoldの輪郭の厳密な補間ではない。SVGから再ビルドする手順と確認結果はRegularのREADMEに記録している。
