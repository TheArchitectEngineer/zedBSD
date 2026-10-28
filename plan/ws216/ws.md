<!-- awesome-plan project=zedbsd record=ws216 -->

# WS216: Notes の描く道具（筆・鉛筆・領域の塗りつぶし・水彩）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。p002〜p006 cleared（T1-580 QEMU PASS）、残りの p007（5330）・p009（Pi 4）は実機の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」）
<!-- retained-after-completion --> phase001・phase002・phase003・phase005 は source の comment（userland/desktop/notes/ の encode.c・main.c・notes.h・save.c・shape.c・ui.c・journal.c・fill-page.c・fill.c、userland/desktop/libpdf/paint.c）が設計の出典として参照している。消すか残すかは Q1 の判断待ち。
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: なし
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

Notes（`userland/desktop/notes/`）に、ペン・消しゴムに加えて筆・鉛筆・領域の塗りつぶし・水彩を足す。筆圧・傾きは WS079 の tablet の入力を使う。undo・PDF の保存と再編集（WS079 p005 の形式）で保たれ、道具は道具の欄からタッチとペンで選べる。

## ユーザーの決定

- 2026-10-13（UAT、原文）「・Notesアプリでペンの感度が有効であることを確認しました。」「・Notesアプリに、筆、鉛筆、領域塗りつぶし、水彩を追加してほしいです。」
- 2026-10-13「ではあとは推奨でお願いします」→ D1〜D12 は推奨（D7 (c)「見えている所に触れたらその物を消す」、D8 Flate は major 3 の file だけ、D10 Pi 4 を受け入れに含める（p009）、D11 鉛筆の粒は tiling pattern と単色の下塗り、D12 囲まれていない所は頁の縁を境に塗る）。
- 2026-10-13（p005 review-1、AskUserQuestion）: D7 (c) の中身は「見えている物を優先 (Recommended)」、塗りの点の上限は「8192 のまま (Recommended)」。
- 2026-10-13（p006 review-1、AskUserQuestion）: 水彩の label は今のまま（toolbar・帯は Water、Tool menu は Watercolor）、描く道具の button に帯が開く印、Pi で数秒かかる塗りは上限なしで待つ、塗りの道具の drag は「Tap to fill」の status。

## 結果

- p002: libpdf の係数の輪郭・範囲の輪郭、ExtGState (ca, BM)、q/Q、粒の tiling pattern、Flate、reader の tiling pattern（host の受け入れ A1〜A11）。
- p003: Notes の model と形式（道具の種類 2〜5、形の列、焼き込み、編集 data 3.0・journal 3、部分消し、PDF の書き出し、host の受け入れ B1〜B10）。
- p004〜p006: 画面（頂点の塊、Multiply、粒の texture、遅延の pipeline）、領域の塗りつぶし、UI。T1-580 の QEMU の AAT（draw-tools・fill-region）で機械の判定 PASS、画は Q1 が見て合格（2026-10-13）。
- 実機（5330 のペンの感触、Pi 4 の道具と速さ）は未実施。FreeBSD の native の build は未実施（p003 の記録）。

## 制限・移管

- 実機の確認は未実施（p007 の 5330 のユーザーの確かめ A5、p009 の Pi 4）。
- 古い Notes が保存した編集 data 3.0 の読み直しは Future Work の F-099。
- p008 の規約の全文は WS の外の一括の見直しへ（2026-10-08 ユーザー「規約の整形（各 WS の全文規約の Phase）はベータ3」、2026-10-27 の精査で一括の見直しに読み替え）。
- 回帰に残す試験（`run-pdf-tools.sh`、AAT の pen の image の `config.mk`）は plan/tools/ へ移す（Q1 の OK の後）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws216-p001 | 設計: 形の列と焼き込んだ係数、PDF への出し方、塗りつぶし、速さと確保の上限、UI、段の分け方 | in-progress（設計 2、review-2 待ち）のまま閉じた。実装は p002〜p006 |
| ws216-p002 | libpdf の係数の輪郭、writer の状態・Flate・粒の pattern、reader | cleared（host の受け入れ A1〜A11） |
| ws216-p003 | Notes の model と形式 | cleared（host の受け入れ B1〜B10） |
| ws216-p004 | 画面（頂点の塊、Multiply、粒の texture、遅延の pipeline） | cleared（T1-580） |
| ws216-p005 | 領域の塗りつぶし | cleared（T1-580） |
| ws216-p006 | 描く道具の UI | cleared（T1-580） |
| ws216-p007 | T1 の QEMU と実機 5330 のペン | in-progress のまま閉じた（T1-580 PASS、5330 は未実施） |
| ws216-p009 | Pi 4 の確かめ | 未着手で閉じた（実機の確認だけ） |
| ws216-p008 | 規約の全文の見直し | WS の外の一括の見直しへ |

## 記録の所在

Phase の計画・結果・試験は削除する（上の retained の 4 つは Q1 の判断の後）。git の commit `fb1f6afee` の `plan/ws216/` にある。Queue の履歴は [plan/history](../history/index.md)。
