<!-- awesome-plan project=zedbsd record=ws155 -->

# WS155: Keiland の app: カレンダー・スケジューラ・オーガナイザ（まず簡単な物）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」。根拠: p000〜p004・p006 cleared、2026-10-13 ユーザー「WS155はベータ2に入れます。UATは終わっています。OKでした。」）
<!-- retained-after-completion --> phase001 の phase.md は残す: userland/desktop/calendar/calendar.h:16 の comment が保存の形式の仕様として参照している。source の comment を直す（または仕様を docs/ へ移す）までの間
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

Keiland の標準 app として、カレンダー・予定の管理（スケジューラ）・オーガナイザの app を作る。まずは簡単な物。

## ユーザーの決定

- 2026-10-04 夜「Keilandアプリとしてカレンダー・スケジューラ・オーガナイザを実装します。まずはシンプルなものでいいです。ユーザの宿題として、アプリの外観の画像を提出するまでブロックします。」
- 2026-10-04 夜「画面右上通知領域の日付・時刻をクリックすると、カレンダーappが表示されるようにします。」
- 2026-10-05（デザイン案の提出、画像は会話の添付で file は無い）「カレンダーのデザイン案をお渡しします。このままにする必要はなく、要素で利用してほしいです。3Dを利用します。Linuxデスクトップのような格好つけだけのクールさはいらないです。Windows 11のように、実用性があるミニマルな中に、知性を感じさせるデザインがいいです。それでも、少しの動的なデザインがほしいです。そこに3Dのアニメーションを入れて、落ち着いた知性を表現したいです。モックを見て再指示するので、具体的デザインをお任せします。」
- 2026-10-05（T1-179 の mock を見て）「カレンダーのスクショ、いいですね。気に入りました。カレンダーはglass透過にして、ペインは分離して背景はなしの、Filesと同じスタイルにしましょう。右下のカレンダーの画像部分のスペースがちょっと無駄っぽく見えるので、メモ・ノートの領域にしましょう。日付ごととかでなくて、アプリの唯一のメモ領域でOKです。メモはドラッグして日付にドロップすると、メモ属性の予定として追加して、でもこれは予定追加ではなくて、予定追加の機能を応用した、メモの保存にしましょう。」
- 2026-10-07「…カレンダーの続き…」（ベータ3 へ。後に取り消し）
- 2026-10-08「コーディング規約による整形はベータ3」→ p005 は WS の外の一括の見直しへ。
- 2026-10-13「WS155はベータ2に入れます。UATは終わっています。OKでした。」

## 結果

- Calendar の app（`userland/desktop/calendar/`: 月・週・日の表示、予定とメモの追加・編集・削除、app に 1 つのメモの領域と日付への drag での保存、3D の卓上カレンダー、開始の通知、`~/Documents/Calendar` の iCalendar の保存）と、system bar の時計の click での起動が main にある。
- 証拠: QEMU の T1-297 の AAT（needs-person、Q1 が PNG で目視、p002〜p004）、T1-561 の AAT PASS（p006）。実機は 2026-10-13 のユーザーの UAT「OKでした」。

## 制限・移管

- 繰り返しの予定・CalDAV やクラウドの同期は範囲の外（同期は WS146・WS147 の候補）。
- 規約の全文（p005）は WS の外の一括の見直しへ。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws155-p000 | UI の mock（デザイン案の要素、3D の日めくり） | cleared |
| ws155-p001 | 外観の画像に基づく設計（画面・操作・保存・通知・試験） | cleared |
| ws155-p002 | 予定とメモの保存（`~/Documents/Calendar`、iCalendar） | cleared（T1-297） |
| ws155-p003 | app: 保存・編集・Week と Day・開始の通知 | cleared（T1-297） |
| ws155-p004 | system bar の時計から Calendar を開く | cleared（T1-297） |
| ws155-p005 | 全文規約の見直し（directory なし） | 規約の全文は WS の外の一括の見直しへ |
| ws155-p006 | 予定の double click で編集、空き地の click で新規作成（2026-10-13 UAT） | cleared（T1-561、ユーザーの UAT） |

## 記録の所在

phase001 は上の理由で残す。他の Phase の記録と試験は削除する（Q1）。git の commit `fb1f6afee` の `plan/ws155/` にある。Queue の履歴は [plan/history](../history/index.md)。
