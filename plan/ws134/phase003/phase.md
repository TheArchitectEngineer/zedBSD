<!-- awesome-plan project=zedbsd record=ws134-p003 -->
# ws134-p003: システムモニターの 3D と動き（M2）

Status: uncleared（q645、P2、2026-10-03。T1-043 で sim の fps 4.7（条件 15 以上）。P2 はユーザーの指示でラップアップ、直しは未着手）
Parent: [WS134](../ws.md)
設計: [design.md](../design.md) §2.2・§3・§4.2

## 実装

- `space.c`（新）: 状態コア（3 つの入れ子の箱、CPU で上面が明るく、GPU で中殻が厚く脈動、memory で殻の間の点が密に（Available 10% 未満でアンバー）、
  network で 3 本の軌道の点が速く、disk の仕事で下のリングの明るい弧が回る、内の光は 4 秒の呼吸、level の色）、CPU のレリーフ（core ごとの箱が
  load でせり上がる、85% 超は縁が光る、奥から描く）、motion（pointer に 2 Hz の spring で追う camera の傾き、値への 1 次遅れ、phase）。
  CPU で投影して面ごとに陰影を付けた 2D の三角形にし、shape の shader で描く（design.md §4.2 の追記: i915 の compiler の制約、depth・instancing 無し）。
- `draw.c`・`draw.h`（新）: 描画の基本（色の token、quad・rect・gradient・plate・disc・text・segment・triangle）を scene.c から分けた。
- `scene.c`: 層ごとの視差（L0 4・L1 2・L2 1 論理 px、pointer の傾き＋12 秒の揺れ）、警告の段の plate（Elevated で縁が淡いアンバー、Warning で
  アンバーと前へ、Critical で coral）、値の slide（変わった桁が 180 ms で上（増）・下（減）から入り、前の値が淡く残る）、Network の 2 本の流れ
  （RX は左から・TX は右から、量で数と明るさ）、Disk の lane の流れ（latency で奥に詰まる）、GPU が 1 つの時の使用率の履歴、GPU の短い名前。
- `rules.c`: rule ごとの level（plate の警告に使う）、`count_simulated`（sim の source の時だけ sim の値で段を付ける。system の時は除く）。
- `main.c`: titlebar の時間の control を group 無しの text の pill に（group の face は icon で「...」と出ていた、T1-041 の PNG）、sim の GPU の名前を
  描画の device の名前に（「GPU 0 GPU 0」の重複）、sim は起動の前に 5 分の履歴を埋める（graph が右端だけだった）、Memory の上段は使用量と「of 8 GiB」。
- 試験: replay `calm8`・`warning`・`critical`（8 CPU、130 秒）、`monitor-p003.sh`（3 つの replay の固定の時計の PNG と level の順、sim 16 CPU・2 GPU を
  pointer を動かして 20 秒、fps 15 以上）。host の preview（`tests/host/preview.sh`・`preview.c`・`preview.py`）。

## 確かめ

- build: zedBSD の monitor（-Werror）warning 0、image（`build/p2-p002-img`、replay を含む）exit 0（image は scene.c の comment と空行だけの直しの前の tree）。
  Linux の flag の gcc `-fsyntax-only -Werror` 全 file。style-check 違反 0。host 試験 PASS（rules の count_simulated を足した）。
- host の preview（guest の絵の参考）: [critical-host.png](preview/critical-host.png)（Critical: CPU と Disk の plate が coral、コアの内が赤橙、Events 3 件）、
  [sim-1920-host.png](preview/sim-1920-host.png)（1920x1240、16 CPU・2 GPU、pointer で傾けた）。
- replay の level の時刻（host で計算）: warning は Elevated 70 s・Warning 120 s、critical は Elevated 15 s・Warning 65 s・Critical 110 s（latency 60 ms の 10 秒）。
- QEMU（T1 に依頼）: `monitor-p003.sh`。結果は未着。

## stub の項目

p002 と同じ（全ての値が sim か replay、本物は hostname・CPU の数・uptime）。

## 結果（Q1、2026-10-03、T1-043、QEMU）

