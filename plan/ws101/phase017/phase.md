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
