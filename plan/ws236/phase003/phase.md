<!-- awesome-plan project=zedbsd record=ws236-p003 -->

# ws236-p003: MMAL の `video_decode` の client と R1 の起動時の probe

Parent: [WS236](../ws.md)
Status: in-progress（P3、2026-10-14。最小の client（同期、EOS なし、NV12、bounce に受ける）・H.264 の AU 分割と SPS の書き換え・probe を実装、host の試験と build まで。test-wait（T1 の実機の probe、下の依頼）
Phase disposition: normal

## 範囲

[design.md](../design.md) §4（bulk の規則 (a)〜(i)）・§5・§12、[review-1](../review-1.md) の Q1 の判断（最短の道: p003 は最小で S4 の規則を守り、§12 の probe を先に T1 の実機に）。

## 変えた file

| file | 内容 |
| --- | --- |
| `src/drivers/platform/rpi4/rpi4-mmal-wire.c`・`.h`（新） | MMAL の message の byte の配置（32 bit ARM の EABI で測った値、design §14 m3）と組み立て（create・component・port info get/set・port action・parameter・buffer） |
| `src/drivers/platform/rpi4/rpi4-mmal.c`・`.h`（新） | decoder の client: service `mmal`（v16/min10、GET_VERSION を log）、request と返事（waiter）、`ril.video_decode` の作成、input H.264・output NV12（幅 32・高さ 16 に丸め）、TIMESTAMP_FIFO・INTERPOLATE を false、input の buffer は firmware の推奨の数と大きさ（最大 16）で AU を分け最後だけ FRAME_END（規則 (g)）、input は常に bulk で 4 に丸め（(d)）、staging は 2 面で TX_DONE と buffer の返却まで再利用しない（(i)）、output は bounce（最低 3）に到着の順に受信の bulk（4 に丸め、空の EOS は 8 byte、(a)(b)）、256 byte を超える event は bulk（(c)）、FORMAT_CHANGED で output を disable・format・enable し直す、flush は PORT_FLUSH と 8 byte の dummy bulk（(e)）と output の PORT_ACTION FLUSH、bulk が残った decoder は retire して memory を返さない。VCHIQ の受信の thread からは送らない |
| `src/drivers/platform/rpi4/rpi4-h264.c`・`.h`（新） | Annex B の NAL と AU の分割、SPS の VUI を「並べ替え 0」の VUI に置き換える書き換え（VUI の前の bit は全て保つ、scaling list も読み飛ばす、emulation prevention） |
| `src/drivers/platform/rpi4/rpi4-mmal-probe.c`・`.h`・`rpi4-mmal-probe-data.inc`（新） | §12 の probe（`CONFIG_DRIVER_BCM2711_VIDEO_PROBE`、試験の kernel だけ）。352 と 1080 の 2 つの stream を (a) そのまま・(b) SPS 書き換えで decode し、各出力を checksum で参照と照合して decode の順の何番か・遅れを log。`.inc` は `plan/ws236/tests/make-probe-data.py` の生成 |
| `src/drivers/platform/rpi4/rpi4-vchiq.c`・`.h` | bulk の queue が満ち（方向ごと 4）たら最大 1 秒待つ（EBUSY で返さない） |
| `src/kern/platform/rpi4.c`・`Makefile`・`platform/arm64/vmunix.mk` | `CONFIG_DRIVER_BCM2711_VIDEO_PROBE`（既定 n、-D 1 行）、MMAL の file は VIDEO か PROBE の時、probe は PROBE の時、`#if` で probe の起動 |
| `plan/ws236/tests/` | `config-rpi4-mmal-probe.mk`、試験の stream（ws083 から複写した 3 つ、`pb-high-1080.h264` は生成）、`make-probe-data.py`、`h264-tool.c`・`run-h264-test.sh`、`mmal-wire-test.c`・`run-mmal-wire-test.sh` |

## 検証（2026-10-14、host）

