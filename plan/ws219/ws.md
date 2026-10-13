<!-- awesome-plan project=zedbsd record=ws219 -->

# WS219: Venus の protocol の op の流用の整理（定数名の付け直し、Venus の著作権表示を外し、引用の明記に留める）

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-13 P2 が p001 を実施、判断待ち。旧: planning（2026-10-13 Q1 作成））
Primary Milestone: MG006
Related Milestones: なし
<!-- awesome-plan-current:end -->

## 原文（2026-10-13 ユーザー）

「WSを作ります。VenusのOPを流用しているのですが、定数名を変えて、Venusの著作権表示を外し、引用を明記するに留めます。」

## 目的と完了の条件

対象は Venus（virtio-gpu の Vulkan の protocol、Mesa・virglrenderer の venus-protocol）の op（command の型の番号など）を使っている所: `src/drivers/gpu/venus/`、`userland/desktop/libvulkan/`（wire.c など）、`include/uapi/gpu-op.h`、その他の参照（p001 で洗い出す）と、license の表示（source の頭の注記、`tools/release/license-components.json`、`/usr/share/licenses/` の file）。

1. Venus 由来の物を洗い出し、「op の番号・値（事実）」と「code・表・comment の表現の写し」を分けて記録する。写しが見つかった所は書き直すか、表示を残す判断をユーザーに返す。
2. 定数名を Venus の header の名前（`VK_COMMAND_TYPE_*`、`vn_*` など）から zedBSD の名前に付け直す（Vulkan の公開の API の名前 `vk*`・`VK_*` そのものは対象外）。
3. 1・2 が済んだら、Venus の著作権表示と license の本文・inventory の記載を外し、引用元（venus-protocol の版・file）の明記を、引用の箇所に残す。
4. build の warning 0、libvulkan・venus の host 試験と license の inventory の試験（`plan/ws129/tests/license-inventory-test.sh`）が PASS。QEMU の Venus の起動の回帰は T1。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| [p001](phase001/phase.md) | 監査: Venus 由来の物の洗い出し、事実と表現の写しの区別、付け直す名前の一覧 | in-progress（2026-10-13 P2 実施。表現の写しは無し、Google の表示は WS167 で既に除去済み。判断 4 点を Q1 → ユーザー） |
| p002 | 定数名の付け直し、著作権表示の除去と引用の明記、試験 | planned |
