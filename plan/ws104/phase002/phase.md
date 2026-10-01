<!-- awesome-plan project=zedbsd record=ws104-p002 -->

# ws104-p002: audio の漏れを libkeiland へ（`keiland_audio_available`）

Status: cleared
Disposition: normal
Parent: [WS104](../ws.md)
Queue: q516 / q516-i01
依存: p001
実行者: phase-runner（high）でよい

## 目的

Settings（`userland/desktop/settings/`）だけが、音の service（zedBSD の audiod）の存在を **socket の file `/run/audiod.sock` を `stat` して** 調べている
（`settings/look.c` の `se_look_sound()`）。これは libkeiland の抽象（`keiland_audio_*`）から漏れた OS の知識である。Linux では音の service は
ALSA の device（WS105）で、socket は無い。libkeiland に「音の service があるか」を答える関数を足し、Settings はそれを使う。

## 用意してある物

[`../patches/p002.patch`](../patches/p002.patch)（p001 の後の tree に当たる。2026-10-01 の survey が copy の tree で当て、target の compile（`-Werror`）・style-check 0・
host で「socket が無いと 0、あると 1、version 21」・ws089 `host-build`・ws100 `host-audio` を確かめた）。中身:

- `userland/desktop/keiland/keiland.h`: `KEILAND_VERSION` を 20 → 21、版の注釈の列の最後に `21: whether a sound service runs`、`keiland_audio_feedback` の宣言の後に
  `int keiland_audio_available(void);` とその注釈。
- `userland/desktop/libkeiland/audio.c`: `#include <sys/stat.h>`、`keiland_audio_feedback` の後に `keiland_audio_available()`（中身は今の `se_look_sound()` と同じ:
  `stat(AUDIO_SOCKET_PATH, ...)` が成功し `(st_mode & S_IFMT) == S_IFSOCK` なら 1）。`exports.map` は既に `keiland_audio_*` を持つので変えない。
- `userland/desktop/settings/look.c`: `LOOK_SOUND_SOCKET` の define と `se_look_sound()` を消す（`<sys/stat.h>` は `fstat` が使うので残す）。
- `settings.h`: `se_look_sound` の宣言を消す。`page-home.c`・`page-input.c`: `se_look_sound()` → `keiland_audio_available()`（両方とも `settings.h` 経由で `<keiland.h>` を読む）。

## 手順（`<W>` は `ws104-p002`）

1. `git apply plan/ws104/patches/p002.patch`
2. `grep -rn se_look_sound userland plan --include=*.c --include=*.h --include=*.sh` が 0 件。`grep -n '"/run/' userland/desktop/settings/*.c` が 0 件。
3. build と warning の数え（[commands.md](../commands.md) §1）。
4. host の試験: `sh plan/ws100/tests/host-audio.sh`（`host-audio: N/N passed`）、`sh plan/ws089/tests/host-build.sh`（exit 0）。
5. Settings の回帰（commands.md §8 の Settings の 5 行。約 10 分）: `settings-regress: PASS`。
6. 音の頁が「service が居る」と出ることの確かめ（任意。手順が組めなければ「未実施」と書き、code の同値を理由にする）: 音量の試験の image
   （`sh plan/ws100/tests/build-volume-image.sh build/ws104-p002-volume`）で `plan/ws100/tests/volume-p005.sh build/ws104-p002-volume/hdd-image.img build/ws104-p002/volume-p005`
   （Settings の Sound の頁を開く試験。`volume-p005: PASS`）。
7. boot test（commands.md §4、`OUTPUT=build/ws104-p002/boot`）。
8. commit: `git commit -m WIP -- userland/desktop/keiland/keiland.h userland/desktop/libkeiland/audio.c userland/desktop/settings`

## 完了の条件

- 手順 2 の grep が 0、3 の build が通り warning 0、4・5・7 が PASS、6 は PASS か理由つきの未実施。

## 結果

q516-i01 cleared（2026-10-01T02:25:22.416961+00:00）。

公開版 21 の `keiland_audio_available()` を追加し、Settings の audiod socket 直接参照を除去。旧関数・Settings の `/run/` literal は 0。amd64 disk-image exit 0、自前 warning 0（raw filter の 1 行は OpenSSH の並列 stderr が分断された EC_KEY の非推奨 warning と前後から確認）。host audio 14/14、Settings host build、Settings guest 8 本の回帰、boot 全て PASS。新 API の socket 不在・通常 file・Unix socket を 0/0/1、版 21 と host で確認。style-check 0。clang-format 19.1.7 を新関数の範囲に使用し、全文規約が指定する定義の引数改行は手動で復元。任意の audiod 有り Sound 頁は未実施（socket 判定は旧関数と同値、positive host probe 済み。WS 全体の p008 で volume-p005 を実行）。証拠: `plan/history/ws104/q516/`。実機・Linux は未実施。


Implementation: `7e3ac1bc26ede65bd94e364eee2d23c84a6668d0`（WIP）。詳細 log: `build/ws104-p002/`（一時物）。実機・Linux は未実施。GitHub へは未公開。

Execution started UTC: 2026-10-01T02:14:13.207998+00:00。Approval: current user, 2026-10-01「お、いい調子ですね！その調子で、ws104の完了まで自律的に作業を進めてください。」。既存 WS104 p002〜p008 全範囲、依存順の 1 Phase Queue と検証・記録・WIP commit を承認。push / GitHub 公開は承認対象外。
