<!-- awesome-plan project=zedbsd record=ws101-p017 -->

# ws101-p017: L2 3 倍（libGLESv2 の buffer の使い回しと copy の削減）

Status: uncleared（2026-09-30、サブエージェント P4、worktree `ws101-compute`（branch `wt/ws101`））
- 実装と QEMU の計測は済み。GPU の call は 835 → 212 ms。
- 目標（GPU の中央値 ≤ CPU の中央値 / 3。Q1 の依頼の文は「≤ CPU × 3」）には、QEMU ではどちらにも届かない。
- 判定は素の 5330 の数でする。5330 は実機の lock（`/tmp/i915-hw.lock`）が使用中で未実施。

Disposition: normal
Parent: [WS101](../ws.md)
Queue: なし（2026-09-30 Q1 の割り当て）

## 範囲と受け入れ

p016 の候補 2 つを libGLESv2 の中だけで実装する。toolchain と Noct は変えない。

1. 削除された buffer object の device の copy を取っておき、同じ大きさの次の glBufferData で使い回す（上限と捨てる時を決める）。
2. copy を減らす。
   - glBufferData は device の buffer に直接書き、データがあれば 0 埋めを省く。
   - glMapBufferRange(READ) は device の写像を直接返す。

判定: CPU と GPU の checksum が同じで wrong=0。QEMU で前後の GPU の call の中央値。5330 は lock が空いていれば 1 run。

## 設計

**使い回し（spare）**
- 置き場所: `gles_state.spares`。
- 上限: `GLES_SPARES` = 8 個、合計 `GLES_SPARE_BYTES` = 64 MiB。
- 取っておく時:
  - buffer object の device の copy は、garbage に入るときに写像と大きさを持つ（`gles_garbage.mapped`・`size`）。
  - frame が終わって garbage を片付けるとき（`gles_garbage_destroy`）、解放せずに spare に入れる。
  - 場所が無ければ、古い spare から解放する。
- 使い回す時: buffer object の device の copy を作るとき（`buffer_device_copy`）、同じ大きさの spare があればそれを使う。
- 捨てる時:
  - `GLES_SPARE_FRAMES` = 8 frame 使われなかった spare は、`gles_collect` で解放する。
  - context の解放で全部を解放する（`gles_spares_release`）。
- 小さい buffer（12 byte の scalar など）も同じく使い回す。
- S13 の 1 call は、16 MB × 2 と小さいもの 1 つ。前の call の分が frame の終わりに spare になり、次の次の call で使われる。そのため、常に 6 個・約 64 MB を持つ。

**device の上の bytes（`gles_buffer.on_device`）**
- 対象: `GLES_ON_DEVICE_MIN` = 64 KiB 以上の glBufferData。
- glBufferData: bytes を CPU の写しに置かず、device の copy（spare か新しいもの）の写像に直接 1 回書く。データが無ければ 0 で埋める。
- `gles_buffer.data` は、その写像を指す。
- 同期と読み戻し:
  - `gles_buffer_sync` は何もしない（upload が無い）。
  - `gles_buffer_fetch` は device の終わりを待つだけで、copy しない。
  - glMapBufferRange(READ) は、写像をそのまま返す（readback が無い）。
- CPU が書くとき（`gles_buffer_writable`）:
  - 対象: glBufferSubData、glCopyBufferSubData の書き先、glReadPixels の pack buffer、書くための map。
  - その frame が copy を読んでいる（`used == frame`）ときだけ、bytes を CPU に写して従来の形に戻す。copy は frame に残り、次の使用で新しい copy を作る。
  - それ以外は、写像にそのまま書く。
- 64 KiB 未満は、従来どおり CPU の写し。0 埋めはデータが無いときだけにした（データがあれば最後の 1 byte だけ 0）。
- 試して戻したもの:
  - buffer object の copy を CPU の cache 付きの memory（HOST_CACHED）に置く案。QEMU で 221 → 213 ms と差が小さかった。
  - i915 の実機では snoop の memory を GPU が読む速さが分からないので、入れなかった。

