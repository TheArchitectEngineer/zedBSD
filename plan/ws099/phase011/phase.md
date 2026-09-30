<!-- awesome-plan project=zedbsd record=ws099-p011 -->

# ws099-p011: BUG-121 窓の角の drag で窓が消える、の切り分けと直し

Status: cleared（2026-09-30、サブエージェント P5、worktree `ws035-keiland`（branch `wt/ws035`）。QEMU の Venus と 5330 の i915 passthrough）
Disposition: normal
Parent: [WS099](../ws.md)
Queue: なし（2026-09-30 Q1 の割り当て「BUG-121 を ws099-p011 として」）
依存: p010（cleared）

## 範囲と受け入れ

- [BUG-121](../../bugs/BUG-121.md): Model viewer の窓の角を drag すると窓が消えた（ws075-p025、5330 の passthrough、1 回）。
- resize の stress の試験を作り、QEMU の Venus で Model viewer（Vulkan）と wlshm（wl_shm）で角の drag 100 回、QEMU で出なければ 5330 の
  passthrough で 20 回。消えたら app と compositor のどちらが先かを log で切り分けて直す。
- 目標: QEMU 100 回・実機 20 回で窓が消える回数 0。回帰は `criteria.sh` の C2・C9 と boot test。
- IME の file（`ime.h`・`text-input.c`・`input-method.c`）と seat.c の IME の hook、ブラウザは触らない（触っていない）。

## 試験（`plan/ws099/tests/`）

- `resize-stress.sh [APP] [COUNT] [OUTDIR]`（QEMU の Venus、criteria の image、1920x1280）: 角を 4 つ順に、外へ 60〜196 px、次に同じだけ戻す
  （8 段、16 ms 間隔）。drag ごとに compositor の log の `ZWL RESIZE start`・`settled` が 1 つ増えるか、app の process と
  `ZWL UNMAP`・`ZWL CLIENT gone` を見る。消えた回（vanished）、resize にならなかった回（missed: press が他の物に行った）、error を数え、消えた時は
  app と compositor の行を順に保存して app を起こし直す。`CROWD="Files,PDF Viewer"` は先に他の app を開き、窓を他の窓の上に置く。
  APP は `mview`（App Home から、デモと同じ経路）と `wlshm`（SSH から session の socket に、`--band --hold`）。
- `resize-hw.sh IMAGE OUTDIR [COUNT]`（5330 の passthrough、WS075 の `hdmi-h4-hw.sh`、flock の下、QEMU は 10 分まで）: App Home の 10 app を
  p025 と同じ順（`hdmi/apps8.sh`、Model viewer は 8 番目）に開き、最初の画面から Model viewer の本体（灰色 0x333333）を求め、角を内へ縮めて
  同じだけ戻す drag を COUNT 回。毎回撮り、本体の見えない画面を消失と数える。最後に Terminal で session の log を
  `/home/kei/resize-hw.log` に写し、image から読む（/run は disk に無い）。`ORDER=top` は Model viewer を最後に開く。

## 切り分けと原因

5330 の passthrough（hw2、直しの前、p025 と同じ並び）: 20 回のうち **6 回**窓が見えなくなった（drag 2・3・10・11・18・19、どれも左上の角）。
log では左上の角の上で resize の矢印（`ZWL CURSOR frame edges=5`）が出た後、press は下の PDF Viewer（client 6）に行き（`ZWL PING pong
client=6`）、`RESIZE start` は無い。画面（`build/ws099/hw2/shots/drag-2-live.png`）では PDF Viewer が前に出て Model viewer を覆っている。
app は落ちておらず、compositor も窓を失っていない。**窓が他の窓の後ろに回った**のが「消えた」の正体。

- 原因 1（compositor、`titlebar-shell.c`）: client の title bar の control の press（`zwl_titlebar_button`）は、最後に描いた control の領域
  （`shell_hit_at`）だけで決まり、その点で上にある窓を見ていなかった。上の窓の frame の帯（外側 8 px、cursor は resize の矢印）が下の窓の
  title bar の control に重なると、press は下の窓の control が取り、その窓を前に出した。menu（`menu-shell.c`）と drop（`zwl_titlebar_drop_at`）は
  「その点で上の窓がその窓の時だけ」の check を持っていたが、press には無かった。上の窓の title bar・本体の下に control が隠れている時も同じ。
- 原因 2（compositor、`shell.c`、hw1 で見つけた）: 新しい窓の置き場所（`zwl_glass_place`・`zwl_glass_fit`）は下と左右に 12 px しか空けず、
  Wiseview の下の帯（20 px）と desktop の swipe の左右の帯（16 px）が frame の帯（8 px）を丸ごと覆った。画面の下まで置かれた窓（1920x1080 で
  `ORDER=top` の Model viewer、本体の下端 1068）は下の角で resize できず、左下の角を上へ drag すると Wiseview が開いて窓が見えなくなった
  （hw1 の drag 6・7・14・15、`build/ws099/hw1/shots/drag-6-live.png`）。帯は frame より先に press を取る設計（`frame_under_pointer`）。

