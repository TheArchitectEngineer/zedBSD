<!-- awesome-plan project=zedbsd record=ws146 -->

# WS146: SSH を使う独自のオンラインストレージ（差分の同期と on-the-fly のアクセス、OneDrive のような機能）

<!-- awesome-plan-current:start -->
Status: planning（最初に「アイディアだけ描く」Phase を実行しユーザーと議論、2026-10-05 ユーザー）。以前: planning
Primary Milestone: MG006
Related Milestones: MG005
Parent: [Master](../master.md)
Queue: なし（「あとで」の要望、時期は未定）
Resume point: 未着手。時期が決まったら p001（調査と設計）から。
Target: **ベータ4 以降**（2026-10-05 user「WS037, WS044,WS048,WS141, WS112, WS118, WS124, WS125, WS126, WS119, WS096, WS097, WS039, WS038, WS144, WS143, WS146,WS147, WS152,  WS119, WS080, は、ベータ4以降としてください。…WS027, WS015, WS047, WS028, WS017,  WS077, はキャンセルします。」）
<!-- awesome-plan-current:end -->

## 単一目標

SSH で接続できる server を利用者のオンラインストレージとして使い、file の木を差分で同期し、手元に file の本体が無くても on-the-fly に開けるようにする（OneDrive の「必要な時に取得」に当たる）。

## ユーザーの要望（2026-10-04 夜、原文）

「さらに、あとでSSHベースの独自オンラインストレージシステムを作って、初回にサーバへPythonスクリプトか何かを転送し、それを使って、ファイルツリーとメタデータのデータベースを管理して、差分で同期でき、さらにon-the-flyでアクセスできてローカルにファイル本体がなくてもいいという、OneDriveのような機能を作りたいと思いました。」

## 追加の要件（2026-10-05 ユーザー）

ユーザー「SSHベースのクラウドストレージについて、動画ストリーミング再生ができることを要件に入れておいてください。」→ **要件: 手元に本体の無い動画を、全部を取得する前に再生できる（stream の再生、seek すると必要な範囲だけを取る）**。on-the-fly のアクセスは範囲の読み（offset と長さ）で取り、先読みを持つ。player（WS122）は普通の file として読めること。

## 範囲（p001 で設計して確定）

- **server 側**: 初回の接続で server に agent（Python の script など、server に要る物を最小に）を転送し、SSH の上で動かす。agent が file の木と metadata の database を管理し、変更の差分を返す。
- **client 側**: 同期の daemon。database の差分で双方向に同期し、衝突を扱う。file の本体は必要な時に取得する（placeholder・on-the-fly のアクセス）。手元の cache の管理。
- **on-the-fly のアクセスの仕組み**: [WS150](../ws150/ws.md)（userland の file system の kernel の枠組み、FUSE に当たる物）の上に作る（2026-10-04 ユーザー、WS150 が先）。
- **desktop**: Files（WS127）での表示（同期の状態・手元に無い印）、Settings の Sharing の頁での設定（[ws089-p025](../ws089/phase025/phase.md) の「あとでクラウドストレージ」）。Linux・FreeBSD の Keiland の扱い。
- 鍵・認証（SSH の鍵、Keiland の秘密の store）、暗号、server の agent の安全性（server の上で走る code）。

## Phase（案）

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| ws146-p001 | 調査と設計（agent と protocol、database と差分、衝突、on-the-fly のアクセスの kernel の口、Files・Settings、試験の方法） | planning | 時期の決定、WS150 |

## アイディアの Phase（2026-10-05 ユーザーの指示）

最初にアイディアだけを描く Phase（ws146-p000、code も詳細な設計も書かない、選択肢と論点を並べる）を実行し、ユーザーと議論してから先へ進む。
