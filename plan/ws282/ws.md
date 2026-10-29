<!-- awesome-plan project=zedbsd record=ws282 -->

# WS282 camera の driver の枠組み（/dev/camN）と USB Video Class の driver「uvc」

- Master: [plan/master.md](../master.md)（2026-10-23 作成）。Primary Milestone: MG006。Related: MG008（Pi 5）。
- Status: incomplete・保留（設計だけ、実装は後日）（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: 2026-10-23 ユーザー「おっと、意外と工数がかかりそうですね……。設計だけ残してラップアップして、後日実装することにします。」）。旧: incomplete（p001 の設計は済み、実装は後日）。残り・制限: p002〜p011（設計 版 3 は済み）
- 出典: 2026-10-23 ユーザー「では、MIPI CSIのカメラから取り込んだ画像をYOLOで画像認識することをゴールにして、・/dev/cam0 ... camera driver frameworkと、それに準拠した CSI camera driver "rpicam"・Pi5 VideoCore VIIでの compute・YOLOシリーズの非常に小さなモデルをC言語とVulkan Computeで実装・カメラ入力をobject detectionしてbounding boxを表示するデモアプリを構築 ここまでやりたいです。」
- 範囲の変更（2026-10-23 ユーザー、Q1 経由）:「USB Video classを優先しましょう。CSIは今回はやりません。/dev/camNのAPIはそのまま使います。uvcドライバを実装します。カメラは今から購入します。まずは手元にあるHDMIキャプチャでもデバッグには使えそうですか？」→ 最初の source は UVC。CSI（rpicam）は [design.md](design.md) §13 と付録 A（版 1 の調べ）に残し、別の WS で。
- 関係: WS282（camera）・WS283（compute）・WS284（YOLOX-Nano）・WS285（demo）の 4 つで 1 つの到達目標（camera の画像を YOLOX-Nano で検出し、bounding box を表示する）。

## 目的と完了の条件

