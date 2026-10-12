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
