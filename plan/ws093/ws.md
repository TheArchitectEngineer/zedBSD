<!-- awesome-plan project=zedbsd record=ws093 -->

# WS093: Files から app の起動（file の種類と app の対応）

<!-- awesome-plan-current:start -->
Status: completed（2026-09-29）
Primary Milestone: MG006
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし（main の依頼で worktree `wt/ws093` の subagent が p001〜p004 を実行）
Resume point: —（完了の処理: Phase の directory の削除と試験の `plan/tools/` への移動は main）
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

「ファイラーからアプリの起動（画像、テキストをダブルクリック）」

- Files で file を double click（touch では double tap）すると、種類に応じた app（画像 → Image Viewer、text → Text Editor、PDF → PDF Viewer、
  HTML → ブラウザ）がその file を開いて起動する。対応の表（system の既定と利用者の上書き）、「このアプリで開く」の menu。WS071 は変えず、この WS で Files に足す。

## 結果

設計は [design.md](design.md)。WS071 の Files がすでに持つ開く仕組み（利用者の一覧・system の一覧・組み込みの表、Open With の menu、
double click・Enter・double tap で既定の way）の上に足した。

- **既定の対応**（`files/apps.c` の組み込みの表 = system の既定）: png・jpeg・gif → Image Viewer（`/bin/imageview %f`）、text 系（`text/*`・json・
  xml・shellscript・javascript）→ Text Editor（`/bin/textedit %f`）、html → Browser（`/bin/browser %f`）、pdf → PDF Viewer（従来）。program の
  無い image では従来の既定（`needs`）。他の画像形式は Quick Look のまま。
- **利用者の上書き**: File menu と context menu の「Always Open With」（その file の way、区切り、「Use System Default」）。選ぶと利用者の一覧
  `~/.config/keiland/open-with`（`$XDG_CONFIG_HOME` があればその下）の先頭に `# set by Files` と `TYPE<TAB>NAME<TAB>COMMAND` を書き（同じ type の
  Files の行だけを置き換え、利用者の行は残す。新しい file に書いて rename）、その file をその way で開く。Use System Default は Files の行を消す。
  「このアプリで開く」（Open With）は WS071 の menu がそのまま担う。
- 規約: `plan/coding-style.md` の全文と照合（p004）、style-check 0 件。

確認（すべて QEMU、Venus）: host の `plan/tools/files/host-default.sh`（21 件）・`plan/tools/files/host-model.sh` PASS、build の warning 0、guest の
`open-guest.sh` の mouse（全ての種類の double click と Enter）・always・info・注入の touch の double tap、boot test PASS。画面は worktree の
`build/ws093-shots/`（p004 の回帰は `p004/`）。

## 制限・移管

- 実機（i915、touch panel）は未確認。
- 注入の touch の長押しからの Always Open With は未実施（context menu 自体は WS071 の長押しで開く）。
- 情報の card の opener の pill は選んだ way で開くだけ（既定にはしない）。
- system の一覧 `/etc/keiland/open-with` を image に入れることは範囲外（組み込みの表が system の既定を担う）。
- `apps_parse_line`（WS071）の 3 節の条件を 1 行に置く箇所は WS093 の範囲外のまま。
- `plan/tools/files/host-build.sh` は p003 で libkeiland の gesture.c・scroll.c・motion.c を build するように直した（main の許可）。

## Phase の一覧

| Phase | 内容 | 結果 |
| --- | --- | --- |
| [ws093-p001](phase001/phase.md) | 設計（[design.md](design.md)） | cleared（2026-09-29） |
| [ws093-p002](phase002/phase.md) | 既定の対応（画像・text・HTML）、double click・Enter・double tap の確認 | cleared（2026-09-29） |
| [ws093-p003](phase003/phase.md) | Always Open With・Use System Default、利用者の一覧への書き込み、host-build.sh の修正 | cleared（2026-09-29） |
| [ws093-p004](phase004/phase.md) | 全文の規約と回帰 | cleared（2026-09-29） |

## 試験（完了の処理の候補）

| file | 役割 | 候補 |
| --- | --- | --- |
| `tests/open-guest.sh` | Venus の guest の Files からの起動（mouse・always・info・touch の手順） | `plan/tools/files/` へ移して Tools 節に登録（`files-open.sh` 等の名前、`make-images.py` は `plan/tools/imageview/` を参照、`make-touch-pdf.py` は `plan/ws081/tests/` を参照している点に注意） |
| `tests/host-default.c`・`host-default.sh` | Always Open With の利用者の一覧の書き込み（host） | `plan/tools/files/` へ移す、または `host-model.c` の section に取り込む |
