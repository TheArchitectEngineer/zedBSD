<!-- awesome-plan project=zedbsd record=ws035p119 -->

# ws035-p119: 全画面の窓を守る（上に普通の窓が来ても system bar を出さない）

Phase ID: `ws035-p119`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 ユーザーの決定「バーの Kei の印・全画面」、main 経由の割り当て。p116 の main への一覧の 5）

## 範囲

全画面の窓（Notes 等）がある間は、普通の窓（PDF Viewer 等）を開いても前に出しても system bar を戻さない。普通の窓は全画面の窓の上に浮かび、
bar は端のジェスチャーか Home で呼ぶ。Wiseview・docking・左上と右上のジェスチャー・lock screen との関係を確かめる。Venus で Notes の全画面の
上に PDF Viewer があって bar の無い画面を撮る。

## 設計（`userland/desktop/wayland/shell.c`）

- `bar_cover()`: 今の desktop の、出力の上端を覆う窓（全画面か docked）のうち最も上のものが**全画面**のとき、その窓を返す（bar を出さない）。
  - 浮いている普通の窓は数えない（全画面の窓の上に浮かぶ）。docked の窓が全画面の窓より上なら bar を出す（bar は docked の title を持ち、
    docked の本体が全画面の窓を覆うので守るものが無い）。
  - App Home（開く・開いた・閉じる途中）と Wiseview の間は bar を出す。これが「bar は端のジェスチャーか Home で呼ぶ」の経路。
- 描画: `zwl_glass_draw` は `bar_cover()` が窓を返す frame で `draw_system_bar` を描かない。変わった時だけ `ZWL GLASS bar hidden fullscreen=ID`・
  `ZWL GLASS bar shown` を log（`server->bar_hidden`、`zwl.h`）。
- 入力（`zwl_glass_button`）: bar を出さない間は bar の場所が全画面の窓のもの。launcher（左上 40 px）と network の icon は押せず
  （network の menu が開いていればそのまま menu のもの）、`bar_press` にも行かない。App Home は全画面の窓の上と同じく左上の 28x28 の角の
  press と Home 自身の press の続きだけ（`home_without_bar()`）。上端の press は下の窓（Notes の toolbar）に届き、その窓が前に出る。
- 全画面の窓が最前面のとき（fullscreen mode、direct scanout）は前から bar が無い。右上の角のヒント等で合成に戻る間も、前は bar が出たが、
  今は Home・Wiseview 以外では出ない。

## 相互作用の確認

- Wiseview: 開くと bar が出る（`bar shown`）、Esc で閉じると再び隠れる（`bar hidden`）。walk の log で hidden・shown が交互に 4 回。
- 左上のジェスチャー: 全画面の Notes の上（fullscreen mode）でも、PDF Viewer が上にある合成の状態でも App Home が開く（walk 07・09・11）。
- 右上のジェスチャー（Notes）: walk 06。回帰 ws079 zdesktop-p010。
- docking: docked の窓が上なら bar が出る（規則）。回帰 zdesktop-p059・p062・p064。
- lock: lock screen は greeter の画だけを描く（変わらない）。unlock の後、PDF Viewer が Notes の上にあるので bar は隠れたまま（walk 10）。
- 上端を押すと下の全画面の窓が前に出て fullscreen mode に戻る（`window_raise`）。

## 検証（2026-09-28、QEMU の Venus。実機は未実施）

- demo-walk（[p120](../phase120/phase.md) の kei、1920x1280）: 07 で `ZWL GLASS bar hidden fullscreen=9` ok。
  画面 `/home/awe/zedBSD-rpi4/build/ws035-shots/p118-20260929-notes-fullscreen-pdf-no-bar.png`（全画面の Notes の上に PDF Viewer、bar 無し、
  Notes の toolbar が全部見える）、Wiseview の bar `p118-20260929-kei-08-wiseview.png`、unlock 後 `p118-20260929-kei-10-unlocked.png`。
- 回帰（lean files image `build/p118-files`、1280x800）: 結果は下の「回帰」。
- build warning 0、`plan/tools/style-check.py` shell.c 0。

## 回帰

lean files image（`plan/tools/files/build-files-image.sh build/p118-files`、p118〜p120 の compositor）、1280x800、QEMU の Venus:
ws079 `zdesktop-p010.sh`（右上・左上・下端のジェスチャー、全画面の Notes の上の Home・Wiseview、docking）PASS、`zdesktop-p064.sh` PASS、
`zdesktop-p059.sh` PASS、`zdesktop-p062.sh` PASS（log の FAIL・MISSING 0）。zdesktop-p053・p102 は走らせていない（cursor・lock の描画は
変えていない。lock は demo-walk の 09・10 で確認）。
