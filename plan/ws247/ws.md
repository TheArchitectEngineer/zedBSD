<!-- awesome-plan project=zedbsd record=ws247 -->

# WS247: nvrtx の comment と log から開発の管理情報と出典の名前を除き、出典を block ごとに明記する

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-16 p001 cleared、残りは p002）
Primary Milestone: MG006
Parent: [Master](../master.md)
Related: [Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」、[WS246](../ws246/ws.md)（compiler のまとめ）、[WS037](../ws037/ws.md)
Resume point: p002（規約の全文の見直し、planned、未承認）。
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-16）

「nvrtx全体で、wsやphaseの番号など、開発管理情報を削除し、開発中に必要だったが、成果物として必要ないコメントを整理してください。」
「compute-launch.cはファイル先頭に引用情報があります。referenceは箇所ごとに移してください。さらに、nvrtxのすべてのコメントで、引用以外でnouveauとかMesaといった単語は書かないでください。なぜなら、最初から翻案なのではないか？といあらぬ疑いをかけられるからです。監査してください。」
「reference以外のLinuxという単語も削除対象です。」
「boot.cはファイル先頭にまとめてreferenced:が書かれています。これはガードレール違反ですので、参照箇所ごとに分散させてください。」
「channel.cは stage P4 のような表記がありますか、これは開発内部情報なら消してください。」「channel.cの定数がどこから来たのか、referencedが書かれていないです。」「compute.cには P9cなど内部の開発情報があるので消してください。」
段の名前（クリック、2026-10-16）: 「log も含めて改名」。

## 済み（Q1、2026-10-16）

- WS・Phase の番号・試験の回（R7d など）・plan/ への参照・日付つきのユーザーの決定を nvrtx の全 file の comment から削除（301f60672）。
- compiler/: file の上部の出典と Collabora・Red Hat の表示を削除、block ごとの Values/Procedure referenced:（複数行）、sm75.h・sm75.c にまとめた（WS246）。
- nouveau・Mesa・NVK・NAK・Linux・open-gpu の語を comment の本文から除き、出典を関数・定義の直前の referenced: に移した（29 file のうち 26 file、8f1799d64・f0c4e4180）。file の先頭の comment の referenced: は該当の関数・定義へ移した（boot.c・falcon.c・identify.c・perf.c・compute.c・compute-launch.c・disp-first.c・display.c・gr.c・vram.c・nvrtx-disp.h・nvrtx-draw.h・nvrtx-exec.h）。

## 段の名前の対応（2026-10-16 Q1 が実施、log・nvrtx.stop=・comment）

N0 identify、N1 console、P0 layout、P1a devinit、P1b frts、P2a booter、P2b gsp-init、P3 static-info、P4 address-space、P4m vram-window、P5 copy-channel、P5e copy-fill、P5g golden-context、P6 channel-stop、P7 unload、P8 display、P8a display-client、P8b takeover、P8c gpu-device、P9c compute-test、P9g draw-test、P9w work-channel、P9p performance。試験の config（plan/ws037/tests/config-amd64-nvrtx.mk の nvrtx.stop=）も追従。過去の記録（plan/ の T の結果など）は旧名のまま（履歴）。

## Phase

| Phase | 内容 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 下の「残り」1・2・4・5：定数ごとの出典、本文の出典の名前の除去、vram.c の path（P1・P2・P3 の並列、q984〜q986） | cleared（2026-10-16、main 50a95d261） |
| p002 | 規約の全文の見直し（WS247 が変えた comment を plan/coding-style.md の全文と照らす） | planned |

範囲外で見つかった物（WS037 の後続へ）: scanout.c:1273 が定義の無い番地 0x64013c を読んで捨てている（SOR の caps は 0x640144＋or*8）。[p001](phase001/phase.md) の「出典の無い値」。

## 残り（2026-10-16 に p001 で全て済み）

1. **本文に出典の名前が残る 3 file**: draw-push.c（40）・scanout.c（21）・disp-records.c（16）。file の上部の出典の一覧（draw-push.c）も block ごとへ。
2. **定数ごとの出典**（2026-10-16 ユーザー:「disp-records.c ... 定数にreferencedがない／draw-push.c ... ファイルヘッダにまとめてreferenceが書かれている／このパターンをnvrtx内すべてのファイルで修正してください。」、進め方はクリック「次のセッションで」。一覧: [unreferenced-defines.md](unreferenced-defines.md)、60 file・403 block。disp-first.c は済み（5aa6d52d5）。file の先頭の出典の一覧は draw-push.c ほか、Values/Procedure referenced: を block ごとへ。推奨の進め方: file の群ごとに担当を並列に起動、Q1 が merge と build・host 試験）: channel.c など、出典の書かれていない定数・register の値に、block ごとに Value(s) referenced: を付ける（ユーザーの指摘: channel.c）。nvrtx の全 file を定数・構造体・関数ごとに監査する。
3. **（済み、Q1 の commit は下）段の名前（N0・N1・P0〜P9p）の改名**: log（nvrtx_stage_mark の行）と comment の両方で、開発の段の名前を説明的な名前（例 P9c → compute launch、P4m → VRAM window）に変える。comment には約 600 か所。log の名前を照合する試験・道具（plan/ws037/tests・plan/tools・T の手順）を追従させる。
4. vram.c の 2 つの open-gpu-kernel-modules の path（osinit.c・kern_bus_gm107.c）は手元の取得物に無く未確認。確かめる。
5. 確かめ: build warning 0、nvrtx の host 試験（sm75・vk-d3・vk。gsp の host 試験は WS242 の未追従で compile が通らない）。
