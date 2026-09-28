<!-- awesome-plan project=zedbsd record=ws035p108 -->

# ws035-p108: greeter・lock・既定の壁紙・files を Kei の見た目に（見た目の段階 2 の 2）

Phase ID: `ws035-p108`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て 4 の後半。[kei-identity-design.md](../kei-identity-design.md) の段階 2）

## 範囲

起動画面（[p107](../phase107/phase.md)）に続き、greeter・lock の画面、既定の壁紙の調子、files の Welcome（Home の hero）と
空の画面を、印＋「Kei」の見た目に揃える。語は必ず「Kei」の 3 文字（K 1 文字にしない）。画面の文字に Keiland は出さない。

## 実装（2026-09-28）

- **印**（`userland/desktop/artwork/mark.c`・`mark.h`、新。wayland と files の両方に compile する）: 印を 4 枚の coverage の
  層で描く（棒、棒の影（足の方へ濃い）、葉、葉の影（下の尖りの方へ濃い））。棒は 4 隅の半径が違う角丸の矩形、葉は splash の
  上下の縁の点に合わせた 2 つの円の共通部分。1 画素 16 標本。host の `plan/ws035/tests/p107/mark-host.c` で元の絵と見比べた。
- **zdesktop**（`glass.c`・`glass.h`・`greeter.c`、Makefile に mark.c）: glyph の atlas に印の 4 層（128 px）を入れ、
  `glass_draw_mark()` が層ごとの色（淡い青、深い青）で描く。greeter と lock の画面（同じ描画）の左下に印（48 px）と白い「Kei」。
  既定の壁紙（`--wallpaper` の無いとき CPU が描く風景）を Kei の調子に: 明るい空と太陽、遠くは淡い青・近くは若葉の緑の山、
  湖、左上のぼけた葉と足もとの草地、白い花のぼけ、全体を白へ 20 %。
- **files**（`ui-home.c`・`ui-grid.c`・`files.h`、Makefile に mark.c）: `fm_mark_draw()`（層を大きさごとに一度作って保つ）。
  Home の hero card の右上に印（56 px）と slate の「Kei」、壁紙の無いときの hero の風景を明るい空と緑の丘に。空の place の
  「Nothing here」の上に淡い印（72 px、45 %）。`plan/tools/files/host-build.sh` に mark.c。

## 検証（2026-09-28）

- host: files-render で Home と空の folder（`build/ws035-shots/p108-20260928-host-files-home.png`・`-host-files-empty.png`）、
  印の比較（`-host-mark-compare.png`、左が描いた印、右が splash）。
- guest（amd64、Venus、graphical-network の image `build/ws035-d3`）: `plan/ws035/tests/zdesktop-p108.sh`（新）PASS:
  試験の image の写真の壁紙を外して greeter を起こし直し、Kei の調子の描いた壁紙と左下の印・Kei（`greeter.png`）、root で
  login した desktop（`desktop.png`）、Super+L の lock（`locked.png`）と解除、session で files の Home（`files-home.png`）と
  空の folder（`files-empty.png`）。session の log に ERROR なし。終わりに壁紙を戻す。
- 画面: `build/ws035-shots/p108-20260928-{greeter,desktop,locked,files-home,files-empty}.png`。
- 回帰 PASS: zdesktop-p102（login・lock・解除・idle の lock）。build warning 0。style-check は変えた 5 file とも 0。
- 未実施: 実機、i915。

## 残り

- greeter・lock は壁紙を暗めに tint して白い文字を読ませるので、splash ほど明るくない。印は単色の層の重ね（splash の
  すりガラスの縁の光や細かい gradient は無い）。
- 既定の壁紙は描いた風景（写真の壁紙は tree に入れない方針）。App Home・system bar の「Kei」の見出しは今までどおり文字だけ。
