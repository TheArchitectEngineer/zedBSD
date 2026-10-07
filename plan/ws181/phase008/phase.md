<!-- awesome-plan project=zedbsd record=ws181-p008 -->
# ws181-p008: UAT 2026-10-07 の 5 回目（App Home への遷移の effect、touchpad の端の 2 本指 swipe、上端の swipe down）

Status: planned（担当はユーザーに確認中）
Disposition: normal
Parent: [WS181](../ws.md)

## 由来（2026-10-07 ユーザーの UAT、5320 実機）

「App Homeへ遷移するとき、現在のスライドのアニメだと、一時的な印象を与える。アプリのあるデスクトップが奥に消えていき、ホーム画面が奥から表れる、iOSと同様なエフェクトがいいと思う。」
「タッチパッドの左端、右端からの２本指スワイプについて、指が２本ともエッジにないと認識されていない。片方がエッジにあれば認識されるようにしたい。上端、下端も同様。」
「タッチパッドの上端からのスワイプダウンについて、現在はアプリ最大化解除に割り当てられているので、ホーム画面への遷移に割り当ててほしい。」

## 範囲

1. App Home への遷移: desktop（app の窓）が縮みながら奥へ消え、App Home が奥から大きくなって現れる（iOS と同じ）。戻る時は逆。今の slide を置き換える。
2. touchpad の端からの 2 本指 swipe（左・右・上・下）: 2 本のうち 1 本が端の帯にあれば端の swipe と認める。
3. touchpad の上端からの swipe down を App Home への遷移に（今は docked の解除）。docked の解除は他の操作（bar の 2 回 tap など）に残す。
4. 画面の左上からの右下への swipe は今 App Home だが、[WS184](../../ws184/ws.md) の左手デバイスの OSK に移るので、App Home の入口が上端の swipe down・Super・他に揃うよう整理する。

## 受け入れ

- build warning 0、host 試験、T1 の QEMU（ws181-guest.sh・p003-guest.sh の追従）、5320 でユーザーの UAT。
