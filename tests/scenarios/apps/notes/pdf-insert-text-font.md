---
id: apps.notes.pdf-insert-text-font
title: Notes で PDF に文字を挿入し、font を変える（日本語を含む）
status: draft
areas: [notes, libpdf, ime]
paths: [userland/desktop/notes/, userland/base/libpdf/]
machine: either
human: none
since: ws175-p001
---

## 目的
Text の道具で新しい文字を挿入し、font を Sans・Mono・Japanese に変えられること、IME で日本語を打てること、保存した PDF の文字が他の reader で読めること（埋め込みの subset と ToUnicode）を確かめる（WS175、ws175-p004・p007）。

## 準備
- `/tmp/aat-samples/notes-edit.pdf`。Notes で開き、1 頁の下の余白が見えている。
- 日本語の入力方式が設定済み（`desktop.input-method.choose-method` の後の状態）。

## 操作と確認
1. 操作: toolbar の Text の道具を押し、1 頁の下の余白を click して `Hello Notes` と打つ。
   確認事項: 挿入の box。正解: click した所に文字が出る（既定の Sans）。確認方法: 撮影。
2. 操作: Esc で確定。
   確認事項: 挿入。正解: 文字が頁に残り選ばれた状態。確認方法: log `NOTES EDIT insert page=0 kind=text object=I`、`NOTES EDIT text page=0 object=I chars=11 font=sans`。
3. 操作: 操作の帯の Font で Mono を選ぶ。
   確認事項: font の変更。正解: 文字が等幅の font に変わる。確認方法: log `NOTES EDIT font page=0 object=I font=mono`、撮影。
4. 操作: 別の余白を click し、入力方式を日本語に切り替えて `nihongo` と打ち変換して確定、Esc。
   確認事項: 日本語。正解: 「日本語」が出る（Japanese の font、自動の fallback）。確認方法: log `NOTES EDIT text page=0 object=J … font=cjk` か `fallback=1`、撮影。
5. 操作: Ctrl+S。host へ file を取り `pdftotext -f 1 -l 1` と `pdffonts`。
   確認事項: 保存した PDF。正解: 「Hello Notes」と「日本語」が出る。pdffonts に `+JetBrainsMono` と `+DroidSansFallback` の subset が `emb yes sub yes uni yes` で載る。確認方法: command の出力。

## 合格
1〜5 の正解。

## 注記
draft。日本語の入力の手順は AAT の IME の道具（`desktop.input-method.*`）に合わせて p009 で具体にする。
