<!-- awesome-plan project=zedbsd record=ws309-p011 -->

# ws309-p011 手書きの照合の pool（P1、N = min(max(CPU−1,1),4)）

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §3.4（pool）・§4.9（手書きの照合）。Queue: q1662-i08（P1）。
- 状態: cleared（2026-10-29 14:2x P1 q1662-i08、branch `agent/p1-ws309h`。host `hand-pool/run.sh`・QEMU `hand-guest.sh` PASS。実機・FreeBSD の build は未実施）。
- 依存: p002（cleared）。

## 実装（keyboard-hand.c・keyboard.c・keyboard.h・kwl.h・main.c）

- template の loader の thread（`hand_loader`、その都度）を取り除き、lane の pool `hand_pool`（`kwl_lane_start(&hand_pool, "hand", N, wake)`、名前 hand-1〜hand-N）に置き換えた。
  N は `hand_pool_size`: CPU の数 − 1（1 以上）、上限 4（`HAND_POOL_MAX`）。手書きの face を初めて出した時（`kwl_hand_preload(result, server->wake[1])`）に起動し、
  `KWL OSK hand pool threads=N` と `KWL THREADS` を出す。終了は service_cleanup の input の後に `kwl_keyboard_hand_stop`。
- template: `hand_templates_ready`（`hand_lock`・`hand_read` の cond・`hand_reading`）。最初に要った thread が lock の外で読み、他は cond で待つ。読んだ後は読むだけ。
  preload は template だけの job（key 2）を積んで先に読ませる。失敗（-1）は今どおり再び読まず note。
- 照合: `keyboard_hand_recognize` は ink の写し（job、ink 約 131 KB と thread ごとの点の部屋）と世代（`keyboard_ink_generation`、stroke の始まり・ink の消去で増える）を
  `kwl_hand_submit` で積む（key 1: 未着手の古い ink は取り消し）。main は毎 pass（main.c の `kwl_glass_blur_take` の後）`kwl_keyboard_hand_take` で答えを取り、
  今の世代の物だけを `keyboard.result` に入れて `KWL OSK hand recognize ...`（行の形は今どおり）を出す。古い世代は `KWL OSK hand stale generation= now=` で捨てる。
- pool が無い時（起動の失敗）・job の確保の失敗の時は今の main の経路（`kwl_hand_recognize_on`、main の部屋 `hand_main_scratch`）。使われていない `kwl_hand_load` は API から外した。

## 確かめ

- build: amd64 `make ZEDBSD_CONFIG=plan/ws309/tests/config-amd64-p1.mk build/amd64/bin/wayland` exit 0・warning 0。arm64 `config/ci/config-rpi5.mk` の build/rpi5/bin/wayland・riscv64 `config/ci/config-visionfive2-keiland.mk` の build/visionfive2/bin/wayland（この worktree で sysroot-arm64・sysroot-riscv64 を作って）exit 0・warning 0。Linux `make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws309/keiland-linux all`（-Werror）exit 0。
- host: 新しい `plan/ws309/tests/hand-pool/run.sh`（keyboard-hand.c・hand-cloud.c・lane.c・threads.c、ASan+UBSan と TSan）PASS 両方: pool 4 本（host）、template は 1 回だけ読まれ、
  答えの世代、pool の答え = main の答え、28 件を続けて積むと古い物が取り消され（答え 5 件）最新が最後、stop で終わる。
- QEMU の試験 `plan/ws309/tests/hand-guest.sh OUT IMAGE WAYLAND`。判定の条件（走らせる前に決めた物、2026-10-29 14:0x）: READY、`KWL OSK hand pool threads=N`（1〜4）と
  その後の `KWL THREADS` に hand-1〜hand-N、template の行が 1 つ（count=228 error=0）、3 画と 4 画の `KWL OSK hand recognize strokes=3|4 ... candidates=[1-4] first=... note=`（note 無し）、
  5 画目の後の clear で strokes=5 の照合が無い、`KWL OSK hand clear`、KWL FAILED・VULKAN_ERROR 無し。pool の本数・stale の数・pass_max_us は表示だけ。
- QEMU Venus（image `build/ws309/img`、wayland `build/amd64/bin/wayland`）: 1 回目 `build/ws309/p011a` は guest が SSH に 120 s で答えず（script の wait の上限が grave-guest の 300 s より短かった。
  host の load average 11〜15。wayland を入れる前なので compositor と無関係）。wait の上限を 300 s に（判定の条件ではない。resize-guest.sh も同じ）して 2 回目 `build/ws309/p011b` **PASS**:
  `KWL OSK hand pool threads=3`（guest は 4 CPU）、`KWL THREADS` に hand-1〜hand-3、template の行 1 つ、3 画 `candidates=4 first=- note=`、4 画 `candidates=4 first=- note=`、
  clear の後に 5 画の照合なし、stale 0、pass_max_us（表示だけ）2501621 は起動を含む最初の区間、次の区間は 18360。
  その後に `kwl_hand_preload` の `state == 1 || !hand_pool_running` を 2 つの if に割った（同じ動き、規約）。amd64 の build と host の試験を流し直して PASS、QEMU は流し直していない。
