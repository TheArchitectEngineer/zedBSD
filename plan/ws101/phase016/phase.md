<!-- awesome-plan project=zedbsd record=ws101-p016 -->

# ws101-p016: L2 時間の分解（計測だけ）

Status: cleared（2026-09-30、サブエージェント P4、worktree `ws101-compute`（branch `wt/ws101`）。QEMU の Venus だけ。5330 の passthrough は実機の lock（`/tmp/i915-hw.lock`）が 2 回見て使用中（P1 の p029）で、未実施）
Disposition: normal
Parent: [WS101](../ws.md)
Queue: なし（2026-09-30 Q1 の割り当て。ユーザーの判断で WS101 の最適化は優先が低いので、計測だけで小さく終える）

## 範囲と受け入れ

S13（`mix.nct`、N = 4,000,000、1 要素 32 演算）の CPU の run と GPU の run の時間を部分に分ける。
部分は、EGL の初期化、shader の compile、upload、dispatch、readback など。まず QEMU で道具を作る。
5330 は lock が空いていれば測る。p017（3 倍）で直す候補を 1〜2 挙げる。toolchain（Noct と accel の patch）は変更・build しない。

## 道具

- **libEGL・libGLESv2 の時間の行**（`KEI_GLES_COMPUTE_TRACE=2`。1 は従来どおり dispatch の行だけ）:
  - 各段階の時間を stderr に `gles: time step=NAME us=N bytes=B`（libEGL は `egl: time step=NAME us=N`）で出す。
  - 時間を測る段階:
    - EGL: eglInitialize・eglCreateContext・eglCreatePbufferSurface・eglMakeCurrent
    - shader: glCompileShader・glLinkProgram
    - buffer: glBufferData・glDeleteBuffers・glMapBufferRange・glUnmapBuffer
    - dispatch: glDispatchCompute の記録
    - 内部: device の buffer の作成、その buffer への copy（upload）、frame の待ち、device からの copy（readback）、frame の memory の解放（collect）
  - 公開の関数は、中身を static の `timed_*` に移し、時間を測る薄い包みにした。
  - 変数が無いときは時計を読まない。
  - 変えた file: `userland/desktop/libglesv2/gles.c`・`gles.h`・`buffer.c`・`program.c`・`compute.c`・`feedback.c`、`userland/desktop/libegl/egl.c`。
- **`plan/ws101/tests/time-split.sh`**:
  - 走っている guest で CPU の run と GPU の run（trace 2）をし、log を写して `time-split.py` で分ける。
  - `BIN` を渡すと library を入れる。`NOCT` を渡すと、accel 付きの Noct の binary を `/tmp/noct` に入れる。
- **`plan/ws101/tests/time-split.py`**:
  - 1 回だけの部分（EGL・compile・link）と、call ごとの部分を分けて出す。
  - call は、delete-buffers の後の最初の buffer-data で区切る。
  - frame-wait の中で走る collect は差し引いて、gpu-wait に分ける。
  - libGLESv2 の外（Noct 自身）は、mix.nct の call の ms から libGLESv2 の合計を引いた残り。
  - 最初の call の後の中央値を出す。
- 注意:
  - zedBSD の CLOCK_MONOTONIC は 1 ms 単位で、1 ms 未満の段階は 0 に見える。
  - accel 付きの Noct は build せず、この worktree の既存の `build/ws101-p011-hw/rootfs/bin/noct`（p011 の image のもの）を guest に入れた。
    guest の image（`build/ws081/demo-win-venus.img` の複写）の `/bin/noct` は accel が無かった。

## 結果（QEMU の Venus、guest の時計、ms。3 回の run で同じ傾向）

CPU の run: 1 call 22 ms（最初も 21〜24 ms。JIT の差はほぼ無い）。GPU の run: 最初の call 547、以後の中央値 835。

1 回だけの部分（process ごと）:

| egl-initialize | egl-create-pbuffer | egl-create-context・make-current | compile-shader | link-program |
| --- | --- | --- | --- | --- |
| 411（2 回呼ばれ、1 回約 205） | 262（2 回） | 0 | 4 | 51 |

GPU の call ごと（最初の call の後の 5 回の中央値。入力と出力の 16 MB が 2 つ、12 byte が 1 つ）:

| 部分 | ms | 中身 |
| --- | --- | --- |
| collect | **341** | frame-wait の中で、前の call の device の buffer（16 MB × 2）を解放する |
| device-buffer | **144** | 16 MB × 2 と 12 byte の device の buffer の作成（vkCreateBuffer・vkAllocateMemory・map） |
| upload | 80 | CPU の写しから device の buffer への copy（16 MB × 2） |
| buffer-data | 76 | glBufferData の malloc・0 埋め・copy（16 MB × 2） |
| gpu-wait | 70 | submit と GPU の計算の待ち |
| readback | 39 | device の buffer から CPU の写しへの copy（16 MB） |
| record | 20 | dispatch の記録（descriptor など） |
| delete・map の残り | 0 | |
| libGLESv2 の外（Noct） | 73 | Noct の中の copy（glMapBufferRange の後の memcpy）、buffer の準備など |
| **合計** | **835** | |

- 最初の call の collect は 10 ms だけ（解放する前の call が無い）。そのため最初の call（547）のほうが後の call（835）より速い。
- GPU の計算そのもの（gpu-wait の 70 ms）は call の 8 % で、残りは buffer の作成・解放と 16 MB の copy（5 回）。
- 5330 の数（p011: GPU 260 ms、CPU 10 ms）の内訳は未計測。Venus では Vulkan の呼び出しに約 10 ms ずつかかる（F-064）ので、作成・解放の割合は実機では小さい見込み。copy の割合は実機でも残る見込み。

## p017 で直す候補（大きい順）

1. **device の buffer の使い回し**（libGLESv2）:
   - 案: 削除された buffer object の device の buffer（同じ大きさ）を捨てずに取っておき、次の glBufferData に使う。
   - 効果: call ごとの作成（144）と解放（collect の 341）を省く。QEMU で 835 ms の約 6 割。
   - 範囲: Noct は変えない（toolchain の外）。
2. **copy を減らす**（libGLESv2）:
   - 今の copy: glBufferData は malloc・0 埋め・copy の 2 回の書き込みで CPU の写しを作り、upload でそれを device へもう 1 回写す。readback は device から CPU の写しへ写し、Noct がそれをさらに写す。
   - 案: glBufferData は、使っていない device の buffer に直接書く（0 埋めはデータがあれば省く）。glMapBufferRange(READ) は、device の写像を直接返す（readback の copy を省く）。
   - 効果: 16 MB の copy を 5 回から 2 回（Noct の入力と出力）に減らす。QEMU で buffer-data・upload・readback の合計 195 ms。
   - 範囲: 1 と合わせて libGLESv2 の中で閉じる。

参考: EGL の初期化が 2 回ある（Noct が 2 回 display を開く）。1 回の run の始まりに約 670 ms かかるが、call の中央値には入らない。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make -j16 ZEDBSD_CONFIG=plan/ws101/tests/hw/g3/config.mk BUILD=build/amd64 build/amd64/dynamic/libEGL.so build/amd64/dynamic/libGLESv2.so` | rc 0、warning 0（Noct・toolchain は build していない） |
| style | `plan/tools/style-check.py`（変えた 7 file）、`git diff --check` | 0 件 |
| 計測 | `BIN=build/amd64 NOCT=build/ws101-p011-hw/rootfs/bin/noct plan/ws101/tests/time-split.sh build/ws101-p016/qemu` | 上の表（`build/ws101-p016/qemu/split.txt`）。CPU と GPU の checksum が同じ、check wrong=0 |
| 回帰: trace なし | `KEI_GLES_COMPUTE_TRACE` なしで GPU の run | check wrong=0、時間の行は出ない |
| 回帰: egltest | zdesktop の上で `egltest --scene=feedback`・`--scene=queries`（60 frame） | どちらも failures=0 |
| 5330 | `flock -n /tmp/i915-hw.lock` | 2 回とも使用中。未実施 |

## 残り

- 5330 の passthrough と素の 5330 での同じ分解（lock が空いたとき）。
  - 実機の image には accel 付きの Noct が要る。今の build の directory（`build/ws101-p011-hw`）の Noct を使えば、toolchain を build しなくて済む。
- p017 の候補の選択（上）。
