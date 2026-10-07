<!-- awesome-plan project=zedbsd record=ws181-p009 -->
# ws181-p009: UAT 2026-10-07 の 6 回目（5320 実機）

Status: in-progress（P2、実装・build・host まで、QEMU は T1、5320 の session.log は未読）
Disposition: normal
Parent: [WS181](../ws.md)

## 由来（2026-10-07 ユーザーの UAT、5320 上）

「・アレンジメントのポップアップで、ポイント項目が変わったとき、1番目の選択要素が一瞬青くなって、ちらついています。
・スワイプはうまく動いています。
・気のせいかもしれないんですが、描画が重くなってFPSが落ちたような気がします。completion waitが入った？
・App Home画面で、トラックパッドの下端から2本指でスワイプアップすると、デスクトップに戻ってほしいです。二本指の判定はほかのスワイプと同じで、どちらかの指が下端にあればいいようにしてほしいです。
・App Home画面で、1番目のページから左にスライドできてしまいます。2番目のページから右にも。ないページへはスライドできないのがいいです。
・5320でもサウンドが再生されました。
・App Home画面では右上の日付をクリックしてもカレンダーが出ないでほしいです。
・デスクトップ画面で右上の日付をクリックしたとき、整列が反映されないので、整列モード中は反映されてほしいです。
・Dockの真ん中の仮想デスクトップのislandですが、通知アイコンのすぐ左に移動してほしいです。この位置にカメラホールがある機種のことを考えました。また、Filesのメニューの一部が仮想デスクトップの島の左になってしまい、おかしいと感じたことも理由です。」

## 範囲

1. 整列の popup: pointer の項目が変わる時に 1 番目の項目が一瞬青くなる（ちらつき）を直す。
2. 描画が重くなった疑い: 5320 の session.log の PERF・frame の時間を ws181-p008・BUG-246（cfdb21813、animation の間は毎 tick で dirty）の前後で比べ、animation が終わっても frame を頼み続けていないか、App Home の層の描画（layer_opacity・blur）が増えていないかを確かめ、原因があれば直す（「completion wait」の有無も確かめて答える）。
3. App Home の上で touchpad の下端からの 2 本指 swipe up で desktop に戻る（端の判定は他と同じく 1 本が下端にあれば）。
4. App Home の頁: 1 枚目から左へ、最後の頁から右へは滑らせない（無い頁へは動かない）。
5. App Home の上では右上の日付の click で calendar を出さない。
6. desktop の上の右上の日付の click で出る calendar（窓）を、整列モードの時は整列に入れる（他の窓と同じく枠へ）。
7. bar の真ん中の仮想 desktop の island（猫・鳥・ウサギ）を、通知の icon のすぐ左へ移す（camera の穴の機種、Files の menu が island の左に来るのを避ける）。

ユーザーの報告: 5320 で音が鳴った（HDA、記録だけ）。swipe は良好。

## 受け入れ

- build warning 0、host 試験、T1 の QEMU（ws181-guest.sh ほかの追従）、5320 でユーザーの UAT。

## 実装（2026-10-07 P2）

