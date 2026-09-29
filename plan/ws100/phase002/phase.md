<!-- awesome-plan project=zedbsd record=ws100p002 -->

# ws100-p002: audiod の確かめの音と software の音量

Phase ID: `ws100-p002`
Parent: [WS100](../ws.md)
Status: cleared（2026-09-30、サブエージェント、worktree `ws100-volume`（branch `wt/ws100`）。QEMU、実機は未実施）
Phase disposition: normal
Queue: なし（2026-09-30 main の割り当て「ws100-p002（audiod: 確かめの音と software の音量）」）

## 変更（`userland/base/audiod/`）

- `protocol.h`: `AUDIOD_FEEDBACK`（11、header だけ）。version は 1 のまま（古い audiod は `ERROR EINVAL` を返す）。
- `audiod.h`: device に software の音量（`soft`・`soft_left`・`soft_right`・`soft_muted`）と、確かめの音（`feedback`・`feedback_length`・`feedback_next`）。
- `device.c`:
  - `audiod_device_set_volume`: 値を覚え、`/dev/mixer0` の `KERN_AUDIO_SET_VOLUME` が通れば device の音量（`soft = 0`）、通らない（mixer が無い、
    codec に amplifier が無い）なら audiod が掛ける（`soft = 1`）。前は失敗を黙って捨て、音量が効かなかった。
  - `audiod_device_get_volume`: `soft` の間は覚えた値を返す（`VOLUME_CHANGED` の報告が本当の値になる）。
  - `audiod_device_feedback`（新）: 音を頭から鳴らし直す（鳴っている途中でも。重ならない）。device が無ければ何もしない。
  - `audiod_device_open`: 最後に `audiod_feedback_make`。
- `mix.c`:
  - `audiod_feedback_make`（新）: device の rate で 100 ms、880 Hz の正弦（libm を使わない 2 項の漸化式、cos・sin は級数）、5 ms の立ち上がりと
    2 乗の減衰、峰は -12 dBFS（mix の尺度で 2^29）、両 channel 同じ。
  - `audiod_mix_period`: stream を混ぜた後に `feedback_mix`（鳴っている分を足す）と `soft_volume`（`soft` の時だけ、1% あたり 0.6 dB の 60 dB の幅、
    0 は無音、mute は無音）。`soft == 0` で音が鳴っていなければ、どちらも何もしない（今までの bit exact の経路は変わらない）。
- `main.c`: `AUDIOD_FEEDBACK` の request（長さが header でなければ `EINVAL`）。
- 規約: 新しい関数は `plan/coding-style.md` の全文どおり。`style-check.py` で、変更の前後の指摘を比べて**新しい指摘は 0**（audiod の既存の code は
  規約の前の書き方で、既存の指摘はそのまま）。`git diff --check` 0。build（`plan/ws100/tests/build-audiod-image.sh`）: audiod の warning 0。

## 試験（`plan/ws100/tests/`）

- `audiod-feedback.c`: HELLO の後に語（`volume P`・`mute`・`feedback`・`get`・`sleep MS`・`raw TYPE`）を順に送り、答えを 1 行ずつ出す client。
  socket は byte の流れなので、header の length で message を区切る（1 回目は 1 回の recv を 1 通と扱い、DONE と VOLUME_CHANGED が一緒に届いて
  取りこぼした。client の誤りとして直した）。
- `config-amd64-audiod.mk`・`build-audiod-image.sh`: userland の試験の image に HDA・audiod・client。client は build の clang で、build の
  audiod と同じ compile・link の形。worktree の `build/amd64/sysroot` は main の checkout の完成した sysroot の複写（toolchain は build していない）。
- `audiod-qemu.sh`: case ごとに**自分の run の directory の QEMU だけ**を起こして止める（既存の `plan/ws035/tests/boot-hda.sh` は host の全ての
  QEMU を kill し main の checkout に cd するので使わない）、serial で login、client を走らせ、wav を `hda-wav-check.py windows`（0.25 秒ごとの最大）で見る。

## 結果（`plan/ws100/phase002/audiod-qemu.txt`、QEMU の `intel-hda` + `hda-duplex` → wav）→ `audiod-qemu: PASS`

| case | 見たもの | 結果 |
| --- | --- | --- |
| feedback（mixer on） | 1 秒おきの FEEDBACK 3 回 | 3 つの音、どれも峰 8124（-12 dBFS）、答えは DONE |
| restart | 1 回の後、30 ms おきに 5 回 | 5 回は 1 つの音に（鳴らし直し）、峰は 1 回と同じ 8124（重なって足されない） |
| soft（`mixer=off`: codec の amplifier が無い） | 音量 100・50・10・0 で FEEDBACK | 峰 8124・257（-30 dB）・16（-54 dB）・無音。audiod の報告は 0 |
| mute | mute で FEEDBACK、戻して FEEDBACK | mute の間は無音、戻すと 8124 |
| hardware（mixer on: codec の amplifier） | 音量 100・50・10 で FEEDBACK | 峰 8124・4047（-6 dB）・765（-20.5 dB） |
| no-device（HDA 無し） | FEEDBACK 2 回と get | `welcome device=0`、FEEDBACK は DONE、落ちない |
| unknown | type 99 | `ERROR`、error 3（zedBSD の EINVAL は 3） |

音量の曲線: software の音量は 1% あたり 0.6 dB（dB に線形）。pci-hda は百分率を codec の段へ線形に換算し、実機の HDA の amplifier は 1 段が
一定の dB（amp caps の StepSize）なので、実機でも dB に線形になり、software の音量と同じ性質になる。QEMU の codec の model は段を振幅に線形に
当てるので、QEMU の `mixer=on` だけ 50% が -6 dB と浅く見える（QEMU の model の違いで、実機は未確認）。

## 未実施

- 実機（5330）: p006。
- ws035 の audiod の回帰（`plan/ws035/tests/run-audiod-qemu.sh` の bitexact・sum・volume など）: 起動の script が host の全ての QEMU を kill するので走らせていない。
  変更は `soft == 0` かつ音が鳴っていない時には何もしない（bit exact の経路は同じ）ことを code で確かめた。

## Resume point

2026-09-30: cleared。次は p003（libkeiland の `keiland_audio_*` と `keiland_audio_feedback`、KEILAND_VERSION は適用の直前の main の次の番号）。
