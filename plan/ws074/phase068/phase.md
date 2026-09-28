<!-- awesome-plan project=zedbsd record=ws074p068 -->

# ws074-p068: 外の stylesheet と rule の索引

Phase ID: `ws074-p068`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: p032

## 範囲

デモの目標（amazon.co.jp、[amazon-goal.md](../amazon-goal.md) §4 の 2）: `<link rel=stylesheet>` と `@import` の外の sheet を読み
（非同期の loader、届いたら style と layout をやり直す）、1〜2 MB の CSS でも速いように sheet ごとの rule の索引（最右の compound の
id・class・tag）を作る。

## 設計と実装

- CSS（`css/index.c` 新、`css/internal.h`、`css/parser.c`、`css/cascade.c`、`css/css.h`）:
  - rule の索引: sheet を parse した後に、各 selector を最右の compound の id → 最初の class → type（無ければ universal）で
    分類し、(種類, atom, rule, selector) で整列した entry の配列と、種類ごとの open addressing の表（atom の address が key、
    run の開始と数）を sheet の arena に作る。pseudo-element の compound は入れない（要素に一致しない）。
  - 照合: 要素の id と class の各語を `vm_atom_find_units` で atom に（atom が無ければどの selector も名指ししていない）、
    id・各 class・tag の run と universal の run を候補に集め、2 つ以上なら (rule, selector) で整列し、rule ごとに一致した
    selector の最大の specificity で宣言を足す。順序は 64 bit（sheet の番号 << 44 | rule << 16 | 宣言の番号）。
  - `@import`: style rule より前の `@import "x"`・`url(x)`・`url("x")` の URL を sheet に記録（media は p061）。
  - 公開の API: `css_sheet_create`・`_destroy`・`_import_count`・`_import`・`_rule_count`・`css_sheet_resolve_urls`
    （宣言の url() と import を呼ぶ側の resolver で絶対に）、`css_engine_add_parsed`（engine は借りるだけ、自分で parse
    した UA の sheet 等だけを解放）。
- page（`page/sheets.c` 新、`page/page.c`・`page.h`・`script.c`）:
  - page の sheet の表: 外の sheet は location で、`<style>` は text（hash と内容の比較）で一度だけ parse して page の寿命の
    間保つ。style の再計算（script の DOM の変更、sheet の到着）のたびに engine を作り直し、sheet を貸すだけ（再 parse しない）。
    使われなくなった `<style>` の sheet はその時に捨てる。
  - tree の順に `<style>` と `<link rel~=stylesheet href>`（rel に alternate があるもの、disabled は除く）、各 sheet の前に
    その import（深さ 8 まで、同じ location は一度）。
  - http・https は loader で非同期（画像と同じ）、届いたら parse、url() と import を sheet の location で解決、import の取得を
    すぐ始め、`sheets_generation` を進める → `page_needs_layout`・`page_layout`・`page_dump_style` が再計算
    （`page_update_styles`）。file・data と loader の無い時は即座に読む。
- 試験: `tests/pages/sheets.html` と `sheets-main.css`・`sheets-import.css`・`sheets-late.css`・`sheets-alt.css`（新、golden 4 つ）、
  `run-http-tests.py --async` に `async-sheets`（http の link と import の computed style）。
- 道具: `tests/amazon-capture.py`（新）: 私たちの UA でトップと検索を一度だけ取得し（2 request）、script を除いた版と、
  stylesheet・CSS の url() の画像・`<img>` を CDN から一度だけ複写して local に置き換えた版（`*-local.html`、
  `*-local-noscript.html`、srcset は除く）を `build/ws074-amazon/` に作る。比較で両 browser が同じ bytes を読み、site に再び
  問い合わせない。

## 確認（host は Debian の cc。guest は未実施、下を参照。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check（変更した source）: 新しい指摘 0（`cascade.c` の 1 件は既存）。
- 回帰（plain と ASan の両方）: golden 36/36（sheets の 4 つを追加）、host-view 59/59、host-form 28/28、run-js-tests 7/7、
  run-dom-tests 6/6、run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17（async-sheets を追加）、host-base・heap・
  interp・link・number・object・position・text 全て 0 failed。ASan の `--render` で Amazon の検索（local、script なし）と
  トップ（local、script 付き）に報告なし。
- `render-compare.py sheets.html`（800x600）: 97.58%（残りは glyph の縁）。computed style を手で確認: id の背景、compound の
  枠の色、import の `!important` が後の通常の宣言に勝つ、style 要素の import、alternate・disabled の sheet は使わない。
- Amazon（2026-09-28 20:37 に取得した capture の `*-local-noscript.html`、`live-compare.py`、1280x900、Chromium 153、
  私たちの UA）:

  | page | 前（p032 の engine） | 後 |
  | --- | --- | --- |
  | トップ | 画素 17.16%、ink 7.60% | 画素 14.97%、ink 7.86% |
  | 検索 | 画素 61.07%、ink 41.98% | 画素 75.75%、ink 19.47% |

  外の CSS で header の帯・検索の欄・結果の枠が Amazon の色と大きさになった。ink の一致が検索で下がったのは、前は両方に
  あった左上の生の text が消え、今は flex・float・inline-block が無いため配置がずれた部品が多いから（p061・p035・p060 で改善
  する見込み）。検索の `--render` は host で 1.24 s（外の CSS 約 1.9 MB を含む。ASan は 3.7 s）。
- 番号の注意: p067・p032 の数（トップ 11.56%・9.19%、検索 2.42%・39.08%）は別の日の capture の script を除いた版（外の
  CSS は Chromium だけが CDN から読む）。今回の capture で p032 の engine を測り直した値を「前」にした。

## 写真（`/home/awe/zedBSD-rpi4/.claude/worktrees/agent-ae19962dd0a453495/build/ws074-shots/`、host の証拠）

- `p068-20260928-sheets.png`（私たち | Chromium | 違い）。
- `p068-20260928-amazon-top-local-noscript-before.png`・`…-top-local-noscript.png`、`…-search-local-noscript-before.png`・
  `…-search-local-noscript.png`。

## 未実施・残り

- **guest の窓の試験（browser-p032.sh ほか）は未実施**: この worktree の image の build が target の sysroot を要し
  （`Missing target sysroot … Run 'make toolchain'`）、`make toolchain` は subagent が走らせない toolchain の操作に当たるため止めた。
  main の許可（worktree での `make toolchain`、または sysroot の用意）を待つ。
- `<link media>`・`@import` の media（p061）、`@layer` の順、Content-Type と charset の確認、head の sheet を待ってから描く
  （今は届いた時に描き直す）、`<link>` の load・error event。
