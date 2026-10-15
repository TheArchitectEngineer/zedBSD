<!-- awesome-plan project=zedbsd record=ws247 -->

# WS247: nvrtx の comment と log から開発の管理情報と出典の名前を除き、出典を block ごとに明記する

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-16 Q1 が途中まで実施）
Primary Milestone: MG006
Parent: [Master](../master.md)
Related: [Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」、[WS246](../ws246/ws.md)（compiler のまとめ）、[WS037](../ws037/ws.md)
Resume point: 下の「残り」1 から。
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

## 残り

1. **本文に出典の名前が残る 3 file**: draw-push.c（40）・scanout.c（21）・disp-records.c（16）。file の上部の出典の一覧（draw-push.c）も block ごとへ。
2. **定数ごとの出典**: channel.c など、出典の書かれていない定数・register の値に、block ごとに Value(s) referenced: を付ける（ユーザーの指摘: channel.c）。nvrtx の全 file を定数・構造体・関数ごとに監査する。
3. **段の名前（N0・N1・P0〜P9p）の改名**: log（nvrtx_stage_mark の行）と comment の両方で、開発の段の名前を説明的な名前（例 P9c → compute launch、P4m → VRAM window）に変える。comment には約 600 か所。log の名前を照合する試験・道具（plan/ws037/tests・plan/tools・T の手順）を追従させる。
4. vram.c の 2 つの open-gpu-kernel-modules の path（osinit.c・kern_bus_gm107.c）は手元の取得物に無く未確認。確かめる。
5. 確かめ: build warning 0、nvrtx の host 試験（sm75・vk-d3・vk。gsp の host 試験は WS242 の未追従で compile が通らない）。
