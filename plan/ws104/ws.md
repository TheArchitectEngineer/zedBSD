<!-- awesome-plan project=zedbsd record=ws104 -->

# WS104: Keiland の OS の境界の整理（zedBSD の上で、振る舞いを変えずに）

<!-- awesome-plan-current:start -->
Status: planned
Primary Milestone: MG006
Related Milestones: MG007
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: 計画済み（2026-10-01、survey と design-reviewer の後の改訂 2）。最初は p001（header の移動。toolchain の範囲の sysroot.mk を含むので main が実行する。2026-10-01 にユーザーが差分を許可。Queue は未設定）
<!-- awesome-plan-current:end -->

## 目標

Keiland（compositor・libkeiland・desktop の app）の **OS に依存する code を、OS ごとの source の module に閉じる**。zedBSD の上で行い、
**zedBSD の振る舞いは何も変えない**（refactor）。これが終わると、[WS105](../ws105/ws.md)（Linux への移植）は Linux の module を足すだけで済む。

この WS は Linux の code を 1 行も書かない。Linux の build（`Makefile.linux`）も作らない。

## 背景（なぜこの WS があるか）

2026-10-01 にユーザーが「Linux移植を進めます」と言い、移植の手順を検討した。全体の決定と理由は [WS105 の ws.md](../ws105/ws.md) の節「決定と理由」にある。
**この WS を実行する agent は、先にその表（D1〜D23）を読むこと。** この WS に直接関わる決定:

- D3: desktop の公開の header（`keiland.h` など）を libc（`include/libc/`）から `userland/desktop/keiland/` へ移す。`vulkan/` は libc に残す（OS の API）。
- D14: libkeiland の OS の部分を OS ごとの C の source に分ける（macro の block は非常に細かい所だけ）。
- D16: compositor の evdev の device の扱いを OS の source に分ける。key の code の定数だけは小さな header の macro で切り替える。
- D17: compositor の `zwl_buffer_layout` を zedBSD の module の中だけにし、OS の境界を「client の buffer を VkImage と memory にする」に上げる。
- D18: zedBSD の上での整理（この WS）と Linux への移植（WS105）を分ける。

調べた事実（2026-10-01、Q1 の subagent の調査）:

- desktop の header は `include/libc/` にあり、build の時に `toolchain/llvm/sysroot.mk` が sysroot（`build/amd64/sysroot/usr/include/`）に写す。
  desktop の source は `-isystem <sysroot>/usr/include` でそれを読む（`-Iinclude/libc` は無い）。
- libkeiland の OS の部分は `network.c`（networkd の protocol）・`network-link.c`（socket の ioctl、zedBSD の鍵の file）・`audio.c`（audiod）の 3 file で、
  app は全て `keiland.h` の関数だけを使っている。抽象から漏れているのは `settings/look.c` の `se_look_sound()`（`/run/audiod.sock` を `stat`）の 1 箇所。
- compositor（`userland/desktop/wayland/`）の OS の部分:
  - GPU の client の buffer: `gpu-zedbsd.c`（WS103 で作った wire の decode）、`protocol.c` の `factory_request`・`factory_fence`・`factory_alpha`、`import.c` の image の作成。
  - 入力: `input.c`（`/dev/input` の列挙・open・`EVIOCGBIT`・`read`）、`tablet.c`・`touch.c` の `read_axes`（`EVIOCGABS` など）。zedBSD の入力は Linux の evdev と同じ API。
  - session: `handoff.c`（sessiond との fd 3 の会話）。sessiond が居ないとき、compositor はすぐ画面を取り、Log Out で終わる。
  - install の path の直書き（`/bin/terminal`・`/usr/share/fonts/keiland.ttf`・`/etc/keiland/apps.conf` など、desktop 全体で約 70 箇所）。

## 達成基準

| # | 基準 | 確かめ |
| --- | --- | --- |
| A1 | desktop の公開の header が `userland/desktop/keiland/` にあり、`include/libc/` に無い。zedBSD の sysroot の `usr/include` の中身（file の名前と内容）が移動の前と同じ | p001 の sysroot の hash の比較 |
| A2 | `settings` が audiod の socket を直接見ない（`keiland_audio_available()` を使う） | p002 |
| A3 | libkeiland・compositor の OS に固有の source が `<package>/zedbsd/` にあり、共通の source は `<uapi/...>`・`"userland/base/..."` を include しない（evdev の定数の header 1 つを除く） | p008 の `plan/tools/keiland-os-boundary/check.sh` |
| A4 | `zwl_buffer_layout` が `wayland/zedbsd/` の中だけにある。compositor の共通の code と OS の module の間の関数は `zwl-gpu.h`・`zwl-input.h`・`zwl-os.h` に宣言してある | check.sh、p004〜p006 |
| A5 | desktop の install の path が `userland/desktop/paths.h` の macro で書かれ、zedBSD の build では今と同じ文字列になる | p007 の binary の文字列の比較 |
| A6 | zedBSD の振る舞いが変わらない: build の warning 0、boot test、WS099 の C1・C2・C9、WS103 の GPU の境界の試験、glass（p059）と pen（notes-pen）、Settings・音量の試験 | 各 Phase と p008（[commands.md](commands.md)） |

