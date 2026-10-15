<!-- awesome-plan project=zedbsd record=ws248 -->

# WS248: Intel AX211 の driver の出典の表記と規約の整理

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-15 作成、P2 が p001 を実行中）
Primary Milestone: MG005
Parent: [Master](../master.md)
Related: [Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」、[coding-style.md](../coding-style.md)、[WS004](../ws004/ws.md)（AX211 の試験は plan/ws004/tests/）、[WS247](../ws247/ws.md)（nvrtx での同じ整理）
Resume point: q1201（P2）で p001 → p002。
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-15）

「Intel AX211ドライバが、ガードレールに沿っていない部分がありそうです。引用は引用部分の直前でブロック単位で行う、ファイル先頭にまとめて引用元を書かない、referenced:形式で引用元を書く、あとはコーディングスタイルの修正が必要ですね。intel-ax211.cには不要な関数宣言が多数ありますし。これもWSを作ってコードを整理しましょう。サブエージェントを1つ割り当ててください。」

## 対象

`src/drivers/wifi/intel-ax211/`（約 32,000 行、43 file）と `include/drivers/wifi/intel-ax211/`。動作は変えない（comment・宣言・書き方だけ）。

## 完了の条件

- file の先頭の comment に、参考にした source・引用したデータ・派生の宣言・出典の一覧が無い（例: intel-ax211.c の先頭の「Sources of the values」の段落）。
- 引用した値・表は、その block の直前の複数行の comment に「Value referenced: <版> <path> (<名前>)」（複数は「Values referenced:」）、参考にした手順は「Procedure referenced: <版> <path> (<関数>)」。今の「Source: …」の形は改める。
- 引用以外の comment の本文に出典の名前（Linux・iwlwifi・OpenBSD・iwx など）を書かない（nvrtx の WS247 と同じ扱い）。
- 不要な関数の前方宣言（intel-ax211.c に多数）を除き、plan/coding-style.md の全文に合わせる（`plan/tools/style-check.py` で機械的な規則を確かめ、残りは読んで確かめる）。
- build（warning 0）と AX211 の host 試験（plan/ws004/tests/run-intel-ax211-*.sh）が今と同じ結果。preprocess の後の比較で comment 以外の差が意図した物（宣言の削除・並べ替え）だけ。

## Phase

| Phase | 内容 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 出典の表記: file の先頭の出典を除き、block ごとの Value(s)/Procedure referenced: へ。本文の出典の名前の除去 | in-progress（q1201） |
| [p002](phase002/phase.md) | 規約の全文の見直し: 不要な前方宣言の削除と coding-style.md の全文 | planned（q1201 の後半） |
