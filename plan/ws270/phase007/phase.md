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

（Q1 が追記）
