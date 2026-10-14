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

## T1-624 の結果（実機 Pi 4、2026-10-14 夜）と次の版

- `rpi4-vchiq: connected (version 8)`・`rpi4-mmal: firmware MMAL 16.1 (service version 16)` まで実機で動いた。VCHIQ の初期化（channel 3）と service の open は合格。
- 全ての decoder の作成が `decoder not configured (13)`（13 = zedBSD の ENODEV）。configure の最初の `port_info` が ENODEV を返した。返すのは reply の status が 0 でない時か `found` が 0 の時で、BSD-3 の userland の client は `found` を読まない（status だけ）。最有力は firmware が `found` を 0 のまま返すこと。
- 直し（この版）:
  - `port_info` は status だけを見る。`found` は log に出す。
  - 1 回の起動で原因を切り分ける log:
    - 各 request の名前・送った長さ・返事の長さ・status の語と MMAL_STATUS_T の名前（最初の 96 行）。
    - 各 port の status・found・handle・buffer の最小・推奨・今の値・encoding・大きさ。
    - component の handle と port の数。
    - configure が止まった段の名前（port info・input format・output format・port info again・input enable・output enable・component enable）。
  - output の NV12 が拒まれたら I420 で試す。probe は I420 の frame も NV12 と同じ checksum にする。
- T1 の次の依頼: image は `plan/ws236/tests/config-rpi4-video-probe.mk`（video・probe・vkvideo-probe を 1 回の起動で）。手順は phase005 と同じで、dmesg の grep に `rpi4-mmal: (trace|port|.*created|.*not configured|NV12)` の行を全て含める。

## T1-625 の結果（実機 Pi 4、2026-10-14 夜）と次の版（44839b871）

- COMPONENT_ENABLE まで全て SUCCESS。I420 の出力で動いた。
- 観測:
  - run=a: format change の中の output の DISABLE が返らない（42 = ETIMEDOUT）。
  - run=b: 4 枚が decode の順・lag 0 で出た（R1 は有望）。ただし frame 1〜3 の checksum が不明（display/decode が -1）。その後に bulk が 1 秒で終わらず、decoder が退役した。
  - 以後の session は全て 17（EBUSY）。退役した decoder が唯一の slot を塞いだ。
- 直し:
  1. **返事を待つ間に返ってきた buffer を受け取る**（`pump_buffers`、request に decoder を渡す）。firmware は EFCH の後ろに frame の BUFFER_TO_HOST を積み、その bulk を ARM が受け取るまで DISABLE に答えない、と見た。受け取る順は到着の順のまま。bulk を伴う event（256 byte 超）より後ろの buffer は取らない。
  2. **payload_in_message**: firmware は 128 byte 以下の payload を message の中（offset 164）で送り、その時は bulk が来ない。BSD-3 の userland の client の規則。これまで無視して bulk を待っていた。bulk が 1 秒で終わらない原因の最有力。input・output の両方で、短い payload は bulk を出さない。
  3. **input の buffer に data が付いて返る時は、userland と同じく scratch に bulk で受けて捨てる**（変えない）。log に毎回 len・short・flags を出し、本当にそうなるかを見る。
  4. **出力の buffer の数**: 推奨の数を使う（最小・推奨・3 の最大、上限 8）。これまでは最小と 3 の最大だった。
  5. **DPB**: R1 の SPS の `max_dec_frame_buffering` を level の MaxDpbFrames にした（以前は 4）。参照の数以上、16 以下。frame 1〜3 の不一致は DPB が小さすぎて参照が足りなかった、と見た。
  6. **退役で塞がない**: 壊れた decoder、または bulk が残る decoder を destroy すると、MMAL の service を閉じる。firmware は service の bulk を abort し、component も落とす。次の decoder は service を開き直す。close の後も残る bulk の memory だけを永久に保持し、decoder の slot は空く。返事が来なかった request は、その decoder を broken にする。
  7. **probe**:
     - 輝度だけの checksum の参照を足した（`make-probe-data.py`）。全体が合わず輝度だけ合う frame は `match=luma`（chroma の配置の誤り）とする。
     - pts（各 unit の decode の番号）で順を判定する `order_pts` を足した。pixel が違う frame でも順が分かる。
     - 出力の順の pts の列（`pts_seq=`）を出す。
