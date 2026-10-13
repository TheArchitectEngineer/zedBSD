<!-- awesome-plan project=zedbsd record=ws219-p003 -->
# ws219-p003: gpu-op.h の op ごとの詳しい説明

Status: cleared（2026-10-13 Q1: comment だけの変更、名前と値の不変を確かめた、build warning 0・host 試験 PASS。旧: 2026-10-13 P2 実施。comment の記入・build warning 0・host 試験 PASS。Q1 の判定待ち。QEMU は comment だけの変更なので依頼していない）
Parent: [WS219](../ws.md)

## 範囲（ws.md の「ユーザーの指示（2026-10-13、p002 の後）」）

ユーザー（原文）:「コメントです。gpu-op.hでは、OPごとに詳細な説明をいれてください。でないと、Venusの翻案になってしまいます。」

`include/uapi/gpu-op.h` の全部の op に、op ごとの詳しい説明の comment を書く（WS218 の AX211 の header と同じ方針）。値・名前は変えない。

## 行ったこと

### file の頭

- 「What this file is.」の段落: 番号は venus-protocol の XML registry を 1 次資料として集め、他のこと（用途、引数の順と reply の形、zedBSD の送り手と受け手、順序、送らない op）は zedBSD の実装から調べて独自に書いた。Venus の header の翻案ではなく、同じ stream を話す他の system の開発者が単独で使える参照として書いた、と英語で書いた。
- 出典の段落（p002）に、各 entry が `VK_EXT_command_serialization.xml` の行（`VK_COMMAND_TYPE_<command>_EXT`）を示すこと、0〜180 の 145 個を 1 つずつ照合したことを足した。
- 共通の事実: path の略記（libvulkan/・i915/・bcm2711/・venus/）、wire の形（little-endian、u32・u64、libvulkan が選ぶ u64 の object の identity、pointer の presence word、配列は u32 の count と u64 の要素数、structure は宣言順で sType・pNext）、command の header（[番号][flags]、reply の flag は record と transport では 0）、reply の形（番号の echo、VkResult、出力）、2 つの共通の形（create form・destroy form）、1 回の submission の枠（178・本体または 180・179・137）、各 executor の dispatch の道筋（i915 の objects → command → 範囲 → transport・instance、bcm2711 の vulkan-stream と vulkan-dispatch の route、拒否は stream を止め libvulkan は VK_ERROR_DEVICE_LOST）、VM では venus/venus.c が stream を読まずに host へ渡すこと。

### op ごと（enum の 159 個: venus-protocol の番号 145 個と zedBSD 独自の video 14 個）

各 entry に: Vulkan の command と zedBSD での用途、番号の出典の行、引数（header の後）、reply、送り手（libvulkan の file:関数と、そこへ来る公開の関数）、受け手（i915 と bcm2711 の dispatcher → handler）、必要なら順序。recorded の vkCmd*（93〜136）と video（0x10000〜）は群の頭に共通の事実（record の header、batch と flush、受け手の範囲、video の capset の bit）を書いた。`GPU_OP_FLAG_REPLY`・2 つの record の型・`GPU_OP_PROTOCOL_VERSION` にも送り手・受け手を書き足した。

調べ方: P2 の scratchpad の道具 `ws219p3/xref.py`（全 `GPU_OP_*` の使用を file:関数で列挙）、`senders.py`（libvulkan の各送り手の write/read の並び）、`cases.py`（i915・bcm2711 の switch の case → handler、i915 の数値の別名 `I915_VK_COMMAND_*`・`I915_OPCODE_*` を番号で対応）。範囲で振り分ける所（i915/dispatch.c の route、bcm2711/vulkan-query.c）は code を読んで確かめた。番号は venus-protocol の `xmls/VK_EXT_command_serialization.xml`（commit `ca19b6358d7cc491bc3e4de76f04c6700876a8fa`、P2 の worktree の `build/tmp/ws219-p003/xml/` に取得）と 145 個すべて一致（行 24〜203、553〜555）。

### zedBSD が送らない op（15）

libvulkan が送らない: 1（vkDestroyInstance は session を閉じる）、9・10（proc addr は自分の表）、13〜16（extension・layer は自分で答える）、17（queue は 155 で作成時に取る）、19・20（空の submit と private fence で待つ）、23〜26（map は共有 storage の CPU view、flush/invalidate は local）、39（kernel の job の通知で待つ）。このうち 1・19・20 は executor が解く（i915 は 19・20 に即 VK_SUCCESS、bcm2711 は 19・20 を EOPNOTSUPP）。残り 12 はどの executor も解かない。

参考: libvulkan は送るが native の executor（i915・bcm2711）のどちらも解かず、Venus host だけが答える op が 29 ある（27、32〜34、42〜46、61〜64、99、108、109、117、118、120、123〜125、130、131、134、136、148、150、161）。i915 だけが解かない物: 78・84。bcm2711 だけが解かない物は多い（query・event・buffer view・compute・index draw など、entry に記した）。code の事実として記録しただけで、直していない（範囲外）。

## 確かめ（2026-10-13、P2 の worktree `/home/awe/zedBSD-worktrees/p2`）

| コマンド | 結果 |
| --- | --- |
| 名前と値の比較（HEAD と変更後の `#define`・enum の行を抜き出して diff） | 同じ（164 行: macro 5、enum 159） |
| `make -j8 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws219-p003 vmunix build/ws219-p003/dynamic/libvulkan.so` | rc 0、warning 0、kernel include check PASS（369 objects）、amd64 vmunix check PASS |
| `make -j8 ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/ws219-p003-rpi4 vmunix` | rc 0、warning 0、arm64 vmunix check PASS（bcm2711 の vulkan-*.c を含む） |
| `sh plan/ws083/tests/run-host-libvulkan-native.sh`・`-status.sh`・`-video.sh` | PASS（plain、ASan/UBSan） |
| `sh plan/ws141/tests/vulkan-device-host-test.sh build/ws219-p003-device-host` | PASS |
| `sh plan/ws141/tests/vulkan-stream-host-test.sh build/ws219-p003-stream-host` | PASS |

未実施: QEMU・実機（comment だけの変更で、生成される code は同じ。T1 には依頼していない。p002 の T1 の依頼が残っている）。comment の行は 80 桁以内。

## 残り・再開条件

- Q1 が内容を判定したら cleared。
- 削除の依頼（Q1）: この Phase の build の出力 `build/ws219-p003/`・`build/ws219-p003-rpi4/`・`build/ws219-p003-*.log`・`build/ws219-p003-device-host/`・`build/ws219-p003-stream-host/`、取得した XML `build/tmp/ws219-p003/`、ws083 の試験が作った `build/tmp/ws083-libvulkan-{native,status,video}` と `build/tmp/ws083-libvulkan-*.run.*`（いずれも P2 の worktree の中）、scratchpad の `ws219p3/`。
