<!-- awesome-plan project=zedbsd record=ws269-p017 -->

# ws269-p017 非同期の submit（D-P4 の変更）

- 親: [WS269](../ws.md)、Status: planning（2026-10-21 P1、q1528 / q1528-i01。設計を書いた。実装は設計の確認の後）
- 起点（ユーザー 2026-10-21）: 「表示内容自体は完璧です。カーソル移動や画面遷移は、全画面書き換えは2fpsも出なかったり、カーソルも3fpsくらいだったりしてます。これは最適化できそうですか？」。D-P4（submit は ioctl の中で待つ）を Q1 が問い、ユーザーがクリックで「非同期に変える (Recommended)」を選んだ。submit は kick して戻り、完了は fence で待つ。原文は Master の decisions-log（2026-10-21）。

## D-P4 の変更

| | 前（design-p010 §9 D-P4） | 後（この Phase） |
| --- | --- | --- |
| submit | ioctl の中で各 job を kick して完了まで待つ（2 s で切る） | kick して戻る |
| 完了の知らせ | ioctl が戻った時に fence が立ち、libvulkan の marker もすぐ配られる | GPU が終えた時に marker が配られ、fence が立つ |
| 時間切れ | 2 s で ETIMEDOUT → DEVICE_LOST、session は abandoned | core の job の期限（`CONFIG_GPU_JOB_EXECUTION_MS`、60 s）と firmware の HWR。DEVICE_LOST は fence の待ちで返る |

## 今の道（読んだ事、2026-10-21）

1. **libvulkan の submit**: GPU の command（`GPU_OP_QUEUE_SUBMIT`、`vk-queue.c` の `submit`）として送られる。同じ時に libvulkan は job の marker を予約・commit する（`render-runtime.c` の `runtime_reserve`・`runtime_commit`）。
2. **marker の配り方**: commit で、session の ring（`pending`）に `session->submitted`（compute の timeline の値）と一緒に入る。`render-queue.c` の `take_reached` が compute の timeline を読んで、順に配る。
   - 今は submit が全部を待ってから戻る。なので marker の値は commit の時にはもう届いていて、すぐ配られる。
3. **fence**: `vk-queue.c` の submit の最後に `fence->signaled = true`。`GET_FENCE_STATUS` はその印を返す。
   - libvulkan の `vkWaitForFences` は marker の通知（core の completion）を待ってから status を読む（`sync.c` の `vulkan_fences_wait`）。
4. **command の追い越しの禁止**: `runtime_command` は、session の ring に配る前の callback がある間、EAGAIN を返す（frame が先の callback を追い越さない）。
   - 非同期にすると GPU の仕事の間ずっと ring に callback が残る。そのため、次の submit・fence の status・資源の作成など全部の command が EAGAIN で止まる。**この規則を変えないと非同期の効果が無い**（下の 3）。
5. **待っている所**: 次の 4 か所。
   - compute の stream（`vk-queue.c` の `run`、2 s）。
   - render の job（`render-job.c` の `powervr_render_job_run` → `powervr_render_finish`、2 s）。
   - transfer（`transfer-job.c` の `transfer_one` → `powervr_transfer_finish`、行ごとに kick と待ち。1 つの memory を行で使い回す）。
   - executor の compute の stream の buffer（session に 1 つ、submit ごとに書き直す）。

## 設計

### 1. 完了の点（context と値）

- 仕事の完了を (context の種類, timeline の値) で表す。種類は compute（`session->compute` の timeline）、fragment（render context の 3D の timeline）、transfer（transfer context の timeline）。
- session は「最後に kick した点」を持つ。
- ring の entry（`struct powervr_pending`）に種類を足す。`take_reached` は entry の種類の timeline と比べて、順に配る。
- marker の commit・frame の callback は、session の「最後に kick した点」で ring に入る。

### 2. fence

- submit の最後に、fence に「最後に kick した点」を記録する（signaled ではなく pending）。
- `GET_FENCE_STATUS` は、点に届いていれば signaled にして SUCCESS を返す。届いていなければ NOT_READY、session が lost なら DEVICE_LOST。
- reset は点を消す。

