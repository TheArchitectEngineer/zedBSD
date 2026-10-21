<!-- awesome-plan project=zedbsd record=ws270-p007 -->

# ws270-p007 VF2 の desktop の速さ（使える水準へ）

- WS: [WS270](../ws.md)。状態: in-progress（2026-10-21 Q1）。前提: p006a（desktop が PowerVR で動く、lockup 解決）。
- 出典: 2026-10-21 ユーザー「lockupは解決したのですが、描画が遅すぎてデスクトップが利用可能な水準ではありません。たぶん2fpsくらいです。これを解決したいです。」
- 板: Q1 だけが触る（kei@10.0.30.2、SwitchBot で電源）。担当は commit と板で流す手順を Q1 に送る。

## 受け入れ（Q1 の案、ユーザーに示した）

1. カーソルの移動・文字の入力・窓の一部の更新が目に見えて滑らか（KWL の frame の GPU の時間 ≤ 16 ms）。
2. 窓の移動・全画面の書き換えが 20 fps 以上（frame ≤ 50 ms）。
3. 見た目は同じ（keiland-shot で比べる）、vkmatrix 全 PASS・HWR 0。

## 出発点の数（板 d42〜d46、1920x1080、396 MHz）

clear 3.85 ms、LOAD 23.35 ms、平らな quad 15.1 ms、1/4 面積 7.2 ms、402 op の ALU 268.75 ms、texture 38 ms。KWL の rec submit 108〜162 ms/frame。
固定費 ≈ 5.4 ns/pixel（≈ 2 clock/pixel、BXE-4-32 の名目は 4 pixel/clock）。LOAD・texture は GPU の read が約 400 MB/s 相当。

## 道（並行）

| Queue | 担当 | 道 |
| --- | --- | --- |
| q1538 | P1 | Keiland: frame ごとに描く量を減らす（damage の範囲だけを scissor で描く、壁紙・窓の再 sample を減らす、全画面の redraw をやめる） |
| q1539 | P2 | driver の draw の道: 全画面の quad の PPP・ISP・PDS・pixel の出力の語を Mesa と比べる、PUNCH_THROUGH、三角形の数を変えた perf |
| q1540 | P3 | GPU の memory の read の速さ: SLC bypass・cache の属性・MMU・device-local を cached にすると板が再起動する原因 |

## 板の結果

- d48（main の kernel、P1 dee85df07 の計数）: 3 窓の frame は全画面 1 pass、263 draws → 64 draw ごとに job 5 個・全画面 LOAD 4 回、rec submit 約 390 ms。
- d50（P2 2d3c67809 512 draws、P1 ef1a02591 glyph の batch）: 132 draws・1 job、job wait 約 305 ms。perf: clear 3850、flat 15700、tri1/tri128 15750/16050、stack4 16000、draws4 19200、load 18750、texture 38150 us。P3 perf-read: 8 MB の texture の read は追加 0 → memory の read は主因でない（q1540 の道は閉じる、SLC の試し B・D は未実施）。
- d51（ta: ptr1・pds_tempsize 0・pixel merge off）: draws4 16050 の他は不変。d52（tb）は起動の不調で未測定。
- d53〜d55: HDMI の cable が抜けていて boot framebuffer なし（ユーザー 2026-10-21「すみませんHDMIが抜けていました。」）、desktop は出ず。d55 の perf（P2 vf2c、compiler で fragment の group 数を約半分、ta 既定）: clear 4000、flat 9200、quarter 5650、draws4 9100、load 10250、texture 17750 us、vkmatrix 41/41、HWR 0。
- d56（vf2c + P1 98c7a4ccf 壁紙の穴）: 見た目同じ（build/review/vf2-desktop/shot56.png）。3 窓の全画面 frame 165 draws、rec submit 約 160 ms。terminal の出力の frame は部分（916x632）だが blended 2.5 M px（area の 6 倍）で約 66 ms → P1 の opaque region（案 A）へ。
