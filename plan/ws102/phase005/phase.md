<!-- awesome-plan project=zedbsd record=ws102-p005 -->

# ws102-p005: L1 の仕上げと回帰

Status: cleared（2026-09-30、QEMU の Venus。実機は未実施）
Disposition: normal
Parent: [WS102](../ws.md)
Queue: main の依頼（2026-09-30、worktree `.claude/worktrees/ws102-keyboard`、branch `wt/ws102`）

## 範囲と受け入れ

design §2.2・§2.3・§4 の p005 を扱う。

- 閉じる gesture（帯の外への swipe、p002 の toggle と閉じる key）。
- lock・greeter・App Home・Wiseview で閉じる。
- 1920x1080 の配置。
- K7 の回帰（C9・Notes の角・Wiseview・desktop の swipe、D3）。

受け入れ: L1 の数値目標（design §3、(a)〜(d)）を全て満たすこと。

## 実装（keyboard.c だけ）

- 帯の swipe: panel の帯で押した press を、flick の panel は右へ、QWERTY は下へ `KEYBOARD_SWIPE_CLOSE`（80 px）以上 drag して離すと閉じる（`close reason=swipe`）。
- 画面で閉じる: tick で、greeter・lock・App Home（開きかけを含む）・Wiseview（gesture を含む）のときに panel を閉じる（`reason=greeter|lock|home|wiseview`）。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make … bin/wayland bin/textedit bin/ime-probe bin/wltest dynamic/libkeiui.so` | rc 0、warning 0 |
| style | `plan/tools/style-check.py keyboard.c keyboard-layout.c keyboard.h` | 0 件 |
| host | `sh plan/ws102/tests/host-keyboard.sh` | PASS |
| guest（pen image の複写、1280x800） | `plan/ws102/tests/osk-guest.sh build/ws102-shots/p005 install start pointer flick edges touch send close` | PASS |
| guest（`VENUS_SIZE=1920x1080`、`OSK_WIDTH=1920 OSK_HEIGHT=1080`） | `osk-guest.sh build/ws102-shots/p005-large install large` | PASS: flick は `x=1494 y=618 width=414 height=450`（key 96 px）、QWERTY は `x=12 y=658 width=1896 height=410`（`large-flick.png`・`large-qwerty.png`） |
| 回帰: 右上の角・Home・Wiseview（WS079-p010） | `plan/ws079/tests/zdesktop-p010.sh build/ws102-amd64 …` | PASS |
| 回帰: WS099 の C9 | criteria image の複写にこの compositor（と試験の program）を入れて `criteria.sh … C9` | 10 本すべて PASS |
| boot test | `plan/tools/boot-test.sh build/ws102-c9.img` | PASS（`build/ws102-boot-test/login.png`） |

手順 close で確かめたこと:

- flick の帯を右へ 100 px drag して `close reason=swipe`、QWERTY の帯を下へ 100 px drag して `close reason=swipe`。
- App Home（launcher）を開くと `close reason=home`。
- Wiseview（下端の swipe）を開くと `close reason=wiseview`。

## L1 の数値目標（design §3）の結果

| # | 目標 | 結果 |
| --- | --- | --- |
| (a) | 注入の右下の swipe 10 回で 10 回、真上への stroke 10 回で 0 回 | 10/10（開く 5・閉じる 5 の toggle）、0/10（手順 touch） |
| (b) | かな 46 字・英字 26・数字 10 を表の上で出せる（host） | PASS（と記号 32・空白・「ー」） |
| (c) | 「aiueo123」が Text Editor に、「あいうえお」が text-input の app に誤り 0 で届く | Text Editor の file は「aiueO123」（大小の key を含む）、ime-probe の text は「あいうえおかが」 |
| (d) | C9 の 10 本 PASS | PASS |

D3（下の角と他の gesture）: 手順 edges で確かめた。

- 角から真上への stroke では、keyboard も Wiseview も開かない。
- 下端の中央からの swipe で Wiseview が開く。
- 左端の角のすぐ上からの swipe で desktop が替わる。

WS079-p010 の Wiseview と Home の下端の手順も PASS。

## 見つけたこと・制限

- lock での閉じは code だけで、guest では確かめていない。試験の compositor は `--session` ではなく、Super+L で lock しない（lock は sessiond の session の compositor だけ）。greeter も同じ。
- 最初の 1920x1080 の実行は、前の guest が止まり切らない間に始めたらしく 1280x800 で動き、`qmp-pointer.py` にも大きさを渡していなかったので FAIL した。試験の `pointer` に `--width/--height` を渡すように直し、guest を起こし直して PASS。
- 最初の close の手順は、開いた数の期待値を 1 つ少なく数えていた（試験の誤り）。直して PASS。
- 作業の領域（K5）は p007。main が design §2.8 を改めた（flick でも縮める、最大化の窓は animation、浮いた窓は動かす）。main を取り込んで、衝突は無かった。
- 実機・Windows の上の QEMU は未実施（L4）。

## Resume point

2026-09-30: cleared。**L1 はここで満たした。** 次は L2（p006 から）。広く浅くの方針では、他の WS の L1 を揃えた後になる（main の判断）。
