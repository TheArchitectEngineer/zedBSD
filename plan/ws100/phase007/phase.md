<!-- awesome-plan project=zedbsd record=ws100p007 -->

# ws100-p007: 規約（coding-style の全文）への合わせと全体の確かめ

Phase ID: `ws100-p007`
Parent: [WS100](../ws.md)
Status: cleared（2026-09-30、サブエージェント、worktree `ws100-volume`（branch `wt/ws100`）。QEMU、実機は未実施）
Phase disposition: normal
Queue: なし（2026-09-30 main の割り当て「p007（規約と全体の確かめ）。audiod の既存の code の規約違反は、WS100 で変えた関数とその周りだけ直す。
全体の書き直しはしない。残りは phase.md に一覧で記録する」）

## 範囲（WS100 が変えた source）

audiod（`protocol.h`・`audiod.h`・`device.c`・`mix.c`・`main.c`）、libkeiland（`audio.c`、`keiland.h` の節、`exports.map`・`Makefile`）、zdesktop
（`volume.c`、`icons.c`・`icons.h`、`shell.c`・`seat.c`・`preferences.c`・`zwl.h`・`glass.h`・`Makefile`）、試験の client（`plan/ws100/tests/audiod-feedback.c`・
`host-audio.c`）。規約は `plan/coding-style.md` の全文。道具は `plan/tools/style-check.py`（自動で見られる規則）と、読んでの確かめ（関数の頭の comment、
前方宣言、宣言の位置、return の comment、名前）。

## 直したもの

| file | 関数 | 直したこと |
| --- | --- | --- |
| `audiod/device.c` | `audiod_device_open`（WS100 が末尾を変えた） | 条件の中の ioctl・allocate_buffers を変数に受けてから判定（呼ぶ順と短絡の順は同じ）、段落の comment、`format` を 0 で初期化（GET_FORMAT が失敗したときの未初期化の読みを無くす: 前も後も「使わない」になる）、最後の成功の return の comment |
| `audiod/device.c` | `audiod_device_get_volume` | 条件の中の ioctl、段落の comment |
| `audiod/mix.c` | `audiod_mix_period`（WS100 が feedback と software の音量を足した） | 条件演算子を if に、入れ子の宣言（`sample`）を関数の頭へ（`sample16`・`sample32`）、複数行の本体に括弧、段落の comment と空行 |
| `audiod/main.c` | `handle_message` の `AUDIOD_DEVICE_VOLUME`・`AUDIOD_FEEDBACK` の case、`send_volume`・`broadcast_volume`（音量を配る周り） | 段落の comment と空行 |
| `plan/ws100/tests/audiod-feedback.c` | 全体（新しい code） | 語の判定を `test_word_of` に分け、条件の中の strcmp を無くす。段落の comment と空行 |
| `plan/ws100/tests/host-audio.c` | 全体（新しい code） | 条件演算子を if に、条件の中の bind・listen・recv を変数に。段落の comment と空行 |

結果: WS100 の新しい file（`audio.c`・`volume.c`・試験の client 2 つ）と、zdesktop の変えた file（`shell.c`・`seat.c`・`icons.c`・`preferences.c`）は `style-check.py` 0。
`git diff --check` 0。

## 例外（1 件）

- `audiod/mix.c` の `audiod_mix_period` の `if (sigsetjmp(audiod_bus_jump, 1) != 0)`: C の規格（7.13.1.1、sigsetjmp は setjmp と同じ）は、呼びを
  条件式の中で定数と比べる形か、式文そのものの形でしか許さない。代入（`jumped = sigsetjmp(...)`）は未定義の動作になるので、規約の「条件の中で
  関数を呼ばない」の例外として残し、その場に理由の comment を書いた。

## 残した既存の指摘（WS100 が変えていない関数、書き直さない）

`style-check.py` の指摘の数（関数ごと）。どれも WS035 p009 の audiod の書き方（規約の前）のまま。

| file | 関数（指摘の数） | 計 |
| --- | --- | --- |
| `audiod/device.c` | audiod_device_service 3、audiod_device_timeout_ms 1、fill_mapped 1、fill_written 2、finish_drains 1、read_capture 1 | 9 |
| `audiod/mix.c` | audiod_capture_period 10、audiod_played_store 1、audiod_stream_rates 2、device_to_stereo 4、fetch 1、store 1（と上の例外の 1） | 19 + 1 |
| `audiod/main.c` | accept_client 3、audiod_now_ns 1、audiod_send_event 2、create_stream 7、destroy_stream 2、find_stream 2、handle_message（音量の case の外）17、listen_socket 4、main 13、read_client 4、reap_clients 1、send_message 3、send_result 2 | 61 |

指摘の種類は、段落の comment と空行（paragraph-comment・blank-after-brace）が多く、他に条件の中の呼び（call-in-condition）と条件演算子（conditional）、
入れ子の宣言（device_to_stereo）。挙動の誤りは見つけていない（読んだ範囲）。

## 全体の確かめ（直した後の source）

- build: `build-audiod-image.sh`（audiod の試験の image）と `build-volume-image.sh`（Venus の volume の image）、どちらも exit 0、audiod・desktop の warning 0。
- `plan/ws100/tests/audiod-qemu.sh build/ws100-p007-audiod` → **PASS**（feedback・restart・soft・mute・hardware・no-device・unknown、p002 と同じ数値）。
- `plan/ws100/tests/volume-p004.sh build/ws100-volume.img build/ws100-shots/p007` → **PASS**（A1〜A6）。
- `plan/ws100/tests/host-audio.sh` → 14/14。
- WS099 の C7・C8・C9 は p004 で PASS（p007 は audiod の書き方と試験の client だけを変え、zdesktop は変えていない）。
- 未実施: 実機（5330、p006）。p005（Settings）はユーザーの判断。

## WS の受け入れ（A1〜A7）の状態

A1〜A6: QEMU で満たす（p004、p007 の確かめ）。A7（5330 の speaker と headphone）: p006（ユーザーが起きてから、実機の image で。デモに必須ではない）。
WS100 を completed にするのは A7 の扱い（p006 の結果か Future Work への移し）が決まってから。

## Resume point

2026-09-30: cleared。次は p006（実機、main がまとめて頼む）。p005 はユーザーの判断。
