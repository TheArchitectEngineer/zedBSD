<!-- awesome-plan project=zedbsd record=ws216 -->

# WS216: Notes の描く道具（筆・鉛筆・領域の塗りつぶし・水彩）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-13 Q1 作成。担当は BUG-296（Notes の pinch の落ち）の後に割り当て）
Primary Milestone: MG006
Related Milestones: なし
<!-- awesome-plan-current:end -->

## 原文（2026-10-13 ユーザーの UAT）

「・Notesアプリでペンの感度が有効であることを確認しました。」
「・Notesアプリに、筆、鉛筆、領域塗りつぶし、水彩を追加してほしいです。」

## 目的と完了の条件

Notes（`userland/desktop/notes/`）に、今のペン・消しゴムに加えて次の道具を足す。筆圧・傾きは WS079 の tablet の入力をそのまま使う。

1. 筆: 筆圧と速さで太さが大きく変わり、入りと抜きが細る線。
2. 鉛筆: 細く、紙の粒のような質感と、筆圧で濃さが変わる線。
3. 領域の塗りつぶし: 閉じた線で囲まれた所（または同じ色の続く所）を選んだ色で塗る。
4. 水彩: 半透明で、重ねると濃くなり、縁がにじむ線。
5. 全部が undo・PDF の保存と再編集（WS079 p005 の形式）で保たれる。PDF の中の見た目は他の viewer でも近い形で読める。
6. 道具の選択は Notes の道具の欄から行え、タッチとペンで使える。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| p001 | 設計: stroke の型と PDF への出し方（vector か raster か）、塗りつぶしの方式、描画の速さ（Pi 4・5330） | planning |
