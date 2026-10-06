# font1 — Bold / Regular

2026-10-06 userの指示で、承認された最初の字形を **Bold** として保存し、Monaco Regularと同程度の線幅を目標に **Regular** の画像案を生成した。

- [Bold](bold/README.md): 元の95文字と原画像を変更せず保存。初案のcommitは `aad48d2a`。
- [Regular](regular/README.md): 同じ骨格を参考に線を細くした95文字の案。
- [ウェイト比較](weights-comparison.png): Monaco / font1 Regular / font1 Bold を同じ大文字の高さで比較。
- [線幅の比較値](weight-measurements.json): 保存した個別PNGのHを同じ方法で測定した値。

各ウェイトに `glyphs/`（1024×1024・黒・透明背景、ASCII 95文字）、`ascii-overview.png`、`specimen.png`、`generated-atlas.png`、`glyphs.json`、`generation-prompt.txt` がある。SPACEは空の透明画像。

Regularの生成には組込み `image_gen` を使用。承認されたBoldの原画像を編集対象、Monacoを線幅の参照として渡した。薄い最初の候補を採用せず、同じ大文字の高さに対する縦線幅でMonacoに近い候補を選んだ。Hの縦線の幅/高さ（原画像、alpha>128で測定）は Bold 約15.5%、Regular 約12.5%、Monaco約13.0%。この数値はHの比較であり、全字形の線幅が一致するという意味ではない。

Boldの全99 PNGを移動前後のSHA256で照合し、変更がないことを確認。両方95文字の対応・非空字形・透明SPACE・画像端での欠けを確認し、一覧と文章の見本を目視確認した。

この段階は字形検討用の画像案。画像生成では細部や比率にわずかな変動があるため、RegularはBoldの輪郭の厳密な補間ではない。両ウェイトの輪郭・メトリクスを確定してTrueType化する作業は別途必要。
