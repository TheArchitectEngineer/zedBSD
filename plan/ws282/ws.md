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
| [p001](phase001/phase.md) | 設計（/dev/camN の API、uvc、xHCI の isochronous の制約、試験、Phase）→ [design.md](design.md)（版 2。版 1 の review は [phase001/review-20261023.md](phase001/review-20261023.md)） | — | in-progress（2026-10-23 P3、版 2。Q1 の判定待ち） |
| p002 | 枠組み: `include/uapi/camera.h`、`src/drivers/camera/cam.c`（cdev と退役、所有、buffer と mmap、slot の API、queue、poll、metadata、counters、状態機械）、host の試験（design §11、6〜8 LW） | p001 | planned |
| p003 | uvc の bind と descriptor の解析と dump、VS の claim、PROBE/COMMIT、登録、build。devfs の 0600（Q1）。板（5330 + HDMI capture）で dump（3〜4 LW） | p002 | planned |
| p004 | streaming（isochronous・bulk、worker、payload の組み立て、停止）、`camcap`。板で最初の frame（5〜7 LW） | p003 | planned |
| p005 | xHCI の isochronous の拡張（HS の high-bandwidth、SS、WS197 の source、Q1 の承認）、T1 に SCO・HID の回帰（3〜4 LW） | p004（並行可） | planned |
| p006 | `camcheck` と pattern で画素の試験、drop の測定、堅牢化（stop/start・抜去・2 つ目の open）、device の quirk（2〜3 LW） | p004 | planned |
| p007 | Pi 5・Pi 4・VF2 の確認（T1、1〜2 LW） | p006 | planned |
| p008 | `CAM_MEMORY_IMPORT`（GPU が確保した buffer を camera が取り込む、D3。GPU core と bcm2711 の share の差分）、`camvk`（4〜5 LW） | p004、WS279 の V3D | planned |
| p009 | `docs/reference/camera.md`、sessiond の seat（Q1 に依頼）、`camcap`・`camcheck` を Tools に（1〜2 LW） | p006 | planned |
| p010 | control（UVC の processing unit、id 32 以降。ユーザー「最初は無し」なので後で、2〜3 LW） | p004 | planning |
| p011 | 規約の全文の見直しと license の監査（2 LW） | p008・p009 | planned |

## 現在の状態・再開点

- 2026-10-23: 作成。p001 の設計から始める。
- 2026-10-23 P3: p001 の設計の版 1（CSI）。ユーザーの判断 D1〜D7 は推しで決定。Q1 の手配の review（H3・M11・L7）。
- 2026-10-23 P3: 範囲の変更（UVC を優先、CSI は今回やらない）を受けて版 2。review の枠組み・buffer・cache・所有・devfs/sessiond の指摘を反映、CSI は付録 A に。HDMI capture は使える（制約: 今の xHCI の isochronous は 1024 byte/interval まで、p005 で拡張）。再開点: Q1 の判定と版 2 の review の後、p002 から。Q1 の判断 D9〜D11（design §6.2）。
