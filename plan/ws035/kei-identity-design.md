# Kei の見た目の基準（起動画面・壁紙・Welcome）

2026-09-28 ユーザー:「起動画面、この画像を使えますか？そのままでなく加工や生成をしてもいいです。」「デスクトップの壁紙とか、
ファイラーのWelcomeなどにも、これをデザインベースにして進めていきませんか。」

2026-09-28 ユーザー:「この画像に限って言えば、ソースツリーに入れてしまってOKです！」→ この画像は
[`userland/desktop/artwork/kei-boot-splash.png`](../../userland/desktop/artwork/kei-boot-splash.png)（1672x941）として tree に入れた。
他の参考の画像は今までどおり tree に入れない。部品は、この画像を加工するか、script が図形から描く。

## 起動画面の構成（中央揃え、上から）

1. **印（mark）**: すりガラスの 2 つの形の重なり。左は縦に長い角の丸い棒（上端は丸く、下端は右へ回り込む）、右下に
   葉の形（左下から右上へ伸びる、先の尖ったレンズ形）が棒に重なる。色は淡い水色から青（左上が明るく、重なりと葉の左下が
   濃い青）の gradient、半透明で、重なった所が濃くなる。縁はわずかに明るい。
2. **語（wordmark）**: 「Kei」。細めの幾何学的な sans-serif、濃い灰青（slate）。K の 1 文字だけの印は使わない（KDE の商標）。
3. **副題**: 「powered by zedBSD」。小さく、字間を広く取った灰色の小文字（zedBSD は kernel の名前の表記のまま）。
4. **進み**: 青い点を円く並べた spinner（8 つほどの点、明るさが回る）。
5. **背景**: 明るくぼかした風景（湖・山・空・手前の緑と白い花）。全体に白っぽく、淡い水色と若葉の緑。

## 使う所

- **起動画面**（loader の logo と kernel の quiet console の進み）: 今の「Z と zedBSD」の logo を置き換える。
- **greeter・lock**: 背景と色を合わせる（今のぼかした壁紙と glass の card はそのまま合う）。
- **壁紙**: 既定の壁紙を同じ調子（明るい、ぼかした風景、淡い水色と緑）に。
- **File Manager の Welcome** など、空の画面や説明の画面: 印と「Kei」の語を置く。

## 手順（案）

- 段階 1（main、2026-09-28）: `tools/build/make-boot-logo.py` を Kei の印・語・副題に描き直す（淡い背景の単色、loader は左上の
  画素の色で画面を塗る）。
- 段階 2（Keiland の agent）: 全画面の起動画面（`kei-boot-splash.png` を画面の大きさに合わせて PPM にし、loader が全画面に描く。spinner は kernel の進みで動かす）、spinner を kernel の
  進みの枠へ、greeter・lock・壁紙・Welcome に反映。
- 段階 2 の起動画面（2026-09-28、[ws035-p107](phase107/phase.md)）: `tools/build/make-boot-splash.py` が画像から spinner を消した
  1440x810 の `fit=cover` の PPM を作り、loader が画面を覆って描く。spinner は kernel（`splash.c`）が同じ場所に描いて回す。
- 段階 2 の desktop（2026-09-28、[ws035-p108](phase108/phase.md)）: 印を `userland/desktop/artwork/mark.c` の 4 層で描き、greeter・lock・
  files の Home と空の画面に置いた。既定の描いた壁紙を同じ調子に。
- 段階 2 の仕上げ（2026-09-28、[ws035-p109](phase109/phase.md)）: greeter・lock は壁紙を暗くせず淡い空色へ寄せ、文字は slate、時刻の
  後ろに白い glow。印は 7 層（重なりの濃い青、縁の白い光、sheen）のすりガラス。
