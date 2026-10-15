<!-- awesome-plan project=zedbsd record=ws247 -->

# WS247: nvrtx の comment と log から開発の管理情報と出典の名前を除き、出典を block ごとに明記する

<!-- awesome-plan-current:start -->
Status: completed（2026-10-15）
Primary Milestone: MG006
Parent: [Master](../master.md)
Related: [Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」、[WS246](../ws246/ws.md)（compiler のまとめ）、[WS037](../ws037/ws.md)
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-15）

「nvrtx全体で、wsやphaseの番号など、開発管理情報を削除し、開発中に必要だったが、成果物として必要ないコメントを整理してください。」
「compute-launch.cはファイル先頭に引用情報があります。referenceは箇所ごとに移してください。さらに、nvrtxのすべてのコメントで、引用以外でnouveauとかMesaといった単語は書かないでください。なぜなら、最初から翻案なのではないか？といあらぬ疑いをかけられるからです。監査してください。」
「reference以外のLinuxという単語も削除対象です。」
「boot.cはファイル先頭にまとめてreferenced:が書かれています。これはガードレール違反ですので、参照箇所ごとに分散させてください。」
「channel.cは stage P4 のような表記がありますか、これは開発内部情報なら消してください。」「channel.cの定数がどこから来たのか、referencedが書かれていないです。」「compute.cには P9cなど内部の開発情報があるので消してください。」
段の名前（クリック、2026-10-15）: 「log も含めて改名」。

## 結果

- nvrtx の全 file の comment から WS・Phase の番号・試験の回・plan/ への参照・日付つきの決定を削除（Q1、301f60672）。
- nouveau・Mesa・NVK・NAK・Linux・open-gpu の語を comment の本文から除き、出典は関数・定義・定数の block の直前の複数行の「Value(s) referenced:」「Procedure referenced:」だけに書いた。file の上部の出典の一覧は無くした（Q1 の 8f1799d64・f0c4e4180、p001 の q984〜q986 で 60 file・403 block）。
- compiler の sm75.c・sm75.h にも同じ規則（p003、q987〜q989）。Collabora・Red Hat の表示は削除。
- 段の名前（N0〜P9p）を log・`nvrtx.stop=`・comment で説明的な名前に改名（例 P4m → vram-window、P9c → compute-test）。試験の config も追従。過去の記録は旧名のまま（履歴）。
- 規約の全文の見直し（p002、q990）。
- 確かめ（Q1、main 1d8cc4d19）: 61 file を `cpp -fpreprocessed -P` で比べ、59 file は comment だけ、差は runtime.c・vk-node.c の log の段の名前の 5 行（意図した改名）。build warning 0、host 試験 sm75 287/0・desktop 138 compiled/0 disagreed・vk-d3 122/0・vk 60/0。

## 完了の判断（2026-10-15 ユーザー、クリック）

1. 出典の 2 つ目以降の path の省略（`Linux v6.19 drivers/gpu/drm/nouveau/…` の後の `nvkm/…` など約 73 か所）:「省略を認める」。
2. sm75.c の 3 か所の出典: ユーザー自身の 8c8b74671 で「the CUDA compiler emits」を「the SASS compiler emits」に書き換え済み。「解決（今のまま）」。

## 制限・移管

- scanout.c が定義の無い番地 0x64013c を読んで捨てている（SOR の caps は 0x640144＋or*8）。WS037 の後続で。
- 直さずに残した点: identify.c の `NVIDIA open-gpu-doc (the class numbers)` に path が無い、nvrtx-gsp.h の `as both other drivers give it`、log の冗長（`devinit begin: devinit` など）、style-check.py の 716 件（大半は範囲外の code）。
- latency の if の連鎖の表への書き換えと構造の見直しは [WS246](../ws246/ws.md) p002。
- gsp の host 試験は WS242 の未追従で compile が通らない（WS242）。

## Phase

| Phase | 内容 | 状態 |
| --- | --- | --- |
| p001 | 定数ごとの出典、本文の出典の名前の除去、vram.c の path（P1・P2・P3 の並列、q984〜q986） | cleared（2026-10-15、main 50a95d261） |
| p003 | compiler の sm75.c・sm75.h に p001 と同じ規則（q987〜q989） | cleared（2026-10-15、main 70e7a254f） |
| p002 | 規約の全文の見直し（q990） | cleared（2026-10-15、main b64f2f7a2） |

Phase の directory と作業の一覧（unreferenced-defines.md）は完了の時に削除した（git の履歴にある）。
