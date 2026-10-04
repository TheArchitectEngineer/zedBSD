<!-- awesome-plan project=zedbsd record=ws089-p023 -->

# ws089-p023: Settings の Storage の頁: folder の階層ごとの使用量の解析（multi-thread、逐次の更新、Stop）と Trash を空にする

Status: planning
Disposition: normal
Parent: [WS089](../ws.md)
Queue: 未定

## ユーザーの要望（2026-10-04 夜、UAT-3 の後、原文）

「SettingsのStorageタブは、解析ボタンを押すと、フォルダ階層ごとの使用量を解析して、リアルタイムに解析結果をアップデートして表示し、Stopボタンで止められるマルチスレッド実装にしてほしい。また、Storageタブに、Trashのクリアを実装してほしい。」

## 範囲

1. Storage の頁に「解析」の button。押すと folder の階層ごとの使用量（du に当たる）を解析する。
   - 解析は UI の thread と別の thread（複数の worker の thread で directory の木を分けて走査する multi-thread）で行い、UI を止めない。
   - 結果は走査の途中から逐次に表示を更新する（folder ごとの合計が増えていくのが見える、上位の folder の大きい順、子へ降りられる表示は設計で決める）。
   - Stop の button で止められる（worker の thread を安全に止め、途中までの結果は残す）。
   - 対象（利用者の home か、mount している volume ごとか、権限の無い directory の扱い、hard link・別の file system を跨がないか）を設計で決める。
2. Storage の頁に「Trash を空にする」。利用者の Trash（Files（WS127）の Trash の場所と形式に合わせる）の大きさを表示し、確認の後に空にする。Files が開いている時の通知・表示の更新も考える。
3. 権限・OS の境界: app の自分の機能の file の走査なので、app の中で行ってよい（Guardrail の「app の自分の機能のための OS の依存はこの規則の対象外」の考え方）。libkeiland の thread・UI の部品を使う。

## 受け入れ（案）

- 大きな木（例: 数万 file）の解析中も UI が応答し、表示が逐次に更新され、Stop で 1 秒以内に止まる。
- 結果の合計が `du` の値と合う（host の試験と QEMU）。
- Trash を空にすると Trash の中身が消え、表示の大きさが 0 になる。Files の Trash の表示とも合う。
- C の全文の規約、build warning 0。QEMU は T1、実機は UAT。

## 依存

WS127（Files の Trash の実装）、libkeiland の thread の扱い。
