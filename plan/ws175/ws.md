<!-- awesome-plan project=zedbsd record=ws175 -->
# WS175: Notes で PDF の画像と文字を編集する

Master: [master](../master.md)
Status: planning（2026-10-06 追加、ベータ2（Q1 の案、ユーザーの確認待ち））
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
| [ws175-p001](phase001/phase.md) | 設計: libpdf の書き出し（増分の更新か作り直しか）、content stream の text・image の object の取り出しと書き換え、font（埋め込みの subset の扱い、置き換えの font）、複数頁、Notes の UI、試験。design-reviewer | in-progress（設計 [design.md](phase001/design.md) と review 済み、review の反映は未了。判断 D1〜D7 待ち） | — |

見積もり（Q1 の概算）: 6 LW。
