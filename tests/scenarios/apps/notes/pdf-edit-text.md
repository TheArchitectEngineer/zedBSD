---
id: apps.notes.pdf-edit-text
title: Notes で PDF の中の文字を書き換え、消す
status: draft
areas: [notes, libpdf]
paths: [userland/desktop/notes/, userland/base/libpdf/]
machine: either
human: none
since: ws175-p001
---

## 目的
既存の文字の行を、元の埋め込みの font のまま書き換えられること、元の font に無い文字では置き換えの font になり利用者に知らされること、行を消せることを確かめる（WS175、ws175-p002・p003・p004・p007）。

## 準備
- `/tmp/aat-samples/notes-edit.pdf`（`edit-basic.pdf`。1 頁の段落の 1 行目は「The quick brown fox jumps over the lazy dog」、subset の TrueType）。
- Notes で開き、1 頁を表示、Select の道具。

## 操作と確認
1. 操作: 段落の 1 行目を double-click。
   確認事項: 編集の box。正解: 行の上に caret の有る編集の box。確認方法: log `NOTES EDIT select page=0 object=I kind=text text="The quick brown fox`、撮影。
2. 操作: End、Backspace を 3 回（`dog` を消す）、`fox` と打ち、Esc。
   確認事項: 元の font のまま。正解: 行が「… the lazy fox」になり、見た目の font が他の行と同じ。確認方法: log `NOTES EDIT text page=0 object=I chars=43 font=original fallback=0`、撮影。
3. 操作: 2 行目を double-click し、End の後に `Zürich` と打って Esc。
   確認事項: 置き換えの font。正解: 行の文字が変わり、status に元の font に無い文字のため Inter にした旨が出る（subset に Z・ü が無い場合）。確認方法: log `NOTES EDIT text page=0 object=J … fallback=1`、撮影。
4. 操作: 3 行目を click して Delete キー。
   確認事項: 行の削除。正解: 3 行目が消え、4 行目以降は動かない。確認方法: log `NOTES EDIT delete page=0 object=K`、撮影。
5. 操作: Ctrl+S。host へ file を取り `pdftotext -f 1 -l 1`。
   確認事項: 保存した文字。正解: 「the lazy fox」と「Zürich」が出て、元の 3 行目の文字は出ない。確認方法: command の出力。

## 合格
1〜5 の正解。

## 注記
draft。pdftotext は最新の revision の page を読む（消した行の bytes は base の revision に残る。D1）。
