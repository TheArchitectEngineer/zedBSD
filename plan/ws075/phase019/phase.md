<!-- awesome-plan project=zedbsd record=ws075p019 -->

# ws075-p019: 性能 3: present mode と vsync（ws031-p027）

Phase ID: `ws075-p019`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-29、サブエージェント、worktree `.claude/worktrees/ws075-rps`。FIFO の先行（UAPI なし）を実装し実機の passthrough で確認。MAILBOX・IMMEDIATE はユーザーの判断「デモの後に回す」で範囲の外、[F-057](../../future-work.md) へ）
Resume point: なし（MAILBOX・IMMEDIATE は F-057、提案は [proposed/p019-present-mode.md](../proposed/p019-present-mode.md)）
Phase disposition: normal

## 範囲（正本は WS031 の ws.md の p027 の行と [phase018](../../ws031/phase018/phase.md) の 5）

present mode（FIFO・MAILBOX・IMMEDIATE）で vsync を選ぶ。今は build の option（`I915_PRESENT_NO_VSYNC`）だけで、既定は FIFO:
presentation は flip の latch まで presenting thread（compositor）を待たせる（`display/present.c` の `i915_present_shared()`、
2026-09-29 の実機の perf で present ごとに flip 約 5 ms）。UAPI で運べなければ変更を事前に提示する。

## 着手前に決めること

- compositor（`userland/desktop/wayland`）は直さない（WS075 の規則）。libvulkan の present mode から display の present の
  flag へ運ぶ道（`include/drivers/gpu.h` の `gpu_display_present`）。UAPI を変えるなら差分を `plan/ws075/proposed/` に置いて提示する。
- MAILBOX の「新しい frame が勝つ」の実装（今の NO_VSYNC の armed flip の buffer への描き直しは copy が latch と重なり tear する）。

## 依存

[ws075-p008](../phase008/phase.md)。

## 2026-09-29 の試み（サブエージェント）

### 決めたこと

- compositor（`userland/desktop/wayland/display.c`）は `GPU_DISPLAY_PRESENT` を FIFO で直接呼ぶ。GPU core（`gpu.c`）は FIFO の立たない present を断り、
  libvulkan の display WSI は FIFO だけを報告する。**MAILBOX・IMMEDIATE を運ぶには UAPI（present の flag）の追加が要る**。WS075 の規則により差分を
  [proposed/p019-present-mode.md](../proposed/p019-present-mode.md) に置き、適用していない（要る判断は下）。
- **FIFO は UAPI を変えずに先行させた**（`display/present.c`）: present は flip を arm して返り、次の present（と display の wait）が前の flip の latch を待ってから、
  panel が見せなくなった buffer に描く。compositor は flip の vblank を待つ間に次の frame を描ける。copy が scanout 中の buffer に書くことは無い（tear なし）。
  - 前は shared の経路が present の後に latch を待ち、CPU の経路は待たずに armed の buffer に描き直していた（CPU の経路は tear し得た）。今は両方の経路の前に
    `i915_present_latch()`（`drv_i915_lcd_modeset_flip_wait`）。`I915_PRESENT_NO_VSYNC` の build は待たない（前と同じ、tear し得る）。
  - display の wait（`drv_i915_present_display_wait`）は newest の flip の latch を待ってから完了を報告する。

### 確認

| 確認 | 結果 |
| --- | --- |
| kernel の build（main の config、demo の passthrough の config） | rc=0、warning 0 |
| 規約（`plan/tools/style-check.py display/present.c`） | 変更の前と後で指摘の差なし |
| QEMU の boot test（GPU なし、main の image の複写の kernel を差し替え） | PASS（`build/ws075-shots/ws075-p019-boot-test.png`） |
| host の試験 | 該当なし（present.c は host の fixture に無い） |

実機（5330 の VFIO passthrough、`hdmi-h4-hw.sh`（所有者の確認つき）、`flock` の下）。image は p020 の `rps.img` の複写の ESP の vmunix を、
変更の前（この tree の present.c を main 4e17c34f のものに戻した kernel）と後の kernel に替えたもの。起動の約 130 秒後から:

| 物差し | p020（変更の前） | p019 FIFO の先行 |
| --- | --- | --- |
| `latency A 10` | 10/10、中央値 32.3 ms（15.8〜82.6） | 10/10、中央値 32.4 ms（15.7〜94.2） |
| 入力なしの 3 秒の flip | 0 | 0 |
| `rate A 10` | 56.0/s・55.0/s | 58.7/s・57.0/s |
| `freq 5 20 move`（要求 / CAGF の平均） | 705 / 700 MHz | 534 / 525 MHz |
| `freq 5 100`（idle） | 100 / 100 MHz | 100 / 100 MHz |
| kernel の log の `did not latch` | 0 | 0 |

- 率は 60 Hz の上限に近づき（+2.5/s）、latency は同じ、同じ率で GT の周波数が下がった（描画と flip の待ちが重なるため）。画面は
  `build/ws075-shots/ws075-p019-fifo-desktop.png`（desktop）。
- tear が無いことは構成による（copy は latch の後の、scanout していない buffer にだけ）。画面の目視での tear の検査はしていない。

### 要る判断（main・ユーザーへ）

- [proposed/p019-present-mode.md](../proposed/p019-present-mode.md) の UAPI の追加（`GPU_DISPLAY_PRESENT_MAILBOX`・`_IMMEDIATE`、info の `GPU_DISPLAY_MAILBOX`）の承認。
  - 承認されたら: resident の buffer を 3 つにして tear の無い MAILBOX（armed の flip の置き換え）、IMMEDIATE、libvulkan の display WSI の報告と flag の写し。
  - 承認されなければ: display の present mode は FIFO だけとし、MAILBOX・IMMEDIATE を範囲の外に移して p019 を cleared にする。

### 判断と結果（2026-09-29）

- ユーザーの判断（main の中継、質問への回答）: 「MAILBOX・IMMEDIATE はデモの後に回す」。範囲から外し、[F-057](../../future-work.md)（main が記録、提案の file を指す）へ。
- p019 は FIFO の先行で cleared（受け入れの「FIFO（vsync）」「tear なし」「latency・率が p020 より悪くならない」を満たす。MAILBOX・IMMEDIATE の扱いは F-057）。

### 未実施

- 素の 5330。tear の画面での検査（構成による）。
