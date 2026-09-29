<!-- awesome-plan project=zedbsd record=ws090 -->

# WS090: widget・control の共有 library（慣性の smooth scroll を含む）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（設計）から
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

「スクロールやボタンなど、ウィジェットやコントロールを共有ライブラリにする。独自のものでよい。慣性スムーズスクロールは少なくともライブラリにして再利用したい。」

- 今は Files・Terminal・Notes・PDF Viewer・ブラウザ・（作成中の）Settings が、scroll・button・list・sidebar・card 等をそれぞれ持つ。慣性の scroll は
  WS081 で `libkeiland/motion.c`・scroller・gesture にまとめ始めた。
- 独自の共有 library（Kei の見た目、Vulkan と Wayland の上）に、少なくとも慣性の smooth scroll（touch・wheel・key・overscroll の rubber band）を
  部品として出し、既存の app を順に移す。button・toggle・slider・list・sidebar・card・text field 等の範囲と、library の名前・置き場（libkeiland を
  広げるか、新しい library か）・API の形は p001 で決める。新しい app（WS089 Settings、WS091・WS092）はこれを使う。

## Phase（案）

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| ws090-p001 | 設計 | planning | — |
