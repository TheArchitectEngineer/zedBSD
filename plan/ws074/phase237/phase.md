<!-- awesome-plan project=zedbsd record=ws074-p237 -->

# ws074-p237: `<input>`・`<button>` の border-radius（google.com の「Google 検索」）

B1 (2026-10-15)。Status: host の確認まで cleared 相当（QEMU・実機は未実施）。p236 の続き。

## 観測と原因

google.com の「Google 検索」「I'm Feeling Lucky」（`input[type=submit]`、`border-radius:8px`）が Chrome は角丸、ours は四角。`paint/list.c` の `list_control`（`<input>` など replaced の form control）が背景を `list_rect`、枠を `list_borders` で描き、角丸を見ていなかった（`<button>` 要素は普通の box として描くので丸かった）。再現: [pages/input-radius.html](../tests/pages/input-radius.html)。

## 変更

`list_control`: `list_border_shape` が丸いと言うとき、背景を `list_round_fill`、（プラットフォームの 1px 枠でない）style の枠を `list_round_borders` で描く。

## 確かめ（host）

- 再現頁の submit・button・text が角丸になった。固定 WPT reftest の前後（同 revision、前は p236 直前の binary）: css-forms 3/13、css-ui 65/116、css-backgrounds 179/602（error 13）とも前後同数、PASS↔FAIL の差 0。build warning 0。
- 未実施: QEMU・実機。

## 残り

- 「I'm Feeling Lucky」が Chrome より約 2px 上（inline-block の input の baseline と行の高さ）、`<input>` の box-shadow（hover）。
