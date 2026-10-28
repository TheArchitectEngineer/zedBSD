<!-- awesome-plan project=zedbsd record=ws210 -->

# WS210: Keiland の app の描画を GPU へ（libkeiland の canvas・文字・画像を Vulkan の draw に）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 ユーザー「これはすでにVF2での最適化で完了しています。completedにしてください。」。同じ日の P3 の精査 q1608 の「incomplete・保留（描画の最適化の終了）」をこの指示が置き換えた）
Completed: 2026-10-27（ユーザーの指示）
Primary Milestone: MG006
Related Milestones: MG008
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

libkeiland の描画（塗り・角丸・影・線・文字・画像・clip・透明度）を Vulkan の draw で行い、app の frame の経路で CPU が画素を触らない。app は今の libkeiland の API のまま GPU の描画になる。

## ユーザーの決定

- 2026-10-12（由来）「PI4のような低消費電力のモバイルSoCでは、画像をCPUで触ると、体験を損ないます。すべてGPUで処理する必要があります。」「Keiland の app の描画を GPU に、は、新WSで進めてください。着手OKです。」
- 2026-10-12「承認します。」（G1〜G5 は推しの (a)）: G1 描画の口は同じ・window の frame の口を足す、G2 CPU の back end を残す、G3 Text Editor・PDF Viewer・Image Viewer・Notes の overlay と Calendar の 3D・Video Player の色の変換も WS210、G4 線の端を capsule の SDF に、G5 完了は「app の側の CPU の画素の処理 0、Pi で前より速い（数で）」。
- 2026-10-12（Settings の network の graph）「列の帯の quad で描いて、GPU だけで済ませる」
- 2026-10-13「いや、これ以上詰めなくていいです。SMPに進みます。…」（描画の最適化をここで終える）
- 2026-10-27「これはすでにVF2での最適化で完了しています。completedにしてください。」

## 結果

- 中間の直しを実装した（main）: app の present を変わった所の copy に（p013、staging から swapchain の image へ、image ごとの古い矩形）、libkeiland の frame の枠組み（p014: 描いた時と今の hover、kl_ui の F1・F3、7 app の take_frame、window ごとの pacing（KL_VERSION 92、Settings ほか 7 app が申請））。
- GPU の canvas の核（p003〜p012）は着手していない。ユーザーの 2026-10-27 の指示により、目標は VF2 での最適化で達成とした。
- host の試験: present の部分の copy（run-host-present-damage.sh）、Terminal の frame の pacing（run-host-terminal-pace.sh）。

## 制限・移管

- p013 の E5 の方式の比べ、p014 の (d) ペインの部品（F4）と Pi の確認は未実施。
- [BUG-292](../bugs/BUG-292.md)（p014 の review で見つかった前からの問題、tracking）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws210-p001 | 調査と設計 | in-progress のまま WS を閉じた（設計 1.2） |
| ws210-p002 | spike（bcm2711 の shader の数え、Pi の draw の費用） | in-progress のまま WS を閉じた（host の数は済み、Pi は未着手） |
| ws210-p003〜p012 | GPU の canvas の核・atlas・frame の口・app の移行・Calendar・Video Player・T1・規約 | 未着手のまま WS を閉じた（ユーザー「これはすでにVF2での最適化で完了しています。」） |
| ws210-p013 | 中間: app の present を変わった所の copy に | in-progress のまま WS を閉じた（実装は main、T1・Pi は未実施） |
| ws210-p014 | libkeiland の frame の枠組み | in-progress のまま WS を閉じた（(a)(b)(c) は main、(d) は保留） |

## 記録の所在

Phase の計画・設計・review は削除した。git の commit `fb1f6afee` の `plan/ws210/` にある。Queue の履歴は [plan/history](../history/index.md)。