## 計測（QEMU の Venus、p016 の道具 `time-split.sh`、ms、最初の call の後の 5 回の中央値）

| | p016（前） | 後（3 回の run） |
| --- | --- | --- |
| GPU の call | 835 | **212・212・217** |
| CPU の call | 22 | 23・23・23 |
| 倍率 | CPU が 38 倍速い | CPU が約 9.2 倍速い |

後の内訳（中央値）:

| 部分 | 前 | 後 |
| --- | --- | --- |
| collect（解放） | 341 | 10 |
| device-buffer（作成） | 144 | 0（spare） |
| upload | 80 | 0 |
| buffer-data | 76 | 14（device の写像への 1 回の copy） |
| readback | 39 | 0 |
| gpu-wait | 70 | 70〜79 |
| record | 20 | 20 |
| libGLESv2 の外（Noct） | 73 | **95** |

- 最初の call は 461〜471 ms（spare がまだ無いため、作成が入る）。
- libGLESv2 の外（Noct）が 73 → 95 に増えた。Noct が map の後にする 16 MB の copy が、CPU の写しではなく device の写像から読むようになったため（readback の 39 ms が消え、その一部がここに移った）。
- 残りの約 212 ms: Noct 自身 95、GPU の待ち 70〜79、記録 20、glBufferData 14、collect 10。
  - GPU の待ちと記録には、Venus の同期の呼び出しの約 10 ms ずつ（F-064）が含まれる。

