<!-- awesome-plan project=zedbsd record=ws147 -->

# WS147: Microsoft OneDrive の client（Settings の Sharing の頁から設定）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG005
Parent: [Master](../master.md)
Queue: なし（時期は未定）
Resume point: 未着手。p001（調査と設計）から。
<!-- awesome-plan-current:end -->

## 単一目標

Settings の Sharing の頁で Microsoft のアカウントを登録し、OneDrive の file を zedBSD に同期・参照できるようにする。

## ユーザーの指示（2026-10-04 夜）

「SharingにOneDriveを追加したいです。これは独立WSにします。」

## 範囲（p001 で設計して確定）

- 認証: Microsoft identity platform の OAuth 2.0（device code の流れか browser の流れ）、token の保存（Keiland の秘密の store）、app の登録（client ID の扱い）。
- API: Microsoft Graph の OneDrive の API（drive の item の一覧・delta による差分・upload・download）。HTTPS・TLS は既存の OpenSSL（WS032）。
- 同期: 同期の daemon（差分・衝突）。手元に本体の無い on-the-fly のアクセスは **[WS150](../ws150/ws.md)（userland の file system の kernel の枠組み、FUSE に当たる物）の上に作る**（2026-10-04 ユーザー「クラウドストレージ用のカーネル機能のフレームワークを作るWSが先に必要」）。WS146 と同じ枠組みを共有する。
- desktop: Settings の Sharing の頁（[ws089-p025](../ws089/phase025/phase.md)）に OneDrive のアカウントの追加・削除・同期の状態、Files（WS127）での表示。
- Microsoft の API の利用規約・商標の扱い、外部の実装を使う場合の license の監査。

## Phase（案）

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| ws147-p001 | 調査と設計（認証・Graph の API・同期と cache・on-the-fly の口（WS146 と共有）・Settings と Files・試験の方法） | planning | 時期の決定、WS150 |
