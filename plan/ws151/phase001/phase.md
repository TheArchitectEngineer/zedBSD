<!-- awesome-plan project=zedbsd record=ws151-p001 -->
# ws151-p001: Settings の Accessibility の頁の検討と結論の案

Parent: [WS151](../ws.md)
Status: cleared（2026-10-06 ユーザーの決定（Q1 経由のクリック）: (a) 頁を無くす。頁を取り除くだけで、他の設定は作らない。実装は [ws148-p002](../../ws148/phase002/phase.md)（q824））
Disposition: normal
Queue: q821（P2 の第 1 段の列）

## 調べた事（2026-10-06 の main の source）

| 候補の項目 | 今の実体 | 実現に要る物 |
| --- | --- | --- |
| key のリピート | 有る（Keyboard の頁） | 済み（Keyboard に有る） |
| pointer の速さ・加速・自然なスクロール | 有る（Mouse・Touchpad の頁） | 済み |
| 動きを減らす（animation を止める） | 無い（compositor の窓の開閉・Wiseview・page の turn などの animation） | compositor の animation の時間を 0 にする設定 `motion.reduce`。compositor と libkeiland の motion（`motion.c`）が読む。小さい |
| 文字を大きく（UI の文字の倍率） | 無い（libkeiland の widget の文字は固定の pixel） | libkeiland の text の大きさの倍率と各 app の layout の追従。中〜大 |
| 高い contrast | 無い（light・dark の 2 つの theme） | libkeiland の theme の 3 つ目の palette と compositor の glass の無効化。中 |
| 画面の拡大（magnifier）、色の反転・色覚の補正 | 無い | compositor の合成の shader と拡大の入力。中〜大 |
| 固定 key（sticky keys）・遅い key | 無い（`sticky` の語は Wiseview の切り替えの別の意味） | compositor の key の処理。中 |
| 視覚の bell・mono の音声 | 無い | audiod と通知（WS156）。中 |
| 読み上げ（screen reader） | 無い（accessibility の API が無い） | AT-SPI に当たる API と読み上げの engine。大（別の WS） |

## 結論の案（ユーザーが選ぶ）

| 案 | 内容 | 規模 |
| --- | --- | --- |
| **(b1) 頁を残し、小さい 2 項目から（推奨）** | 「Reduce motion」（`motion.reduce`、compositor と libkeiland の animation を止める）と「Sticky keys」（compositor が Shift・Ctrl・Alt・Super を 1 回押しで次の key に掛ける）。Keyboard・Mouse の頁への link。文字の倍率・高い contrast・拡大は次の段の Phase、読み上げは別の WS | 1.5 LW（2 項目） |
| (b2) 頁を残し、文字の倍率から | 見る人に一番効く「Larger text」（libkeiland の倍率、Settings・Files・Text Editor の layout の追従）から | 2〜3 LW |
| (a) 頁を消す | 実体ができるまで頁を表から外す（Keyboard・Mouse に有る物で足りる、とする） | 0.5 LW |

推奨の理由: Accessibility は消すより残して育てる頁で、利用者が最初に期待する「動きを減らす」と「固定 key」は compositor の中で小さく作れる。
読み上げは大きいので別の WS として判断してほしい。

## 次

ユーザーの判断の後に ws151-p002（設計）・p003（実装）、または (a) の削除。
