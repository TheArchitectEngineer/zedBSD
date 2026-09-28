<!-- awesome-plan project=zedbsd record=ws079-p003 -->

# WS079 Phase 003: compositor の `zwp_tablet_manager_v2` と pointer の fallback

<!-- awesome-plan-current:start -->
Status: in-progress（1 回目の区切り: p002 の試験の経路を guest で確かめた。compositor の実装は次）
Disposition: normal
Parent: [WS079](../ws.md)
Design: [design-input-notes.md](../design-input-notes.md) §3
Resume point: 下の「残り」
<!-- awesome-plan-current:end -->

## 範囲

[設計](../design-input-notes.md) §3: tablet の分類、`zwp_tablet_manager_v2` v1（seat・tablet・tool、pad なし）、focus と暗黙の grab、
tablet を bind しない client への pointer の fallback、compositor の自分の UI はペンを pointer として扱う、libwayland の client の表。
加えて（main の依頼）p002 の試験の経路（注入の device を guest で確かめる）を先に仕上げる。gesture（p010）は範囲外。

## 2026-09-28 の作業（subagent、1 回目: p002 の試験の経路）

### 変更

| 場所 | 内容 |
| --- | --- |
| `userland/base/tests/peninject/`（`plan/ws079/tests/peninject/` から移した） | 他の試験の program（`userland/base/tests/*`）と同じく `ZEDBSD_USERLAND_PACKAGE` で登録（`peninject`、platform `*`、既定 n、分類 base、`/bin/peninject`、台本は `/usr/share/peninject/stroke.pen`）。userland の通常の link の規則（`AMD64_USER_BASIC_COMMAND`）で link される。program を 3 つの mode にした: 台本の再生（従来）、`-c`（拒否の確認）、`-d MS`（名前「Test pen (input-inject)」の evdev の node を探し、名前・5 軸の absinfo・event を印字）。台本に `hover`・`lift` を足し、`stroke.pen` は `size` の後に 1.5 秒待つ（読み手が node を開く間） |
| `plan/ws079/tests/config-amd64-pen.mk`・`build-pen-image.sh`・`pen-guest.sh`（新） | 試験の image: lean な Venus の image（`plan/tools/titlebar/config-amd64-menu.mk`）に `CONFIG_INPUT_TEST_INJECT := y` と `peninject`。guest は runtime `build/ws079-run` |

### 確認（実行したもの、QEMU の guest。実機は無い）

| 確認 | 結果 |
| --- | --- |
| `plan/ws079/tests/build-pen-image.sh`（worktree の `build/amd64`。sysroot は共有の `build/amd64/sysroot`（09-28 12:53）の写し。`make toolchain` は worktree で LLVM の source の展開を始めるため使わない） | exit 0。`build/amd64/bin/peninject`（warning 0、`-Werror`） |
| guest（Venus、`pen-guest.sh start`）: `ls -l /dev/input-inject` | `crw------- root wheel`（0600） |
| `peninject -d 9000 & peninject /usr/share/peninject/stroke.pen`（root） | 再生 exit 0。node は `/dev/input/event4`、名前「Test pen (input-inject)」。absinfo: X 0..21600、Y 0..13500、PRESSURE 0..4095（res 0）、TILT_X/Y −60..60（res 57）。ramp の値の列 0,63,127,…,4031,4095（64 段、刻み 63〜64）、`BTN_TOOL_PEN`=1 → 軸 → `BTN_TOUCH`=1、`BTN_STYLUS` 1/0、up で touch 0 → tool 0、`BTN_TOOL_RUBBER`=1 で touch 1/0 → tool 0。計 182 event、injector を閉じると読み手の read が 0（EOF）で終わり、node が消える。出力 `build/ws079-p003/pendump.txt` |
| `peninject -c`（root） | 15 件すべて ok（`build/ws079-p003/pencheck.txt`）: 非 root の open は node の mode（0600）で **EACCES**。node を 0666 にすると driver が **EPERM**（zedBSD の errno 47）。2 つ目の open が **EBUSY**。setup の面積 0・magic 違い、pressure 4096、tilt 61、X が面積の外、EV_REL、BTN_LEFT、button の値 2、端数の write、65 event の write が **EINVAL**。拒否の後も正しい event（pressure 4095）は通る |

設計との差: 依頼の「非 root は EPERM」は、実際は devfs の mode（0600）が先に EACCES で断り、driver の EPERM はその後ろの二重の守り。
両方を確かめた（`-c` は chmod 0666 にして driver の EPERM を見てから 0600 に戻す）。

未実施（p002 の残りのうち）: QEMU の `usb-wacom-tablet` を付けた起動、INPUT_PROP の判断、arm64・pcat の build。

## 残り（resume の条件）

1. compositor: tablet の分類、`zwp_tablet_manager_v2` v1、focus・暗黙の grab、fallback、自分の UI では pointer（`tablet.c`）。
2. libwayland: `tablet-protocol.c`、`include/libc/wayland/tablet-unstable-v2-client-protocol.h`（header は書いた）。
3. 試験の client と guest での確認、画面（`build/ws035-shots/ws079-p003-20260928-*.png`）。
