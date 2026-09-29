<!-- awesome-plan project=zedbsd record=ws100p003 -->

# ws100-p003: libkeiland の音量（keiland_audio_*）

Phase ID: `ws100-p003`
Parent: [WS100](../ws.md)
Status: cleared（2026-09-30、サブエージェント、worktree `ws100-volume`（branch `wt/ws100`）。host の試験と target の build、guest は p004 で）
Phase disposition: normal
Queue: なし（2026-09-30 main の割り当て「p003（libkeiland）。KEILAND_VERSION は適用の直前の main の最新の次（今は 14）」）

## 変更

- `include/libc/keiland.h`: `KEILAND_VERSION` 14 → **15**（main の 67757652 を取り込んだ直後、最新は 14）。節「The sound output's volume」:
  `struct keiland_audio`、`struct keiland_audio_state`（reachable・device・rate・channels・left・right・muted）、`KEILAND_AUDIO_CHANGED_REACHABLE`・
  `_VOLUME`、`keiland_audio_open`・`_close`・`_fd`・`_update`・`_get_state`・`_set_volume`・`_feedback`。WS089 の案
  （`plan/ws089/proposed/libkeiland-audio.md`）の API に `keiland_audio_feedback` を足したもの。
- `userland/desktop/libkeiland/audio.c`（新）: 1 つの接続で HELLO と SUBSCRIBE、`VOLUME_CHANGED` と `WELCOME` を読む。socket は non-blocking、
  byte を貯めて header の length で区切る。切れたら 1 秒後の update でつなぎ直す（network.c と同じ）。送れない request は接続を落とす
  （`MSG_NOSIGNAL`）。DONE・ERROR は読み捨てる（古い audiod の FEEDBACK の ERROR も）。socket の path は `AUDIO_SOCKET_PATH`（既定は
  `AUDIOD_SOCKET_PATH`、host の試験の build だけが別の path を渡す。環境変数ではない）。
- `exports.map`: `keiland_audio_*`。`Makefile`: `audio.c`。
- 規約: `style-check.py audio.c` 0、`git diff --check` 0。

## 試験

- host: `plan/ws100/tests/host-audio.sh`（host の cc で audio.c と `host-audio.c`、偽の audiod を子の process で）→ **14/14 passed**:
  audiod 無しで open・fd -1・reachable 0・set は ENOTCONN・範囲の外は EINVAL。audiod が来ると 1 秒後の update でつながり、WELCOME（device・rate）と
  今の音量（70）。set が報告で戻る（35、muted）。FEEDBACK に ERROR EINVAL を返す（古い）audiod でも接続は残る。audiod が消えると reachable 0、
  device の無い audiod で戻ると reachable 1・device 0。
- target: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws100-amd64 build/ws100-amd64/dynamic/libkeiland.so` → exit 0、warning 0、
  `check-dynamic-elf.py`（shared library の検査）通過。
- guest（本物の audiod と）: p004 の zdesktop の試験で。

## Resume point

2026-09-30: cleared。次は p004（zdesktop の volume.c、A1〜A6）。
