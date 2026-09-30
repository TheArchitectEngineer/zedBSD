<!-- awesome-plan project=zedbsd record=ws103 -->

# WS103: compositor を libvulkan だけにする（GPU の UAPI の直の ioctl を無くす）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-01）
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2
Parent: [Master](../master.md)
Queue: なし（最後は q514）
Resume point: 完了。Linux・FreeBSD の backend は [F-065](../future/F-065-keiland-portable.md)
<!-- awesome-plan-current:end -->

## 目標（2026-09-30 ユーザー）

ユーザー:「私はKeilandコンポジターがlibvulkanのみを使用していると思っていたのですが、ioctlを使ってしまっているのですか？」→ Q1 が残っている直の ioctl を
説明し、「規則にして今移す」を選んだ。規則は [Guardrail](../guardrail.md)。同日 夜にユーザーが最優先の WS にし、「ws103完了まで自走してください。phaseごとにコミットしてください。」で p005〜p007 を自走した。

Linux・FreeBSD の構成（2026-09-30 ユーザーの決定）は [F-065](../future/F-065-keiland-portable.md): app は我々の WSI を持つ libvulkan を使い後段の system の libvulkan に chain する。
client と compositor の間は我々の protocol 1 本で、運ぶ中身だけが OS で変わる。WS103 はその compositor の側の OS の境界を作った（Linux・FreeBSD の backend は作らない、D1）。

## 決定（2026-09-30 夜 ユーザー）

| # | 判断 | 決定 |
| --- | --- | --- |
| D1 | 範囲 | 案 A: zedBSD の上で GPU の直の ioctl を全て消し、buffer・fence の OS 固有の部分を OS の backend の境界の後ろに閉じる。Linux・FreeBSD の backend は作らない |
| D2 | `--direct` と Vulkan が開けないときに落ちる道 | 消す（Vulkan が開けなければ起動の失敗） |
| D3 | buffer の記述の安全の確かめ | libvulkan の中へ。規格の戻り値の制約から、dedicated の import（`vkAllocateMemory`）で照らし、bind でも守る形に具体化した（design §2.3） |
| — | 達成基準 V1・V4 の言い回し | 2026-09-30 夜 ユーザー「書き直してOKです」: macro でなく OS ごとの module、V4 は前後の比較 |

## 結果

| 基準 | 判定 | 証拠 |
| --- | --- | --- |
| **V1** GPU の直の ioctl を Vulkan へ、OS 固有は module に、compositor は GPU の fd を持たない | 満たす | `plan/tools/gpu-boundary/v1-check.sh` PASS: GPU の UAPI を include するのは `gpu-zedbsd.c` だけ、GPU の ioctl は 0、GPU の UAPI の header を `#error` にして compositor の 51 個の source が compile できる（対照: `gpu-zedbsd.c` は失敗する）、`/dev/gpu` と `--gpu` は無い。evdev の ioctl は対象の外 |
| **V2** 起動・login・Log Out・Shut Down、app と窓の操作、全画面、keyboard が今と同じ | 満たす（QEMU・5330 の passthrough） | QEMU の Venus: WS099 の C1（2 本）・C2・C9 の 10 本（p052・p053・p072・p076・p126・p128・p134・p137・p138・cursor-owner）・Notes の右上の swipe・boot test。5330 の passthrough: 起動・login・10 個の app・App Home と Wiseview の開閉（`c5-hw.sh`、各 Phase で PASS）。**5330 を USB から単独で起動する実機の確かめは未実施** |
| **V3** buffer の記述と実物の一致が libvulkan の側で保たれる | 満たす | libvulkan の dedicated の import の照合（`dedicated.c`、host の試験 18 件）、compositor の wire の値の確かめ（`gpu-zedbsd.c`、host の試験 17 件）、偽の buffer（allocation の capability を image として送る）を断り compositor は動き続ける（`forge-guest.sh`、QEMU の Venus）。本物の image の capability に偽の記述を付ける端から端までの試験は、image の capability を公開の API で作れないため無い（host の試験で確かめた） |
| **V4** 性能が落ちない | 満たす | QEMU の Venus の app の起動から最初の画像: Model viewer 前 4015〜4241 ms・後 3905〜4008 ms、Files 前 2111〜2272・後 2080〜2187 ms（最初の計測で見えた約 130 ms の遅れは、libvulkan が image の問い合わせの答えを覚える形にして解消）。wltest の 600 present の時間は変わらない。5330 の passthrough: frame の率（60/s・窓 10 個 22〜23/s）は同じ、C6 は試料 200 個ずつで中央値 前 56.4 ms・後 58.8 ms、Mann-Whitney z = −0.82（ばらつきの内） |

