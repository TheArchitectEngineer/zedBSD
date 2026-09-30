<!-- awesome-plan project=zedbsd record=ws075p029 -->

# ws075-p029: L2 の実装: すりガラスの blur を窓ごとに有効・無効に（既定は無効、Settings は有効）

Phase ID: `ws075-p029`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-30、P1。blur は窓ごとに選べ既定は無効、Settings は有効。L2 を満たす: 10 app（Settings を含む）で C6 中央値 67.3 ms・p90 102.6 ms（5 run）、stress 100 回で停止 0。Settings を 9 窓の上に出した参考 71.3 ms。C7 PASS（最小 4.68）・C9 10/10・boot test PASS。Files と keyboard の既定はユーザーが画面で決める）
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

## 検証（2026-09-30）

最終の tree 8bdc6f4b（main の ws099-p016: import の barrier を合成の command buffer の頭で記録する変更を取り込んだ後）。

| 確認 | 結果 |
| --- | --- |
| build（compositor・libkeiland・libwayland・settings・demo の image） | warning 0 |
| libwayland の host の試験（ws014 の wayland-client、path を直した命令） | PASS |
| style-check（変えた file、前との差分） | 新しい指摘 0（直した） |
| C7（`plan/ws099/tests/criteria.sh ... C7`、QEMU の Venus、この tree の criteria の image、既定と生成の 5 枚の壁紙） | PASS: 72 箇所 fail 0、最小 contrast 4.68（基準 4.5） |
| C9（同 C9、10 本） | 10/10 PASS。p076 は変更の前の最初の実行で 1 回 FAIL（move・resize の手順）、同じ image と変更を除いた image で再実行して両方 PASS: 変更と無関係の揺れ |
| QEMU の boot test（demo の image の複写） | PASS（`build/ws075-p029/boot-test/login.png`） |
| 5330 の passthrough: stress-117 の 100 回 | 停止 0（3 s に最少 50 flip）、draw の拒否 0・set の消失 0 |
| 5330 の passthrough: C6（measure-apps 5 run、10 app に Settings（blur 有効）を含む、`c6.py`） | **中央値 67.3 ms**・p90 102.6 ms、run の中央値 60.7〜73.8 ms（標準偏差 5.1 ms）、10 app の flip 17.1〜19.1/s、compositor の 1 run 6.4〜6.9 ms、draw の拒否 0 |
| 参考: Settings（blur 有効）を 9 窓の上に出した 1 run | C6 中央値 71.3 ms・p90 114.9 ms、flip 18.8/s、compositor の占有 62.8%（Settings の glass は下の 9 窓の blur を透かす: `build/ws075-shots/ws075-p029-settings-raised.png`） |
| 新しく開いた app の中身（ws099-p016 の影響、i915 の実行器で UNDEFINED → GENERAL の barrier が描いた後） | 5330 で 10 app を順に開いた撮影で全て正しい中身（`build/ws075-shots/ws075-p029-apps.png`: Files・Notes・Settings・Terminal・PDF・Images・Browser・Model viewer・Gears・X terminal） |
| App Home から Files の最初の frame まで | QEMU の Venus（`import-launch.sh`、3 回の中央値）: 窓の最初の image まで 2298 ms、次の合成の frame まで 2418 ms。5330 は未実施（session の log の取り出しの click が Terminal でなく Settings に当たった） |

判定: L2（C6 中央値 75 ms 以内）を満たす（5 run）。C6（50 ms）は L3。

QEMU と実機: C7・C9・画面の比較・boot test・Files の起動の時間は QEMU（Venus）、stress・C6・app の中身は 5330 の passthrough。素の 5330 は未実施。

## 残り

- Files と keyboard の panel の blur の既定（今は両方とも無効）: ユーザーが `files-glass-off-on.png`・`keyboard-glass-off-on.png` を見て決める。keyboard を
  有効にするなら zdesktop の起動（sessiond の session の引数）に `--keyboard-blur` を足すか、設定の口にする（Q1・ユーザー）。
- 5330 での Files の起動の時間。
- ws035 の `zdesktop-p057.sh`（backdrop が下の窓を見せることの試験）は、今は blur を有効にした窓（Settings）でだけ成り立つ（C9 の一覧には無い）。
