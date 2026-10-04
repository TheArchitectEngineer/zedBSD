<!-- awesome-plan project=zedbsd record=ws155 -->

# WS155: Keiland の app: カレンダー・スケジューラ・オーガナイザ（まず簡単な物）

<!-- awesome-plan-current:start -->
Status: planning（**blocked: ユーザーの宿題**）
Primary Milestone: MG006
Related Milestones: —
Parent: [Master](../master.md)
Queue: なし
Resume point: **ユーザーが app の外観の画像を提出するまで止める**（2026-10-04 ユーザーの指示）。画像が届いたら p001（設計）から。
<!-- awesome-plan-current:end -->

## 単一目標

Keiland の標準 app として、カレンダー・予定の管理（スケジューラ）・オーガナイザの app を作る。まずは簡単な物。

## ユーザーの指示（2026-10-04 夜、原文）

「Keilandアプリとしてカレンダー・スケジューラ・オーガナイザを実装します。まずはシンプルなものでいいです。ユーザの宿題として、アプリの外観の画像を提出するまでブロックします。」

## 止めている理由（blocked）

- **ユーザーの宿題**: app の外観の画像の提出。届くまで設計・実装を始めない。

## 範囲（画像が届いた後に p001 で設計して確定）

- 月・週・日の表示、予定の追加・編集・削除、繰り返しの予定は「まず簡単な物」の範囲に入れるかを決める。
- To-do・memo（オーガナイザ）の範囲。
- 保存の形式（iCalendar（.ics）の file を利用者の領域に、など）と、他の app（Files・通知）との関係。通知（予定の時刻の知らせ）は compositor の通知の仕組みに。
- libkeiland の UI の部品で作る（他の標準 app と同じ）。Linux・FreeBSD の Keiland でも動く。
- 同期（CalDAV・クラウド）は後の候補（WS146・WS147 と関係）。

## Phase（案）

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| ws155-p001 | 外観の画像に基づく設計（画面・操作・保存・通知・試験） | planning（blocked） | **ユーザーの外観の画像** |
