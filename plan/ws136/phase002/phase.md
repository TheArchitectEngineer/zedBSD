<!-- awesome-plan project=zedbsd record=ws136-p002 -->
# ws136-p002: login の試験（p095・p102・p103・p104）を 2026-09-29 の既定の image に合わせる

Status: in-progress（q655、P1 generation12、2026-10-04。直した、T2 の再試験待ち）
Disposition: normal
Parent: [WS136](../ws.md)
Queue: q655（Q1 の dispatch 2026-10-04: 「古い試験は見つけた担当がすぐ直す」方針。T2-007 の p095・p102 の FAIL の判定から）

## 原因（判定、2026-10-04）

T2-007（ws131-p006a、graphical の login の image、QEMU）の p095・p102 の FAIL は p006a の退行ではなく、試験の前提が古い。
- 2026-09-29 の既定の image の変更（Master）: base の passwd に kei（uid 1000、password kei）、root の password は root、sessiond は
  `/etc/keiland/autologin`（package の既定 kei）の利用者を起動のたびに login する。
- T2-007 の sessiond.log: `SESSIOND AUTOLOGIN user=kei` → `SESSION start user=kei` → `HANDOFF … go written=3`（sessiond は構成どおり）。
- p095: 自分で `sessiond --graphical` を起こすので kei が自動の login になり greeter が出ない。greeter は uid 1000 以上を passwd の順に出すので
  `users=2 selected=alice` は `users=3 selected=kei` になる。
- p102: 「boot で greeter、root の password は空で Enter」が前提（/run/user/0）。
- p104: boot の greeter と「kei を空の password で足す」が前提。
- p101 は ws035-p127 で直っていた。p103 は greeter の service を止め自分の zdesktop を起こすので、前提は今の image でも成り立つ（変更なし）。

## 直したこと（`plan/ws035/tests/`）

| 試験 | 直し |
| --- | --- |
| zdesktop-p095.sh | 開始の前に autologin を空に（終わりに戻す）。greeter の期待を `users=3 selected=kei`、Down で alice を選ぶ（`ZWL GREETER select user=alice`）。 |
| zdesktop-p102.sh | boot の kei の session を止め、autologin を空にして sessiond を起こす。kei（password kei、/run/user/1000、`user=kei`）で login・lock・unlock。終わりに autologin と session の script を戻す。 |
| zdesktop-p104.sh | 同じく boot の session を止め autologin を空にして sessiond を起こし、`users=[0-9]+ selected=kei` に password kei で login（2 回とも）。終わりに autologin を戻す。 |

## 検証

- `sh -n` で 3 本の構文。guest での実行は T2 に依頼（T2-007 の 2〜4 の再試験として）。未実施。
