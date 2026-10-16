<!-- awesome-plan project=zedbsd record=ws039-p006 -->

# ws039-p006: 段 A6〜A8（display の引き継ぎ: flip・割り込み・cursor・display 付きの登録）

Status: in-progress（2026-10-16 P3: 実機 10.0.10.3 で A6・A7・A8 ok、ユーザーの目視 合格（「テストは成功です」）。Q1 の merge と clearance の判定待ち）
Disposition: normal
Parent: [WS039](../ws.md)
Queue: q1220 / q1220-i01
承認: 2026-10-16 ユーザー「p006, p009, p012の順でどうですか？」
実行者: P3（worktree `/home/awe/zedBSD-worktrees/p3`、branch `agent/p3-ws039`、base main `4aadd5780`）

## 範囲（[design.md](../design.md) §16 の p006 の行）

client の surface と flip、vblank・flip・HPD の割り込み、cursor、display・scanout の ops と DISPLAY_CONTROL、display 付きの登録、release で GOP に戻す。**実機: A6〜A8 の ok（画面はユーザーの目視）**。design §8.3〜8.5（GOP の framebuffer を console のまま残し flip だけ、timing・link・clock は触らない）、§15 #8（最初は `amdrdna.disp.own=1` の opt-in）、mode set は p014 以降。Guardrail の scanout の規則（GOP の出力先以外に scanout を始めない）: 引き継ぐのは GOP が点けた OTG0→HUBP0 の plane と cursor だけで、別の出力は触らない。

## 実装（2026-10-16 P3）

