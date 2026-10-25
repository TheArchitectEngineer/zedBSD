<!-- awesome-plan project=zedbsd record=ws288-p019 -->

# ws288-p019 zedBSD の `vk/`（Vulkan の executor = 検証する中継）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §6.7（D/R/R'/X、wire の reader、返事と reply thread、session）、§2.4.4（gpu-op の表）、§6.4（drv_gpu との結び: capset・jobs・command）、§8 の p019 の行。
- 状態: cleared（2026-10-25 10:00 Q1。firmware と RTL の上の確かめは p011、実機は p028）
- 依存: p006（driver、cleared: capset.c・jobs.c は build と host 試験だけで未結線）、p010（compiler、cleared）、p017・p018（firmware の `GHCI_*` の形。p018 は P2 が並行で実装中: GHCI_HEAP 0x10108、record の時の µop、`include/ghci_layout.h`、scratch の kind 14）。
- 承認: 2026-10-25 ユーザー「…そのあと、p010,p018,p019,p006,p011を実装します。…」「ゴールまで推しで進んでよいです。判断に迷ったら、Vulkan 1.0の原則論で進めてください。…」

## 範囲（design §8 の p019 の行、ZYBO を除く）

- `src/drivers/gpu/ghci/vk/`: codec の生成（`ghci_vkwire.inc`、firmware と共有）、wire の reader（i915 の `i915_wire_reader` を powervr と同じ形で再利用）、D/R/R'/X の表、driver の帳簿、D の返事（capset・limit・memory requirements・`ghci_layout.h` の写し・fence）、R' の書き換え（`GHCI_MEMORY`、pipeline の作成で stage ごとに compile → `GHCI_PROGRAM`、scratch の kind 14 を含む）、staging と `GHCI_FRAME`、result page と reply thread、jobs の marker、`EXECUTE_STREAMS` の入れ子、session の open/close（`GHCI_HEAP` で heap を貸す）。
- p006 の残り: capset・COMMAND・JOB・NOTIFICATION・SHARE の capability の結線（executor ができたので出す）。
- limit と feature の表は design §15（MSAA）・§16（p030）・p010 の決定（U49 の任意の feature、fragmentStoresAndAtomics など）と GHCI の FMT capability に合わせる。
- host の試験: gpu-op の frame を作って staging と ring の packet の列と返事の blob を比べる。
- ZYBO の実 driver の試験は対象外（ZYBO は外した）。Verilator の端から端は p011、実機は p028。

## 受け入れ

host の試験が PASS（staging の列と返事の blob の形が libvulkan の読む形）、amd64 の vmunix（`plan/ws288/tests/config-amd64-ghci.mk`）が warning 0、`plan/coding-style.md` の全文に合う。

## 記録

- 2026-10-25 08:46 Q1: 新設、P3 に（q1569）。
- 2026-10-25 08:59 Q1: 範囲に追加（ユーザーの問い「scanoutはバッファをコピーせずにレンダリングターゲットをそのまま出力できますか？」への Q1 の答えから）: **copy しない flip**。swapchain の image を DC の条件（BGRA8・RGBA8・RGB565、1 sample、base と行の揃え、同じ大きさ・stride・format）に合わせて driver が確保し、present は FB_NEXT に address を書いて FLIP_DONE を待つ。合わない buffer だけ今の BLT の copy。DC の割り込みが PCIe の MSI に届くかを GHCI §13・§15.2 に書く。
- 2026-10-25 09:02 Q1: 「copy しない flip」の段は p035（DC を GHCI に統合、present は queue の操作）に置き換えた。p019 では今の display.c の copy の present のまま。
- 2026-10-25 09:58 P3（q1569-i01、worktree p3-p006、基点 d34c3228d → 4dbc9ac6f）: 実装・build・host 試験。段 1 c705971a5（vk/ 一式と driver の結線）、段 2 ea6bbc5f9（host 試験）、段 3 0b74cae78（SHARE）、c1b97a9d5（規約の直し）、14785db68・289f7cf06・4dbc9ac6f（records の無い frame の解放と query の写し、試験の追加）。判定は Q1。

## 結果（2026-10-25 P3）

