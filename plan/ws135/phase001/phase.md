<!-- awesome-plan project=zedbsd record=ws135-p001 -->
# ws135-p001: 設定の API と desktop.conf の設計

Status: in-progress（q652-i01、P2 generation7。design.md 第 2 版、design-reviewer の review を反映。ユーザーの判断 D1〜D6 と Phase の ID 待ち）
Disposition: normal
Parent: [WS135](../ws.md)
Queue: q652 / q652-i01（2026-10-04 user「P1、P2はBUG-143, 053, 052, 162, 103, 129, 033, 157, 139を修正します。直す順序は任せます。」）

## 範囲

WS135 の「完了の条件」を満たす設計を `plan/ws135/design.md` に書く。

1. 今の設定の項目と読み手・書き手の棚卸し（`libkeiland/preferences.c`、`wayland/preferences.c`・`volume.c`、`settings/look.c`・`sound.c`、Terminal などの app 固有の設定、試験と道具）。
2. 項目ごとの解決の先（libkeiland が直接: 例 音量 → audiod / compositor の拡張: 壁紙・透明度・pointer・keyboard ほか）と、値の型・範囲・既定。
3. libkeiland の API: 読み・変更・監視（通知の callback、event loop への組み込み、別の process の変更の伝わり方）。WS131 の `kl_` の命名と backend の境界に合わせる。
4. compositor: 設定をメモリに持つ store、拡張の settings の interface（`kl_system_manager_v1` の version の予約）、session の開始の読みと終了の書き（Log Out・Shut Down・Restart・SIGTERM）、書きを event loop で待たない方法、監視の thread の除去。
5. 移行の順と Phase の分け方、試験（host・QEMU）、WS131 p010・p011 の表の書き換えの案。

## 受け入れ

design.md と Phase の表があり、design-reviewer の review を反映している。ユーザーに方針の要点を示す。

## 結果（2026-10-04、q652-i01）

- [design.md](../design.md) 第 1 版を source の棚卸しから書き、design-reviewer（agent a1381fb0763e9133f）の敵対的レビュー（blocker 1・major 9・minor 6）を受けて第 2 版に改訂（対応表は design.md §11）。
  主な改訂: desktop.conf の終わりの書きを「この session の差だけ merge」に、Log Out の書きは不変の snapshot を worker に、settings の object は libkeiland の専用の queue に置いたまま、
  壁紙の読みを worker と通常の file の検査に、音量を compositor の項目に（D2）、WS131 の共通の result・capabilities・`kl_backend_peer_uid` に揃えた、session の終わりの経路の表（SIGHUP を足す）、
  keyboard の repeat の送り直し、試験の計画（merge・手での編集・SIGTERM・cache の層・静的な検査）。
- Phase の案（design.md §7）: p002 compositor の store と protocol、p003 libkeiland の `kl_settings_*` と probe、p004 Settings の移行と監視・旧 API の除去、p005 app だけの設定（D3）、p006 全文規約と回帰。
- 判断待ち（design.md §9）: D1 manager を WS135 が先に作るか、D2 音量の解決の先、D3 app だけの設定、D4 crash の喪失、D5 未知の key、D6 system bar の読み替え。Q1 経由でユーザーへ。
- 未実施: build・試験（設計のみ）。
