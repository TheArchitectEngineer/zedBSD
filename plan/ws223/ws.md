<!-- awesome-plan project=zedbsd record=ws223 -->

# WS223: 全画面は compositor の合成で、game mode は Wayland の拡張で求められた時だけ（Video Player は game mode をやめる）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-13 Q1 作成。P3 が p001 の第 1 版 [phase001/phase.md](phase001/phase.md)（agent/p3）。design-reviewer と、ユーザーの D1〜D6 待ち）
Resume point: p001 の review の後、D1（求めの形、推奨は `wp_content_type_v1` の `game`）と D2（Video Player の `KL_CONTENT_VIDEO` の呼びを残すか）が決まれば p002 を開始できる。p003（wltest の `--content-type`、game mode の AAT と host 試験）は p002 の後。WS217 p012 は p002・p003 に依存。
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

p001 第 1 版の要点（2026-10-13 P3）: 「Game Mode でない全画面」は既に実装されている（全画面の窓は合成、不透明で何も重ならなければその窓 1 枚だけを描く `shell.c` `fullscreen_whole`、bar は 2 秒で隠れ、端の swipe・Esc/F11・screen off の抑止は合成のまま効く）。game mode の入り口は `scanout-rules.c:37` の 1 条件（content type が video か game）だけで、content type は `wp_content_type_v1` だけから来る。今 `game` を求める app・package は無く、`video` は Video Player（`main.c:240`）だけ。**Pi 4 では bcm2711 が `GPU_DISPLAY_BLOB` を立てないので game mode は今も入れない**（`reason=backend`）—— Pi 4 の全画面の動画は今日も合成で、この決定で経路は変わらない。推奨: 求めは `wp_content_type_v1` の `game` のまま（D1）、Video Player の `video` の hint は残す（D2）、BUG-223 はこの決定で resolved（D3）、Pi 4 は計測の記録だけ（D4）、全画面の窓の HVS の plane 化は Future Work（D5）。

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 調べと設計: 今の game mode の入り口（content type・全画面・size の一致）、合成の全画面の速さ（5330・Pi 4）、求めの拡張の決め、WS217 p012 との関係、試験の洗い出し、段の分け方 | planning（第 1 版 2026-10-13 P3、design-reviewer とユーザーの D1〜D6 待ち） | — |
| p002 | 規則: `scanout-rules.h/.c` の content の条件を `game` だけに（`KWL_SCANOUT_CONTENT_VIDEO` を削除）、`scanout.c` の `scanout_note` を game だけに、注記（scanout.c・display.c・kwl.h・backend の header・content-type.c・keiland.h）、Video Player の注記か呼びの削除（D2）。AAT の `apps.videoplayer.fullscreen` と `helpers_apps.py` を「`KWL SCANOUT` の行が出ない」の否定の check に直す | planned（p001 の review と D1・D2 の後） | p001 |
| p003 | 試験: wltest に `--content-type=none|photo|video|game`、AAT の `desktop.game-mode.wltest`（Venus の `direct=1`・`reason=shot`・`reason=pointer`・`reason=no-window`、video の対照）と `full.suite`、AAT の image に wltest、`plan/tools/compositor/scanout-rules-host-test`（規則の表、ASan・UBSan、master の Tools への登録は Q1） | planned | p002 |
| p004 | 実機の確かめ（Q1 → T1・ユーザー）: 5330 の Video Player の全画面（`KWL SCANOUT` 無し、`FRAMES late=` が window と同じ桁）と wltest の game の `direct=1`（BUG-223 の i915 の証拠）、Pi 4 の全画面の再生の計測の記録（D4） | planned | p003 |
| p005 | 記録の整理の確認（WS122・BUG-223・guardrail・master・WS217 p001 第 5 版、Q1 の編集）と規約の全文の見直し | planned | p002〜p004 |
