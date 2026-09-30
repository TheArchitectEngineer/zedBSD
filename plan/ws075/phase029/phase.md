<!-- awesome-plan project=zedbsd record=ws075p029 -->

# ws075-p029: L2 の実装: すりガラスの blur を窓ごとに有効・無効に（既定は無効、Settings は有効）

Phase ID: `ws075-p029`
Parent: [WS075](../ws.md)
Status: in-progress（2026-09-30、P1）
Phase disposition: normal
承認: 2026-09-30 Q1 の指示。ユーザーの判断は ws.md の「ユーザーの判断（2026-09-30、すりガラス）」（最初は「blur をやめる」、同日に「窓ごとに
選べ、既定は無効、Settings は有効、Files と keyboard は比べて決める」に改めた）。

## 経緯

- 最初の指示「blur をやめる」で backdrop（backdrop.c）を消す実装を作り（14b42c2f）、実機で計測を始めたところで範囲が「窓ごと」に改まったので、
  計測を止め（機械は返した）、消した変更を戻した（e5129e7a、backdrop は blur を有効にした窓のために残す）。

## 設計

### 選ぶ口（app 側）: Keiland の glass の protocol と libkeiland の API の両方

blur はすりガラスの見え方の選択で、すりガラスは窓ごとの `keiland_glass_v1`（zdesktop の glass の protocol、libkeiland の `keiland_glass_*` が包む）に
属する。窓の glass（panel と title bar）を持つ app は既にこの object を持つので、そこに要求を 1 つ足すのが自然（新しい global や窓の属性の
経路を作らない）。app は protocol の header を直接使わず libkeiland を通す決まりなので、API も足す。

- protocol（`userland/desktop/libwayland/glass-protocol.c`・`zed-glass-v1-client-protocol.h`、compositor の `protocol.c` の global）: `keiland_glass_v1`
  の version 2 に `set_blur(uint enabled)`（1: 下の窓を blur して透かす、0: blur 済みの壁紙だけ、既定 0）。double-buffered（surface の commit で
  適用、set_panels と同じ）。manager の global は version 2。client の `keiland_glass_manager_v1_get_glass` は新しい glass を manager の version で
  作る（前は 1 に固定で、version 2 の要求を送ると client の libwayland が接続を壊した: 最初の実機の build で Settings が落ちた原因）。
- libkeiland（`userland/desktop/libkeiland/glass.c`・`include/libc/keiland.h`）: `int keiland_glass_set_blur(struct keiland_glass *, int enabled)`。
  compositor の glass が version 1 なら ENOTSUP。manager は告げられた version と 2 の小さい方で bind。**KEILAND_VERSION 16 → 17**。
- compositor（`panels.c`・`panels.h`）: `pending_blur`・`blur`、`zwl_panels_blur(surface)`。変わった時だけ `ZWL GLASS client=N surface=M blur=B` の行
  （既存の `ZWL GLASS ... panels=` の行の形は変えない: files-p015・p017 の試験が読む）。glass が消えると次の commit で 0。

### 描き方（compositor、`shell.c`）

- 窓の loop: 下に窓があり Home が層を持たず、その窓の `zwl_panels_blur` が 1 のときだけ `draw_backdrop`（今までの backdrop）。それ以外の窓は
  `zwl_backdrop_reset` で blur 済みの壁紙（下の窓を描き直さない）。前は下に窓がある全ての窓が backdrop を描いていた。
- Settings（`userland/desktop/settings/glass.c`）: glass を作った直後に `keiland_glass_set_blur(glass, 1)`（ユーザーの判断: 常用ではないので有効）。
  Files・他の app は呼ばない（既定の無効）。Files は比べて決める（下の図）。

### compositor の側の決め

| 面 | 扱い | 理由 |
| --- | --- | --- |
| 窓の panel と title bar | 窓ごとの flag（上） | 窓の glass の選択 |
| keyboard の panel（keyboard.c、P3 の担当） | `server->keyboard_blur`（zdesktop の `--keyboard-blur` で 1）。1 で panel が出ている時、shell.c が keyboard の描画の直前に全ての窓の backdrop を描き、keyboard の glass（`shape.set` を持たない）はそれを透かす。既定 0（blur 済みの壁紙） | keyboard.c は変えない。keyboard.c は `zwl_keyboard_showing()` を既に出しているので読む口はそれで足りる。既定を設定（Settings・preferences）で選ぶ口にするかは Files と keyboard の見た目の判断の後（ユーザー） |
| App Home | 変わらない（Home が層を持つ間は前から backdrop を描かない） | 撮った画面で off と on が画素まで同じ |
| Wiseview | 変わらない（Wiseview は窓の loop の前に描いて返る、前から backdrop 無し） | 同上 |
| system bar・popup・menu・音量・network の popup | 変わらない（窓の loop の後、前から blur 済みの壁紙） | backdrop を使ったことが無い |

## 画面（QEMU の Venus の guest、1280x800、この tree の criteria の image）

`plan/ws075/tests/glass-shots.sh`（Settings の上に Files、QWERTY の keyboard、App Home、Wiseview）を 2 通り:
off = 既定（Files は blur 無効、keyboard は無効）、on = Files を `plan/ws075/phase029/files-blur-shot.patch` で blur を有効にした build（撮影だけ）+
zdesktop `--keyboard-blur`。

- Files: `build/ws075-shots/p029b/files-glass-off-on.png`（左 無効: Files の sidebar・title bar・本体のすりガラスが壁紙（緑・花）を透かす。右 有効:
  下の Settings が blur されて透ける）。
- keyboard の panel: `build/ws075-shots/p029b/keyboard-glass-off-on.png`（左 無効: 壁紙の木々が透ける。右 有効: 下の窓（白）が透ける）。
- App Home・Wiseview: `build/ws075-shots/p029b/home-wiseview-off-on.png`（off と on で上の bar の下は画素まで同じ）。

Files と keyboard の既定（有効・無効）はユーザーが見比べて決める（今は両方とも無効）。