変わった所（要点）:

- compositor（`userland/desktop/wayland/`）: 起動の問い合わせは VK_KHR_display（`compose_display`・`compose_refresh`、wl_output の refresh も Vulkan の mode から）。`--direct` と
  表示の claim・present・release の削除。client の buffer は OS の backend（`gpu-zedbsd.c`・`zwl-gpu.h`）で wire の値を確かめてから dedicated の import。fence は poll だけ。
  `/dev/gpu0`・`--gpu`・GPU の UAPI の include を削除。`--log-frames` に `ZWL ACQUIRE_FENCE`、`ZWL DISPLAY` の行、`ZWL IMPORT` の行の形の変更、`ZWL PERF` から直の present の計数を削除。
- libvulkan: `VK_KHR_get_memory_requirements2`・`VK_KHR_dedicated_allocation`（公開の header は pinned の宣言から道具で生成、command 170 → 173）、image の capability の
  dedicated の import の照合と bind の守り、dedicated の無い import と allocation の capability の dedicated の import を拒む、WSI は Wayland の present ごとに新しい fence
  （slot の `sent`）、新しい fence の最初の submit の host の reset を省く、image の memory の要求と色の配置の答えを覚える。
- kernel・HAL・toolchain・protocol は変えていない。

## 制限と移管

- Linux・FreeBSD の backend（dma-buf・modifier・sync_file、KMS の画面の出力、後段の libvulkan への chain）は [F-065](../future/F-065-keiland-portable.md)。
- 5330 を USB から単独で起動する実機の確かめは未実施（passthrough の証拠だけ）。
- 本物の image の capability に偽の記述を付ける端から端までの試験は無い（上の V3）。bind の守り（別の image の bind を拒む）は正しい利用では通らない道で、端から端では試していない。
- p002 の途中で見つけた範囲の外の件: `plan/ws014/tests/wayland-qemu.py` の直の表示の試験（`ZWL PRESENT` を読む）は直の道の削除で使えなくなった（WS014 の file、変えていない）。
  toolchain の lock と libcxx の複写は [BUG-126](../bugs/BUG-126.md)。
- 試験は `plan/tools/gpu-boundary/` に移した（master の Tools 節）。設計は [design.md](design.md)（改訂 3、design-reviewer 2 回）。Phase の directory は削除した（git の履歴に残る）。

## Phase

| Phase | 目的 | 結果 | Queue |
| --- | --- | --- | --- |
| ws103-p001 | 調査と設計（design.md） | cleared | q508 |
| ws103-p002 | 起動の問い合わせを VK_KHR_display へ、`--direct` の削除 | cleared（attempt は uncleared: 5330 の smoke の失敗は試験の image の `login=graphical` の重複と特定、既定の boot の行で PASS、ユーザーの指示で COM1 の mirror を解析に使った） | q509 |
| ws103-p003 | libvulkan: dedicated allocation と import の記述の照合、bind の守り | cleared | q510 |
| ws103-p004 | compositor の dedicated の import、`RESOURCE_IMPORT`・`DESTROY` の削除、OS の backend、allocation の capability の穴の修正、偽の buffer の probe | cleared | q511 |
| ws103-p005 | WSI が Wayland の present ごとに新しい fence を送る | cleared | q512 |
| ws103-p006 | fence を poll だけに、GPU の fd と UAPI を無くす、V1 の確かめ | cleared | q513 |
| ws103-p007 | 規約の全文の見直し、回帰、V4（問い合わせの答えを覚える対策） | cleared | q514 |
