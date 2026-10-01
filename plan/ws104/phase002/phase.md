<!-- awesome-plan project=zedbsd record=ws104-p002 -->

# ws104-p002: audio の漏れを libkeiland へ（`keiland_audio_available`）

Status: planned
Disposition: normal
Parent: [WS104](../ws.md)
Queue: なし
実行者: phase-runner（high）でよい

## 目的

Settings（`userland/desktop/settings/`）だけが、音の service（zedBSD の audiod）の存在を **socket の file `/run/audiod.sock` を `stat` して** 調べている
（`settings/look.c:226` の `se_look_sound()`）。これは libkeiland の抽象（`keiland_audio_*`）から漏れた OS の知識である。Linux では音の service は
ALSA の device（WS105）で、socket は無い。libkeiland に「音の service があるか」を答える関数を足し、Settings はそれを使う。

## 変える file

1. `userland/desktop/keiland/keiland.h`（p001 の後の場所）:
   - `KEILAND_VERSION` を `20U` から `21U` に上げ、その上の注釈の列の最後に `21: whether a sound service runs` を足す。
   - `keiland_audio_feedback` の宣言の後に足す:
     ```c
     /*
      * Tells whether the sound service runs (KEILAND_VERSION 21): 1 when it
      * does, 0 when it does not.  It does not connect to the service and does
      * not wait; a service that runs may still have no sound device
      * (struct keiland_audio_state's device).
      */
     int keiland_audio_available(void);
     ```
2. `userland/desktop/libkeiland/audio.c`: `keiland_audio_available` を実装する。中身は今の `se_look_sound()` と同じ（`stat(AUDIO_SOCKET_PATH, ...)` が成功し、
   `S_ISSOCK` なら 1）。`AUDIO_SOCKET_PATH` は既に audio.c の 36〜37 行にある（host の試験が上書きできる macro）。`<sys/stat.h>` を足す。
   注釈・書き方は audio.c の他の関数に合わせる（coding-style.md）。
3. `userland/desktop/libkeiland/exports.map`: `keiland_audio_*` が既に global にあればそのまま（無ければ `keiland_audio_available;` を足す）。
4. `userland/desktop/settings/`:
   - `look.c`: `se_look_sound()` とその上の `LOOK_SOUND_SOCKET` の define、要らなくなった include を消す。
   - `settings.h:751` の宣言を消す。
   - `page-home.c:278`・`page-input.c:213` の `se_look_sound()` を `keiland_audio_available()` に置き換える（どちらも `<keiland.h>` を既に include しているか確かめる）。
   - 他に `se_look_sound` を使う所が無いことを `grep -rn se_look_sound userland plan` で確かめる。

## 確かめ

1. build: `make -j16 BUILD=build/amd64 disk-image`、warning 0。
2. libkeiland の host の試験（音）: `sh plan/ws100/tests/host-audio.sh`（PASS）。
3. Settings の回帰（Venus の guest）: `plan/ws089/tests/build-settings-image.sh` で image を作り、`plan/ws089/tests/settings-guest.sh start` の後
   `plan/ws089/tests/settings-regress.sh build/ws104/p002-settings`。全て PASS。使い方は各 script の先頭。
4. 音の頁が「service が居る」と出ることの確かめ: 音量の試験の image（`plan/ws100/tests/build-volume-image.sh`）の guest で Settings の Sound の頁を開き、
   screenshot に `Running` が出ていること（ws100 の試験の手順に従う。audiod が動いていない image では `Not running` が正しい）。
   この確かめが手順として組めなければ、code の同値（関数の中身が同じ）を記録し、「未実施」と書く。
5. boot test: `plan/tools/boot-test.sh build/amd64/hdd-image.img`。

## 完了の条件

- `settings/` に `audiod`・`/run/` の文字列が code として無い（注釈は可）: `grep -n '"/run/' userland/desktop/settings/*.c` が 0 件。
- 確かめ 1・2・3・5 が PASS、4 は PASS か理由つきの未実施。

## 結果

（実行の後に書く）