### 作った物

- `src/drivers/gpu/ghci/vk/`（9 file と 3 header、約 8.7k 行）: `vk-engine.c`（firmware の heap の貸し出し `GHCI_HEAP [addr][bytes][RESIDENT_WARPS]`、制御の packet を送って fence まで待つ道、staging の環 4 MiB、device 全体の frame の列、service の thread での返事の完成・trailer・`drv_gpu_complete`、reset の後の frame の失敗と session の lost と heap の再送、job の marker の退役）、`vk-node.c`（capset id 4・176 byte、`commands.submit/drain`、`jobs.reserve/commit/cancel/capacity`、`recovery.fault`）、`vk-session.c`（open で `GHCI_SESSION_OPEN`、close で `GHCI_SESSION_CLOSE`、object の帳簿）、`vk-stream.c`（frame の decode、178・179・137・180、record `[number][flags][index][args]` の staging、`GHCI_FRAME`、返事の VkResult の後埋め、fence と解放の後処理）、`vk-device.c`（D: instance・physical・features・limits・format・memory・queue family・device・queue2・properties2・external の問い）、`vk-format.c`（FMT capability（id 10）を start で全 VkFormat 読む。無い GPU は RGBA8/BGRA8/D24S8/D32F の最小の表。storage は shader の道として driver が足す）、`vk-memory.c`（R': `ALLOCATE_MEMORY` → `GHCI_MEMORY`、import、free の遅延解放、buffer/image の要求と subresource layout（`layout.h`）、fence（D）、event と query の host 側（heap の中の address を firmware の aux から））、`vk-program.c`（module は D、pipeline cache は空、pipeline は stage ごとに compile → GPU memory → `GHCI_PROGRAM` → create を中継。fragment を先に compile して w の varying が要るか見る。specialization は OpSpecConstant* を定数に書き換えてから compile）、`vk-relay.c`（R: 残りの object・command buffer・記録 93〜136・`QUEUE_SUBMIT`。i915 の生成 codec で解いて長さを決める）。
- `share.c`（SHARE と ALLOCATION_SHARE: export は memory を hold する record、import は同じ memory を見せる blob）。`ghci.c`（vk の起動・結線、blob_id ≠ 0 の blob = Vulkan memory を見せる alias、GPU_COMMAND は native だけ）、`cp.c`（RESULT の IRQ、service の vk の処理、reset の後の vk_reset）、`memory.c`（hold/unhold）、`ghci.h`。
- header の写し: `frame.h`・`packets.h`・`hcr.h`（amaterrace-gpu p2-p018 2e012f9、`GHCI_RESIDENT_WARPS` 0x04C を含む）、`layout.h`（新規の写し）。UAPI: `GPU_OP_GHCI_HEAP` 0x10108。build: `platform/amd64/vmunix.mk` に vk/ と share.c。
- 試験: `plan/ws288/tests/ghci-vk-host-test.c`（`ghci-driver-host-test.c` の model を `GHCI_MODEL_ONLY` で取り込み、model の CP が firmware の代わり: 制御の gpu-op は成功、frame は record ごとの result と frame の result を書いて RESULT）。`ghci-host-test.sh` が 3 つを流す。

### 自分で決めた事（Vulkan 1.0 の原則と GHCI の文面で）

