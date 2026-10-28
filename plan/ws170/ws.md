<!-- awesome-plan project=zedbsd record=ws170 -->

# WS170: Phone の app（連絡先・SMS/MMS/RCS・VoIP を統合したタイムライン）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」。根拠: p000〜p004 cleared（連絡先とタイムライン、T1-298）、UAT は 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」。本物の SMS・通話は WS197）
<!-- retained-after-completion --> phase001 の phase.md は残す: userland/desktop/include/keiland/keiland.h:1654、libkeiland/system/kl-system-protocol.h:665、phone/phone.h:13、wayland/phone-shell.c:9 の comment が設計の §3 と保存の形を参照している。source の comment を直す（または仕様を docs/ へ移す）までの間
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

最初の範囲（ユーザー）: 連絡先から連絡先ごとのタイムラインを表示するところまで。最終の世界観は、キャリアの技術（SMS・MMS・RCS・回線の通話）とインターネットの技術（VoIP、将来の標準）をブリッジし、1 つの app で連絡先ごとに統一の操作で扱うこと。

## ユーザーの決定

- 2026-10-05:

「[Phone app]
- 連絡先、SMS/MMS/RCS、VoIPを統合した総合phoneソフト
- 連絡先ごとにiOSのメッセージ風のタイムラインがある
- 連絡先のタイムラインからメッセージか電話ができる
- コンポジタにメッセージを操作するAPIを追加する
- コンポジタバックエンドがサポートしていればSMSを送受信可能
- バックエンドはローカルのモデムをサポートできるし、連携スマホアプリ経由で送受信も可能
- スマホ連携はBluetoothではなくIP経由も可能で、SMS, MMS, 通話をブリッジできる
- 連携するスマホは持ち歩いてもいいし、家に固定しておいてもいい
- 家の中でスマホを持ち歩かず部屋で充電しておいて、Keilandタブレットを家中持ち歩くユースケース
- キャリア回線のSMS, MMS, 通話は、キャリア通信を融合する目的
- 将来はキャリア系の技術でなくインターネット技術に移行することを見越した世界観
- キャリア技術、インターネット技術をブリッジして１つのアプリに融合し統一操作できる世界観
- メッセージや添付ファイルは失われないように、クラウドストレージにバックされたローカルフォルダに入れて管理する(~/Documents/以下はクラウドbackedとみなす)
- VoIPは対応プロバイダをバックエンドで追加可能。電話番号対応のゲートや、VoIPのみのゲートがあると思う。ただこれはオプション
- チャット/メッセンジャーは無理に統合せず、サービスのネイティブのWebやアプリを推奨しつつ、APIバックエンドで一部統合できるようにするオプション機能
- メッセージングは流行り廃りがあり、今だとRCSにフォーカスするのがいいと思うが、将来はキャリア技術でなくインターネット技術の標準ができるかもと思う
- まずは連絡先からタイムラインを表示するところまで実装
- 本物のメッセージ送受信や通話はあとで実装」

- 2026-10-05 11 時前「WS170は、ガワのインタフェースのモックだけでよいので、優先して作ってみてください。12時に予定しているUATのあとでいいです。おそらく17時か18時頃にもう一度UATをやるので、そのときにあればうれしいですが、必須ではないです。」
- 2026-10-08 ユーザー: 規約の全文の Phase はベータ3 → p005 は WS の外の一括の見直しへ。
- 2026-10-10: Bluetooth のスマホの bridge（SMS の MAP、連絡先の PBAP、通話の HFP）の層の流れと interface は [WS197](../ws197/ws.md) p004 で確定（ユーザーの指示で P1）。
- 2026-10-13「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」

## 結果

- Phone の app（`userland/desktop/phone/`: 連絡先の一覧と iOS のメッセージ風のタイムライン、送信の欄と電話の button）、連絡先と会話の `~/Documents/` の保存、compositor のメッセージの API の骨格（KL_VERSION 55）と偽の backend が main にある。2026-10-10 に WS197 p004b・p004c が保存（E.164 の key、Source ごとの index）・同期・「Use as phone」・SMS の通知を足した。
- 証拠: QEMU の T1-298 の AAT（needs-person、Q1 が PNG で目視: SEND・STATUS・RECEIVED の log と Echo の PNG、backend 0 で拒否、p002〜p004）。実機は 2026-10-13 のユーザーの一括の cleared（個別の観察の記録は無い）。

## 制限・移管

- 本物の SMS・MMS・RCS の送受信、モデム、スマホの bridge、VoIP、chat の API の統合は範囲の外。Bluetooth のスマホの bridge は WS197。
- 規約の全文（p005）は WS の外の一括の見直しへ。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws170-p000 | UI の mock（連絡先の一覧とタイムライン、固定の試験 data） | cleared（T1-177b の撮影の後、p002〜p004 で実装） |
| ws170-p001 | 要件と設計（保存の形・API・backend の口・UI） | cleared |
| ws170-p002 | 連絡先と会話の保存（`~/Documents/`） | cleared（T1-298） |
| ws170-p003 | Phone の app: 連絡先の一覧とタイムライン | cleared（T1-298） |
| ws170-p004 | compositor のメッセージの API の骨格と偽の backend | cleared（T1-298） |
| ws170-p005 | 規約の全文の見直し（directory なし） | 規約の全文は WS の外の一括の見直しへ |

## 記録の所在

phase001 は上の理由で残す。他の Phase の記録と試験は削除する（Q1）。git の commit `fb1f6afee` の `plan/ws170/` にある。Queue の履歴は [plan/history](../history/index.md)。