uncleared。sim の fps 4.7（条件 15 以上、2 回とも同じ）、他の項目と PNG は ok（worktrees/t1/build/t1-monitor/t1-043/）。guest の描画は llvmpipe（`ZMON READY device="llvmpipe"`、Venus 無し）。Q1 の目視: sim の Graphics に「GPU 1 Simulated GPU」の名前が出る（ユーザーの指示「simという表記はつけなくていいです」に反する）、GPU 0 の名前が「llvmpipe (LLVM 19.1.7, 2」で切れる。再開: P2 が fps の原因（llvmpipe の CPU 描画の重さか、app の描画の量か）を調べ、Venus で測るか条件を直すか描画を軽くし、表記を直して T1 に再依頼。

## 結果（T1-043、a02f7eecd、QEMU、2026-10-03）

uncleared。`monitor-p003.sh` を 2 回（66 s・65 s、変更なし）流して同じ:

- FAIL: `sim: 4 fps (want 15 or more) MISSING`（sim.log `ZMON FRAME fps=4.7 wait_ms=5.21`・`4.7 / 6.92`・`4.6 / 5.96`）。guest は llvmpipe で描いている
  （`ZMON READY … device="llvmpipe (LLVM 19.1.7, 256 bits)"`）。流し直しの時の host: CPU の圧力 0、IO の avg10=6.87、T2 の QEMU が 1 つ。
- ok: 3 つの replay の level の順（Elevated → Warning → Critical）、state の文字、sim の cpus=16 gpus=2、failure 無し、zdesktop の ERROR 無し。
- PNG（T1 の目視）: critical.png に 8 個の CPU の箱・Critical の赤い枠・Events 3 行、sim.png に 16 個の箱と GPU 2 台。
  `/home/awe/zedBSD-worktrees/t1/build/t1-monitor/t1-043/`・`t1-043-retry/`。
- 実機・Venus の image では未測定。

### fps の調べ（途中）

- host で scene を作る CPU の時間を測った（`preview.c` に `PREVIEW_BENCH=N` を足した、commit 1227ff647）:
  `PREVIEW_BENCH=200 plan/ws134/tests/host/preview.sh OUT 1200 690 120000 sim:3:16:2` → **0.284 ms/frame**（20796 vertices、62 draws）。
  guest の CPU が host の 10 倍遅くても 3 ms で、scene の build は原因ではない。
- guest の `wait_ms`（present の後の fence の待ち）は 5〜7 ms で、1 frame の 213 ms の大半ではない。残りは未測定:
  `vkAcquireNextImageKHR`（FIFO で compositor が buffer を返すまで）、`vkQueueSubmit`・`vkQueuePresentKHR`（llvmpipe が描画や wl_shm への複写を
  ここで同期に行う可能性）、compositor の frame callback の間隔（zdesktop の合成も llvmpipe）。

### 再開の条件（次の担当へ。Q1 の 2026-10-03 の指摘 3 点）

1. fps: `render.c` の各段（acquire・submit・present・fence）と frame callback の間隔を `ZMON FRAME` に足して、QEMU の llvmpipe の image と
   **Venus の image の両方**で測り、phase.md に両方書く。llvmpipe の fill が重いなら描画を軽くする（全面の背景・plate の重なり・殻の半透明の
   overdraw を減らす、変わらない層を毎 frame 描き直さない等）。条件を llvmpipe に合わせて下げるだけで済ませない。比較に同じ image の他の
   Vulkan app（Notes 等）の fps も見る。
2. sim の Graphics の 2 台目が「GPU 1 Simulated GPU」と出る → ユーザーの「simという表記はつけなくていいです」に反する。`source.c` の sim の
   GPU 名を、それらしい名前に（例: 実在の型番を騙らない一般的な名前）。`plan/ws134/tests/host/host-test.c` の期待も合わせる。
3. GPU 0 の名前「llvmpipe (LLVM 19.1.7, 2」が途中で切れる → 括弧の前で切るか省略記号（`scene.c` の `short_name` の周り）。
4. 直したら build（warning 0）・host 試験（`tests/host/run.sh`）・preview を流し、T1 に `monitor-p003.sh` を再依頼。


## 再開（q657、P1 generation12、2026-10-04）

Q1 の割り当て（P2 は WS135）。再開の条件 1〜4 に沿って:

1. **計測**: `render.c` の `sm_renderer_draw` が段ごとの時間（acquire・record・submit・present・fence の wait）を `struct sm_frame_times`（`app.h`）に返し、
   `main.c` が scene の build と frame callback の答えの時間（`wl_surface_frame` から `done` まで）を足して、`ZMON FRAME` の行に
   `build_ms acquire_ms record_ms submit_ms present_ms callback_ms` を加えた（`fps=`・`wait_ms=` は前と同じ位置）。`monitor-p003.sh` は sim の全ての
   `ZMON FRAME` の行を出し、段 3 として zdesktop `--log-frames` で compositor の合成の回数を 10 秒数える（測るだけ、判定しない）。
   描画の軽量化は、この計測で重い段が分かってから行う（host の見積り: 1920x1240 で scene の fragment は画面の 2.3 倍、うち 1.0 は全面の背景の
   gradient、build は host で 0.28 ms。T1-043 の fence の wait 5〜7 ms と合わせると、213 ms/frame の大半は acquire か compositor の callback の待ちの見込み）。
2. **sim の GPU 名**: `source.c` で「Simulated GPU」をやめ、1 台目「Integrated Graphics」（描画の device 名で置き換わる）、2 台目「Discrete Graphics」
   （3 台以上は番号付き）。実在の型番は名乗らない。
3. **GPU 名の切れ**: `scene.c` の Graphics の card は `%.24s` で切っていた。新しい `fit_name` が card の幅に収まるかを atlas の幅で測り、全体 → 括弧の前
   （`short_name`）→ 「...」の順に縮める。
4. 確かめ: monitor の build（zedBSD -Werror）warning 0、Linux の gcc `-fsyntax-only -Werror`（変えた 4 file）、`tests/host/run.sh` → `monitor-host: PASS`、
   style-diff 0、host の preview（1200x760 と 1920x1240、sim 16 CPU・2 GPU）で「GPU 0 Virtio-GPU Venus (llvmpipe)」「GPU 1 Discrete Graphics」。
   image `build-monitor-image.sh build/p1-ws134` exit 0。QEMU は T1 に依頼（llvmpipe の image。Venus の image の計測は Venus の renderer が使える時）。


## P1 generation12 のラップアップ（2026-10-04、ユーザーの指示で P1 を終了）

q657（P1）で計測・GPU 名・名前の切れを実装（8af2512、統合 c9b09ad）、T1 に monitor-p003.sh（試験の依頼 8、image build/p1-ws134/hdd-image.img）を依頼済み。再開: T1 の `sim frame: ZMON FRAME` の段ごとの時間（acquire・present・callback のどれが重いか）と `compositor: N compose frames a second` を見て軽量化を決める。host の見積りは fragment が画面の 2.3 倍（全面の背景 1.0）。

## T1-055（Q1、2026-10-04、QEMU Venus KVM、8af2512 の image、段ごとの時間を入れた後）

uncleared のまま。2 回とも `sim: 4 fps (want 15 or more)`（replay 3 つは ok、ERROR なし）。sim の frame（ms）: build 2.2〜3.1、acquire 0.04〜0.25、record 30〜36、submit 9〜10、present 48〜50、**callback 208〜223**、wait 2〜6。`compositor: 4 compose frames a second`。monitor の device は Venus の guest なのに `llvmpipe (LLVM 19.1.7, 256 bits)`（guest に /dev/gpu0 は在る）。読み: 1 frame の大半は compositor の frame callback の待ちで、compositor 自身が毎秒 4 回しか合成していない。monitor が Venus でなく llvmpipe を選んでいる（ICD の選び方か環境）。証拠 worktrees/t1/build/t1-055/。再開: (1) monitor が Venus を使わない理由、(2) compositor の合成が 4 回/秒の理由（monitor の CPU 描画の重さで compositor が待つのか）を調べる。
