<!-- awesome-plan project=zedbsd record=ws090 -->

# WS090: widget・control の共有 library（慣性の smooth scroll を含む）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。2026-10-08 夜 ユーザー「WS050・WS051・WS090・WS183 は実機で動作を確認」、p020・p021 は 2026-10-13 ユーザー「今の外観で OK」）
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

2026-09-29 ユーザー「スクロールやボタンなど、ウィジェットやコントロールを共有ライブラリにする。独自のものでよい。慣性スムーズスクロールは少なくともライブラリにして再利用したい。」

各 app が個別に持っていた scroll・button・list・sidebar・card などを、Kei の見た目の独自の共有 library（Vulkan と Wayland の上）にまとめ、少なくとも慣性の smooth scroll（touch・wheel・key・overscroll の rubber band）を部品として出し、既存の app を移す。設計は [design.md](design.md)。

## ユーザーの決定

- 2026-09-29「テキストエディタのファイルピッカーは、KeiのUIライブラリに入れるのがいいと思いました。」→ 最初の共有の部品は file chooser。
- 2026-09-29: 文字の view の touch は 1 本指で選択・2 本指で scroll（p003）。
- 2026-09-30「File Pickerは、独立したタイトルバーを持つウィンドウではなく、親ウィンドウのタイトルバーにぶらさがって前面に表示されるスタイルにしたいです。親ウィンドウがない場合は独立にします。可能でしょうか？」→ p014。
- 2026-09-30「File Chooserは透過ウィンドウをやめましょう。」→ p014 で chooser の窓を不透明に。
- 2026-10-05: File Chooser の右の pane の白い背景を左と揃える、または左右とも desktop を少し透かす → p016。
- 2026-10-08「規約の整形（各 WS の全文規約の Phase）はベータ3」→ p012 は WS の外の一括の見直しへ。
- 2026-10-08 夜「WS050・WS051・WS090・WS183 は実機で動作を確認」。
- 2026-10-13（AskUserQuestion）「今の外観で OK」→ p020（Mahora の font）・p021（窓の中身の padding 0）。

## 結果

- 共有の部品は libkeiland（`kl_*`）にまとめた。最初に libkeiui（`kui_*`: scroll view・入力の層・窓の土台・部品・file chooser・text-input-v3）を作り、後に描画の層（canvas・text・icons）・欄（`kl_field`）・慣性の scroll（`kl_scroll`）・複数選択の list と icon の grid を libkeiland へ一本化した（p009・p015・p017・p019・p024）。
- 移した app: Text Editor・PDF Viewer・Image Viewer・Terminal・Notes・Files・Settings（欄と自前の部品）。全ての文字の入力で IME（p022）、Browser の form の欄の IME（p025）。
- file chooser は親の title bar の下の sheet（Titlebar の mode 3 SHEET、KEILAND_VERSION 20）で不透明（p014・p016）。UI の font は Mahora（p020）、窓の中身の padding は 0（p021）。
- pointer の追従の再描画（hover・drag）を一定の frame rate に（p018）、変わった所だけの present（VK_KHR_incremental_present の層 1・2・4、p026）。
- 証拠は QEMU（Venus）の host 試験・guest 試験・T1 の回帰（T1-245・T1-261・T1-272・T1-382・T1-400 ほか）。実機（5330）は 2026-10-08 夜のユーザーの動作の確認。

## 制限・移管

- p026 の層 3 は Q1 の判断で保留のまま閉じた（5330 で `ZFILES DESKTOP band`・`KWL PERF` を測ってから決める予定だった）。
- p009・p018 は T1 の PASS の後の Q1 の判定の印が付かないまま閉じた（実装は main）。
- 規約の全文（p012）は WS の外の一括の見直しへ。
- Wi-Fi の鍵の欄・Disconnect・copy の取り消しの × の 5330 の個別の UAT は未確認。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws090-p001 | 設計（design.md） | cleared（2026-09-29） |
| ws090-p002 | libkeiui の骨組みと描画の層 | cleared（2026-09-29、host 13/13） |
| ws090-p003 | scroll view・入力の層・文字の view の touch | cleared（2026-09-29、host 63/63） |
| ws090-p004 | 窓の土台（`kui_window`）と Text Editor | cleared（2026-09-30、QEMU） |
| ws090-p005 | 部品（button〜progress）と見本の program | cleared（2026-09-30、host 94/94・QEMU） |
| ws090-p006 | file chooser を libkeiui へ | cleared（2026-09-30、QEMU） |
| ws090-p007 | Settings の欄を `kl_field` へ | cleared（2026-10-08 Q1 の判定） |
| ws090-p008 | PDF Viewer・Image Viewer を移す | cleared（2026-09-30、QEMU） |
| ws090-p009 | Files と Settings の描画の層を libkeiland へ | test-wait のまま WS を閉じた（T1-261・T1-272 PASS、Q1 の判定の印なし。実装は main） |
| ws090-p010 | Files の名前の変更の欄を `kl_field` へ | cleared（2026-10-08 Q1 の判定） |
| ws090-p011 | Terminal・Notes の窓 | cleared（2026-09-30、統合の試験 demo-s8-s9.sh） |
| ws090-p012 | 規約の全文との照合と回帰 | 規約の全文は WS の外の一括の見直しへ（Phase の directory なし） |
| ws090-p013 | `kui_window` の text-input-v3 と Text Editor | cleared（2026-09-30、QEMU の IME） |
| ws090-p014 | file chooser を親の title bar の sheet に | cleared（2026-09-30、QEMU） |
| ws090-p015 | Terminal・Notes の scroll を `kl_scroll` へ | cleared（2026-10-08 Q1、T1-382） |
| ws090-p016 | File Chooser の右の pane の背景 | cleared（2026-10-05 Q1、T1-117） |
| ws090-p017 | 慣性 scroll と開始の遅れ（BUG-211・BUG-218） | cleared（2026-10-08 Q1 の判定） |
| ws090-p018 | pointer の追従の再描画の frame rate（BUG-221・BUG-226） | test-wait のまま WS を閉じた（T1-233・T1-389・T1-393、Q1 の判定の印なし。frame の callback の集約は任意の残り） |
| ws090-p019 | 慣性 scroll を libkeiland に一本化 | cleared（2026-10-06 Q1、QEMU の T1） |
| ws090-p020 | UI の font を Mahora へ | cleared（2026-10-13 ユーザー「今の外観で OK」） |
| ws090-p021 | 窓の中身の padding を 0 に | cleared（2026-10-13 ユーザー「今の外観で OK」） |
| ws090-p022 | 全ての文字の入力で IME、自前の text box を libkeiland へ | cleared（2026-10-07 Q1） |
| ws090-p023 | Files・Settings の残りの自前の UI 部品 | cleared（2026-10-08 Q1 の判定） |
| ws090-p024 | 複数選択の list と icon の grid | cleared（2026-10-07 Q1、T1-282） |
| ws090-p025 | Browser の web の form の欄の IME | cleared（2026-10-07 Q1、T1-328・T1-338） |
| ws090-p026 | 変わった所だけを present する | in-progress のまま WS を閉じた（層 1・2・4 は T1-400 で回帰 fail 0、層 3 は保留） |

## 記録の所在

Phase の計画・結果・試験（`phaseNNN/`・`tests/`）と作業の手引き（`guide.md`）は削除する（Q1 の pipeline）。git の commit `fb1f6afee` の `plan/ws090/` にある。Queue の履歴は [plan/history](../history/index.md)。
