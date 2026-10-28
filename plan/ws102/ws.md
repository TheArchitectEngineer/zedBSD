<!-- awesome-plan project=zedbsd record=ws102 -->

# WS102: スクリーンキーボード（compositor に直接、flick と QWERTY と手書き）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。L1・L2 は達成（flick・QWERTY・手書きの面・作業の領域・編集の面・クリップボードの履歴・色付きの絵文字、main）。L3 の速さ（p010・p011）は 2026-09-30 午後 ユーザー「いったん描画の高速化はラップアップして…」で優先を下げた）
<!-- retained-after-completion --> Phase phase019とtests/ の 3 項目 はその場に残す: src/・userland/・platform/・Makefile・config/ の source か build の file が path を名指す（AGENTS.md §10 の source の comment の掃除の後に消す。2026-10-28 Q1 の方針、q1660）
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

2026-09-30 ユーザー「スクリーンキーボードを実装したいです。画面右下からスワイプで、画面右側にフリック入力パネル。画面左下からスワイプで、画面下側にQWERTYキーボード、手書き入力。フリック入力は、アルファベット、記号、日本語。スクリーンキーボードはWaylandコンポジタに直接実装するのがいいと思います。システムの一体感を重視します。手書き入力の認識処理はあとで実装して、スタブでいいです。」

達成基準 K1〜K8（右下の flick の panel、かな・英字・数字・記号、左下の QWERTY、手書きの面、作業の領域を縮める、Kei の見た目、既存の gesture とぶつからない、focus の app へ送る）。設計は [design.md](design.md)、段は L1〜L4（design.md §3）。

## ユーザーの決定

- 2026-09-30（K5）: keyboard が出ている間、作業の領域を keyboard の無い範囲に縮める。最大化の窓は animation で縮み、浮いた窓は animation で収まる位置へ動く（はみ出た分はそのまま）。flick・QWERTY のどちらでも縮める（p007）。
- 2026-09-30: keyboard の inset を app に知らせる（p015）。
- 2026-09-30 D1「libkeiui に入れる」: text-input-v3 の受け口を `kui_window` に入れる（WS090 の Phase、IME の file は変えない）。
- 2026-09-30 D2「IME にかなの口を足す」: IME に「かなを入力として受け取る」口を足し、keyboard はかなをそのまま渡す（L3、WS095 と調整）。
- 2026-09-30: 縁に組み込んだ見た目（flick の panel を右の列の全体に、QWERTY・手書きを下端の全幅に、余白・外の角丸・影をやめる、縁から伸び出す動き）→ p021。
- 2026-09-30「色付きにする」→ 色付きの絵文字（p019・p022）。
- 2026-09-30 午後「いったん描画の高速化はラップアップして…」→ L3 の速さの優先を下げた。同日、P3 の担当を終え、再開はユーザーが言う時。
- 2026-10-13（UAT）: flick のかなを OSK の preedit に、送り仮名つきの変換、a/A を Shift に、かなの面で IME ON・英字で OFF、QWERTY の Return・Backspace の位置、flick の Del → 削除、道具の行の高さ、Del → 次のApp → p026。
- 2026-10-13「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」→ p026 cleared。

## 結果

- compositor の `keyboard.c`・`keyboard-layout.c`: 右下・左下の角の認識器、flick の 3 つの face（かな 46＋ー・英字 26・数字 10・記号 32）、文字の送出（evdev と Shift、text-input の commit）、QWERTY と補助の key の列、手書きの面（線・stub の認識・候補）、多指の連打（`ROUTE_OSK`）、作業の領域と `keiland_keyboard_inset_v1`、右の列の道具の面と編集の面、`keiland_edit_v1`、クリップボードの履歴（記憶の中だけ 10 件、secret を残さない）とその tab、縁に組み込んだ見た目、Noto Color Emoji（CBDT、SIL OFL 1.1）と色の glyph、絵文字の面、full keyboard の IME（p025）、UAT 2026-10-13 の直し（p026）が main にある。
- 証拠は QEMU の Venus（pen の image の注入の touch）と host の試験（osk-guest の全手順、C9、WS079-p010、boot test）。L1 の (a)〜(d)、L2 の 30 文字 5 文字/秒で誤り 0・2 本指 100 打鍵で取りこぼし 0 を QEMU で満たした。実機は未実施。
- L3 の計測の道具（p010）: T1-510 で frame p95 103 ms（目標 50 ms）。

