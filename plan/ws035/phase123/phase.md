<!-- awesome-plan project=zedbsd record=ws035p123 -->

# ws035-p123: App Home のアイコンを Kei の見た目の絵に

Phase ID: `ws035-p123`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-29、サブエージェント。QEMU の Venus と host。実機は未実施）
Phase disposition: normal
Queue: なし（2026-09-29 main の割り当て「App Home のアイコン」、demo critical。2026-09-27 に Future Work とした icon を demo のために前倒し）

## 範囲（依頼）

デモの App Home（`plan/ws035/demo/apps.conf` の Files・Notes・Terminal・PDF Viewer・Browser・Model viewer・Gears・X terminal）で、
アイコンの無いもの・仮のもの（名前の頭文字の monogram。Lock Screen と Log Out が同じ「L」、p116 の一覧の 7）を Kei の見た目に揃える。
手描きの vector か code で描く。第三者の画像・フォントを git に入れない。

## 設計

- 絵は `userland/desktop/wayland/icons.c`（titlebar の control の icon と同じ仕組み: 24 単位の格子の上の線・円・箱を、画素の中心からの距離で
  塗る）に App Home の 10 枚を足す。全部この file の座標で描いた線画で、既存の icon 集や font から取っていない。
  - Files: 左上に tab のある folder。Notes: 行の書かれた page と、その上を書く pen。Terminal: 画面と prompt（`>`）と cursor（`_`）。
    PDF Viewer: 角の折れた文書と 2 行。Browser: 地球（円・赤道・横から見た子午線）。Model viewer: 上の角から見た立方体。
    Gears: 8 つの歯の歯車と中の穴。X terminal: 画面と X と cursor。Lock Screen: 鍵穴のある南京錠。Log Out: 開いた扉から出る矢印。
  - 描くための部品を 3 つ足した: 枠（角の丸い箱の輪郭 `ICON_FRAME`）、円弧（`ICON_ARC`、中心・半径・始まりの角度・掃く角度）、
    穴（`ICON_HOLE`、他の部品の塗りを抜く円）。線分は自分の太さ（e、0 なら既定の 1.8 単位）を持てる。1 枚の部品の上限は 8 → 12。
- 白い線画を、app の色の角の丸い四角（72 px、角 18 px）の中央に 40 px で置く。atlas には titlebar の icon（16・20 px）とは別に
  App Home の絵だけを 40 px で 1 行に描く（`GLASS_APP_ICON_PIXELS`、10 枚で 410 px 幅）。起動の時に icon が 1.3 倍に育つ間は絵も一緒に拡大する。
- 四角の塗りを Kei の glass の調子に: 上半分は上ほど白く（最大 22%）、下半分は下ほど暗く（最大 10%）の縦の gradient を 24 本の帯で描き
  （各帯は四角全体の角の丸みで切るので継ぎ目が出ない。12 本では帯の段が見えたので 24 本）、縁に薄い白の rim（22%）。
  前の上半分の硬い sheen の帯はやめた。
- どの絵を使うかは `apps.conf` の 5 番目の欄（`name|command|keywords|RRGGBB|picture`、picture は `files`・`notes`・`terminal`・`pdf`・`browser`・
  `model`・`gears`・`xterm`）。無い・知らない名前は今までどおり頭文字（4 欄の既存の apps.conf はそのまま動く）。組み込みの一覧と
  Lock Screen（`lock`）・Log Out（`logout`）は code で名前を渡す。試験用の Vulkan test・Shared memory は頭文字のまま。
- デモの `apps.conf` に絵の名前を足した（WS075 の HDMI の demo の image と demo の Venus の image の両方が使う file）。

## 実装（2026-09-29）

| file | 変更 |
| --- | --- |
| `userland/desktop/wayland/icons.h` | `GLASS_ICON_APP_*`（10）、`GLASS_ICON_FIRST_APP`・`GLASS_ICON_APPS`、`zwl_icon_named()` |
| `userland/desktop/wayland/icons.c` | 10 枚の部品の表、`ICON_FRAME`・`ICON_ARC`・`ICON_HOLE`、線分の太さ、`icon_arc_distance()`、穴の引き算、名前の表と `zwl_icon_named()` |
| `userland/desktop/wayland/glass.c` | App Home の絵を 40 px で atlas の 1 行に（`app_icons`）、`glass_draw_icon()` が絵の番号ならその glyph を使う |
| `userland/desktop/wayland/home.c` | `struct home_app` の `picture`、5 欄の読み取り、組み込みの一覧の絵の名前、`home_draw_tile()`（gradient の帯と rim）、絵か頭文字 |
| `plan/ws035/demo/apps.conf` | 8 行に絵の名前 |
| `plan/ws035/tests/zdesktop-p090.sh` | 試験の前に止める process の名前を改名後の名前に（`zdesktop` のままで前の compositor が残り、swapchain が作れず FAIL していた） |

## 検証（2026-09-29、QEMU の Venus。実機は未実施）

- build warning 0（wayland、demo の Venus の image）、`style-check.py` icons.c・icons.h・glass.c・home.c 違反 0。
- host: `plan/tools/titlebar/icons-host.c` を新しい icons.c で build して実行、`icons-host: PASS`（全 25 枚が 16・20・32・64 px で何かを塗り、
  枠の端に触れない）。scratch の preview（40 px の絵を 72 px の四角に）で絵の形を目で確かめてから guest で確かめた。
- QEMU（demo の Venus の image、1920x1280、利用者 kei）: `demo-walk.sh` の 03 の App Home に 9 枚の絵（Browser は start page が無いので出ない）。
  拡大して線の太さ・gradient の段が無いことを確かめた。`p121-20260929-demo-03-apphome.png`。
- `zdesktop-p090.sh build/p121-p090 demo`（1280x800、demo の一覧の 7 つを絵の icon から順に起動）PASS: 全部 `mapped`、`zdesktop: no ERROR`。
  `p123-20260929-p090-{00-home,…,99-all}.png`。
- `zdesktop-p071.sh`（30 個の app、4 欄の apps.conf、2 page、drag・wheel・key・select・launch の grow・close）PASS。頭文字の tile も新しい
  gradient で描かれる: `p123-20260929-p071-page1.png`。
- 未実施: 実機（5330 + HDMI の 1920x1280、i915 での atlas の新しい行と帯の描画）、Browser の絵の画面（demo の image に start page が無い）。

## 残り

- window の title bar の左の app の印（`shell.c` の `draw_title()`、青い四角に頭文字）も、app ID が分かる app はこの絵にすると揃う
  （shell.c は入力の作業と近いので今回は触っていない）。
- 絵の意匠の最終の判断（ユーザー）。
