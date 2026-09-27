<!-- awesome-plan project=zedbsd record=ws035p101 -->

# ws035-p101: 表示の引き継ぎ（g4）: greeter と session の間に文字 console を出さない

Phase ID: `ws035-p101`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て「g4 seamless handoff (no text-console flash between the loader logo / greeter and the session)」。[login-manager-design.md](../login-manager-design.md) §5・§9 の g4）

## 前の状態（計測）

Venus の guest（graphical の image）で login の間の Venus の画面を 1 秒に数回撮った（`frames.py`）: greeter が終わって lease を
返すと、Venus の driver の console の worker が kernel の文字 console の snapshot（init・dhcpc の行）を描き、**約 3.3 秒**
文字 console が出てから desktop になった（`p101-20260928-before-console.png`）。`kmsg=quiet` の hidden は pcat の framebuffer
への描画だけを止め、snapshot は隠していなかった。

## 実装（2026-09-28）

- **kernel**（`src/drivers/platform/pcat/graphics/text.c`、HAL ではない）: 隠れた（`kmsg=quiet` で reveal 前の）console の
  snapshot は黒（文字を描かない）。表示が console に戻る間（Venus の console の worker）も画面は暗いまま。reveal
  （console の読み・panic・`kern_text_reveal`）の後は今までどおり文字。
- **引き継ぎの手順**（zsessiond と zdesktop の 1 行の protocol、`userland/base/zdesktop/handoff.c`（新））:
  - 表示を取る側は、遅い準備（Vulkan の device・wallpaper・glyph・入力）を済ませ、最初に表示を取る直前に `READY` を送り
    `GO` を待つ（20 秒で待たずに取る）。greeter は `--auth-fd`、session は新しい `--control-fd`（zsessiond が descriptor 3 と
    `--control-fd=3` を session の script に渡し、`session.sh` が `"$@"` で zdesktop へ）。
  - 手放す側は、zsessiond の合図で**先に表示を返し**（swapchain と lease、`zwl_handoff_release`）`RELEASED` を送ってから
    残りの後始末をする。
  - login: 認証の OK の後、greeter は「Starting session...」を出して残る（入力は取らない）。zsessiond は seat を user に移し
    session を起こし、session の `READY` で greeter の socket を shutdown → greeter が `RELEASED` → session に `GO` → greeter を
    reap（`zsessiond_greeter_release`・`_finish`）。
  - Log Out: session の zdesktop は `LOGOUT` を送って表示を続ける。zsessiond は seat を `_greeter` に移し新しい greeter を起こし、
    その `READY` で session に `QUIT` → session が表示を返し `RELEASED` して終わる → greeter に `GO`。main の loop はその greeter を
    引き継ぐ（`GREETER adopt`）。zsessiond の無い zdesktop（試験・手で起こす）は今までどおり（`--control-fd` 無し）。
  - zsessiond の session の loop は session の socket を読む（1 秒の poll。g5 の lock の `UNLOCK` もここに足す）。
- 計測用に zdesktop の `ZWL HANDOFF`・`ZWL GREETER closed`・`ZWL MODE window`・`ZWL EXIT` の行に `at_ms=`（単調時計）。
- 試験の道具 `plan/ws035/tests/frames.py`（新）: VNC で Venus の画面を続けて撮り、各画を black・text（文字 console）・picture に分ける。

## 検証（amd64、Venus の guest、graphical の image、2026-09-28）

- `plan/ws035/tests/zdesktop-p101.sh`（新）PASS: login と Log Out を撮り続け、**文字 console の画は 0**（login 114 枚・
  Log Out 129 枚の run）。黒の間は login・Log Out とも**約 1.1 秒**（前は文字 console 約 3.3 秒 + 黒）。zsessiond の log に
  `GREETER stays`・`HANDOFF session ready=1`・`greeter released=1`・`go written=3`、Log Out に `greeter ready: waits`・
  `session released=1`・`greeter go written=3`・`GREETER adopt`、引き継いだ greeter で再び login（`AUTH ok`）。
  画面: `p101-20260928-starting-session.png`（OK の後の greeter）、`-login-gap-black.png`（間の黒）、`-desktop-after.png`、
  `-greeter-after-logout.png`、`-login-again.png`、前の状態 `-before-console.png`。
- 回帰 PASS: zdesktop-p100（zsessiond の無い zdesktop: 引き継ぎは何もしない）。p098 の流れ（boot → greeter → login →
  Log Out → greeter）は p101 の試験が同じ道を通る。
- 規約: 新しい `handoff.c` の style-check 0、変えた zsessiond の 4 file・zdesktop の file は増えていない（text.c は HEAD と同じ 12）。
  build warning 0。
- 未実施: 実機（i915。i915 の console は firmware の framebuffer の写しで、隠れた console は logo のまま残るはず）、
  文字の console の boot（text.c の変更は hidden の snapshot だけ）。

## 残り（follow-up）

- **黒の約 1.1 秒も無くす**（本当の継ぎ目無し）: 表示の lease を持った GPU の fd を zsessiond → greeter → session と渡す
  （設計 §5）。libvulkan の「開いた fd と lease を使う」入口（WS075・libvulkan の持ち主と相談）と、渡した後の取り上げ（revoke、
  別 WS）が要る。今の残りの内訳: 手放す側の表示の返却 + 取る側の swapchain の作成（約 0.5 秒）+ 最初の frame。
- 黒の代わりに greeter の最後の画（または wallpaper）を kernel が保つ案（Venus の driver の変更、WS075 と相談）。
- session が `READY` を言わずに動き続ける（別の script、止まった zdesktop）と greeter が最大 30 秒残る（その後 zsessiond が
  終わらせる）。session が落ちて socket が閉じればすぐ終わらせる。