- `sh plan/ws236/tests/run-mmal-wire-test.sh` → `mmal-wire: all PASS`（大きさ・offset・fourcc・parameter の id を測った値と照合、組み立ての各 field）。
- `sh plan/ws236/tests/run-h264-test.sh` → `h264: all PASS`（ws083 の 3 つの stream で AU の数 = ffprobe の frame の数（15・3・10）、書き換えた SPS を ffmpeg の trace_headers が読み max_num_reorder_frames 0・max_dec_frame_buffering = max_num_ref_frames、VUI の前の SPS の field が全て同じ（cqm の scaling list を含む）、並べ替えの無い 2 つは decode の結果が同じ）。注: ffmpeg は並べ替えのある stream で reorder 0 と言われると遅れた 3 枚を捨てる（firmware も同じなら probe で missing として見える）。
- `sh plan/ws236/tests/run-vchiq-wire-test.sh` → all PASS（p002 のまま）。
- build（warning 0、全て `arm64 vmunix check: PASS`）: probe の config（`build/ws236-probe`）、VCHIQ だけ（`build/ws236-rpi4`）、既定の `config/ci/config-rpi4.mk`（`build/ws236-rpi4-off`）。既定で `rpi4.c` の preprocess の結果が 9fc15a7b8 と byte で同じ。
- 実機・QEMU: 未実施（下の T1 の依頼）。

## T1 への依頼（Q1 経由）

1. image: `plan/ws236/tests/config-rpi4-mmal-probe.mk`（`config/ci/config-rpi4.mk` ＋ VCHIQ ＋ probe）で agent/p3 の tree（または merge 後の main）から rpi4 の disk image を build。
2. QEMU raspi4b（回帰）: `plan/tools/boot-test.sh` で login prompt。dmesg に `rpi4-vchiq: emulated board without a VPU; no VCHIQ`、`rpi4-mmal-probe: started; waiting up to 15000 ms for VCHIQ`、15 秒後に `rpi4-mmal-probe: VCHIQ did not connect in 15000 ms`、fatal・panic・stall が無い。
3. 実機の Pi 4: その image で起動し、login の後 30 秒待ってから `dmesg | grep -E 'rpi4-(vchiq|mmal)'` の全行を返す（ssh か serial の対話。log の判定は dmesg の行だけで）。画面の PNG（login の画面）も。
4. 合否（Q1 が判定、design §12）:
   - 前提: `rpi4-vchiq: connected (version N)`、`rpi4-mmal: firmware MMAL x.y`、`rpi4-mmal: decoder ...`・`output 3231564e ...`（NV12）の行。
   - **R1**: `rpi4-mmal-probe: 352 run=b order=decode ... missing=0 lag=0` と `1080 run=b order=decode ... lag=0`、対照の `run=a` が `order=display`（または lag > 0）。
   - (b) が `order=display`・`other`・missing > 0 なら R1 は外れ → ベータ2 は Baseline だけ（design §5）。
   - 参考の値: 1080 の各出力の `copy_us`（3 MiB の uncached の bounce の CPU の読み）、gpu_mem の不足を示す MMAL の error の行の有無。
5. 時間の目安: build 約 5 分、QEMU 約 3 分、実機の起動と dmesg 約 5 分。

## T1-621 の結果と直し（2026-10-14）

- T1-621（QEMU raspi4b、a9eb23eea の merge 後の main）: boot-test PASS、`rpi4-vchiq: emulated …` は出たが probe の行が 150 秒後も 0。
- 原因: `kthread_create` は止まった thread を作り、`thread_start` で走らせる（`src/kern/main.c`・`rpi4-bt.c` などの作り）。VCHIQ の受信の thread と probe の thread の両方で `thread_start` が抜けていた（実機でも VCHIQ の受信が動かず CONNECT が返らなかった）。
- 直し: 両方に `thread_start` を足し、probe は最初に `rpi4-mmal-probe: started; …` を必ず 1 行出す。以後は接続なし（15 秒後の 1 行）・decoder の作成の失敗・各 run の要約・`done` のどれかが必ず出る。

## 残り

- T1 の結果の判定（Q1）。R1 が合えば p004 は D-S1、外れれば Baseline だけ。
- p004 で: 出力を image の memory へ直に（今は bounce だけ）、session ごとの decoder、S2（待つ間 mutex を離す）。
