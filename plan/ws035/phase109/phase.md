<!-- awesome-plan project=zedbsd record=ws035p109 -->

# ws035-p109: greeter・lock を起動画面の明るさに、印をすりガラスに（p108 の残り）

Phase ID: `ws035-p109`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て 1。[p108](../phase108/phase.md) の残り）

## 範囲

p108 の残りの 2 つ: (1) greeter と lock の画面は壁紙を暗く tint して白い文字を読ませていたので、起動画面（splash）より暗い。
splash と同じ明るく淡い水色・緑の調子にし、文字は明るい背景で読める扱い（濃い slate の文字、明るいガラス、局所の scrim）にする。
(2) 印は単色の層の重ねだった。splash の印のように、縁の柔らかい光・半透明・重なりの濃さを持つすりガラスにする。

## 実装（2026-09-28）

- **印**（`userland/desktop/artwork/mark.c`・`mark.h`）: 形の判定を「中か外か」から「縁までの距離」（棒は角丸の矩形、葉は 2 円の
  共通部分）に変え、層を 4 から 7 に: 既存の 4 層に、重なり（棒と葉の両方の中、葉の下の尖りの方へ濃い）、縁の光（各 pane の縁から
  高さの 4.5 % の内側へ柔らかく消える白）、sheen（棒の上部と葉の先端の白）。葉の影は下の尖りへ集まる（二乗）ようにした。
  `<math.h>` の `sqrtf`。
- **色**（`userland/desktop/wayland/glass.c` の `glass_draw_mark`、`userland/desktop/files/ui-home.c` の `fm_mark_draw`、同じ値）:
  棒・葉の不透明度を下げて背景が透け（0.69・0.67）、重なりは深い青（#2f7cf3、0.78）、縁の光は白 0.67、sheen は白 0.24。
  atlas の印の行は 7 × 129 = 903 px（幅 1024 に収まる）。
- **greeter・lock**（`userland/desktop/wayland/greeter.c`、同じ描画）: 暗い tint（紺 28 %）をやめ、ぼかした壁紙を淡い空色へ
  30 % 寄せる。時刻・日付は濃い slate（#263549 前後）の文字にし、後ろに白い柔らかい glow（MODE_SHADOW の白、局所の scrim）を
  置いて、明るい写真や細かい壁紙でも読めるようにした。Restart・Shut Down は明るいすりガラス（白 55 %、pointer の下で 78 %）に
  slate の label、左下の「Kei」も slate。card の影を薄く（0.30 → 0.16）。
- 試験の道具: `plan/ws035/tests/zdesktop-p109.sh`（p108 の手順に、試験の image の写真の壁紙の上の greeter の画面を足した）、
  `plan/ws035/tests/p109/mark-preview.py`（host で印の層を描き、files の色で splash の空の上に重ね、splash の印と並べる）、
  `plan/ws035/tests/p109/compare.py`（guest の画面と splash を並べ、左下の印を 4 倍に拡大して splash の印と比べる）。

## 検証（2026-09-28）

- host: `mark-preview.py` で描いた印と splash の印の比較（`p109-20260928-host-mark-compare.png`、左が描いた印）。重なりの濃い青、
  縁の白い光、棒の上の sheen、葉の先の淡い空色が splash と同じ向き。
- guest（amd64、Venus、graphical-network の login の image `build/amd64`（この worktree）、`zdesktop-guest.sh start`）:
  `zdesktop-p109.sh` PASS: 写真の壁紙の greeter（`greeter-photo.png`）、描いた壁紙の greeter（`greeter.png`）、root の login
  （`desktop.png`）、Super+L の lock（`locked.png`）と解除、files の Home（`files-home.png`、hero の印）と空の folder
  （`files-empty.png`）。session の log に ERROR なし。
- 比較: `p109-20260928-compare-greeter.png`（描いた壁紙の greeter と splash、下は左下の印の 4 倍と splash の印）、
  `-compare-greeter-photo.png`（写真の壁紙）、`-compare-before.png`（p108 の greeter と splash、前の暗さ）。
- 回帰 PASS: zdesktop-p102（login・key と App Home の lock・誤った password の FAIL・解除・idle の lock）。誤った password の
  赤い文は card の上で読める（`p109-20260928-lock-wrong.png`）。1 回目は guest の ssh が上がる前に script が session の
  script を書き換えようとして idle の段だけ MISSING。guest の起動を待ってからの 2 回目で PASS（環境の順序、コードの変更なし）。
- build warning 0（wayland・files・artwork）。`plan/tools/style-check.py` は mark.c・greeter.c・glass.c・ui-home.c とも 0。
  `git diff --check` 0。
- 画面: `/home/awe/zedBSD-rpi4/build/ws035-shots/p109-20260928-{greeter,greeter-photo,desktop,locked,files-home,files-empty,
  lock-wrong,compare-greeter,compare-greeter-photo,compare-before,host-mark-compare}.png`。
- 未実施: 実機、i915。

## 残り

- greeter の左下の印は 48 px なので、縁の光は細かく見えにくい（大きくするかは見た目の判断）。印の形（葉の傾き）は p108 の
  当てはめのまま。
