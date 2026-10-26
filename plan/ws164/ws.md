<!-- awesome-plan project=zedbsd record=ws164 -->
# WS164: OS の起動時の Welcome の画面

Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001・p002 cleared、UAT は 2026-10-13 ユーザー「UATの結果待ちのものは…clearedに」）。旧: incomplete（ベータ2 の範囲は完了（2026-10-13、2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」）、残りは規約の全文の見直し（ベータ3、p003）。旧: incomplete（2026-10-08 Q1 の判…。残り・制限: p003 の規約の全文は一括の見直しへ
Master: [master](../master.md)
Primary Milestone: MG006

## 由来

ユーザー（2026-10-05）「OS起動時のWelcome画面（すでにあるStartでもいいかも。要検討）」

## 範囲（案、p001 の設計で確定する）

最初の起動（または毎回）の Welcome の画面。既存の Start（Files の Today など）で足りるかの検討から始める。

## Phase

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws164-p001](phase001/phase.md) | 要件と設計 | cleared（2026-10-08 Q1 の判定（sweep-beta2-rc2 §2）: H1〜H4 決定済み、p002 が cleared。旧: planning（設計の第 1 版、2026-10-05 P1 q732。2026-10-05 夜 H1〜H4 決定済み、cleared の…） | — |
| [ws164-p002](phase002/phase.md) | 設定の key・compositor の起動・Settings の welcome の mode・host 試験・T1 | cleared（2026-10-06 Q1 判定: T1-269 の QEMU で期待どおり。実機は UAT）（2026-10-08 q910 P2 の照合で phase.md に合わせた。旧: in-progress（2026-10-06 q821 P2: Settings の側と compositor の起動を実装、host PASS。QEMU は …） | p001、H1〜H4 |
| ws164-p003 | 全文規約の見直し | planned | p002 |
