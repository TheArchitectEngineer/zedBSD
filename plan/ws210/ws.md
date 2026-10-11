<!-- awesome-plan project=zedbsd record=ws210 -->

# WS210: Keiland の app の描画を GPU へ（libkeiland の canvas・文字・画像を Vulkan の draw に）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-12 Q1 作成）
Primary Milestone: MG006
Related Milestones: MG008
Parent: [Master](../master.md)
Queue: [P1 の lane](../agents/P1/queue.md)
Resume point: p001 の設計 1.1（review-1 を反映、phase001/phase.md）を Q1 が確かめた後、p002 の spike（host の shader の数え、Pi の draw・頂点の費用）。
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
| [p001](phase001/phase.md) | 調査と設計（今の canvas・text・image の作り、app の present の経路、GPU の back end の形、shader、fallback、移行の順、試験） | in-progress（設計 1.1、Q1 の確認待ち） |
| p002 | spike: bcm2711 の shader の命令数・register（host）、Pi の draw・頂点・fragment・compile の時間 | planned |
| p003 | GPU の canvas の核（記録、shape の pipeline、W の readback、host の一致の試験） | planned |
| p004 | 文字の atlas・画像の cache・polygon の coverage・mark・fill_replace | planned |
| p005 | window の frame の口、基準の PNG と Pi の基準、Settings を移す | planned |
| p006 | Pi の計測と調整 | planned |
| p007 | widget・chooser・picker と app の第 1 組を移す | planned |
| p008 | Text Editor・PDF Viewer・Image Viewer と Notes の overlay・IME の popup | planned |
| p009 | Calendar の 3D を GPU に | planned |
| p010 | Video Player の色の変換を GPU に | planned |
| p011 | T1 の QEMU の回帰と Pi の実機の確認 | planned |
| p012 | 規約の全文の見直し | planned |

見積 計 約 32 LW（p001 の設計 1.1 の Phase の表）。

## 2026-10-12 ユーザーの決定: G1〜G5

ユーザー「承認します。」→ G1〜G5 は推しの (a): G1 描画の口は同じ・window の frame の口を足す（review-1 の M3: 公開の struct を変えない）、G2 CPU の back end を残す、G3 Text Editor・PDF Viewer・Image Viewer・Notes の overlay と、後の Phase で Calendar の 3D・Video Player の色の変換も WS210（review-1 の M2・L7 を見積に）、G4 線の端を capsule の SDF にそろえる、G5 WS210 の完了は「app の側の CPU の画素の処理 0、Pi で前より速い（数で）」、60 fps は WS141 の案 (A) と合わせた目標（review-1 の M8・M9 を足す）。次は設計 1.1（review-1 の H1〜H3・M1〜M11・L1〜L8）→ p002 の最初に Pi の shader の受理と時間の spike。
