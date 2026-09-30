<!-- awesome-plan project=zedbsd record=ws075p026 -->

# ws075-p026: compositor の frame の長さの主因の分析と、縮める手の実装（C6）

Phase ID: `ws075-p026`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-30。主因は executor の同期の run（1 frame 約 58 ms、739 draw を 5〜7 run）と frame pacing の待ち（約 31 ms）。手: pointer が動いた frame は pacing を待たない。C6 は 5 run で中央値 121.1 → 92.3 ms・p90 191.7 → 140.8 ms、flip 9.5 → 14/s。ユーザーの方針「広く浅く」で計測と分析の区切りで終える）
Phase disposition: normal
承認: 2026-09-30 main の指示（p025 の後）。compositor（`userland/desktop/wayland/`）は直してよい（IME の file と seat.c の IME の hook は除く、区切りごとに main を取り込む）。

## 目的

C6（窓 10 個で pointer の移動から表示まで中央値 50 ms 以内、実機）に近づける。1 frame が約 100 ms（約 9.5 frame/s）になる主因を
切り分け、効く手を 1 つ実装し、`c6.py` で前後を比べる。

## 観測の道具（常設）

- worker（`src/drivers/gpu/i915/worker.c`）: context ごとの記録に、同期の batch（executor の submit、呼び手が眠る）の queue の待ち
  （`queue_ns`）と queue から終わりまで（`round_ns`）を足した。`plan/ws075/tests/hdmi/engine-gdb.sh` が gdb で読み、session ごとに
  run あたりの engine・queue・round と、round が時間に占める割合を出す。
- executor（`render/draw.c`・`draw.h`）: session ごとの frame の時間を 5 秒ごとに kernel log へ（毎秒 10 submit 以上の session だけ）:
  `i915: vk: session N: frames F in T ms: per submit R runs (X full) O ops (most M), in submit U us (runs, record), between B us`。
  full は slot（128）が尽きて submit の途中で走った run の数。
- compositor の `ZWL PERF`（stdout、/run/user/1000/session.log）は Terminal で home へ copy して sync し、stop の後に image から読む
  （`build/ws075-p026/zwl.sh`、keys の `;` と `\n` は ssh の shell が食うので 1 命令ずつ、Enter は `hmp sendkey ret`）。

## 切り分け（2026-09-30、実機の passthrough、10 app、p025 の後の tree + 観測）

executor（session 2 = compositor、kernel log）:

| 物差し | 値 |
| --- | --- |
| submit | 約 19/s（表示の frame 約 9.6/s の 2 倍: frame の submit と present の copy が交互） |
| frame の submit | ops 最大 739（平均 370 は copy と半々）、run 4〜7（slot が尽きて途中で走る run が frame あたり約 5） |
| submit の中 | 約 30 ms/submit（うち run 28 ms、executor の記録 2 ms）→ 表示の frame あたり約 60 ms |
| submit の間 | 約 22 ms/submit → 表示の frame あたり約 44 ms |
| engine（gdb） | 6.5〜6.8 ms/run、queue の待ち 0.5 ms/run、round 7.5 ms/run（他の session の待ちは小さい） |

compositor（ZWL PERF、10 app、5 秒の窓）:

| 物差し | 値 |
| --- | --- |
| frames | 約 51 / 5 s |
| draw_ms（frame の CPU: 記録・submit・present） | 約 62 ms（うち submit+present 58 ms、acquire 0.06 ms、記録 約 4 ms） |
| frame_ms（frame の開始から fence まで） | 約 63 ms |
| event loop | poll 31%・work 69% |

読み: 1 frame ≒ 100 ms =

1. **executor の同期の run（GPU）約 58 ms**: 739 の draw を 128 slot ごとに区切って 5〜7 回、同期に走らせる。engine の時間は draw あたり
   約 64 µs（Model viewer の小さな draw も約 47 µs）で、draw ごとの固定の費用（context setup の flush と CS stall、PIPELINE_SELECT、
   slot ごとの STATE_BASE_ADDRESS、全 cache の invalidate）が大きい。draw の数は窓の数の 2 乗で増える: 2 枚目からの窓ごとに、
   すりガラスの下の scene（壁紙と下の全ての窓）を 1/8 の大きさで描き直して 2 回 blur する（`shell.c` の draw_backdrop）。
