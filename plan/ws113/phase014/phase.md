<!-- awesome-plan project=zedbsd record=ws113-p014 -->
# ws113-p014: 拡張の時に個々の display を off にする（Settings の Display の頁）

Status: planned（P1、p007 の後）
Disposition: normal
Parent: [WS113](../ws.md)

## 由来（2026-10-08 ユーザーの UAT、5330 の実機）

「HDMIに出力されました。extendもmirrorも動いています。マウスはまだextendに移動できません。extendのとき、個別のディスプレイをオフにできる設定を作ってください。そうすれば、タッチパネルのテストができるのではかどります！」

→ D-MODES（2026-10-02 の「出力ごとの off は Settings に出さない」）をこの指示で置き換える: 拡張の時だけ、display ごとに on/off を選べる。

## 範囲（案、P1 が詰める）

1. Settings の Display の頁: 拡張の時、card ごとに on/off（少なくとも 1 つは on のまま、全部 off は出来ない）。mirror では出さない。
2. compositor（kwl_displays_apply・displays.conf）: off の display は head を閉じる（RELEASE、消灯）。off にしたのが anchor（内蔵の panel など）なら、on の display へ anchor を移す（p004a の switch・p011a の付け替え）。on に戻すと拡張の位置に戻る。保存（D-STORE）。
3. kl_system_displays の protocol: apply に off の印（places の行に off、または新しい field）、snapshot の flags に OFF。libkeiland の API の版を上げる。
4. 試験: host（validate: 全部 off の拒否、anchor の移し替え）、QEMU（Venus 2 出力: head 1 を off・on、head 0 を off で anchor が head 1 へ）、5330 の実機（内蔵を off にして HDMI だけ、HDMI を off にして内蔵だけ、touch の試験に使えること、ユーザー）。

## 受け入れ

- build warning 0、host 試験、T1 の QEMU、5330 でユーザーの UAT。
