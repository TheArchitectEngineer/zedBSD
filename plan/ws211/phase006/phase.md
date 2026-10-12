<!-- awesome-plan project=zedbsd record=ws211-p006 -->

# ws211-p006: 窓の drag の移動を 60fps に近づける

Parent: [WS211](../ws.md)
Status: planning（調べと案。2026-10-12 P3。計測は Pi の次の boot）
Phase disposition: normal
Queue: P3（2026-10-12 ユーザー「ウィンドウのドラッグ移動が60fpsにならず、30fpsくらい」）

## 今の経路（source で確かめた事実）

- drag の 1 コマの damage は、窓の前の場所と今の場所の矩形（title bar の上端から body の下端まで、`DAMAGE_REACH` の分を広げた物、`shell.c` の `drag_damage`）。全画面ではない。
- drag の間は `glass_still` が偽になる。hover の記録は捨てられ、部分の描き直しになる。
- その矩形の中で、毎コマ次を描く。
  - 壁紙。
  - 下の窓。
  - 動かしている窓の影（MODE_SHADOW、窓より大きな柔らかい形）。
  - title bar の glass（blur した壁紙を sample する）。
  - body（client の画像。Pi の kernel の snapshot は、中身が変わらなければ再利用される）。
  - client が宣言した glass の panel。
  - 窓が中央に dock している時と set_blur の時は、backdrop（窓の下の場面を小さく描いて blur）も作り直す。
- 1 コマずつ（前のコマの fence の後に次を記録する）で、present は vblank に合わせる。そのため 1 コマの間隔は、記録 ＋ GPU ＋ vblank の待ちになる。GPU が 1 refresh（16.7ms）を少しでも超えると、33ms（30fps）になる。

## 計測（次の v3d.perf=1 の boot で）

- 窓（Settings と Files）を、10 秒ずつ drag してもらう。
- 読む物:
  - `KWL LAT`（記録・submit→shown・入力→shown）。
  - `KWL DAMAGE` の大きさ。
  - `KWL DRAWS`（draws・cmds）。
  - v3d の perf（prep・exec・render・tfu・snap・max_us、`v3d: gpu` の qfrag と tile の cycles）。
  - flip perf（wait）。
  - backdrop の有無（`KWL BACKDROP`）。

## 案（計測の後に選ぶ）

1. **動かしている窓を 1 枚の絵にする**: drag の始めに、窓（影・title bar・body・panel）を、layer-cache と同じ形の offscreen の絵に焼く。drag の間は、その絵を 1 quad で描く。影の柔らかい形と glass の計算が、毎コマの fragment から消える。client が drag の間に commit したら焼き直す（drag の間の中身の更新は止めてもよいかを、ユーザーに確かめる）。
2. **影を軽くする**: 影の柔らかい形の計算を、9-slice の絵（角と辺の小さな絵を引き伸ばす）にする。全部の窓に効く。
3. **2 コマを飛ばす（in flight を 2 つにする）**: GPU が 1 refresh を少し超えても、次のコマの記録を前のコマの vblank の待ちと重ねる。buffer の hold・frame callback・scene の扱いが変わるので大きい。
4. **HVS の plane に窓を載せる**: 動かしている窓を overlay の plane に出せば、合成は要らない。kernel の display の変更（WS141）で大きい。最後の候補。
- 計測で、GPU の render が 16ms を超えるかどうかで決める。超えるなら 1 か 2。記録か kernel の準備が大きいなら、p003 の束ね（影と panel）を先にする。

## デスクトップでのマウス移動（place15 の log、2026-10-12 P3）

- ユーザー（place15 の評価）:「デスクトップでのマウス移動が、60fpsにわずかに届いていないようです。」
- log: Pi の今の boot（place15、`/run/user/1000/session.log` と `/var/log/kernel.log`、scratchpad/p3g3/b15）。
- 事実:
  - `KWL CURSOR_PLANE present` の間隔は、中央値 21ms（20〜22ms が 1927 回中 1927 の大半、16ms 以下は 21 回）。kernel の `bcm2711: cadence` の `set`・`move` の中央値も 21000us。約 47/s で、毎 refresh には届いていない。
  - マウスだけを動かした窓（frame_done=0、draw=0）でも `cursor presents=204/5s`。main の work は 36%、poll 58% で、CPU は余っている。
  - present 1 回は約 3.4ms（`present_cycles` 約 18 万、54MHz）。
- 原因（source で確かめた）: C0 の間引き（`cursor-plane.c`）は、前の present から 1 refresh（16.7ms）経つまで動きを保留する。その時刻（`presented_us`）を present の**後**に取っていたため、間隔が 16.7 + 3.4 ms になる。さらに Pi の時計は 1ms 単位で、loop の眠りも ms に切り上げるので、pointer の次の report（8ms ごと）まで遅れて 20〜22ms になる。
- 直し（実装、place16 に入れる）:
  - `presented_us` を present の始めの時刻にする。
  - 保留を 1 refresh から 2.5ms 引いた長さにする（`PLANE_HOLD_SLACK_US`、`plane_hold_us`）。時計と眠りの丸めがあっても、どの refresh にも 1 回は動きが届く。present は最大でも約 13ms に 1 回（約 66/s 程度）で、C0 の前（125/s）より少ない。
  - kernel の cursor の present は即時（`wsi-display.c` の `display_present_cursor` は CURSOR_SET・MOVE をすぐ呼ぶ、FIFO の待ちは無い）ので、間隔を詰めても遅れは積もらない。
- 期待（Pi で確かめること）: `KWL CURSOR_PLANE present` の間隔の中央値が 15〜16ms、kernel の `cadence` の `move` の中央値が 16000 前後、`cursor presents=` が 5 秒で約 300。main の `cursor=` の時間は約 1.4 倍に増える（present 1 回 3.4ms のまま）。
- 残り（範囲外）: present 1 回 3.4ms は WS213（cursor を別 thread へ）の範囲。マウス移動中の `clients=` が 5 秒で 400〜530ms あり、pointer の event への client の応答（set_cursor など）の処理の内訳は未調査。
