<!-- awesome-plan project=zedbsd record=ws223 -->

# WS223: 全画面は compositor の合成で、game mode は Wayland の拡張で求められた時だけ（Video Player は game mode をやめる）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-13 Q1 作成。担当は次に空いた P）
Primary Milestone: MG006
Related Milestones: なし
<!-- awesome-plan-current:end -->

## 原文（2026-10-13 ユーザー）

「Video Playerは、Game Modeをやめます。Game Modeでない全画面を実装します。Game Modeは別途、Wayland拡張で要求があったときのみ使用することにします。Game Modeを利用したアプリは現段階では存在しない、という整理にします。全画面ではコンポジターを利用しましょう。」

## 目的と完了の条件

1. 全画面（fullscreen）の窓は compositor が合成して出す（直の scanout をしない）。Video Player の全画面もこの形。
2. game mode（直の scanout、`userland/desktop/wayland/scanout*.c`）に入るのは、client が Wayland の拡張で求めた時だけ（今は `wp_content_type_v1` の `game`、`content-type.c`。どの拡張を「求め」とするかは p001 で決める）。content type `video` では入らない（`scanout-rules.c:37` の VIDEO の条件を外す）。
3. Video Player（`userland/desktop/videoplayer/main.c:240` の content type video）は game mode を求めない。全画面の再生が合成で滑らかに動く（5330・Pi 4 で確かめる）。
4. 今の zedBSD の app で game mode を使う物は無い、と記録を整える（WS122 の記録、master、試験・AAT のシナリオで video の game mode を前提にした物は直すか消す、AGENTS.md の試験の整理の基準）。
5. WS217（画面の回転）の D18（video の全画面にも game mode の回転の停止を当てるか）はこの決定で不要（video は game mode に入らない）。WS217 p012 の対象は game の拡張を求めた client だけ。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| p001 | 調べと設計: 今の game mode の入り口（content type・全画面・size の一致）、合成の全画面の速さ（Pi 4）、求めの拡張の決め、試験の洗い出し | planned |
