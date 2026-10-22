<!-- awesome-plan project=zedbsd record=ws282 -->

# WS282 camera の driver の枠組み（/dev/camN）と USB Video Class の driver「uvc」

- Master: [plan/master.md](../master.md)（2026-10-23 作成）。Primary Milestone: MG006。Related: MG008（Pi 5）。
- Status: planning
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
| p002 | 枠組み: `include/uapi/camera.h`（layout と `_Static_assert`）、`src/drivers/camera/cam.c`（cdev と退役（停止の一本化・admission）、所有、0 埋めの buffer と mmap、slot の API、queue、DEQUEUE の timeout、poll、metadata、counters、状態機械、FAULT）、host の試験（design §11、6〜8 LW） | p001 | planned |
| p003 | uvc の bind と descriptor の解析（MJPEG の 11 byte の形、GUID の byte 列）と dump、VS の claim、PROBE/COMMIT と補正、`ENOSPC`、登録、build。devfs の 0600（Q1）。板（5330 + HDMI capture + webcam）で dump、仕様の節の確定（3〜4 LW） | p002 | planned |
| p004a | xHCI の HS isoch IN の実地（URB の予約・投入の順の ring・worker・halt だけ、`camcap --probe-only`）。xHCI の不具合は BUG の ticket にして WS197 へ（2〜3 LW） | p003 | planned |
| p004b | streaming（payload の解析と frame の組み立て、COMMIT の再送、watchdog、bulk は host 試験のみ）、`camcap`・`camcommon`。板で最初の frame（P ≤ 1024 の mode、5〜7 LW） | p004a | planned |
| p005 | xHCI の HS の high-bandwidth（encoder・TBC/TLBPC・enqueue、WS197 の source、D10）。**前提: WS197 p007a2 の L2 が PASS して main にある事**、Q1 が順を決める。T1 に `bt-sco-loopback` の前後比較と HID（4〜5 LW） | p004b、WS197 p007a2 | planned |
| p006 | `camcheck` と `pattern.html`（D11）で画素の試験、HDMI capture の YUYV と 1080p の受け入れ、drop の測定、stop/start 100 回の leak、2 つ目の open、device の quirk、bulk の device があれば実地（3〜4 LW） | p004b（高い mode は p005） | planned |
| p007 | Linux の xHCI の quirk の表 → Pi 5・Pi 4・VF2 の確認（T1）、IRQ の中の staging の複写の時間（1.5〜2.5 LW） | p006 | planned |
| p008 | `CAM_MEMORY_IMPORT`（GPU core と bcm2711 の share の差分は WS282 の担当が書き Q1 が merge、D9）、`camvk`（4〜5 LW） | p004b、p007（Pi 5）、WS279 の V3D | planned |
| p009 | `docs/reference/camera.md`、`KERN_SYSTEM_EVENT_CAMERA` と sessiond の seat（Q1 に依頼）、`camcap`・`camcheck` を Tools に（1〜2 LW） | p006 | planned |
| p010 | control（UVC の processing unit、id 32 以降。ユーザー「最初は無し」なので後で、2〜3 LW） | p004b | planning |
| p011 | 規約の全文の見直しと license の監査（2 LW） | p008・p009 | planned |
| p012 | SuperSpeed の isochronous（TD の分割、FIFO の定数、WS197 の source）。USB 3 の device を買ってから（見積もり未定） | p005、device | planning |

## 現在の状態・再開点

- 2026-10-23: 作成。p001 の設計から始める。
- 2026-10-23 P3: p001 の設計の版 1（CSI）。ユーザーの判断 D1〜D7 は推しで決定。Q1 の手配の review（H3・M11・L7）。
- 2026-10-23 P3: 範囲の変更（UVC を優先、CSI は今回やらない）を受けて版 2。review の枠組み・buffer・cache・所有・devfs/sessiond の指摘を反映、CSI は付録 A に。HDMI capture は使える（制約: 今の xHCI の isochronous は 1024 byte/interval まで、p005 で拡張）。再開点: Q1 の判定と版 2 の review の後、p002 から。Q1 の判断 D9〜D11（design §6.2）。
- 2026-10-23 P3: 版 2 の review（H 4・M 10・L 8）を受けて版 3。要点: HS の isoch IN は未実地なので p004 を p004a（実地）と p004b に分け、p005 は WS197 p007a2 の L2 の後（計画に無い依存）。帯域の足りない mode は `ENOSPC`（HDMI capture の YUYV は p005 の後）。bulk は 1 本で未実地。URB は投入の順の ring、32 KiB の予約。frame の組み立てを書き直し。停止は枠組みだけ。合計 32〜43 LW（p010 を除く）。再開点: Q1 の判定と版 3 の review の後、p002 から。ユーザーに提示中: D11（pattern は PC の browser）、D12（webcam）。
