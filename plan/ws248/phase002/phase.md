<!-- awesome-plan project=zedbsd record=ws248-p002 -->

# ws248-p002: AX211 の driver の規約の全文の見直し

Status: in-progress（2026-10-15、q1201 の後半で P2）
Parent: [WS248](../ws.md)
Approval: 2026-10-15 ユーザー（ws.md の原文）

## 範囲

不要な関数の前方宣言の削除（intel-ax211.c ほか）と、plan/coding-style.md の全文への適合。書き方だけで動作は変えない。

## 決定（2026-10-15）

- 前方宣言: ユーザーの回答（Q1 経由、クリック）「規約どおり（P2 の案 1）」。intel-ax211.c の前半で定義の直前に 1 つずつ置かれた前方宣言（8 個）は、file の前方宣言の block にまとめる。後半の約 120 個の前方宣言の block は残す。header の未定義・未使用の宣言（intel_ax211_dma_host_scrub、intel_ax211_firmware_loader_host_parse・intel_ax211_firmware_loader_host_inspect_pnvm、drv_intel_ax211_ring_available の調査の結果による）は消す。「Begin/End consolidated」のつなぎ目の comment も整理する（file は 1 つのまま）。
- 根拠: plan/coding-style.md §3「Every non-`static inline` static function has a forward declaration.」と §14 の checklist。依頼の当初の案（定義の順で足りる物は消す）は §3 に反するため、Q1 に確かめて案 1 になった。

## 確かめ

style-check.py、build warning 0、AX211 の host 試験、preprocess の後の比較で差が宣言の削除・並べ替えなど意図した物だけ。

## 結果

（未記入）
