<!-- awesome-plan project=zedbsd record=ws173 -->
# WS173: AAT（Agent Acceptance Test）— エージェントが素の実機を SSH で操作して受け入れを確かめる枠組み

Status: planning（2026-10-05 追加、最優先。UAT はこの後に遅らせる）
Master: [master](../master.md)
Primary Milestone: MG006

## 由来（ユーザー、2026-10-05 夕）

Q1 の問い「実機をベアメタル起動したときに、SSH越しにマウスイベントを投げて、スクリーンショットを撮り、それを取得するようなコマンドを作れますか？」への Q1 の案（/dev/input-inject にマウスとキーボードを足す、試験の image だけの画面の撮影、host の道具）に、ユーザー「では、それを実装して、AAT (Agent Acceptance Test)としてください。UATの必須確認項目は今挙げてくれたようなデバイス系と、全体の使用感に絞ります。AATの枠組みが完成した時点で実装されている機能でイメージを作成して、AATを実施、フィードバックを記録してください。UATはそのあとに遅らせます。UATの時刻は追ってお知らせします。」

## 決まったこと

- 実装の承認: `/dev/input-inject` にマウス（相対の移動・button・wheel）とキーボードの種類を足す（UAPI `include/uapi/input-inject.h` の追加、承認済み）。画面の撮影の口は試験の image だけ（製品には入れない）。host の道具。
- UAT の必須の確認は、デバイス系（電源 button・蓋・USB の抜き差し・touchpad の指・YubiKey と NFC の実物・音・Wi-Fi の実機の電波・S0 idle）と全体の使用感に絞る。それ以外は AAT。
- AAT の枠組みができた時点で実装済みの機能で image を作り、AAT を行い、結果（feedback）を記録する。UAT はその後（時刻はユーザーが後で知らせる）。

## Phase

| Phase | 内容 | 担当 | Status | 依存 |
| --- | --- | --- | --- | --- |
| p001 | kernel: `/dev/input-inject` にマウスとキーボード（UAPI の追加、試験の kernel の設定だけ） | P1 | planning | — |
| p002 | compositor: 試験の image だけの画面の撮影の口と `keiland-shot`（i915 の実機で合成した画面を読み戻して PNG、QEMU でも同じ口） | P1 | planning | — |
| p003 | host の道具 `plan/tools/aat/`（SSH で click・drag・wheel・key・type・shot の取得・log の行の待ち）と AAT の image の config | P2 | planning | p001・p002 |
| p004 | AAT の項目の一覧（今の実装済みの機能、UAT の項目から機器と使用感を除いた物）と判定の基準 | Q1 | planning | — |
| p005 | 素の 5330 で AAT を行い、結果を記録 | T1 か専任 | planning | p001〜p004、素の起動の方法（ユーザーの判断） |
