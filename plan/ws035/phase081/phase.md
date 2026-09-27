<!-- awesome-plan project=zedbsd record=ws035p081 -->

# ws035-p081: 窓の body・popup・plain の look の wp_viewporter

Phase ID: `ws035-p081`
Parent: [WS035](../ws.md)
Status: in-progress（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示で WS035・WS070・WS071 を 1 つのサブエージェント（WS071 の agent）が進める。main の Queue
への反映は main の session）

## 範囲

[p080](../phase080/phase.md) から分けた残り（2026-09-27 main の指示: WS070 の titlebar（ws070-p010）の後に）: 窓自身・popup の
`wp_viewport` と plain の look を描画と hit に反映する。

- `shell.c`: 窓の大きさ（`window_size`）を buffer の大きさから `zwl_surface_size`（viewport の destination、整数の source、
  それ以外は buffer）に。これで body の矩形・hit・題名 bar の幅と button・配置・Wiseview の tile が viewport の大きさになる。
  `draw_body` の image は source の uv で描き、sub-surface の拡大率は窓の大きさから。Wiseview の tile も source の uv。
- `compose.c`: plain の look の窓を `zwl_compose_surface_quad`（surface の大きさ、source の uv）で描く（`zwl_compose_quad_image`
  を置き換え、popup と plain の sub-surface もこれを使う）。
- `popup.c`: popup の影・hit・grab の chain の hit を `zwl_surface_size` に。`subsurface.c`: 窓・popup の hit も surface の大きさ。
- `display.c`: 配置を surface の大きさで。fullscreen の直接 scanout は viewport のある窓では使わない（切り出し・拡大が要るため）。
  `toplevel.c`: geometry の無い窓の範囲、`protocol.c`: fullscreen の前の大きさを surface の大きさで。
- 試験: extras-probe に `--body-viewport`（窓の buffer の右半分を 600x300 に）、`plan/ws035/tests/zdesktop-p081.sh`。

## 受け入れ

1. Venus（QEMU）で `zdesktop-p081.sh` PASS: glass と plain の look で窓の body が viewport の source を destination の大きさで
   描く（画素）、sub-surface もその上に、題名 bar の閉じる button が 600 px の幅から置かれて効く。
2. 回帰: WS035 の p080（viewporter の sub-surface）・p076〜p079 の選んだもの、menu-regress の選んだもの、WS071 の files。
3. warning 0、style: 変えた file は悪化させない。

## 状態（2026-09-27、中断: main の wrap up の指示）

in-progress。コードは範囲の全部を書いた（style 0、変えた file は悪化なし）が、**build も試験も未実施**。作業ツリーを汚さないよう、
差分を [wip.patch](wip.patch)（shell.c・compose.c・popup.c・popup.h・subsurface.c・display.c・toplevel.c・protocol.c・
extras-probe の `--body-viewport`・新しい `plan/ws035/tests/zdesktop-p081.sh`）に置き、ソースは元に戻した。

再開の手順:
1. `git merge main`、`git apply plan/ws035/phase081/wip.patch`（衝突したら shell.c・compose.c を手で合わせる）。
2. build: `sh plan/ws071/tests/build-files-image.sh build/amd64`（files の image は menu の image の program を含み extras-probe もある）。
3. guest: `GUEST_RUNTIME=$PWD/build/ws071-run plan/ws071/tests/files-guest.sh start build/amd64/hdd-image.img`、
   `plan/ws035/tests/zdesktop-p081.sh`（glass と plain の画素、600 px の幅の閉じる button）。
4. 回帰: `plan/ws035/tests/zdesktop-p080.sh`（sub-surface の viewport。plain の sub-surface も surface の大きさで描くようになった）、
   p076〜p079 から選ぶ、`plan/ws070/tests/menu-regress.sh OUT p059 p064 p068 p072`、`plan/ws071/tests/files-regress.sh`、
   `plan/ws070/tests/titlebar-p010.sh`。
5. 結果を記録し、wip.patch を消して commit。
