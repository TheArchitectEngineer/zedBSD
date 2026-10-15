<!-- awesome-plan project=zedbsd record=ws248-p001 -->

# ws248-p001: AX211 の driver の出典を block ごとの referenced: に

Status: in-progress（2026-10-15、q1201 P2）
Parent: [WS248](../ws.md)
Approval: 2026-10-15 ユーザー（ws.md の原文）

## 範囲

ws.md の「完了の条件」の出典の 3 項目。src/drivers/wifi/intel-ax211/ と include/drivers/wifi/intel-ax211/ の comment だけ。動作は変えない。

## 確かめ

build warning 0、AX211 の host 試験、`cpp -fpreprocessed -P` の前後の比較で comment 以外に差が無い。

## 結果

（未記入）
