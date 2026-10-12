<!-- awesome-plan project=zedbsd record=ws210 -->

# WS210: Keiland の app の描画を GPU へ（libkeiland の canvas・文字・画像を Vulkan の draw に）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-12 Q1 作成）
Primary Milestone: MG006
Related Milestones: MG008
Parent: [Master](../master.md)
Queue: [P1 の lane](../agents/P1/queue.md)
Resume point: p001 の設計 1.1（review-1 を反映、phase001/phase.md）を Q1 が確かめた後、p002 の spike（host の shader の数え、Pi の draw・頂点の費用）。中間の直し: p013（present の部分の copy、設計 1.1、Pi の baseline 待ち、次に優先）→ p014 の F2.2〜（p013 の commit の hook に載せる、Pi の hover の計測の後）。
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
| [p002](phase002/phase.md) | spike: bcm2711 の shader の命令数・register（host）、Pi の draw・頂点・fragment・compile の時間 | in-progress（host の数は済み、Pi は未着手） |
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
| [p013](phase013/phase.md) | 中間: app の present を変わった所の copy に（staging から swapchain の image へ直接、image ごとの古い所の list、方式は Pi の実測で選ぶ） | planning（設計 1.1、review-1 を反映、Pi の baseline 待ち） |
| [p014](phase014/phase.md) | libkeiland の frame の枠組み（(a) Settings・Files の描いた時と今の hover、(b) kl_ui の F1・F3、7 app の take_frame、(c) window ごとの pacing、(d) ペインの部品）。受け入れは app の描く面積と frame の数（compositor の側は p013 と別 WS） | in-progress（(a)(b) と app の移行は merge 済み。(c) F2.1 と F2.2〜F2.6 を実装（Settings が KL_PACING_MOTION・KL_PACING_FRAME を申請、KL_VERSION 92、Pi の place15 の log で WSI の FIFO に frame が並ぶと判断）、Pi の確認待ち。(d) 保留） |

見積 計 約 32 LW（p001 の設計 1.1 の Phase の表）。

## 2026-10-12 ユーザーの決定: G1〜G5

ユーザー「承認します。」→ G1〜G5 は推しの (a): G1 描画の口は同じ・window の frame の口を足す（review-1 の M3: 公開の struct を変えない）、G2 CPU の back end を残す、G3 Text Editor・PDF Viewer・Image Viewer・Notes の overlay と、後の Phase で Calendar の 3D・Video Player の色の変換も WS210（review-1 の M2・L7 を見積に）、G4 線の端を capsule の SDF にそろえる、G5 WS210 の完了は「app の側の CPU の画素の処理 0、Pi で前より速い（数で）」、60 fps は WS141 の案 (A) と合わせた目標（review-1 の M8・M9 を足す）。次は設計 1.1（review-1 の H1〜H3・M1〜M11・L1〜L8）→ p002 の最初に Pi の shader の受理と時間の spike。

## 2026-10-12 ユーザーの決定: Settings の network の graph

ユーザー「列の帯の quad で描いて、GPU だけで済ませる」→ Settings の network の通信量の graph（データで形が変わる半透明の polygon）は、CPU の rasterize の例外にせず、列ごとの帯の quad で GPU だけで描く（review-2 の M）。設計 1.3 で反映。