2. **frame pacing の待ち 約 31 ms**: frame が終わると、その frame が知らせた窓（Gears など絶えず描く client）が commit するのを
   前の frame の時間の半分（最大 50 ms）まで待つ（`display.c`、`frame_wait_ms`）。63 ms の frame では約 31 ms。pointer が動いても待つ。
3. 残り約 10 ms: event loop の他の仕事（shm の copy、client の要求）。

C6 は、動いた時にどの phase にいるかで決まる: 描いている途中（約 63%）なら残りの描画 + pacing の待ち + 次の frame、待ちの途中なら
待ちの残り + 次の frame。中央値 約 115〜125 ms と合う。

## 手: pointer が動いた frame は pacing を待たない（compositor、commit 3b53ae5d）

- `zwl.h`: `pointer_moved`。`damage.c` の zwl_damage_pointer（mouse・touch・tablet の動きの共通の入口）で立て、`compose.c` の frame の
  開始で下ろす。`display.c`: pacing の待ちは `pointer_moved` の間は飛ぶ。
- 絵は変わらない（frame を始める時期だけ）。Gears など動く client の間でも、pointer の動きの後の frame は待たずに始まる。
- 触れた file: compose.c・damage.c・display.c・zwl.h（IME の file と seat.c には触れていない）。

## 検証（2026-09-30、実機の passthrough、5330 の QEMU の VFIO passthrough）

image は同じ tree（main を取り込んだ 846af2b3）で、compositor の変更の有無だけ違う 2 つ（`build/ws075-p026/{base,pace}.img`）。
base 1 → pace 1 → base 2 → pace 2 → base 3 → pace 3 → pace 4 → pace 5。

| run | 10 app: flip の率 | compositor: 占有・1 run・run/s | C6: 中央値・p90（40 試行） |
| --- | --- | --- | --- |
| base 1 | 9.4/s | 44.4%・6.65 ms・66.8 | 114.3・166.5 ms |
| base 2 | 9.2/s | 44.6%・6.63 ms・67.2 | 115.0・213.0 ms |
| base 3 | 9.9/s | 43.8%・6.87 ms・63.8 | 124.4・184.5 ms |
| pace 1 | 11.8/s | 64.0%・6.33 ms・101.1 | 94.0・132.3 ms |
| pace 2 | 14.4/s | 64.5%・6.51 ms・99.1 | 85.3・140.2 ms |
| pace 3 | 14.3/s | 63.8%・6.42 ms・99.3 | 87.2・134.4 ms |
| pace 4 | 14.5/s | 64.1%・6.47 ms・99.1 | 92.1・145.3 ms |
| pace 5 | 14.2/s | 62.8%・6.47 ms・97.0 | 90.1・129.6 ms |

（summarize の C6 の列は nearest-rank、下の c6.py の中央値が正）

- `c6.py`: base（3 run、120 試料）中央値 121.1 ms・p90 191.7 ms・run の中央値 116.8〜125.0 ms。pace（5 run、200 試料）中央値
  **92.3 ms**・p90 **140.8 ms**・run の中央値 89.2〜97.0 ms（標準偏差 3.4 ms）。C6（50 ms）は満たさない。段の L1（100 ms）は満たす。
- engine の占有 53% → 68%（frame が増えた分）。compositor の 1 run の engine の時間は同じ（6.3〜6.9 ms）。
- 画面: base 1 と pace 1 の各 app の撮影は上の bar の下で画素まで同じ（Notes の file 名の時刻と Gears の animation を除く）。
- draw の拒否・set の消失（BUG-117 の形）: 8 run で 0。
- test-hw（この tree）: vkx 9/9・vke1 6/6・vke2 17/17・vkc 9/9 PASS。QEMU の boot test（pace.img の複写）PASS。
- host の vk の全 10 fixture PASS（観測の追加の後）。style-check: 変えた file に新しい指摘 0。
- 素の 5330: 未実施。stress-117 の 100 回をこの image では回していない（L1 の確認に残す、下の段の計画）。

## 残り（段の計画へ）

frame の中の GPU の時間（約 58 ms）が次の主因。draw の数は窓の数の 2 乗で増える（窓ごとの backdrop の描き直し）。段の計画は
[ws.md](../ws.md) の「段の計画」の節。
