<!-- awesome-plan project=zedbsd record=ws175 -->
# WS175: Notes で PDF の画像と文字を編集する

Master: [master](../master.md)
Status: incomplete（2026-10-06: p001 設計 cleared、p002a・p003 を実装。次は p006（libz-compat の許可待ち））
Primary Milestone: MG006

## 由来

ユーザー（2026-10-06）「Notesアプリの機能追加をお願いしたいです。PDF内の画像、テキストを編集できるようにしたいです。コメント的にペンを入れるだけでなくて、画像の位置を変えたり、サイズを変えたり、画像そのものを差し替えたり、新たに画像を入れたり。テキストも編集したり、削除したり、新たに挿入したり、フォントを変えたり。Acrobatみたいになんでもできる必要はないんです。ただ、基本的な編集ができればうれしいと思います。複数ページあるPDFも対応していなければ対応したいです。」

## 目標（単一）

Notes で PDF を開き、基本の編集ができて PDF として保存できる:

- 画像: 移動・大きさの変更・差し替え・新しい画像の挿入・削除。
- 文字: 既存の文字の編集・削除・新しい文字の挿入・font の変更。
- 複数の頁の PDF（頁の移動、頁ごとの編集）。
- 今の pen の書き込み（注釈）は保つ。

範囲外: Acrobat の全機能（form・署名・OCR・頁の並べ替え以上の頁の操作など。必要なら別に）。

## Phase

| Phase | 内容 | 状態 | 依存 |
| --- | --- | --- | --- |
| [ws175-p001](phase001/phase.md) | 設計: libpdf の書き出し（増分の更新か作り直しか）、content stream の text・image の object の取り出しと書き換え、font（埋め込みの subset の扱い、置き換えの font）、複数頁、Notes の UI、試験。design-reviewer | cleared（2026-10-06 Q1、D1〜D7 は推奨どおり） | — |
| [ws175-p002](phase002/phase.md) | libpdf の走査: p002a 画像と図形（先の段）、p002b 文字の行と Unicode の抽出（後の段） | p002a 実装済み（2026-10-06、host PASS）、p002b 未着手 | p001 |
| [ws175-p003](phase003/phase.md) | libpdf の画像・図形の editor（削除・移動・大きさ・差し替え・挿入）、content の組み立て、preview、update の PLACE_EDIT、名前の接頭辞 [M4] | 実装済み（2026-10-06、p003a・p003b、host PASS）、cleared の判定待ち | p002a |
| p004〜p011 | design.md §10 のとおり（p006 deflate と画像の取り込み → p007 Notes の model → p008 Notes の UI → p010 T1 → p011 規約、文字は p004・p005・p002b） | 未作成 | — |

見積もり: design.md §10（全体 17.5〜21.5 LW、画像を先にする段は約 10 LW）。Q1 の当初の概算は 6 LW。
