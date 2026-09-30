# デモの S8・S9 の確かめ方（実機と Windows の QEMU、ユーザー向けの 1 枚）

ws079-p016。Linux の QEMU では `plan/ws079/tests/demo-s8-s9.sh` が自動で通す（注入の touch と pen）。ここは人が触って確かめる手順。
所要は 5 分ほど。

## 用意

- 起動する image: デモの image（既定の image でよい。Notes と PDF Viewer が入っている）。
- 試す PDF: A4 の 10 頁の文書を 1 つ、home の `Documents` に置く。Linux の host で `sh plan/ws079/tests/make-a4-document.sh a4.pdf` で作れる
  （quilt の説明書の先頭 10 頁を A4 にしたもの）。USB メモリか Files の copy で入れる。
- 5330 の実機は mouse と keyboard で、Windows の QEMU は touch の画面（指）で触る。

## S8: Notes（右上の角）

| # | 実機（mouse） | Windows の QEMU（touch） | 見ること |
| --- | --- | --- | --- |
| 1 | pointer を画面の右上の角に置き、左下へ drag する | 右上の角から左下へ指で swipe する | Notes が全画面で開く（白い頁と上の道具の列） |
| 2 | 頁の上を drag して線を書く | 指（Finger の道具）か pen で線を書く | 線が遅れなく指・pointer に付いてくる |
| 3 | Esc を押す | host の keyboard の Esc を押す（全画面を出る touch の操作はまだ無い） | 窓に戻る（title bar が出る）。書いた線は残る |

## S9: PDF Viewer（頁送りと拡大）

| # | 実機（mouse・keyboard） | Windows の QEMU（touch） | 見ること |
| --- | --- | --- | --- |
| 1 | Files で `a4.pdf` を double click | Files で `a4.pdf` を double tap | PDF Viewer が開き、1 頁目が出る |
| 2 | → を 9 回押し、← を 1 回 | title bar の頁ごとの表示の button で頁の表示にしてから、頁を左へ 9 回 swipe し、右へ 1 回 | 押すたびに次の頁がすぐ出る（目安 0.2 秒以内）。title bar の「Page 9 of 10」 |
| 3 | Ctrl + + で拡大、Ctrl + 0 で戻す | 頁を double tap で拡大、もう一度で戻す。2 本の指を広げて拡大 | 文字がにじまずに大きくなる。戻すと頁の幅に合う |

## 結果の書き方

- 各行について「できた」「できない（何が起きたか）」を書き、できない行は画面の写真を 1 枚添える。
- 頁送りが遅いと感じたら、何頁目で遅かったかを書く（PDF Viewer の log の `PDFVIEWER TURN done page= ms=` で時間を見られる。SSH で入れる
  image なら `grep 'TURN done' <log>`）。