p025 の元の 1 回（r1・r2、右下の角の近く）の log は残っていない（/run の session.log は disk に無く、kernel の log は最初の draw だけ）。原因 1・2 の
どちらか、または下の app の失敗かは決められない。

QEMU（Venus、直しの前）: Model viewer 単独と wlshm で 100 回ずつ、消失 0・missed 0（他の窓の無い場面では原因 1・2 は起きない）。
Files と PDF Viewer の上（`CROWD`）では、Model viewer を大きくする swapchain の作り直しが `vkCreateSwapchainKHR` result=-4
（VK_ERROR_DEVICE_LOST）で失敗して app が終わる（`MVIEW FAILED ... errno=8`）。log の順は `ZWL CLIENT gone client=N reason=read`（client が
自分で切った）が `MVIEW FAILED` より先で、compositor は窓を落としていない（app 側）。5 つの app の上では起動時の swapchain の作成でも同じ失敗。
libvulkan・Venus の側（この Phase の修正範囲の外）なので、main に別の bug として登録を依頼する（下の「残り」）。

## 変更

- `userland/desktop/wayland/titlebar-shell.c`: `zwl_titlebar_button` の press で、浮いた title bar の control は、その点で一番上の窓
  （`zwl_glass_window_at`: title bar・本体・frame）がその control の窓の時だけ取る。他は press を先へ渡す（原因 1）。
- `userland/desktop/wayland/shell.c`: `glass_clear_edges` を足し、`zwl_glass_place` の候補の場所を、下の frame の帯が Wiseview の帯の上、
  左右の帯が desktop の帯の内になるよう寄せる（下 20+8 px、左右 16+8 px。入らない大きさの窓は今のまま）（原因 2）。
- `userland/desktop/wayland/main.c`: client を外す時に `ZWL CLIENT gone client=N reason=read|hangup|flush|error` を出す（app が先に切ったか、
  compositor が protocol error で外したかを log で分けるため）。

## 検証

| 確認 | 結果 |
| --- | --- |
| build（criteria の image `build/ws099/p011-after.img`、demo の passthrough の image `build/ws099-demo-pt/hdd-image.img`） | exit 0、compositor の warning 0（noct の既存の 1 件は範囲外）、`git diff --check` 0 |
| 実機 hw1（直しの前、`ORDER=top`、20 回） | 数の上は消失 0 だが、drag 6・7・14・15 で Wiseview が開いて窓が見えない、drag 0 は resize にならず（原因 2） |
| 実機 hw2（直しの前、p025 の並び、20 回） | **FAIL**: 消失 6（左上の角、原因 1）、`RESIZE start` 14 回（`build/ws099/hw2.log`） |
| 実機 hw3（直しの後、p025 の並び、20 回） | **PASS**: 消失 0、`RESIZE start` 20 回、MVIEW FAILED・ERROR 0（`build/ws099/hw3.log`、`hw3/session.log`） |
| QEMU 直しの前: Model viewer 100 回・wlshm 100 回 | 消失 0・missed 0・error 0（`build/ws099/before-mview.log`・`before-wlshm.log`） |
| QEMU 直しの後: Model viewer 100 回・wlshm 100 回 | Model viewer: 消失 0・missed 0・error 0（`build/ws099/after-mview.log`）。wlshm: RESULT_WLSHM |
| QEMU `CROWD="Files,PDF Viewer"` 40 回 | 前: 13 回で消失 6（全て swapchain の失敗）。後: 消失 5・missed 4（swapchain の失敗と、その後の起こし直しの窓）。hostmem=1G: RESULT_HM |
| 回帰 `criteria.sh` C2・C9 | RESULT_REG |
| boot test | RESULT_BOOT |

画面: `build/ws099-shots/after-mview/final.png`（QEMU）、`build/ws099/hw3/shots/drag-*-live.png`（実機）、`build/ws099/hw2/shots/drag-2-live.png`
（直しの前、PDF Viewer が前に出た）、`build/ws099/hw1/shots/drag-6-live.png`（直しの前、左下の角の drag で Wiseview）。

## 残り

- QEMU の Venus で、他の Vulkan・GPU の app の上の Model viewer を大きくすると swapchain の作成が DEVICE_LOST で失敗し app が終わる
  （上の CROWD）。libvulkan・Venus（WS014・WS031）の側。main に新しい bug の登録を依頼する。5330 では 20 回で起きていない。
- 画面の端に置かれた窓を、使う人が端まで動かし・広げた時は、帯が frame を覆う（置き場所だけを直した）。設計（帯が先）はそのまま。
- p025 の元の 1 回の直接の原因は log が無く決められない。