- `display-format.c`（新、host でも動く）: cursor の位置と hot spot（左・上にはみ出た分を hot spot に、全部外なら非表示）、HUBP と DPP の cursor の制御の語（premultiplied、pitch 64、8 行の fetch）、大きさ・timing の語、24 bit の frame counter の差、frame 数と時間からの refresh、OTG の sync status・HUBP の flip 割り込み・HPD の制御の語（ack は頼まれた時だけ書く、HPD の割り込みの極性は今の level の反対）、flip の制御（VUPDATE で latch）と pending。出典は block ごとの Values referenced（dcn_2_0_0_sh_mask.h・dcn20_hubp.c・dcn20_dpp.c・hubp.h・dc_hw_types.h）。
- `display.c`（新）: 段 A6（GOP の picture の検査 → plane・cursor の語の保存 → frame 2 枚（stride × 高さ、64 KiB 整列）と cursor の image 2 slot を CPU の pool の VRAM に → frame counter で refresh を測る（30 frame）→ VMID 0・flip を VUPDATE に → OTG0 の VSTARTUP・HUBP0 の flip・HPD の割り込みを on → 色の帯の frame に flip（HIGH → LOW、HDP flush の後）、`amdrdna.disp.test=<秒>` か 1 秒保持して frame 数・割り込みを数える → GOP の surface に flip で戻り EARLIEST_INUSE を確かめる → underflow を読む）、段 A7（cursor を hidden で設定 → 中央に表示 → 2 frame 後に HUBP・DPP の enable と DPP の UPDATE_PENDING=0 を確かめる → 1 frame 毎に斜めに移動（1 秒か disp.test 秒）→ 隠す）、flip と待ち（FLIP_PENDING=0 かつ EARLIEST_INUSE が要求の address、100 ms で ETIMEDOUT、blank 中は待たない）、refresh の count（frame counter から、割り込みと各 tick の両方で起きる）、IH の DCE client（0x04）の処理（VSTARTUP 0x3C+otg、flip 0x4F+hubp、HPD 0x09 の data[0] の line）、cursor の位置・slot・隠す、plane の blank、停止（cursor off・unblank・console へ flip・保存した語を戻す・割り込み off・VRAM を返す）、control の報告。
- `present.c`（新、nvrtx の present.c と同じ形）: 段 A8（`amdrdna.disp.own=1` の時、A5s は登録せず、A8 が display・scanout の ops と DISPLAY | DISPLAY_EVENTS | DISPLAY_CONTROL を足して 1 回だけ登録）。query（1 output、FIFO・BLOB・REFRESH_COUNTER・POWER_CONTROL・HAS_CURSOR・CONNECTED は HPD の level）、mode（GOP の mode だけ）、claim・release、present（storage は frame へ CPU で copy して flip、blob は stride が GOP と同じで 256 byte 整列なら直接 flip、flip が始まるまで待って返す、format は BGRA8888）、wait、events（HPD で進む topology）、power（HUBP の blank、off の間は refresh の境界を作らない）、refresh、cursor（64x64 の slot に copy、RGBA は並べ替え、2 slot で切り替え）、scanout（query_device・constraints: SHARED|COPY、BGRA、stride と offset 256）。lease の終わり・session の close・表示中の blob の destroy で console の surface に flip で戻る。停止で lease を終わらせ以後 display に触らない。
- 既存の file: `amdrdna.c`（A6〜A8 の段、display の lock の初期化、shutdown で `amdrdna_display_stop`）、`sessions.c`（disp.own=1 の時の登録を A8 に）、`node.c`（close・blob の destroy・stop の hook）、`ih.c`（DCE client を display へ）、`control.c` と `include/uapi/amdrdna.h`（`AMDRDNA_IOC_DISPLAY`: register と count の読み出し、216 byte）、`dcn-registers.c`・`amdrdna-hw.h`（register 19 個: VMID_SETTINGS_0・FLIP_INTERRUPT・EARLIEST_INUSE・CURSOR_SETTINGS・CURSOR0 の 7 個・CNVC_CUR の 2 個・OTG の POSITION・FRAME_COUNT・GLOBAL_SYNC_STATUS・HPD の 2 個）、`stage.c`（boot の parameter の説明）、`amdrdna-node.h`・`amdrdna-private.h`。
- `userland/tests/amdrdnactl/main.c`: `amdrdnactl display`。
- 試験: `plan/ws039/tests/amdrdna-display-test.c`（新）、`amdrdna-host-test.sh` に追加、`amdrdna-dcn-compare.py` に 19 個の Linux の名前、`audit-allow.txt` に一般語 17 個（broken・changed・connected・counter・current・frames・frequency・have・operation・pixel・placement・registration・requested・seconds・steps・taken・topology。operation・placement は zedBSD の UAPI の欄の名前）、`config.mk` に display-scanout・display-control。
- `platform/amd64/vmunix.mk`: amdrdna の行に 3 file。

## host 試験と build（2026-10-16）

- `AMDRDNA_LINUX=plan/ws039/temp/linux plan/ws039/tests/amdrdna-host-test.sh` PASS（PASS の行 375、`amdrdna-display-test` 36 項目、`amdrdna-dcn-compare` 390 registers、`audit-symbols` 48 file）。display-test の初版で DPP の cursor の語の期待値を誤り（PIX_INV の bit を消す前提）、Linux と同じく保つ形に期待値を直した。
- `make -j16 ZEDBSD_CONFIG=plan/ws039/tests/config.mk BUILD=build/p3/usb AMDRDNA_RUN=A1v build/p3/usb/vmunix build/p3/usb/bin/amdrdnactl` rc 0、warning 0、kernel include check PASS、amd64 vmunix check PASS。display-scanout・display-control も build。
- `plan/tools/style-check.py`: 新規・変えた file で短い対称の条件演算子（`? 1U : 0U` の log の値）以外 0。`git diff --check` ok。

## 実機（10.0.10.3、RX 5600 XT、HDMI の monitor を DVI の mode、USB の zedBSD は DHCP 10.0.30.5）

