<!-- awesome-plan project=zedbsd record=ws148-p001 -->
# ws148-p001: Settings の Privacy の頁の検討と結論の案

Parent: [WS148](../ws.md)
Status: cleared（2026-10-06 ユーザーの決定（Q1 経由のクリック）: (a) 頁を無くす。推奨どおり Files の Recents に「履歴を消す」、Storage に「最近の項目を残す」。実装は [p002](../phase002/phase.md)（q824））
Disposition: normal
Queue: q821（P2 の第 1 段の列）

## 調べた事（2026-10-06 の main の source）

| 候補の項目 | 今の zedBSD・Keiland の実体 | 判定 |
| --- | --- | --- |
| 最近使った file の履歴 | 有る。libkeiland の `recent.c`（`$XDG_DATA_HOME/keiland/recent` の text、Files の Recents と各 app の open recent が同じ一覧を使う）。止める・消す口は無い | **Privacy の唯一の実体**。「履歴を残す」の on・off と「履歴を消す」が置ける |
| Trash の自動の削除 | 無い（Storage の頁に Trash の大きさと「空にする」、ws089-p023） | Storage の頁の方が自然。Privacy に置かない |
| 画面の lock の時間、lock の画面の通知 | lock の時間は compositor の `--lock-idle`（既定 10 分、`kwl.h` の `lock_idle_ms`）で、設定の key は無い。通知は WS156 が未実装 | Security の頁（WS149）に置く |
| location・camera・microphone の app ごとの許可 | location の service は無い。camera の driver は無い（`uvc` 等の source が無い）。音の入力（録音）を app に許す仕組みも無い。app ごとの許可の仕組み（portal に当たる物）が無い | 対象外（実体が無い） |
| 画面の共有・録画の許可 | screencopy に当たる protocol が無い（撮影は試験用の `keiland-shot` だけ） | 対象外 |
| 診断・crash の報告の送信 | 外へ送る仕組みが無い（送らない方針） | 対象外 |

## 結論の案（ユーザーが選ぶ）

| 案 | 内容 | 規模 |
| --- | --- | --- |
| **(a) 頁を消す（推奨）** | Privacy の頁を Settings の表から外す（`pages.c`・検索の語・glyph）。最近の履歴の on・off と消去は、Files の Recents の頁に「Clear Recents」を、Settings の Storage の頁の下に「最近使った項目を残す」の switch を置く（Privacy の頁を 1 項目のために残さない） | 0.5 LW |
| (b) 頁を残す | Privacy の頁に「最近使った項目を残す」（switch、`recent.keep`）と「履歴を消す」（button）の 1 枚の card。将来 app の許可の仕組み（camera・microphone・画面の共有）ができたらここに足す | 0.5 LW |

推奨の理由: 今の Keiland で Privacy に当たる設定は最近の履歴だけで、1 項目の頁は利用者に「何もない頁」に見える（ユーザーの元の指摘）。
Security（WS149）・Accessibility（WS151）の検討と合わせて判断してほしい（[WS149 の案](../../ws149/phase001/phase.md)、[WS151 の案](../../ws151/phase001/phase.md)）。

## 次

ユーザーの判断の後に ws148-p002（(a) の削除と Files・Storage の項目、または (b) の頁）。