1. 長さを決める codec は新しい `ghci_vkwire.inc` を作らず、i915 の生成 codec（libvulkan の encoder の鏡）を使った。firmware（p018）は自前の decoder を持っており、共有の表は無い。
2. timeline: libvulkan は queue ごとに timeline 1〜63 を取って `GET_DEVICE_QUEUE2` で送る（design §2.4.4 の「0 だけ」とは違う）。driver は session の timeline として受け、1 本の ring の順で全部を完了する。
3. design の X の表のうち event・query・indirect・secondary・複数 subpass は firmware（p018）が実装しているので中継する（Vulkan 1.0 core）。X は sparse・video・送られない番号だけ。
4. specialization constant は driver が SPIR-V の写しで OpSpecConstantTrue/False/OpSpecConstant/OpSpecConstantComposite を定数にしてから compile（共有の frontend に spec constant が無い）。OpSpecConstantOp は frontend が拒む。
5. compile の register 数は 0（= 32）: GHCI に register 数の標準の field が無い。KC705（64）では保守的。
6. w の varying は varying の最後の word。fragment が要る時だけ vertex に出させる。
7. 返事の reply thread は既存の service thread に `GHCI_WORK_REPLY` で相乗り。frame は device 全体で ring の順に完成する。
8. pipeline の compile が 1 つでも失敗したら command を中継せず、全部 null の reply（`VK_ERROR_INITIALIZATION_FAILED`）。
9. vendorID・deviceID は GHCI の VENDOR_ID・DEVICE_ID の FourCC（PCI の ID ではない）。device UUID は "ghci" + FourCC + 版。
10. memory type は 2 つ（device local / device local + host visible + coherent）、heap は窓と窓の外。GPU memory への CPU の書き込みは窓（uncached）なので coherent。
11. 外部 memory の問い（150・161）は「機能なし」を答える（SHARE は同じ device の session の間だけ。他の device との共有（CROSS_DEVICE）は ENOTSUP）。
12. recovery は fault だけ（stop_begin/poll・isolate・reset は無し）。fault の後は新しい frame を取らない（再 attach まで）。

### 確認（自分で実行、件数は出力で数えた値）

- host: `plan/ws288/tests/ghci-host-test.sh` → `ghci-host-test: 126 passed, 0 failed`、`ghci-driver-host-test: 76 passed, 0 failed`、`ghci-vk-host-test: 137 passed, 0 failed`、PASS（ASan・UBSan、`-Wall -Wextra -Werror`）。
- build（名指しの target）: `make -j16 ZEDBSD_CONFIG=plan/ws288/tests/config-amd64-ghci.mk BUILD=build/p019 build/p019/vmunix` exit 0、warning 0、`kernel include check: PASS`・`amd64 vmunix check: PASS`。既定の `config/ci/config-amd64.mk`（`BUILD=build/p019-ci`）も exit 0・warning 0・2 つの check PASS。
- **未実施**: QEMU・VFIO・板（p028）、Verilator の端から端（p011）、本物の firmware（p018）との突き合わせ（model は record の中身を読まない）、libvulkan を実 driver で通す試験。

### 残件と再開点

- 規約の全文の見直しは自分で 1 回（paragraph の comment、閉じ括弧の後の空行、複合条件、呼び出しを条件に入れない、public の順）。正式な見直しは p016。
- design §6.7 の「知らない sType の chain を拒む」: 生成 codec は pNext の presence word を読み飛ばす（libvulkan は chain を付けない）。chain の付いた wire は長さを誤るが、firmware の session（tag）の中で閉じる。
- display の present は今の copy の形のまま（copy しない flip と DC の割り込みは p035）。
- 削除の依頼（Q1 の pipeline で）: worktree の `build/p019`・`build/p019-ci`・`build/tmp`・`build/p019-make.log`・`build/p019-ci.log`、symlink `build/llvm`・`build/NoctLang`。
- 2026-10-25 10:00 Q1: agent/p3-p006 7f11e55d4 を main に merge（衝突 1: この phase.md、両方を残した）。merge の後 `plan/ws288/tests/ghci-host-test.sh` 126・76・137 passed 0 failed、`config-amd64-ghci.mk` の vmunix exit 0・warning 0・vmunix check PASS（Q1 が流した）。P3 の build の残りを Q1 が消した。
  - Q1 の判断: cleared（受け入れの host 試験・vmunix・規約）。P3 の決定を受け入れる（i915 の codec の再利用、queue ごとの timeline、event・query・indirect・secondary・複数の subpass の中継、spec constant の driver での書き換え、vendorID・deviceID は GHCI の FourCC）。
  - 判断（Vulkan 1.0 の原則・GHCI の汎用の原則）: lane あたりの register の数を GHCI の標準の field に足す（RESIDENT_WARPS の [23:16] などの形で、次に amaterrace-gpu の RTL を触る担当に）。未知の pNext の鎖は読み飛ばす形を受け入れる（libvulkan は送らない、影響はその session だけ）。
