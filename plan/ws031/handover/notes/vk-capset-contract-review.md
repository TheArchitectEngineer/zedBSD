# レビュー用: i915 の Vulkan capset と STRICT_QUEUE／QUIESCE の契約（案）

WS031 E-110（2026-09-19）。**コード適用前のレビュー資料**。ここに書いた意味が確定するまで、i915 の capset に vendor suffix と flags は立てない。LCD-B（非公開 device）の進行とは独立。
根拠の所在: `handover/notes/gpu-job-completion-contract.md`（GPU core と libvulkan の完了契約の調査）、`handover/notes/vkdemo-dependency-table.md` §5（libvulkan の node 受入条件）。path は `agent-1:~/zedBSD/` 基点。

## 0. 何が未確定か
- flags の意味を述べる文書は libvulkan の comment 2 文だけ（`userland/base/libvulkan/context.c:176-184`）:
  STRICT_QUEUE =「Device creation additionally requires success-only completion of its exact native fence.」
  QUIESCE =「Only this exact profile can retire raw native work without failing unrelated sessions.」
- 168 byte の vendor capset を**書く側は tree に存在しない**（Venus backend は host の値を転送、i915 は 156 byte）。
- したがって下の表は「libvulkan の検査と使い方から逆算した案」で、プロジェクトとしての決定ではない。**名前から意味を推測して実装しない**ために、先に決めてもらう項目を並べる。

## 1. capset の形式（現行 libvulkan の検査との対応）
| offset | 内容 | libvulkan の検査（`context.c`） | i915 の現状（`vk/vk.c:165-180`） |
|---|---|---|---|
| +0 u32 | wire format version | == 1（:150-158） | 1 |
| +4 u32 | VK XML version | == VK_MAKE_VERSION(1,3,269) = 0x0040310D | 0 |
| +8 … +151 | （Venus capset の既存 field。拡張 mask など） | この調査では個別 field の検査箇所を未確認 | 0 |
| +152 u32 | timeline 数 | != 0 | 0 |
| 全長 | | 156〜256 byte を受理。**vendor suffix は全長がちょうど 168 のときだけ読む**（:165-168） | 156 |
| +160 u32 | vendor magic | == 0x5a424453 | 無し |
| +164 u32 | flags: OPAQUE=1、STRICT_QUEUE=2、QUIESCE=4 | **値 1／3／7 だけを認識**（STRICT_QUEUE は OPAQUE と同時でないと無視される。comment に記載なし） | 無し |
| node 採用 | | strict_queue && native_quiescence && GPU_CAP_JOB && GPU_CAP_JOB_CAPACITY が揃わない node は列挙から落とす（`instance.c:654-656`）、`vkCreateDevice` でも再検査（`device.c:278-280`）→ 実質 flags=7 が必要 | — |

**決めてほしいこと（形式）**: (a) +8〜+151 のうち i915 が意味を持って埋めるべき field と、0 のままでよい field。(b) version の互換性規則（wire version 1 のまま suffix の意味を変えてよいか、変えるなら magic か version のどちらを上げるか）。(c) timeline 数の意味（i915 は RCS0／BCS0 の 2 engine。`vkGetDeviceQueue2`(155) が運ぶ timeline index と engine の対応。調査では「timeline 0 を helper は受けるが job ops は EINVAL」という不整合もある）。

