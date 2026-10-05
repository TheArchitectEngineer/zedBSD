<!-- awesome-plan project=zedbsd record=ws150 -->

# WS150: userland の file system の kernel の枠組み（FUSE に当たる物、クラウドストレージの前提）

<!-- awesome-plan-current:start -->
Status: planning（要検討・ブロック、2026-10-05 ユーザー「要検討状態にしてブロックする」）。以前: planning
Primary Milestone: MG004
Related Milestones: MG002、MG006
Parent: [Master](../master.md)
Queue: なし（時期は未定）
Resume point: 未着手。p001（調査と設計）から。WS146・WS147 の前提。
<!-- awesome-plan-current:end -->

## 単一目標

kernel の VFS に、userland の daemon が file system を実装できる枠組み（FUSE に当たる物）を作る。手元に本体の無い file を開いた時に daemon から取り寄せる（on-the-fly のアクセス）などの、クラウドストレージの基盤にする。

## ユーザーの指示（2026-10-04 夜）

「OneDriveについては、クラウドストレージ用のカーネル機能のフレームワークを作るWSが先に必要と思います。FUSEみたいなものだと思います。」

## 範囲（p001 で設計して確定）

- kernel: VFS の新しい file system の種類。mount の時に userland の daemon と通信の channel（device の file か socket）を結び、lookup・getattr・readdir・open・read・write・create・unlink・rename・setattr などの要求を daemon へ送り、応答を待つ。timeout・daemon の死の扱い、page cache との関係、permission。
- protocol: Linux の FUSE の protocol と互換にするか独自にするか（互換なら libfuse 系の既存の実装を移植できる。license の境界を確かめる）を p001 で決める。
- userland: daemon を書くための library（最小）と、試験の file system（例: memory の上の file system、本体を後から取り寄せる placeholder の file system）。
- 使う側: [WS146](../ws146/ws.md)（SSH の独自のオンラインストレージ）と [WS147](../ws147/ws.md)（OneDrive）の on-the-fly のアクセス。Files（WS127）の表示（手元に無い印・同期の状態）の口は使う側の WS で。
- HAL の API の変更は要らない見込み（要るなら差分を plan に置いて承認）。

## Phase（案）

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| ws150-p001 | 調査と設計（VFS の口、要求と応答の protocol、FUSE との互換の判断、daemon の library、cache と一貫性、失敗の扱い、試験の方法） | planning | 時期の決定 |

## 要検討・ブロック（2026-10-05）

ユーザーの指示で要検討の状態にしてブロックする。ユーザーと方針を決めるまで Queue に入れない。