| 項目 | 原因と直し | file |
| --- | --- | --- |
| 1. 整列の popup のちらつき | 光る項目は「pointer の下の項目、無ければ keyboard の選択」で、選択は開いた時の 0 のまま。pointer が cell の間の gap を通ると pointer の下が無くなり、一瞬 1 番目が光った。motion で pointer の下の項目を選択にする（gap では最後に指した項目が光ったまま）。描き直しは光る項目が変わった時だけ | `arrange-shell.c` の `kwl_arrange_motion` |
| 2. 描画が重い疑い | 下を参照。BUG-246 の `arrange_moving` は、描画の中で終わりを記録する状態（menu の settled、fade の closed_ms、窓の glide_ms）が描かれないと戻らず、その間は毎 tick に dirty を立て続けた（例: 整列した直後に別の desktop へ移る・App Home を開く → glide 中の窓が描かれない → 全 frame を描き続ける）。それぞれを時間で区切る（その時間 + 100 ms） | `arrange-shell.c` の `arrange_moving`・`ARRANGE_MOVING_GRACE_MS` |
| 3. App Home で下端からの 2 本指 swipe up | `kwl_glass_gesture` が App Home の上で BOTTOM2 の始まりを `kwl_home_pad` へ渡す（端の判定は touchpad.c の今の規則、1 本が帯にあればよい）。`kwl_home_pad` は始まりに Home が出ていれば閉じる向き（`home_pad_closing`）: 開き具合 = 1 − travel/40 mm、離した時 0.30 以上か flick 100 mm/s で閉じる（`KWL HOME close via=pad`）、足りなければ開いたまま（`KWL HOME pad stays from=`）。log `KWL HOME pad swipe closing=0|1` | `shell.c`・`home.c` |
| 4. App Home の頁の端 | 頁の drag の offset を、1 枚目では右へ（前の頁を出す向き）、最後の頁では左へは 0 に留める | `home.c` の `kwl_home_motion` |
| 5. App Home の上の日付 | `bar_press` の App Home の判定を時計より先に（Home が出ている・開く途中なら何もしない） | `shell.c` の `bar_press` |
| 6. 整列モードの Calendar | 時計の click の前に `kwl_arrange_join_prepare`（今の desktop が整列なら layout と時刻を覚える）、`kwl_home_open_app` に `running`（既に動いていて前に出したか）を足し、後に `kwl_arrange_join_opened`: 動いていた窓は今すぐ同じ layout で整列し直す（`KWL ARRANGE join via=running`）。起動した時は 10 秒以内にその desktop に map した最初の窓が整列を終えずに加わる（`kwl_arrange_mapped`、`KWL ARRANGE join surface=N via=mapped` の後 `KWL ARRANGE apply …`）。他の新しい窓は今どおり整列を終える（WS181 S6） | `arrange-shell.c`・`home.c`・`shell.c`・`kwl.h` |
| 7. 仮想 desktop の island | `bar_layout` の desktops の pill を画面の真ん中から status の pill のすぐ左へ（`status_x − BAR_PILL_GAP − 幅`）。docked の時の bar の animation では status と一緒に動く。title と menu の領域（`desktops_line`）は右へ広がる | `shell.c` の `bar_layout` |

### 2. の答え（描画の重さ、「completion wait」）

- 毎 frame の経路（`compose.c`）に、新しい completion の wait は入っていない。frame の fence は前から `kwl_compose_complete` で待つ（fd が読めた時、または `vkGetFenceStatus`）。今日の p004a の変更は、出力の lost の判定と、250 ms ごとの hotplug の fence の `vkGetFenceStatus`（node への ioctl を 1 回）だけ。
- 見つけて直した物: 上の表の 2.（BUG-246 の毎 tick の dirty が、終わりの描かれない movement で止まらない）。この状態になると、何もしなくても全部の frame を描き続ける（重く感じる・FPS が落ちる原因になりうる）。
- 未確認: 5320 の session.log（`KWL PERF` の passes・frames・draw_ms）による前後の比較。2026-10-07 の作業中、10.0.30.5 は ssh・ping とも届かなかった。5320 が上がったら Q1 経由で読む: idle で `KWL PERF compose frames=` が 0 に近いか（毎秒数十なら dirty が止まっていない）、App Home の上で draw_ms が増えていないか。

### 確認（2026-10-07）

- build（warning 0）: `make -j16 BUILD=build/ws181-p009 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk build/ws181-p009/bin/wayland`、`make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws113-p004a-linux all`（どちらも rc 0）。
- host: `run-host-edge.sh`（69 checks 0 failures）、`run-host-arrange.sh`（1213 checks 0 failures）。変えた所（motion・gesture・bar）の host 試験は無い。QEMU で T1 が確かめる。
- style-check: 新規の違反 0（arrange-shell.c・home.c・shell.c は前後とも 12 件、既存）。`git diff --check` ok。
- host の絵 `plan/ws181/tests/p005-host.py`・`p007-host.py` は ws099 の p034 の bar の model（desktops の pill が真ん中）から位置を取るので、7. の新しい位置を描かない（review の絵だけ、合否には使わない）。
- 1. の確かめのため、光る項目が変わる時に `KWL ARRANGE menu lit item=NAME` を出す。
- QEMU の試験（T1 が流す）: 新しい `plan/ws181/tests/p009-guest.sh BUILD OUTDIR`（pen の guest、7・1・6・5・4・3 の順、合格は最後の行 `ws181-p009: PASS`、PNG は p009-bar・p009-menu-gap・p009-calendar-arranged・p009-home-first-drag・p009-home-last-drag）。guest に /bin/calendar が無ければ、試験の間だけ wltest の窓を開く script を置く。
- 未実施: QEMU（T1）、5320 の UAT。
