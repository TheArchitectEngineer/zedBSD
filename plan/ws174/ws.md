<!-- awesome-plan project=zedbsd record=ws174 -->
# WS174: 起動時の Ctrl / Shift で boot の選択を変える

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」: UEFI の loader の Ctrl・Shift は p001〜p003・p005 cleared、BIOS の p004 はユーザー「BIOSは後日でよいです」で後日）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG003
Related Milestones: MG006（graphical boot）
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

boot の時に **Ctrl** が押されていたら UEFI の bootloader が kernel に渡す `kmsg=` を `console` にし（logo を出さず、GOP に 640x480 を希望）、**Shift** が押されていたら `login=` を `console` にして（sessiond が終わり getty が始まる）kernel を起動し、kernel の最後の message が画面で読める。boot の config の file の形式は変えない。kernel・init・sessiond は変えない。

## ユーザーの決定

- 2026-10-05 夜（由来）「おっと、イメージを実機で起動したら、カーネルの起動の途中でpanicしたとみられますが、グラフィカルブートなのでわかりません。ブート時にctrlキーが推されていたらsafe boot optionsの設定に切り替えるように、ブートローダとブートコンフィグファイルを変更したいです。設計だけできますか？」
- 2026-10-05 夜（定義、Q1 経由）「safe boot optionsとはグラフィカルブートにせずカーネルのメッセージをコンソールに出力するオプションを選択することです。」
- 2026-10-05 夜（仕様の変更）「safe.login=console これですが、おおむねグラフィックドライバでフリーズするので、コンソールログインにしてもあまりうれしくないなあ。仕様を変えましょう。Ctrlキーが押されている→カーネルメッセージがコンソールになる。デフォルトではquietだけどconsoleになる。Shiftキーが押されている→ログインがコンソールになってグラフィカルセッションが開始しない。よってブートコンフィグファイルの仕様変更は不要です。ブートローダのみ変更します。」
- 2026-10-05 夜（第 3 版の判断）Ctrl の時に logo を落とす: Yes。Ctrl の時に 640x480 を希望する: Yes。1 秒止める: No（告知は出すが待たない）。
- 2026-10-05 夜（実装の指示）「OKです。ブートローダの仕様変更を実装してください。BIOSは後日でよいです。」
- 2026-10-27（完了の根拠、q1608 経由）「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」

## 結果

- UEFI の loader に Ctrl・Shift の検出（`bootloader/uefi/boot-keys.c`、Ex protocol の KeyShiftState、標本は入口と config の parse の直後の 2 点）と、kernel に渡す parameter の書き換え（`bootloader/common/boot-override.c`、純粋な module）を入れた。Ctrl で `kmsg=console`・logo なし・640x480 の希望、Shift で `login=console`。告知は best effort。
- host 試験（書き換え O1〜O11、検出 K1・K2）PASS。QEMU（T1-214）で C0 none・C1 Ctrl・C2 Shift・C3 Ctrl+Shift・C4 late の 5 cell が再試行で全 PASS（1 回目の C3 の不検出は [BUG-272](../bugs/BUG-272.md)）。
- 実機（5330）の受け入れ（両方押して kernel の最後の message が画面に残る写真）は未実施。

## 制限・移管

- BIOS（PC/AT、int 16h の Shift/Ctrl の flag）は未着手（ユーザー「BIOSは後日でよいです」）。要る時は新しい WS。
- C3（Ctrl+Shift）を間欠に検出しない件は [BUG-272](../bugs/BUG-272.md)。
- 実機 5330 の確認は未実施。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws174-p001 | 設計（Ctrl・Shift の検出、parameter の書き換え、kernel への渡し方、docs、試験）第 3 版 | cleared（2026-10-05 夜、S2 の標本は Q1 の決定で外した） |
| ws174-p002 | `boot-override.c/.h`（record の書き換え）、vmunix.mk の rule、host 試験 O1〜O11 | cleared（2026-10-06） |
| ws174-p003 | UEFI loader の検出・書き換えの適用・640x480・告知、docs 4 件、QEMU 5 cell | cleared（2026-10-08、T1-214 の再試行で全 cell PASS、C3 の間欠は BUG-272） |
| ws174-p004 | BIOS PC/AT の検出（案） | 未着手のまま閉じた（ユーザー「BIOSは後日でよいです」） |
| ws174-p005 | 規約の全文の見直し | cleared（2026-10-06、指摘 0。T1-213 の後に変えた `zbl_uefi_boot_keys_sample()` の再確認は未実施） |

## 記録の所在

Phase の計画・設計（design.md 第 3 版）・結果・試験は削除した。git の commit `fb1f6afee` の `plan/ws174/` にある。Queue の履歴は [plan/history](../history/index.md)。