## 2. STRICT_QUEUE で決める内容（案と、決めてほしい点）
| 項目 | libvulkan と core から読み取れること | 決めてほしい点 |
|---|---|---|
| 順序を保証する単位 | libvulkan は `vkQueueSubmit` ごとに RESERVE（timeline つき）→ stream 送信 → COMMIT。core の job は session の timeline（= backend の完了 domain）に属する | 「同じ timeline の job は commit 順に完了が観測される」でよいか。timeline をまたぐ順序は保証しない、でよいか |
| submit の受付時点と完了時点 | 受付 = COMMIT が 0 を返した時点。完了 = `drv_gpu_complete()`。core は **commit されていない job の成功を拒否**（EIO に変える） | 「成功の完了通知は、その job に属する GPU 仕事が全部実際に終わった後だけ」を STRICT_QUEUE の中身とするか（comment の "success-only completion of its exact native fence" の解釈）。**legacy i915 の「decode 時点で成功通知」「batch を持たない marker だけが GPU 完了の証明」はこの意味を満たさない** |
| 複数 request への展開 | 1 回の `vkQueueSubmit` が複数の GPU request（command buffer ごと、copy engine と render engine）に展開され得る | job の完了 = 展開した**全 request** の完了、でよいか。engine をまたぐ場合の順序づけ（marker を最後の request の後ろに置く／request 自体に completion を付ける）をどちらにするか |
| 失敗時に後続をどう扱うか | core: 失敗は `status=正の errno` で WAIT を戻し、fence は error つき signal、資源は reset まで保持。libvulkan: 0 以外は全部 `VK_ERROR_DEVICE_LOST`（context に sticky） | 失敗した job の**後ろに既に commit 済みの job** は、(1) 実行せず失敗で終了、(2) 実行して個別に結果、のどちらか。libvulkan が DEVICE_LOST を sticky にする以上 (1) が自然だが、決定が必要 |

## 3. QUIESCE で決める内容
| 項目 | 読み取れること | 決めてほしい点 |
|---|---|---|
| 対象の範囲 | comment は "without failing unrelated sessions"。core の recovery ops は `stop_begin／stop_poll／fault／reset／isolate` で、session 単位の隔離（isolate）と device 単位の reset を区別している | QUIESCE が保証するのは **session 単位**（その session の native work だけを退役し、他 session は生かす）でよいか |
| 返却時に保証すること | libvulkan は `vkQueueWaitIdle`／`vkDeviceWaitIdle` を「空の submit＋WAIT」で実装し、19／20 の opcode は送らない | 「stop／isolate が戻った時点で」①その session の GPU 利用が止まっている、②その session の完了 callback がもう呼ばれない、③その session の資源（GEM object、GGTT／PPGTT の mapping）を解放してよい、のどこまでを保証とするか |
| hang したときの扱い | parity には engine reset と context の ban 相当の経路がある（`eu_hang_dump_reset` は試験用）。legacy i915 は quarantine → `reset` まで資源保持 | 他 session を壊さない退役を engine reset＋当該 context の失敗で実現する、でよいか。engine reset が効かず device reset が必要になった場合は QUIESCE の約束を破るので、そのとき何を報告するか（全 session に session error か、device error か） |
| `RETAINED` | 調査で、`cancel(fault=1)` が `pending_requests` を減らさず `stop_poll` が core の期限切れまで EAGAIN になる経路が見つかっている（意図か欠落か不明） | parity へ移すときの扱い（期限つき escalation を仕様とするか、退役経路を足すか） |

## 4. 完了処理で維持する区別（E-109 で確認済み、変更提案なし）
- ioctl の返却と job の status は別: `GPU_COMMAND_WAIT` が 0 を返しても `status != 0` なら GPU 仕事の成功ではない。
- 未完了／成功／失敗として終了／取消として終了 の四つ。通常 cancel は callback を外して完了を作らない（fence は unsignal のまま unbind）。
- **legacy 側の decode 時点での成功通知は、parity への接続に持ち込まない**。VK batch の request 自体に completion を付ける。
- 新しい UAPI や Linux の構造体は導入しない（既存 GPU core と libvulkan の契約に合わせる）。

## 5. 照合に使える資料
- 既に動く backend（Venus）から取得できる capset の bytes は形式の照合資料になる（host／QEMU 供給の値）。ただし **bytes が一致しても契約の意味は確定しない**ので、§2・§3 の決定は別に必要。
- opcode 180（大きい stream の間接渡し）は初回 vkdemo では使わない見込み（size からの導出で、実 stream は未採取）。未対応時の拒否は維持している。

## 6. 決定後に行う実装（参考、今回はしない）
parity の request／engine／reset の上に `jobs->{reserve,commit,cancel,capacity}` と `recovery->*` を載せ、§2・§3 の意味を満たす試験（成功は実完了後だけ／失敗で待機者が戻る／取消は完了を作らない／退役が他 session を壊さない）が通ってから、capset を 168 byte にして flags を立てる。