### 3. command の追い越し

- ring の entry のうち「GPU の完了を待つだけで error を持たない物」は、command の追い越しを止めない。
- error を持つ callback（失敗した frame の物）がある間だけ、今のように EAGAIN を返す。
- vk の command 自体は session の中で順に処理されるので、順序の問題は起きない（vk の command は CPU の上の object の操作と kick）。

### 4. 実行の順序（context の間）

- **同じ context の中**: firmware が CCB の順に走らせるので、CPU は待たない。
  - render の job が続く時、3D は順。次の job の TA は前の job の 3D と重なってよい（TA は colour を触らない）。
- **context が変わる所**（transfer → render、render → transfer、compute → render など）: 段階 A では、CPU が前の context の最後の点まで待ってから kick する。
  - render が直前の transfer の書いた texture を sample する、などの順序をこれで守る。
  - 段階 C で firmware の同期（CCB の fence の待ちの command）に替える。
- **submit をまたぐ時**: 次の submit の最初の仕事が前の submit の最後の仕事と別の context なら、同じく待つ。
- **executor の compute の stream の buffer**: 前の compute が終わるまで書き直さない。前の compute の点まで CPU が待つ（compute の連続は少ない）。

### 5. memory の後始末を遅らせる

- render の job の memory（`job_memory`、load の draw の写しを含む）は、job の fragment の点と一緒に session の「後始末の列」に入れる。
- 点に届いた物から、次の時に解放する。
  - 次の submit の始め
  - fence の status
  - service の pass（ring を見る時）
  - session の close（drain の後）
- transfer の memory も段階 B で同じにする。段階 A では transfer は今のまま同期（行ごとの kick が 1 つの memory を使い回すため）。

### 6. 時間切れ・lost・reset・vkreset

- submit は待たないので、終わらない仕事は ring の entry が配られないまま残る。
  - core の job の期限（60 s）で session が ETIMEDOUT になり、stop → isolate → reset の道（p015 で確かめた道）に入る。
  - firmware の HWR の context reset の通知（105）でも session は lost になる。
  - fence の status・待ちは DEVICE_LOST を返す。
- lost・isolated の session の「後始末の列」の memory は解放しない。今の abandoned と同じく core に残し、reset で解放する。
- vkreset は、submit か fence の待ちで DEVICE_LOST を期待するので変わらない。DEVICE_LOST が出るまでの時間は 2 s から最大 70 s ほどに延びる（vkreset の待ちは 120 s）。
  - firmware の HWR が先に来れば短い（板 98 は context reset の通知が 2 s で来た）。

### 7. 段階

| 段階 | 中身 | 効果の見込み |
| --- | --- | --- |
| A | render の job と compute を非同期に。fence・marker を完了の点に。command の追い越し。後始末の列。context が変わる所は CPU が待つ。transfer は同期のまま | compositor の frame の 3D（約 44 ms）を待たずに戻る。cursor の present と次の frame の準備が 3D と重なる |
| B | transfer も非同期に（kick ごとの memory、行の kick を 1 つの memory に収める） | upload の多い frame |
| C | context の間の順序を firmware の同期に替える（CPU の待ちを無くす） | transfer → render の待ち |

### 8. 試験

- **host**: p009（executor）と p010（render の job）の試験を、非同期の点（kick の後に timeline を進める stub）に合わせて直す。
  - fence の status が点に届くまで NOT_READY であること。
  - 後始末の列が点の順に解放すること。
  - context が変わる所で待つこと。
- **板**:
  - gpu-selftest 13/13、vkmatrix 20/20、vkcoherency、vkreset（DEVICE_LOST と回復）。
  - desktop の KWL PERF の submit と、cursor の fps。

## 未決（確認したい事）

- §3 の「error の無い entry は追い越しを止めない」は、core の約束（frame が callback を追い越さない）の変更。powervr の中だけで閉じるが、Q1 の確認が要る。
- §6 で DEVICE_LOST までの時間が延びる事。

## 残件

- 設計の確認の後に段階 A を実装する（vk-queue.c・render-job.c・render-queue.c・render-runtime.c、host 試験、VF2 の build で warning 0）。
