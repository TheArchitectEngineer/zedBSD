<!-- awesome-plan project=zedbsd record=ws149-p001 -->
# ws149-p001: Settings の Security の頁の検討と結論の案

Parent: [WS149](../ws.md)
Status: cleared（2026-10-06 ユーザーの決定（Q1 経由のクリック）: (a) 頁を無くす。頁を取り除くだけで、他の設定は作らない。実装は [ws148-p002](../../ws148/phase002/phase.md)（q824））
Disposition: normal
Queue: q821（P2 の第 1 段の列）

## 調べた事（2026-10-06 の main の source）

| 候補の項目 | 今の実体 | 判定 |
| --- | --- | --- |
| 画面の自動の lock の時間 | 有る。compositor の `--lock-idle=秒`（既定 10 分、`main.c` の `MAIN_LOCK_IDLE_MS`、`shell.c` が無操作を見て lock）。利用者が変える口（設定の key）が無い | **Security の中心の項目**。設定の key `lock.idle`（分: 1・5・10・30・しない）を足し、compositor が読む |
| 蓋を閉じた時の lock・sleep からの復帰の password | 蓋は ws132-p008 で lock（15 分以内の開けは password 無し）。sleep（S0i3）は WS052 の後 | 15 分の猶予の on・off は将来の項目（今は固定） |
| login の password・PIN | Users の頁（password の変更、PIN は ws172・`page-users-pin.c`） | Users に有る。Security からは Users への link だけ |
| firewall | packet filter が無い（kernel に無い） | 対象外（別の WS が要る） |
| disk の暗号化 | 無い | 対象外 |
| 更新の方針 | 更新の仕組みは WS152（ベータ3） | 対象外（Updates の頁） |
| SSH | Sharing の頁（Remote Login、ws089-p025） | Sharing に有る |
| 保存した Wi-Fi の鍵 | Network・Wi-Fi の頁（保存した network の一覧と消去） | Network に有る |
| USB の device の許可 | 無い | 対象外 |

## 結論の案（ユーザーが選ぶ）

| 案 | 内容 | 規模 |
| --- | --- | --- |
| **(b) 頁を残して自動の lock を置く（推奨）** | Security の頁に「Screen lock」の card: 「Lock after」（1・5・10・30 分・Never、`lock.idle`、compositor が `lock_idle_ms` に反映、すぐ効く）と「Lock Now」の button。下に「Password and PIN」の行（Users の頁へ）。設定の key は共有の表（settings-keys.c）、compositor の読みは wayland（P1 の領域と調整） | 1 LW |
| (a) 頁を消す | 自動の lock の時間を Display か Power（無い）に置く。Security の頁を表から外す | 0.5 LW |

推奨の理由: 自動の lock の時間は利用者が変えたい項目で、実体（compositor の lock）があり、置き場所は Security が自然。password・PIN への入口も
ここにあると見つけやすい。

## 次

ユーザーの判断の後に ws149-p002（(b) の設計と実装、または (a) の削除）。
