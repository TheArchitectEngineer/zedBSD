# p178 q1002-i01 の承認範囲

2026-10-13 22:39 JST、このチャットでユーザーが以下の exact scope へ「自動で承認します」と回答。[Queue q1002](../queue.md)に選定・開始。原提案の範囲・停止条件は変えない。

## 対象と選定理由

固定 WPT `2d66b9b7998bb58c336138c178323ddee857b586`、q1001 の同一 runner/font/800×600/15000 ms 条件。p179 の参照画像修復後も Acid3 に差56647/480000が残る。参照の `#b` は x=625・幅0の絶対配置、子 `div` は `position:absolute; right:0; width:auto`。現在の参照 layout/paint では子の「100/100」が x=625 から右へ出る。一方、test の得点は x=231.80 から表示される。[CSS 2.1 §10.3.7](https://www.w3.org/TR/CSS2/visudet.html#abs-non-replaced-width) では left と width が auto で right が確定した場合、幅は shrink-to-fit、左位置は右端から逆算する。参照の得点領域を局所対象とする根拠は [p179 の paint](../phase179/evidence/20261013-q1001/acid3-reference.paint) と q1001 の test paint/layout（`build/ws074-acid-q1001/`、独立 tree）。

## 提案する Queue q1002-i01 / p178 再試行

最大90分。`libbrowser/layout/` の絶対配置・auto幅・right指定の一般的な実装を最小ページで切り分ける。原因がその組合せに確定した場合だけ、該当 source を最小修正し、参照の得点文字が右端 x=625 に収まる位置へ移ることを layout/paint と画像で確認する。`host-position` を含む焦点を絞った host 試験、変更箇所の host build warning0、C全文・browser component 規約、同条件の Acid3/Acid2 を実行する。Acid3 の得点100/100・inventory100/100・例外0を維持し、全体差56647と Acid2差1865を増やさない。`reference.png`、WPT本文、runner の比較方式は変更しない。

原因が別、複数 module の設計変更が必要、差が減らない、または90分に達したら当 attempt は uncleared とし、証拠と再開条件を残す。p178 の q1000-i01 履歴は保持し、この局所試行で p100 の全画面差0を主張しない。