- 1 回の起動で切り分ける log（増やした物）:
  - `rpi4-mmal: in N: input|output I len L off O flags F pts P short S zero_copy Z`、`in N: event C len L`
  - `bulk N: receive B bytes into the frame|scratch`、`bulk N: frame received (actual)`
  - 最初の 400 行まで。
  - 返事が来ない時・bulk が終わらない時の `stall at …`: 待ち行列の数・frame と scratch の bulk・staging の bulk と buffer・firmware が持つ出力の数・空いた input の数。待ち行列の先頭 8 件と、`rpi4-vchiq: service … receive queued/completed transmit queued/completed`。
  - destroy で service を閉じる時の `destroyed …; the service is closed and opened again`。
- 検証（host）:
  - build は warning 0、どれも `arm64 vmunix check: PASS`。`build/ws236-vp`（video+probe）、`build/ws236-video`、`build/ws236-probe`。
  - `run-h264-test.sh`（pb-high-352 の buffering=6 ほか）・`run-mmal-wire-test.sh`（short data の offset を追加）・`run-vchiq-wire-test.sh` は全て PASS。
- T1 の次の依頼: T1-625 と同じ手順（image は `plan/ws236/tests/config-rpi4-video-probe.mk`）。dmesg の grep に `rpi4-mmal: (in |bulk |stall|.*destroyed)`・`rpi4-vchiq: service` を足す。

## T1-626 の結果（実機 Pi 4、2026-10-14 夜）と次の版（26729bb11・210a4724c）

- 前進:
  - 352 run=a・1080 run=a・1080 run=b は error=0 で完走した。
  - 2 回目以降の session も作れた。vkCreateVideoSessionKHR は成功し、videoplayer は vulkan-video の backend を選んだ。
- 中身: 一致は 352 run=a の 4 枚だけで、1080 は全て不一致（luma も）。
  - 1080 は run=a と run=b で同じ picture の sum が一致する（出力は決定的）。IDR も違う。
  - 352 run=a では、参照に使われる B（display 2・6）の出力が違う。それを参照する b（1・3・5）は一致する。内部の decode は正しく、出力側に何かが起きている可能性がある。data 待ち。
- 順:
  - run=a は display 順で出る（P の pts は firmware が補間した値: 40004 = 前の pts + 40000）。
  - run=b の 1080 は decode 順・pts は 0〜7 で正しい（R1 は順の面では成立）。
  - 最初の picture は次の入力が来るまで出ない（vkvideo の picture 0 の 42 の原因）。
- 止まる: 352 run=b の frame の bulk が完了しなかった。v3d の起動（clock・power domain・bridge）と同時刻だった。
- 道具: vkvideo-probe は package の platform が amd64 だけで、rpi4 の image に入らなかった。
- この版:
  - **生の frame の取り出し（ユーザーの依頼、最優先）**:
    - probe が各 run の最初の数枚を受け取ったままの bytes で保持し、`/dev/mmal-probe` で読めるようにした（`rpi4-mmal-dump.c`。512 byte の header + 生の bytes）。
    - vkvideo-probe に `--dump=FILE` を足した（同じ形式）。
    - host の `plan/ws236/tests/dump-compare.py` は、ffmpeg の正解と並べた PNG を作る（正解・実際・差分。Y、Cb・Cr、RGB）。別の読み方（pitch・chroma の開始・NV12/NV21/I420/YV12）の比較 PNG と summary.txt も作る。合成の dump で動作を確かめた。
  - probe:
    - 帯と縦帯の checksum の地図を参照と照合して log に出す。
    - run=c（R1 に加えて各 unit の後に access unit delimiter）を足した。
    - VCHIQ の接続後に 30 秒待ってから始める。
  - vkvideo-probe の platform に rpi4 を足した（既定 n）。
  - format change の時に、受信済みの frame と caller が持つ buffer を捨てない（frame 0 の欠け）。
- 検証（host）:
  - `build/ws236-vp`・`build/ws236-video`・`build/ws236-probe` は warning 0・vmunix check PASS。arm64 の `bin/vkvideo-probe` も build できた。
  - 既定の `config-rpi4.mk` の program は 249 で、vkvideo-probe は入らない。
- 次: T1 の dump と PNG を見て中身の不一致の原因を決める。run=c の結果で vulkan-video の最初の frame の遅れを直す（delimiter）。

## 残り

- T1 の結果の判定（Q1）。R1 が合えば p004 は D-S1、外れれば Baseline だけ。
- p004 で: 出力を image の memory へ直に（今は bounce だけ）、session ごとの decoder、S2（待つ間 mutex を離す）。