## 制限・移管

- L3 の数値目標への直し（p011）は test-wait のまま閉じた（直しは WS102 の外の見込み、QEMU の値は参考で合否は 5330 の予定だった）。
- p012（IME と組んだ日本語の変換、右の列の変換候補）の残り・p013（Windows の上の QEMU の物理の touch、WS085）は着手せず。
- 規約の全文と回帰（p014）は WS の外の一括の見直しへ。
- 2026-10-06 UAT の BUG-229・BUG-230・BUG-231 は p025 で直した。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws102-p001 | 設計（design.md） | cleared（2026-09-30） |
| ws102-p002 | `keyboard.c` の骨組みと角の認識器 | cleared（2026-09-30、QEMU） |
| ws102-p003 | flick の face の表と描画 | cleared（2026-09-30、host と QEMU） |
| ws102-p004 | 文字の送出 | cleared（2026-09-30、host と QEMU） |
| ws102-p005 | L1 の仕上げ（閉じる・K7 の回帰・1920x1080） | cleared（2026-09-30、QEMU） |
| ws102-p006 | QWERTY の面と左下の gesture | cleared（2026-09-30、QEMU） |
| ws102-p007 | 作業の領域 | cleared（2026-09-30、QEMU） |
| ws102-p008 | 手書きの面（stub の認識） | cleared（2026-09-30、QEMU。17 ms は QEMU で測れない） |
| ws102-p009 | `touch.c` の `ROUTE_OSK`（多指の連打） | cleared（2026-09-30、QEMU） |
| ws102-p010 | L3: 計測の道具と基準値 | cleared（2026-10-10 Q1、T1-510） |
| ws102-p011 | L3: 数値目標への直し | in-progress（test-wait、T1-586）のまま WS を閉じた（速さの優先を下げた） |
| ws102-p012 | L3: IME と組んだ日本語の変換、右の列の変換候補 | 着手せず（送り仮名つきの変換は p026。Phase の directory なし） |
| ws102-p013 | L4: Windows の上の QEMU での物理の touch | 着手せず（Phase の directory なし） |
| ws102-p014 | 全文の規約と回帰 | 規約の全文は WS の外の一括の見直しへ（Phase の directory なし） |
| ws102-p015 | keyboard の inset の知らせ | cleared（2026-09-30、QEMU と host） |
| ws102-p016 | 右の列の道具の面・編集の面・直前の app | cleared（2026-09-30、QEMU） |
| ws102-p017 | 編集の操作の拡張 `keiland_edit_v1` | cleared（2026-09-30、QEMU と host） |
| ws102-p018 | クリップボードの履歴 | cleared（2026-09-30、QEMU と host） |
| ws102-p019 | 色付きの絵文字 その 1（font と license、色の glyph） | cleared（2026-09-30、host と QEMU） |
| ws102-p020 | QWERTY の補助の key の列 | cleared（2026-09-30、QEMU と host） |
| ws102-p021 | 縁に組み込んだ見た目 | cleared（2026-09-30、QEMU） |
| ws102-p022 | 色付きの絵文字 その 2（絵文字の面） | cleared（2026-10-05 Q1、T1-175b） |
| ws102-p023 | Text Editor の本当の編集の状態 | cleared（2026-09-30、QEMU と host） |
| ws102-p024 | 道具の面の「履歴」の tab | cleared（2026-09-30、QEMU。全手順の回帰は未実施） |
| ws102-p025 | OSK の残り（App Home で消える・引き出し・full keyboard の IME） | cleared（2026-10-08 Q1、T1-394） |
| ws102-p026 | UAT 2026-10-13 の指摘 | cleared（2026-10-13 ユーザー「UATの結果待ちのものは…clearedに…」） |

## 記録の所在

Phase の計画・結果・試験（`phaseNNN/`・`tests/`）と作業の手引き（`guide.md`）は削除する（Q1 の pipeline）。OSK の guest 試験（`osk-guest.sh` ほか）は plan/tools/ へ移す案（Q1 の判断）。git の commit `fb1f6afee` の `plan/ws102/` にある。Queue の履歴は [plan/history](../history/index.md)。
