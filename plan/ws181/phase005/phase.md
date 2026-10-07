<!-- awesome-plan project=zedbsd record=ws181-p005 -->

# ws181-p005: App Home の上の bar、整列のメニューと pill（UAT 2026-10-07 の 2 回目）

Phase ID: `ws181-p005`
Parent: [WS181](../ws.md)
Status: cleared（2026-10-07 Q1 の判定: T1-345 で ws181-guest.sh status 0（arranged-size を含む）、Q1 が PNG を目視: App Home の上は bar 無しで status と時計だけ白、整列のメニューは 5 つの絵だけの glass、窓は枠を埋める、pill は capsule と丸い点）（旧: in-progress（2026-10-07 q843 P2: 実装・build・host の試験と host の PNG まで。QEMU は T1 へ））
Phase disposition: normal
Queue: q843（P2、Q1 の ACK 2026-10-07「範囲 1〜5 で ACK、c10 の窓が枠の大きさにならない件も p005 に」）

## 由来（ユーザー、2026-10-07、原文）

> App Home画面のとき、画面上部のドックバーを表示せず、でも右上の通知アイコン領域と時計領域を、背景色による塗りつぶしなしで、白い文字とアイコンで描画してほしいです。クリックされたときの動作は、App Home以外と同じでよいです。アレンジメニューのポップアップですが、アイコンだけにして、テキストは不要です。また、操作はcurrent virtual desktopに対して行われることにして、1,2,3,4のテキストは不要です。ポップアップにglassを適用してください。アレンジメント使用中に、virtual desktopのアイコンに、アレンジメントの選択が表示されていますが、これは不要です。また、仮想デスクトップの選択状態を表すカプセル領域は、半分の横幅でよく、それぞれのデスクトップを表すアイコンは、正円でいいです。

クリックの回答: desktop の切り替えは「pill の tap は常にメニュー、切り替えは swipe・key だけ」。

## 範囲

1. App Home の上（`kwl_home_progress` > 0）: bar（strip・launcher・線・apps の pill・desktops の pill・docked の title と button）を描かず、status と時計だけを pill の地無しで白（alpha 0.96）で。click は Home の外と同じ（status・時計の press、volume の popup・network の menu が開いている間の全部の button は Home に渡さない）。bar_press は Home の間は時計だけ。
2. 整列のメニュー: desktop の 4 つの絵の行と layout の名前を外し、5 つの layout の絵だけを横 1 列（48×40 の cell）。今の desktop に対して。glass は power の card と同じ（白 0.62、edge 0.70）。key は Left・Right（Up・Down も）。
3. pill の整列の印（`kwl_arrange_draw_mark`）を外した。
4. pill: 今の desktop の capsule を 15×12（slot の半分の幅、slot の中央）、他の desktop の点を 7 px の正円。
5. pill の tap はどこでもメニュー（前から）。メニューから desktop の切り替えを外した（`kwl_glass_desktop_turn` は使う所が無くなったので削除）。
6. （Q1 の追加、T1-344 の c10-arranged）整列の適用の後、窓が枠の大きさにならない: compositor は適用で大きさの configure を送っている（`kwl_glass_place_body`）。`kwl_glass_committed` が floating の窓で、configure の ack の後に来た古い大きさの image（client が configure を読む前に描いた物）を「新しい大きさを描いた」と見なして待ちを終えていた（stale の判定は docked の大きさとしか比べていなかった）→ 送った時の image の大きさ（`resized_from_width/height`、kwl.h）と同じで送った大きさと違う image は古い物として待ちを続ける。試験は client が枠の大きさを描くのを待ってから撮る（下）。T1-344 の c11-swapped が適用の時の大きさで描かれていたので、client の描き直しは 1 回の configure ぶん遅れて見える（client 側の遅さの詳細は全行の log で次の試験で確かめる）。

## 実装（2026-10-07）

- `userland/desktop/wayland/shell.c`: `draw_system_bar`（Home の上は `draw_home_status` だけ）、`draw_status`（pill は呼ぶ側）、`home_bar_passes`、`kwl_glass_button`（Home の前に判定）、`bar_press`（Home の間は時計だけ）、`draw_desktops`（capsule・点）、`DESKTOP_DOT`・`DESKTOP_SHOWN_WIDTH`、`window_resized`・`kwl_glass_committed`（古い image の判定）、`kwl_glass_desktop_turn` の削除。
- `arrange-shell.c`: メニューの cell・描画・開く・item の矩形・act、key、`kwl_arrange_draw_mark` の削除、`arrange_labels` の削除。`glass.h`・`kwl.h` の宣言。
- `userland/desktop/locale/wayland.keys`・`ja/wayland.tr`: layout の名前の 5 行を削除（使われなくなった）。
- 試験: `plan/ws181/tests/ws181-guest.sh` の C10 に `fits_slot`（各 client の最後の `KWL GLASS resized` が適用の後に来て、最後の `KWL CONFIGURE` の大きさと同じ）を足し、3 つの窓が枠の大きさを描くまで最大 8 秒待ってから撮る（`arranged-size-<client>` の判定）。終わりに zdesktop.log の全行と wltest の RESIZE の行を out に保存。`tests/scenarios/desktop/windows/arrange.md` の手順 1・2・6 を新しいメニューと「pill で desktop は切り替わらない」に。
- host の PNG: `plan/ws181/tests/p005-host.sh`（ws099-p034 の host の renderer の上に `p005-host.py`、arrange.c の枠は `p005-icon-dump.c`）→ `build/review/ws181-p005.png`。

## 確かめ（2026-10-07）

- build: zedBSD の compositor（`make ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws181 build/ws181/bin/wayland`）と Linux の Keiland（`make keiland-linux KEILAND_LINUX_BUILD=build/ws181-linux`）warning 0。
- host: `run-host-arrange.sh` 1004/0、`run-host-edge.sh` 29/0、`run-host-layout-state.sh` 43/0。host の PNG（renderer の近似、compositor の撮影ではない）。
- QEMU: 未（T1 へ: ws181-guest.sh の全部（C10 の大きさの判定）と、App Home の上の bar・メニュー・pill の撮影）。
