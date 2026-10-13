<!-- awesome-plan project=zedbsd record=ws216 -->

# WS216: Notes の描く道具（筆・鉛筆・領域の塗りつぶし・水彩）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-13 Q1 作成。p001 の設計 1 を P1、設計 2 を P3 が書いた。D1〜D12 はユーザーの決定で決まった。review-2 の待ち）
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
| [p001](phase001/phase.md) | 設計: 形の列と焼き込んだ係数、PDF への出し方、塗りつぶしの方式、描画の速さと確保の上限（Pi 4・5330）、UI、段の分け方 | — | in-progress（設計 2、review-2 の待ち） |
| p002 | libpdf: 係数の輪郭（style・範囲・gentle な境）、writer（ExtGState の (ca, BM)、`q/Q` の状態、tiling pattern、major 3 の content の Flate）、reader（Flate の content の hash、tiling pattern） | p001 | planned |
| [p003](phase003/phase.md) | Notes の model と形式: 道具の種類 2〜5、形の列、係数の式と焼き込み、鉛筆の区間、編集 data 3.0・journal 3、undo・部分消し、PDF の書き出し | p002 | in-progress（2026-10-13 P1: 実装と host の受け入れ済み、Q1 の review 待ち） |
| [p004](phase004/phase.md) | 画面: 頂点の塊（4 MiB の塊、i915 の 1 確保 16 MiB の上限）、遅延の pipeline と代わり（Pi）、粒の texture 3 段、Multiply、複数の輪郭、継ぎ目の fringe なし | p003 | in-progress（2026-10-13 P1: 実装と host の受け入れ C1〜C8 済み、test-wait（画は p007 で T1）） |
| [p005](phase005/phase.md) | 領域の塗りつぶし: 決まった倍率の帯の画面外の描画と読み戻し、flood fill（許容差・隙間を閉じる）、輪郭化と上限、重なりの順、undo・journal、消しゴム（D7 (c)） | p003、main に入った p004 の出力 | in-progress（2026-10-13 P1: 実装と host の受け入れ F1〜F11 済み、test-wait（画は p007 で T1、Pi は p009）） |
| [p006](phase006/phase.md) | UI: 描く道具の button と帯、Tool menu、key、色と太さの表、pen・mouse・指での選択と塗り、AAT の helper の追従 | main に入った p003〜p005 の出力 | in-progress（2026-10-13 P1: 実装と host の受け入れ U1〜U5 済み、test-wait（画面の操作は p007 で T1）） |
| [p007](phase007/phase.md) | T1 の QEMU（AAT の draw-tools・fill-region）、実機 5330 のペン（ユーザー） | p002〜p006 | in-progress（2026-10-13 P1 q946: scenario・helper・試験の image の config 済み、test-wait（T1 の QEMU、5330 はユーザー）） |
| p008 | 規約の全文の見直し | p007 | planned |
| p009 | Pi 4 の確かめ（起動、代わりの log が出ない、各道具、速さの数） | p007、WS141 p017、WS141 の Multiply の blend の依頼 | planned |

見積 計 約 18 LW（[p001 §8](phase001/phase.md#8-段の分け方と受け入れ試験)）。p002 は Notes の source に触れないので、すぐ始められる。p004〜p006 は同じ `main.c`・`render.c` を触るので同じ担当が順に行う。WS216 は p009（WS141 を待つ）が cleared になるまで completed にならない（D10）。

## ユーザーの決定（2026-10-13）

「ではあとは推奨でお願いします」 → D1〜D12 は全部推奨。review-1 を受けて推奨を変えた D7 は (c)「見えている所に触れたらその物を消す」、D8 は「Flate は major 3 の file だけ、reader は decode してから hash」。D10〜D12 は第 2 版で設計者（P3）が推奨を決め、その推奨を採った（[p001 §10](phase001/phase.md#10-決定d1d12)）: D10 Pi 4 を受け入れに含める（別の段 p009、WS141 の後）、D11 鉛筆の粒は tiling pattern と単色の下塗り（libpdf の reader に tiling pattern を足す）、D12 囲まれていない所は頁の縁を境としてつながった所を塗る（複雑すぎる時は notice）。

2026-10-13 ユーザーの決定（p005 review-1、Q1 の AskUserQuestion）: D7 (c) の中身は「見えている物を優先 (Recommended)」— 線を消すのは中心が線の見える半幅＋約 2 pt の中にある時、当たった所で上の不透明な塗りに覆われている線は数えない、塗りは中心で見えている一番上の塗りだけを消す。線のすぐ近くでは線でなく背景の塗りが消えることがある（受け入れ）。塗りの点の上限は「8192 のまま (Recommended)」、超えたら notice（[p005 §1.1](phase005/phase.md)）。

2026-10-13 ユーザーの決定（p006 review-1、Q1 の AskUserQuestion）: 水彩の label は今のまま（toolbar・帯は Water、Tool menu は Watercolor）、描く道具の button に帯が開く印を付ける、Pi で数秒かかる塗りは上限なしで待つ（時間は log）、塗りの道具の drag は「Tap to fill」の status、帯を開いている間 notice が出ないことは報告として受ける（[p006 §1](phase006/phase.md)）。

2026-10-13 Q1: review-2 の判定「p002 は条件つきで始めてよい」（致命的・重大 0）。p002 の phase.md の設計と受け入れに中の M-6〜M-9（粒の tiling pattern は頁ごと・name_prefix、reader の pattern の cell の解像度、鉛筆の区間の自己重なり、範囲の輪郭の API の index の意味）と M-13（Save Clean Copy の試験）・L-3 を入れてから実装する。残りの中（M-1〜M-5・M-10〜M-12）は担当の段（p003・p005 など）の phase.md で解決する。p002 の担当は次に空いた P。
