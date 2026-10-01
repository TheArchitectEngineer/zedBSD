<!-- awesome-plan project=zedbsd record=ws104-p005 -->

# ws104-p005: compositor の入力の device の層を `wayland/zedbsd/input-zedbsd.c` へ

Status: planned
Disposition: normal
Parent: [WS104](../ws.md)
Queue: なし
実行者: phase-runner（high）

## 目的

決定 D16（ユーザー「evdevは念のため、Linuxはソースコードを分けましょう。（バックエンド分離的に。）」）。zedBSD の入力は Linux の evdev と同じ API
（`include/uapi/input.h` は同じ struct・同じ定数の値・同じ `EVIOC*`）だが、device の見つけ方・開き方・権限（Linux の gdm の下では logind が fd を渡す）が OS で違う。
**device に触る code（列挙・open・ioctl・read・close）** を OS の module に移し、**event の意味を解く code**（frame の組み立て、pointer・key・touch・tablet の解釈、seat への送り）は共通に残す。

key・button・軸の定数（`KEY_*`・`BTN_*`・`EV_*`・`ABS_*`・`REL_*`・`SYN_*`・`MSC_*`）と `struct input_event` などの型は、共通の code が約 60 箇所で使う（input.c 20・tablet.c 16・
touch.c 7・menu-shell.c 11・shell.c 10）。これは小さな header 1 つ（`zwl-evdev.h`）の macro の block で OS の header を選ぶ（ユーザーの「非常に細かい部分ではマクロブロックで分けてよい」）。

## 今の姿（`userland/desktop/wayland/`、2026-10-01 の行番号）

- `zwl.h:44` `#include <uapi/input.h>`。`struct zwl_input_device`（246-268、`int fd`、分類の flag、`struct input_event frame[64]` など）。
  input の関数の宣言（1195-1199）: `zwl_input_scan`・`zwl_input_attach`・`zwl_input_read`・`zwl_input_close`・`zwl_input_cleanup`。
- `input.c`:
  - device に触る: `INPUT_DIRECTORY "/dev/input"`（34）、`zwl_input_scan`（111-161、opendir・readdir）、`event_node_name`（362-383）、`device_open`（386-408）、
    `probe_device`（411-504、**open** と分類）、`read_capabilities`（507-539、`EVIOCGBIT` 4 回）、`read_ranges`（542-568、`EVIOCGABS`）、
    `zwl_input_read`（239-291、`read` の繰り返しと切れた event の検出）、`zwl_input_close`（296-323）・`zwl_input_cleanup`（328-359）の `close`。
  - 意味を解く: `consume_event`（588）、`apply_frame`（651）、`apply_key`（816）、`scale_absolute`・`clamp_position`・`event_time`、`update_capabilities`（961）、
    `zwl_input_attach`（170）・`attach_tablet`（1008）・`attach_touch`（1068）の slot の表、`bit_is_set`（571）・`multitouch`（1123）の bit の検査。
- `tablet.c`: `read_axes`（500-553）が `EVIOCGABS`（5 軸）・`EVIOCGNAME`・`EVIOCGID` を `device->input->fd` に。
- `touch.c`: `read_axes`（659-697）が `EVIOCGABS`（3 軸）。
- `main.c`: 起動時の `zwl_input_scan`（152）、2 秒ごとの再走査（641-642、hotplug の代わり）、poll の set（662-673、741-744）と `zwl_input_read`（807-811）。

## 新しい境界（新しい header `zwl-input.h`、zwl.h から include）

device に触る関数（OS の module が実装する）:

```c
/* The device bits a probe reads (EVIOCGBIT for the event types, keys, relative and absolute axes). */
struct zwl_input_caps { unsigned long event[...]; unsigned long key[...]; unsigned long relative[...]; unsigned long absolute[...]; };
                                     /* the sizes are input.c's struct input_capabilities' (81-86), moved here */

/* Looks for input devices not yet open, opens each, reads its bits and hands it to zwl_input_probe.  Called at start
   and every ZWL_INPUT_SCAN_MS (main.c).  zedBSD: /dev/input/eventN. */
void zwl_input_scan(struct zwl_server *server);

/* Reads an axis's range, the device's name, its identity (EVIOCGABS, EVIOCGNAME, EVIOCGID).  Return 0 or an errno value. */
int zwl_input_device_absinfo(int descriptor, uint32_t axis, struct input_absinfo *info);
int zwl_input_device_name(int descriptor, char *name, size_t size);
int zwl_input_device_id(int descriptor, struct input_id *id);

/* Reads up to capacity whole events without waiting: returns how many, 0 at the end of the device, or -1 with errno
   (EAGAIN when none waits; a torn event is EIO). */
ssize_t zwl_input_device_read(int descriptor, struct input_event *events, size_t capacity);

/* Closes a device's descriptor (on Linux under logind, also gives the device back). */
void zwl_input_device_close(struct zwl_server *server, int descriptor);
```