手順: Debian で USB の sda2 の `vmunix` を差し替え（前の物は `~/ws039/vmunix.before-p6`、cfg は `zedbsd.cfg.before-p6`）、cfg に `amdrdna.disp.own=1`（2 回目の起動で `amdrdna.disp.test=10` も）→ `hw-run.sh boot 0001` → `/tmp` に新しい amdrdnactl・display-scanout・display-control → `amdrdnactl advance A8` → 印と `amdrdnactl display`。vmunix の SHA-256 の先頭 9cd9c42021b55e20（07f4aae72）。電源は warm（Debian 経由）。証拠 `build/tmp/amdrdna-hw-p6-{1,2}/`（worktree の中、commit しない）。

| 回 | 結果（register・印） | 目視（ユーザー、Q1 経由） |
| --- | --- | --- |
| 1（15:25、disp.test 無し） | `A5s ok … gpu device waits for the display (A8)`、**`A6 ok flip pending=0 latch=5407us back=1 hold=1000ms frames=60 frame_irq=60 flip_irq=2/2 refresh=60.000Hz 1920x1080 stride=7680 underflow=0/0 hotplug=5`**、**`A7 ok cursor 64x64 at 0x80007f5000 shown=1 pending=0 moves=60 hidden=1`**、**`A8 ok gpu device registered, display 1920x1080@60.000Hz stride=7680 cursor 64x64 renderer=1`**、`/dev/gpu0`。`amdrdnactl display`: shown=plane=console=0x8000000000、frame_irq 529（約 60/s）、HPD の status 0x12（接続） | 「1回目、オーケーでした」（色の帯 → console → cursor が斜めに動いて消える） |
| 2（15:27、display-scanout --hold=10） | info caps=0xf3df（DISPLAY・EVENTS・CONTROL・SHARE）、roles=0x3、mode 1920x1080@60000、constraints stride=256 offset=256、claim lease=1、blob の export/import/present error 0 sequence 1、cursor set/show/moves=100/hide/again 全部 0 | 「画面は赤になりました」「白い四角は画面のやや右下で止まってました」→ 解析: app の色 0xffd05020 は BGRA8888（memory の並び B,G,R,A。i915 の capture.c の定義と同じ）で R=0xd0 の赤みのオレンジで正しい（app の comment の「a blue」が逆）。cursor は app が中央 10 秒 → 3 秒で右下へ 400 px → 10 秒消す → 終点に 10 秒、で終点の register は x=1360 y=940 |
| 3（15:29:30、display-control --hold=5） | refresh 61/1015ms・60/1000ms、claim lease=2、present 0、power off 0（query powered_off=1、off の間の refresh は ETIMEDOUT）、power on 0、refresh 60/1000ms、done 0。終わりの combiner の underflow の bit が 1（0x400、power off の blank の間に立った見込み。2 回目の起動では power を使わず 0） | 「三回目はオーケーでした」（緑 → 黒 → 緑 → console） |
| Debian への warm reboot | shutdown の hook（lease の終わり・console への flip・語の戻し） | 「Debianへの復帰は問題なしでした」 |
| 4（2 回目の起動、disp.test=10、15:34〜） | `A6 ok … latch=9086us back=1 hold=10000ms frames=600 frame_irq=600 flip_irq=2/2 … underflow=0/0`、`A7 ok … moves=600 hidden=1`、`A8 ok …`、display-scanout --hold=3 全部 error 0、combiner 0 | 「テストは成功です」（色の帯の順、cursor の 10 秒の移動、app の赤みのオレンジと cursor の移動、すべて OK） |

## 未確認・残り

- HPD の抜き差し（hotplug_irq 0、topology 1 のまま。monitor を抜く試験はしていない）。
- power off の後に OPTC の underflow の bit が立つ（blank の間の見込み、画面の乱れは目視で無し）。原因の確定は未。
- compositor（p009）の present・cursor、RGBA の blob（BGRA だけを出している）、cold の直後、長い時間。
- DPP の cursor の PIX_INV の bit（0x4）が既定で立っている（Linux も触らない。4 回目の目視で白い四角・黒い縁は正しく見えた）。
- display-scanout の色の comment「a blue」を BGRA の byte の並び（赤みのオレンジ）に直した（Q1 の指示、`userland/tests/display-scanout/main.c`）。
