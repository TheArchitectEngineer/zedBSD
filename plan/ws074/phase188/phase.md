<!-- awesome-plan project=zedbsd record=ws074-p188 -->

# ws074-p188: 同一ページが観測した訪問履歴と :visited

Status: cleared（q1011-i01、2026-10-13）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074 を継承）
Related: [p100](../phase100/phase.md) の Acid3 pixel 完全一致
Prerequisites: p172・p179〜p187 cleared（p178 は部分 scope cleared）、[q1010](../phase187/evidence/20261013-q1010/report.json) の差画像。
Queue: q1011-i01。2026-10-13 ユーザーの最大90分 Queue 自動反復指示により選定。

## 観測と目的

q1010 の Acid3 差801画素は左上の `#linktest` の文字401画素と、右上の20×20疑似要素400画素。テスト48は同じページの iframe をリンク先へ遷移させ、完了時に `pending` class を外す。CSS は `#linktest:visited {color:white}` を要求するが、現 CSS parser は `:visited` を未対応として無視する。参照は白い文字であり、現行は赤い文字である。

## q1011-i01 exact scope / clearance（最大90分）

このページが成功した同一 origin の子 frame navigation で観測できた URL だけをページ内 visited 記録とし、その URL への HTML `a`/`area` の `:visited` と `:link`、履歴状態に依存しない `:any-link` を区別する。既存の URL 正規化と frame load の成功判定を用いる。ページ外・永続履歴や cross-origin 情報は参照しない。Selector の履歴 privacy はこの観測可能な範囲に閉じる。querySelector / computed style / layout の振る舞い、訪問時の再スタイルも検証する。Acid3 固有 ID や文字列による例外は置かない。

局所 host fixture に未訪問→同一 origin 子 frame 成功訪問→再スタイル、`:link`/`:visited`/`:any-link`、異 origin・失敗時の非訪問、CSSOM と DOM の結果を含める。warning0 host build、変更 C/browser component 全文規約、固定 Acid3/Acid2 再計測。WPT/runner/font/参照画像は変更しない。

Acid3 score100/100、inventory100/100、例外0、差 **801/480000** 非増加、Acid2差 **1865/120000** 非増加が条件。差0となれば p100 全体条件も照合する。範囲外変更、差増加または90分到達時は uncleared、証拠と再開条件を記録。

## 設計根拠

[Selectors 4 §8.2](https://drafts.csswg.org/selectors/#link) は `:link` と `:visited` を排他的にし、同一 origin が自ら観測した訪問に履歴情報を限定する方法を privacy 保護案として示す。今回の同一ページ内子 frame 完了はその範囲である。判定は永続履歴・他 origin の閲覧履歴へ拡張しない。

## q1011-i01 結果（2026-10-13）

**cleared**。成功した同一 origin の子 frame URL を page 内に記録し、`:visited` と `:link` を排他的に、`:any-link` を履歴と独立に評価する。alternate cascade の **color のみ**を `paint_color` へ渡し、CSSOM/DOM から読める style、geometry、selector matching は未訪問のままにした。通常テキストと疑似要素テキストの paint color を区別した。訪問履歴の増加時に style と layout を更新する。CSS/DOM の Acid3 専用例外はない。

- `run-visited-link-tests.py` は成功した同一 origin の iframe、未訪問、外部 URL、response 無しの失敗 navigation を実 HTTP で確認。visited のみ緑で描画し、他は赤。`getComputedStyle` と `querySelector(':visited')` に履歴を開示せず、`:any-link` は4件。全6項目 PASS。
- `run-frame-load-tests.py --program build/ws074-host/plain/host-frame-load` 52/52、`host-css-rule-model` 44/44。`sh plan/ws074/tests/host-build.sh plain` は warning/error0、`git diff --check` PASS。変更 C と browser component 全文規約を手動確認。`clang-format` は環境に無い。QEMU/別OS、共有 main 最新 HEAD の全体再buildは未実施。
- 固定 WPT commit `2d66b9b7998bb58c336138c178323ddee857b586`、800×600、15秒 settle、固定 font の `run-acid-tests.py --out build/ws074-acid-q1011-final`: Acid3 score100/100・inventory100/100・例外0・**RGB pixel 差0/480000（100%一致）**。q1010 の801から801減。Acid2差1865/120000は前回と不変。[report](evidence/20261013-q1011/report.json)、[Acid3参照](evidence/20261013-q1011/acid3-reference.png)、[Acid3描画](evidence/20261013-q1011/acid3-test.png)、[並列](evidence/20261013-q1011/acid3-side.png)、[Acid2並列](evidence/20261013-q1011/acid2-side.png)、style/paintを保存。最終 source の再計測で差0。
- ユーザーの Acid3 画素一致目標は達成。p100 全体は Acid2 の過去 exact 回帰と target 確認を含むため、Phase whole の clearance は別に照合する。WS074 は他の機能目標・最終全文規約 gate が残り incomplete。永続・外部履歴は今回の scope 外。

Source と証拠は独立 branch `codex/ws074-acid3` の commit `60f2c3134` に保存し、main 作業 tree へ投影。GitHub Issue/Project publication と push は保留。`master.md` は変更しない。