共通の側の新しい関数（`input.c`）:

```c
/* Classifies an open device from its bits (tablet, touch screen, keyboard, absolute or relative pointer, as
   probe_device does today) and attaches it; closes it (zwl_input_device_close) when it is none of them or there is no
   room.  Takes descriptor. */
void zwl_input_probe(struct zwl_server *server, int descriptor, const char *path, const struct zwl_input_caps *caps);
```

- `probe_device` の分類の部分（tablet・touch・keyboard・pointer の判定と `attach_*` の呼び出し）は `zwl_input_probe` に、open と `read_capabilities` は OS の module に。
- `read_ranges`・tablet と touch の `read_axes` の ioctl は `zwl_input_device_absinfo`・`_name`・`_id` を呼ぶ形に変える（ioctl の行だけを置き換え、残りはそのまま）。
- `zwl_input_read` は `zwl_input_device_read` を呼び、返った event を今と同じく `consume_event` に渡す。error・EOF で device を閉じる判定は今と同じ。
- `zwl_input_close`・`zwl_input_cleanup` の `close(fd)` を `zwl_input_device_close(server, fd)` に。

## 新しい file

| file | 中身 |
| --- | --- |
| `zwl-evdev.h` | evdev の型と定数を読む唯一の header。中身は次の macro の block だけ（と注釈）: `#if defined(__linux__)` → `#include <linux/input.h>`、`#else` → `#include <uapi/input.h>`。注釈に「the evdev types and codes, which are the same on zedBSD and Linux (WS104 p005, D16)」 |
| `zwl-input.h` | 上の「新しい境界」 |
| `zedbsd/input-zedbsd.c` | `INPUT_DIRECTORY`・`zwl_input_scan`・`event_node_name`・`device_open`・open と `read_capabilities`、`zwl_input_device_absinfo`・`_name`・`_id`・`_read`・`_close` |

`zwl.h:44` の `#include <uapi/input.h>` を `#include "userland/desktop/wayland/zwl-evdev.h"` に置き換える（zwl.h が同じ directory の header を `"zwl-gpu.h"` の形で
読んでいるなら、その形に合わせて `"zwl-evdev.h"` でよい。`zedbsd/` の中の file は root からの path で読む）。

## 手順

1. `zwl-evdev.h`・`zwl-input.h` を作り、zwl.h を直す。
2. `zedbsd/input-zedbsd.c` を作り、上の関数を input.c から移す（中身は変えない）。input.c の `probe_device` の分類の部分を `zwl_input_probe` にする。
3. tablet.c・touch.c の `read_axes` の ioctl の行を置き換える。`<sys/ioctl.h>` の include が要らなくなったら消す。
4. `Makefile` の `KEILAND_ZEDBSD_SOURCES` に `zedbsd/input-zedbsd.c` を足す。
5. 共通の source に device に触る呼び出しが残っていないことを確かめる:
   ```
   grep -n 'ioctl(\|EVIOC\|opendir\|"/dev/input' userland/desktop/wayland/*.c
   ```
   0 件（`zedbsd/` の中は対象の外）。`grep -rn 'uapi/input.h' userland/desktop/wayland` が `zwl-evdev.h` の 1 件だけ。

## 確かめ

1. build、warning 0。
2. compositor の基準: `criteria.sh ... C1 C2 C9`（C9 に p053 の touch の注入の試験が入っている）。
3. tablet: `plan/ws035/tests/zdesktop-p059.sh`（tablet の試験。使い方は script の先頭）。
4. 入力の追加と抜き（hotplug）: 試験の中で QEMU の device を `device_add`・`device_del`（QMP）して、pointer が使えるようになる・seat の capability が変わることを、
   既存の試験に同じ物があればそれで、無ければ「未実施」と書く（再走査の code は移しただけで、中身を変えていない）。
5. boot test。

## 完了の条件

- 手順 5 の grep が条件どおり。確かめ 1・2・3・5 が PASS、4 は PASS か理由つきの未実施。

## 結果

（実行の後に書く）