- `/dev/camN`: camera の driver の枠組み。format は YUV・RGB・RAW・圧縮（MJPEG）を取れる interface（ユーザー「/dev/cam0 はYUV, RGB, RAWで取得できるインターフェースとする。」）。今回は uvc が YUYV と MJPEG を実装。
- `uvc`: USB Video Class の driver（descriptor の解析、PROBE/COMMIT、isochronous と bulk、payload の組み立て）。対象の板は x86（xHCI）・Pi 5・Pi 4・VF2。最初の device は手元の HDMI capture、次に買う webcam。
- 完了: HDMI capture（または webcam）から YUYV と MJPEG の frame が連続して取れ、PC の既知の画面の画素が `camcheck` で許容内、x86 と Pi 5/Pi 4/VF2 で同じ。GPU への取り込み（p008）は WS285 の前提。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計（/dev/camN の API、uvc、xHCI の isochronous の制約、試験、Phase）→ [design.md](design.md)（版 3。版 1 の review は [phase001/review-20261023.md](phase001/review-20261023.md)、版 2 の review は [phase001/review-v2-20261023.md](phase001/review-v2-20261023.md)） | — | in-progress（2026-10-23 P3、版 3。Q1 の判定と版 3 の review 待ち） |
| [p002](phase002/phase.md) | 枠組み: `include/uapi/camera.h`（layout と `_Static_assert`）、`src/drivers/camera/cam.c`（cdev と退役（停止の一本化・admission）、所有、0 埋めの buffer と mmap、slot の API、queue、DEQUEUE の timeout、poll、metadata、counters、状態機械、FAULT）、host の試験（design §11、6〜8 LW） | p001 | in-progress（実装済み・試験未実施、branch agent/p6-ws282c。mapping の寿命は fixture の直し（cam.c は契約どおり）） |
| [p003](phase003/phase.md) | uvc の bind と descriptor の解析（MJPEG の 11 byte の形、GUID の byte 列）と dump、VS の claim、PROBE/COMMIT と補正、`ENOSPC`、登録、build。devfs の 0600（Q1）。板（5330 + HDMI capture + webcam）で dump、仕様の節の確定（3〜4 LW） | p002 | in-progress（実装済み・試験未実施、agent/p6-ws282c。板の dump は未実施） |
| [p004a](phase004a/phase.md) | xHCI の HS isoch IN の実地（URB の予約・投入の順の ring・worker・halt だけ、`camcap --probe-only`）。xHCI の不具合は BUG の ticket にして WS197 へ（2〜3 LW） | p003 | in-progress（実装済み・試験未実施、agent/p6-ws282c。実地は未実施） |
| [p004b](phase004b/phase.md) | streaming（payload の解析と frame の組み立て、COMMIT の再送、watchdog、bulk は host 試験のみ）、`camcap`・`camcommon`。板で最初の frame（P ≤ 1024 の mode、5〜7 LW） | p004a | in-progress（実装済み・試験未実施、agent/p6-ws282c） |
| [p005](phase005/phase.md) | xHCI の HS の high-bandwidth（encoder・TBC/TLBPC・enqueue、WS197 の source、D10）。**前提: WS197 p007a2 の L2 が PASS して main にある事**、Q1 が順を決める。T1 に `bt-sco-loopback` の前後比較と HID（4〜5 LW） | p004b、WS197 p007a2 | in-progress（実装済み・試験未実施、agent/p6-ws282d。前提の WS197 p007a2 は未達） |
| [p006](phase006/phase.md) | `camcheck` と `pattern.html`（D11）で画素の試験、HDMI capture の YUYV と 1080p の受け入れ、drop の測定、stop/start 100 回の leak、2 つ目の open、device の quirk、bulk の device があれば実地（3〜4 LW） | p004b（高い mode は p005） | in-progress（実装済み・試験未実施、agent/p6-ws282d） |
| [p007](phase007/phase.md) | Linux の xHCI の quirk の表 → Pi 5・Pi 4・VF2 の確認（T1）、IRQ の中の staging の複写の時間（1.5〜2.5 LW） | p006 | in-progress（表と試験の config だけ、3 板の確認と複写の時間は未実施、agent/p6-ws282d） |
| [p008](phase008/phase.md) | `CAM_MEMORY_IMPORT`（GPU core と bcm2711 の share の差分は WS282 の担当が書き Q1 が merge、D9）、`camvk`（4〜5 LW） | p004b、p007（Pi 5）、WS279 の V3D | in-progress（実装済み・試験未実施、agent/p6-ws282d） |
| p009 | `docs/reference/camera.md`、`KERN_SYSTEM_EVENT_CAMERA` と sessiond の seat（Q1 に依頼）、`camcap`・`camcheck` を Tools に（1〜2 LW） | p006 | planned |
| p010 | control（UVC の processing unit、id 32 以降。ユーザー「最初は無し」なので後で、2〜3 LW） | p004b | planning |
| p011 | 規約の全文の見直しと license の監査（2 LW） | p008・p009 | planned |
| p012 | SuperSpeed の isochronous（TD の分割、FIFO の定数、WS197 の source）。USB 3 の device を買ってから（見積もり未定） | p005、device | planning |

## 現在の状態・再開点

