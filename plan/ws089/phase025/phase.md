<!-- awesome-plan project=zedbsd record=ws089-p025 -->

# ws089-p025: Sharing の頁に SSHD の ON/OFF

Status: planning
Disposition: normal
Parent: [WS089](../ws.md)
Queue: 未定

## ユーザーの要望（2026-10-04 夜、原文）

「SettingsのSharingタブに、SSHDのON/OFFがあってもいいと思いました。また、あとでクラウドストレージもここで設定できるようにしたいです。」

## 範囲

1. Sharing の頁（今は stub）に SSHD の ON/OFF の switch。状態（動いているか、待ち受けの port、host の鍵の fingerprint は設計で決める）を表示する。
2. 経路: Settings → libkeiland（kl_system_*）→ compositor の拡張 → libkeiland-backend → zedBSD の service の仕組み（WS002 の service の enable・disable と start・stop、`service` の command）。起動時に有効にするかの永続化も service の設定で。app は OS の口を持たない（Guardrail）。
3. 権限: 誰が SSHD を切り替えられるか（管理者だけ、など）を設計で決める。
4. **後で**: クラウドストレージの設定もこの頁に置く（ユーザー）。[WS146](../../ws146/ws.md)（SSH を使う独自のオンラインストレージ）の設定の場所の候補。

## 受け入れ（案）

- switch で sshd が起動・停止し、再起動の後も設定が保たれる。SSH で接続できる・できないを QEMU で確かめる（T1）。
- C の全文の規約、build warning 0、OS の境界の checker。
