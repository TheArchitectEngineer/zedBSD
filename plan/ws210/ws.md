<!-- awesome-plan project=zedbsd record=ws210 -->

# WS210: Keiland の app の描画を GPU へ（libkeiland の canvas・文字・画像を Vulkan の draw に）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-12 Q1 作成）
Primary Milestone: MG006
Related Milestones: MG008
Parent: [Master](../master.md)
Queue: [P1 の lane](../agents/P1/queue.md)
Resume point: p001（調査と設計）から。
<!-- awesome-plan-current:end -->

## 由来

2026-10-12 ユーザー:「PI4のような低消費電力のモバイルSoCでは、画像をCPUで触ると、体験を損ないます。すべてGPUで処理する必要があります。」（Guardrail の「画像の処理は GPU で」）。WS141 p013 の調べ（P3）で、Keiland の app（libkeiland の ui/canvas.c・text.c）は UI を全部 CPU の software rasterizer で描き、CPU に map した canvas を compositor が GPU で sample するため、Pi 4 で client の frame ごとに窓全体の CPU の描画と変換が起き、引っかかりの主因の一つと分かった。Q1 が新しい WS を推し、ユーザー「Keiland の app の描画を GPU に、は、新WSで進めてください。着手OKです。」

## 目標

libkeiland の描画（塗り・角丸・影・線・文字・画像・clip・透明度）を Vulkan の draw で行い、app の frame の経路で CPU が画素を触らない。app は今の libkeiland の API のまま（できる限り）で GPU の描画になる。

## 範囲（p001 で確定）

libkeiland の canvas・text・image の描画の back end、glyph の atlas、app の surface の Vulkan の swapchain（Wayland の WSI）か GPU の buffer の共有、Linux・FreeBSD でも同じ（標準の Vulkan だけ、Guardrail の OS 境界）、Pi 4（V3D 4.2）と i915・Venus・llvmpipe での確かめ、CPU の back end を残すか（GPU が無い時の fallback）。

## 完了の条件（仮、p001 で確定）

Pi 4 で標準 app（Files・Settings・Phone など）の再描画が CPU の画素の処理なしで 60 fps に近い（実機、ユーザーの確認）。i915・Venus・llvmpipe で見た目が今と同じ（T1 の回帰）。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| p001 | 調査と設計（今の canvas・text・image の作り、app の present の経路、GPU の back end の形、shader、fallback、移行の順、試験） | planned |
