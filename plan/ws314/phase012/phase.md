<!-- awesome-plan project=zedbsd record=ws314-p012 -->

# ws314-p012 解説の書き直し（Sonnet）: p011 の構成と comment に沿って人間向けの文章に

- 親: [WS314](../ws.md)。Queue: q1691 / q1691-i01（担当 P6、2026-10-30）。
- Status: cleared（2026-10-30）
- Disposition: normal

## やったこと

- `docs/howto/keiland-programming.md` を p011 の `<!-- WRITE ... -->` の指示に沿って書き直した。章の型は、例の全文 → 行番号の link と引用で
  順に読む（帰納）→ 規則（演繹、例の行への link 付き）→ 落とし穴と次の章。章の順は app → window → titlebar → kl_ui と main loop
  （後半に pace と部分の描画）→ widget → menu → その他の service。導入に例の一覧表、build と run の手順（FONT_DIR）を入れた。
- WRITE と OLD TEXT の comment は全て消した。`<!-- example: ... -->` の marker と例の code block、`keiland-examples/` の file は変えていない。
- 古い文章の誤りは持ち込まなかった: `kl_ui_window_input` の戻り値は「widget の入力の種類なら 1（再描画が要るかは言わない）」、
  `kl_ui_end` の戻りは指の接触・drag・待っている key・lit の変化・scroll の滑りと bar の消えるまで、`KL_LIST_ACTIVATED` は key が
  `kl_list_begin`、double click が `kl_list_row` から、`kl_settings_dispatch` を翻訳の節に入れた。fd の API・`kl_instance_*`・
  `kl_system_media_*` には触れていない。docs/ から plan/ へ link していない。

## 確かめた事

- `sh plan/ws314/tests/doc-examples.sh`（host）: `SUMMARY pass=40 fail=0 not-done=0`（compile clang・gcc、link、「shown」＝ block が file と byte 単位で一致、marker）。
- `python3 -I plan/tools/record-audit/links.py`: `TOTAL 0 broken in 31 files`。
- 本文の行番号の link（`#L...`）は全て、指す行を file で読んで合わせた。引用の code は行番号から機械的に作った。
- 未実施: 文章の人間による読み（ユーザーのレビュー）、QEMU での実行（例の実行は p011 で済み）。