**S13（`s13.sh`、demo の表示）**: 「GPU: 234 ms a run」「The CPU is 10.6 times as fast as the GPU.」「The results are the same」。p015 の実機は 27.7 倍だった。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make -j16 ZEDBSD_CONFIG=plan/ws101/tests/hw/g3/config.mk BUILD=build/amd64 build/amd64/dynamic/libGLESv2.so build/amd64/dynamic/libEGL.so build/amd64/bin/glescompute build/amd64/bin/egltest` | rc 0、warning 0（Noct・toolchain は build していない） |
| style | `plan/tools/style-check.py userland/desktop/libglesv2/*.c gles.h`、`git diff --check` | 0 件 |
| 一致 | `time-split.sh` の 3 回 | CPU と GPU の checksum が同じ（sum=-1291579143 xor=757251536）、check wrong=0 |
| host | `plan/ws101/tests/gles/run.sh` | PASS（reflect-host 0 of 10 failed） |
| guest: glescompute（venus.sh と同じ 3 つ、zdesktop の上） | `glescompute`、`--platform=default`、`--repeat=1000` | 3 つとも `GLESCOMPUTE DONE failures=0`、compositor は生きている |
| guest: egltest | `--scene=feedback`・`--scene=queries`（60 frame） | どちらも failures=0 |
| guest: S13 | `sh /usr/share/gpudemo/s13.sh`（accel 付きの Noct を /bin/noct に入れて） | exit 0、結果が同じ |
| boot test | `OUTPUT=build/ws101-p017-boot plan/tools/boot-test.sh build/ws101-p016-run/disk.img`（新しい library を入れた guest の disk） | PASS |
| 5330 | `flock -n /tmp/i915-hw.lock` | 使用中。未実施 |

- guest の image は `build/ws081/demo-win-venus.img` の複写。accel 付きの Noct は既存の `build/ws101-p011-hw/rootfs/bin/noct`。
- venus.sh の image（`build-image.sh`）は作っていない。同じ 3 つの run を、走っている guest で行った。

## 残り（main の判断が要る）

- **5330 の計測**: 判定に使う素の 5330（と passthrough）の数。lock が空いたとき、P1 の run に入れてもらうのが早い。
  - p011 の実機は GPU 260 ms / CPU 10 ms。QEMU で消えた部分（作成・解放・copy）の実機での割合は分からない。
- **3 倍に届かないとき、次の候補**:
  - (a) Noct の map の後の copy（95 ms の大部分）を無くす: 連続の call で buffer を GPU に置いたままにする。Noct の変更で、toolchain・D5 の扱い。
  - (b) 見本を計算の密度の高いものにする。ユーザーの確認が要る（ws.md の L2 の候補）。
- 目標の向きの確認: ws.md は「GPU ≤ CPU / 3」、Q1 の依頼の文は「GPU ≤ CPU × 3」。QEMU ではどちらにも届いていない。

## 5330 の追記（2026-09-30、P1 が Q1 の依頼で計測）

5330 の QEMU の VFIO passthrough（`plan/ws075/tests/hdmi-h4-hw.sh`、`flock /tmp/i915-hw.lock` の下）。素の 5330 ではない。

- image: `plan/ws101/tests/demo/build-s13-image.sh` と同じ中身を、main（p017 を取り込んだ後、wt/ws075 の 6b409c04）から `build/ws075-s13` に作った。
  accel 付きの Noct は `.claude/worktrees/ws101-compute/build/ws101-p011-hw/rootfs/bin/noct`（Noct と toolchain は build していない）。
  - 注: 同じ directory の `bin/noct` は accel 無しの build（「GPU acceleration is not available in this build.」、1 回目の run で判明）。accel 付きは
    `rootfs/bin/noct`（libGLESv2 を読む）。
  - 注: `build-s13-image.sh` に `NOCT=...` を環境変数で渡すと、make が同じ名前の変数（host の noct）として使い、image の build が
    `native-swap-1024m.img` で失敗する（guest の ELF を host で実行しようとする）。script の中の変数名を変えた複写で build した（元の file は変えていない）。
- 手順（`build/ws075-p029/s13-run.sh`）: session → App Home → Terminal に `sh /usr/share/gpudemo/s13.sh`（s13-hw.sh と同じ）。続けて同じ Terminal で
  time-split.sh の 2 つの run（`/bin/noct -O2 -j --gc-tenure-size=100000000 [--gpu] /usr/share/gpudemo/mix.nct 4000000 6`、GPU は
  `KEI_GLES_COMPUTE_TRACE=2`）を /home/kei に書かせ、stop の後に image から読んで `plan/ws101/tests/time-split.py` で分けた。

**S13（5330、1 回）**: 「CPU: 10 ms a run」「GPU: 221 ms a run (6 kernels ran on the GPU)」「The CPU is 22.1 times as fast as the GPU.」
「The results are the same on the CPU and the GPU.」（画面 `build/ws075-shots/ws101-p017-5330-s13.png`、wt/ws075 の worktree）。
p015 の実機は 27.7 倍（p011 の GPU 260 ms / CPU 10 ms）。

**GPU の内訳（time-split、5330、1 回、4000000 × 6 call）**: checksum は CPU と同じ（sum=-1291579143 xor=757251536）、check wrong=0。

| call | total | libGLESv2 | 外（Noct） | buffer-data | device-buffer | record | gpu-wait |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0（最初） | 373 | 328 | 45 | 9 | 15 | 0 | 303 |
| 1 | 229 | 192 | 37 | 20 | 5 | 0 | 167 |
| 2〜5 | 210〜218 | 175〜183 | 35〜36 | 8 | 0 | 0〜1 | 167〜174 |
| 中央値（最初を除く） | **215** | 180 | 35 | 8 | **0** | 0 | **167** |

（ms。upload・readback・map-other・delete・collect は全て 0〜1。one-time: egl-initialize 1、create-pbuffer 2、compile-shader 1、link-program 1）

- p017 の device buffer の使い回しは実機でも効いている（2 回目の call から device-buffer 0 ms）。
- 1 call の 215 ms のうち 167 ms（78%）は GPU の submit と完了の待ち（gpu-wait）。Noct 自身は 35 ms、libGLESv2 の copy は 8 ms。
  QEMU（Venus）の 212 ms と同じくらいだが、内訳は GPU の待ちが大半（QEMU は Noct 95・待ち 70〜79）。
- CPU は 1 run 10 ms（CPU の 22 倍の遅さ）。GPU の待ちの中身（i915 の実行器の compute の dispatch の実行時間か、同期の往復か）は
  この計測では分からない。次の切り分けは i915 の engine の時間（`plan/ws075/tests/hdmi/engine-gdb.sh` の session ごとの engine・queue・round）。