## 守ること（全 Phase）

- **振る舞いを変えない。** log の行（`ZWL IMPORT ...` などの試験が読む物）の形も変えない。
- 関数を移すときは、中身を書き換えずに移す（`git mv` と切り貼り）。移した先で名前を変える必要があるときだけ変える。
- OS の module の file は `<package>/zedbsd/<役割>-zedbsd.c` の名前にする（例 `wayland/zedbsd/input-zedbsd.c`）。basename を package の中で一意にする
  （object の file の名前が衝突しないように）。WS105 の Linux の物は `<package>/linux/<役割>-linux.c`、Linux と FreeBSD で共有する仕組みの物は
  `<package>/<仕組み>/<役割>-<仕組み>.c`（例 `libkeiland/wpa/network-wpa.c`）。
- directory を跨ぐ include は、tree の慣習どおり repo の root からの path で書く（`#include "userland/desktop/wayland/zwl.h"`。build に `-I.` がある）。
- 新しい code・移した code には [coding-style.md](../coding-style.md) の全文を適用する。
- 試験は amd64 の QEMU。実機（5330）の試験はこの WS では行わない（GPU の扱いの意味を変えないため）。
- 他の WS の試験の script（`plan/wsNNN/tests/`）と `plan/tools/` の file の変更は **main が当てる**（AGENTS.md の subagent の修正可能範囲）。用意した patch は
  `git apply --include='plan/*'`（main）と `--exclude='plan/*'`（subagent）で分けて当てられる。他の WS の記録（`plan/**/*.md`）は変えない。

## 用意してある資料（2026-10-01 の survey）

この WS の Phase は、**実行する agent が調べ物をしなくて済むように**、正確な編集と command を用意してある。

| 資料 | 中身 |
| --- | --- |
| [commands.md](commands.md) | zedBSD の build と回帰の正確な command（build と warning の数え方、sysroot、boot test、compositor の基準、GPU の境界の試験、Settings・音）と、守ること（image の build を同時に走らせない、BUILD・OUTPUT の引数を必ず渡す） |
| [patches/](patches/) | p001（sysroot.mk と path の参照 44 file）・p002・p003・p007 の patch。survey が copy の tree で順に当て、build・host の試験・文字列の同一を確かめた |
| [edits-compositor.md](edits-compositor.md) | p004〜p006 の compositor の編集の正確な手順（行番号・移す関数・新しい code）と、survey で見つけた phase.md の誤りへの Q1 の決定 |

## Phase

| Phase | 目的 | 状態 | 依存 |
| --- | --- | --- | --- |
| [ws104-p001](phase001/phase.md) | desktop の公開の header を `userland/desktop/keiland/` へ移す（sysroot の manifest に 1 directory を足す） | planned | なし |
| [ws104-p002](phase002/phase.md) | audio の漏れ（`settings/look.c`）を libkeiland へ（`keiland_audio_available`、KEILAND_VERSION 21） | planned | p001 |
| [ws104-p003](phase003/phase.md) | libkeiland の OS の 3 file を `libkeiland/zedbsd/` へ | planned | p002 |
| [ws104-p004](phase004/phase.md) | compositor の GPU の buffer の境界を引き上げる（`zwl_buffer_layout` を zedBSD の module の中へ） | planned | p001 |
| [ws104-p005](phase005/phase.md) | compositor の入力の device の層を `wayland/zedbsd/input-zedbsd.c` へ | planned | p004 |
| [ws104-p006](phase006/phase.md) | compositor の session（`handoff.c`）と OS の hook（`zwl-os.h`）を zedBSD の module に | planned | p005 |
| [ws104-p007](phase007/phase.md) | install の path を `userland/desktop/paths.h` の macro に | planned | p006、p003 |
| [ws104-p008](phase008/phase.md) | 規約の全文の見直し、境界の確かめの script、回帰 | planned | p001〜p007 |

依存の図: p001 → p002 → p003 ─┐、p001 → p004 → p005 → p006 ─┴→ p007 → p008。p002〜p003 の列と p004〜p006 の列は別の file を触るので、別の agent が同時に進めてよい。
**ただし image の build（`disk-image` と試験の image）は同時に 1 つだけ**（[commands.md](commands.md) §0）。並べるのは編集と host の試験までで、guest の回帰は順に流す。

## 実行の体制

- **p001 は main（Q1）が行う**（`toolchain/llvm/sysroot.mk` は AGENTS.md の toolchain の範囲。subagent は変えない）。
  2026-10-01 にユーザーが [p001-sysroot.patch](patches/p001-sysroot.patch) の適用を許可した。実行には Queue の選定を要する。
- p002〜p008 は phase-runner（high）に任せてよい。compositor の Phase（p004〜p006）は i915・Keiland の desktop の扱いなので phase-runner（high）を使う。
