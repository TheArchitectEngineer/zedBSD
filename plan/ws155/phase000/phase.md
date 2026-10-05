<!-- awesome-plan project=zedbsd record=ws155-p000 -->

# ws155-p000: Calendar の UI の mock

Status: in-progress（実装・host の PNG と animation の GIF・build は済み。QEMU は T1 待ち。ユーザーが mock を見て再指示する）
Disposition: normal
Parent: [WS155](../ws.md)
Queue: q745（2026-10-05、P2）
依存: なし

## 範囲（Q1、ユーザーのデザイン案と方針）

ws.md の「デザイン案」の要素（このままでなく要素として）と Q1 の方針で、月の表示を中心に sidebar・Add Event の panel・3D の日めくりの animation。data は固定の試験 data、保存は作らない。Windows 11 のように実用的でミニマルな中に知性、格好だけの効果は入れない。動きは少しだけ、落ち着いた 3D。

## 実装（2026-10-05、P2）

- `userland/desktop/calendar/`（新規、package `calendar`、既定では image に入らない `n`）:
  - `render3d.h/.c`: app の中の小さな software の 3D。三角形を 1/z の depth と重心座標で塗る（透視補正の texture、bilinear）、光は 1 つ（環境光＋拡散＋弱い highlight）、2 倍で描いて平均で縮める（輪郭の smoothing）。
  - `scene.h/.c`: box・quad・押し出し・楕円体・輪の mesh。日めくり（頁・下の頁の束・板・脚・綴じの bar と青い 2 つの輪、頁は上の綴じ目の線で回る）、周りを流れる半透明のリボン、種類の icon（Work の鞄・Personal のハート・Study の本・Family の 2 人）。
  - `date.c`（月の長さ・曜日・月と日の移動）、`data.c`（Work 青・Personal 赤・Family 緑・Study 黄の 4 つの calendar と、今日の月からの相対の日付の試験の予定）。
  - `view.c`: 3 つの card（不透明では淡い青の地に白い card と柔らかい影、glass では desktop が間に見える card）。左の sidebar（mark と「Calendar」、Month View・Today・Search・Settings、My Calendars の色の checkbox で表示・非表示、Add Calendar、下に「A more organized you」の card）。中央の上の帯（‹ ›・Today・Month / Week / Day の segmented・検索・…）、曜日の行（日曜は赤・土曜は青）、月を縦に続けた scroll（前後 6 か月、月の見出し、日曜の列は淡い赤・土曜は淡い青、前後の月の日は灰、今日は accent の地と丸、選んだ日は accent の輪、予定は calendar の色の pill、入り切らない分は「+N more」、検索に合わない予定は薄く）。右の panel（「Add Event」と説明、種類の card 2×2 に 3D の icon、Custom、下に 3D の日めくりと「Small plans make big days.」）。
  - 動き: 日めくりがゆっくり呼吸する（7 s と 9 s の周期で小さく傾く、リボンが 14 s で流れる。約 20 fps）。日付を選ぶ・‹ ›・Today・キーで日が変わると頁が上へめくれて薄れて消え（560 ms）、下の頁が新しい日。種類の card を日付へ drag して落とすと、その日に予定が足され（動いている間だけ）cell が 300 ms 沈む。「動きを減らす」（View の Reduce Motion、`--reduce-motion`）では呼吸を止め、頁と cell の変化はその場で。
  - キー: 矢印で日・週、Page Up/Down で月、T で今日。Week・Day・Settings・Add Calendar・Custom・… は「not in the mock yet」の chip。
  - `main.c`: kl_app の loop（Phone と同じ形、glass は Phone と同じに first frame で判定し panel を送る）。今日は system の local の日。
- `platform/amd64/vmunix.mk`: `bin/calendar` の link の規則。`userland/desktop/wayland/apps.conf`: App Home に「Calendar」。
- 試験の image の config: `plan/ws155/tests/config-amd64-calendar.mk`（CI の image ＋ calendar）。

## 確かめ

- host: `sh plan/ws155/tests/run-host-calendar.sh` → PASS 7（start・select（FLIP 10-05→10-14）・drop（DROP Work 10-22）・next（11-22）・hide（Work を隠す）・motion・reduced）。今日は 2026-10-05 に固定。
- PNG（`build/ws155/`）: `host-calendar-start.png`、呼吸の `breath-0..3`（1.75 s 毎）、頁めくりの `flip-0..5`（110 ms 毎）、`drag`（drag の途中）、沈む `sink-0..2`、`november`、`hidden`、`reduced`、`glass`（壁紙をぼかした近似の地）。日めくりの部分の animation は `host-calendar-desk.gif`。
- build: `make ZEDBSD_CONFIG=plan/ws155/tests/config-amd64-calendar.mk BUILD=build/ws155-zed build/ws155-zed/bin/calendar` が warning 0。`sinf` などは zedBSD の libc.so にあることを確かめた。host の gcc `-Werror`・clang `-Wshadow` も 0。
- style-check: 新しい file は違反 0。
- QEMU（T1 に依頼する）: App Home から Calendar を起動、全画面の PNG、日付の click と drag の PNG。
- 未実施: QEMU、実機、Week・Day の表示、保存、3D の CPU の負荷の測定（呼吸の間は約 20 fps で 3D の部分を描き直す）。