- 2026-10-23: 作成。p001 の設計から始める。
- 2026-10-23 P3: p001 の設計の版 1（CSI）。ユーザーの判断 D1〜D7 は推しで決定。Q1 の手配の review（H3・M11・L7）。
- 2026-10-23 P3: 範囲の変更（UVC を優先、CSI は今回やらない）を受けて版 2。review の枠組み・buffer・cache・所有・devfs/sessiond の指摘を反映、CSI は付録 A に。HDMI capture は使える（制約: 今の xHCI の isochronous は 1024 byte/interval まで、p005 で拡張）。再開点: Q1 の判定と版 2 の review の後、p002 から。Q1 の判断 D9〜D11（design §6.2）。
- 2026-10-23: ユーザーの判断で設計だけ残して止める（「設計だけ残してラップアップして、後日実装することにします。」）。再開は実装の指示から。
- 2026-10-23 P3: 版 2 の review（H 4・M 10・L 8）を受けて版 3。要点: HS の isoch IN は未実地なので p004 を p004a（実地）と p004b に分け、p005 は WS197 p007a2 の L2 の後（計画に無い依存）。帯域の足りない mode は `ENOSPC`（HDMI capture の YUYV は p005 の後）。bulk は 1 本で未実地。URB は投入の順の ring、32 KiB の予約。frame の組み立てを書き直し。停止は枠組みだけ。合計 32〜43 LW（p010 を除く）。再開点: Q1 の判定と版 3 の review の後、p002 から。ユーザーに提示中: D11（pattern は PC の browser）、D12（webcam）。

## 実装だけの担当 P6（2026-10-29、q1667）

- 2026-10-29 01:3x ユーザー「P6は設計が決まっているWSについて、テストなしでひたすら実装だけ進めてテスト未完了でincompleteだけど実装はできていてマージされていない状態のWSを増やす。」
- ユーザー「調査しなくてよい、設計ができているか自明なWSについて、ひたすら書いてください。選択したもののうち、調査が必要なものはスキップします。WSごとにサブのサブエージェントを作ってよいです。」（選択: WS223・WS282〜WS284・WS243・WS296）
- 解釈（Q1）: 試験をせず実装だけを P6 の branch に積み、main に merge しない。Status は incomplete のまま（試験は未実施）。調べが要ると分かれば P6 は飛ばして Q1 に返す。
- 2026-10-29 01:56 P6 の配下: p002 の一部を実装（camera.h・cam.h・cam.c・amd64 の build の登録・camcommon、SHA 0eea3815d、cam.c は compile で warning 0）。P6 の指示（優先度）で止めた。未着手: cam-host-test、arm64/riscv64 の登録、p003〜p004b、camcap。再開点は [phase002](phase002/phase.md) の「再開点」。未 commit の物は無し。
- 2026-10-29 P6（q1667-i03、agent/p6-ws282b、基点 1190b629c）: p002 の続き。arm64・riscv64 の vmunix.mk に cam.c（5ff18cf2e、amd64・rpi5・visionfive2 の vmunix が rc 0・warning 0）、`plan/ws282/tests/cam-host-test.c` と `run-cam-host-test.sh`（f30258b62、compile だけ確かめた、流していない）。cam.c に mapping の寿命の不足を 2 つ読んだ（close で stream を止める、mapping が device の参照を持たない）。再開点は [phase002](phase002/phase.md) の末尾。

- 2026-10-29 P6（q1667-i04、agent/p6-ws282c、基点 c60cdbd44）: p002 の mapping の寿命の 2 点は cam.c でなく host の fixture が file の hold を写していなかった物と分かり、fixture を直した（cam.c は変えず）。rpi4 の vmunix も rc 0・warning 0。試験は未実施。続きは p003。
- 2026-10-29 P6（q1667-i04）: p003 の uvc（descriptor・bind・PROBE/COMMIT・登録・devfs の 0600）と p004a/p004b の stream・assembler を実装、amd64・rpi4・rpi5・vf2・pcat の vmunix が rc 0・warning 0。試験は未実施。詳細は [phase003](phase003/phase.md)。
- 2026-10-29 P6（q1667-i05、agent/p6-ws282d、基点 d3e2a737d）: p005（xHCI の high-bandwidth）・p006（camcheck・pattern.html）・p007（quirk の表と 3 板の config）・p008（GPU の allocation の backing、`CAM_MEMORY_IMPORT`、camvk）を実装。host の試験（cam・uvc・xhci-hb・camcheck）と build だけ、実機・QEMU は未実施。詳細は各 phase.md。
