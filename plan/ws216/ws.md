<!-- awesome-plan project=zedbsd record=ws216 -->

# WS216: Notes の描く道具（筆・鉛筆・領域の塗りつぶし・水彩）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-13 Q1 作成。p001 の設計 1 を P1 が書いた。design-reviewer の review とユーザーの判断 D1〜D9 の待ち）
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

| Phase | 内容 | 依存 | Status |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計: stroke の型と PDF への出し方（vector か raster か）、塗りつぶしの方式、描画の速さ（Pi 4・5330）、UI、段の分け方 | — | in-progress（設計 1、review と D1〜D9 の待ち） |
| p002 | libpdf: 輪郭の style と範囲（平らな切り口）、writer の `/BM`・clip・`q/Q`・運ぶ画像の行列の描画、content の Flate | p001 | planned |
| p003 | Notes の model と形式: 道具の種類 2〜5、形の列、筆の幅の関数、鉛筆の区間、水彩の縁、編集 data 3.0・journal 3、undo・消しゴム、PDF の書き出し | p002、BUG-296 の merge | planned |
| p004 | 画面: Multiply と粒の pipeline、粒の texture（PDF と同じ bytes）、複数の輪郭、継ぎ目の fringe なし | p003 | planned |
| p005 | 領域の塗りつぶし: 頁の絵の読み戻し、flood fill（許容差・隙間を閉じる）、輪郭化、重なりの順、undo・journal、消しゴム | p003 | planned |
| p006 | UI: 描く道具の button と帯、Tool menu、key、色と太さの表、pen・mouse・指での選択と塗り | p003 | planned |
| p007 | T1 の QEMU（AAT の draw-tools・fill-region）、実機 5330 のペン（ユーザー）、Pi 4 の速さ（WS141 p017 の後） | p004〜p006 | planned |
| p008 | 規約の全文の見直し | p007 | planned |

見積 計 約 14.5 LW（[p001 §8](phase001/phase.md#8-段の分け方と受け入れ試験)）。p002 は Notes の source に触れないので BUG-296 を待たずに始められる。p004〜p006 は同じ `main.c` を触るので同じ担当が順に行う。

## ユーザーの決定（2026-10-13）

「ではあとは推奨でお願いします」 → D1〜D12 は全部推奨。review-1 を受けて推奨を変えた D7 は (c)「見えている所に触れたらその物を消す」、D8 は「Flate は major 3 の file だけ、reader は decode してから hash」。D10〜D12 は第 2 版で設計者が推奨を決め、その推奨を採る（第 2 版で記録）。
