<!-- awesome-plan project=zedbsd record=ws243-p005 -->

# ws243-p005: 規約の全文の見直し

Parent: [WS243](../ws.md)
Status: cleared（2026-10-29 q1677-i02 P5。WS243 が足した・変えた C の hunk を plan/coding-style.md の全文で見直した。書き方だけで動作は変えていない）
担当: P5（conformance-reviewer に任せ、P5 が diff を読んで commit）

## 範囲

- `git diff 3a2009fc2 9dbf7fda5`（P6 の実装）と `git diff f422d14ae^2 HEAD`（P5 の取り込みと p006）の C の hunk。新しい file（nvdec.c・nvdec-records.c・nvrtx-nvdec.h・vk-video.c・
  vk-video-h264.c・nvrtx-video.h・exec-copy.c・plan/ws243/tests/*.c）は全体。他の WS の既存の code は触れていない。

## 直した事（22 file）

- 段落の目的の comment、`break`・閉じ括弧の後の空行、3 節以上の条件を 1 節 1 行、入れ子の呼び出しを変数へ（`video_mbs`・`nvdec_semaphore`・`exec_swizzle_source`）、
  stub の 1 引数 1 行と comment、試験の file の copyright の形。
- 開発の管理情報: 試験の comment の「WS243」を外し、wire の試験の出力を「video wire round trip: …」に（数の部分は同じ）。
- wire の試験の `calloc` の失敗を check の失敗として数える（試験の code だけ、通常の経路は同じ）。

## 適用しないと判断した事

- host の stub に UNUSED_PARAMETER（host 試験に無い）、include guard 名と include の path（識別子・path で comment ではない）、
  探しの末尾の失敗の return（`nvrtx_nvdec_context_bytes` の ENOENT、`nvrtx_video_dpb_entry` の ENOSPC。全部を見て初めて決まる失敗）。
- `resource_image_supported` の返り値「2」の定数化（既存の関数の返り値の約束に触れるので comment に留めた）。

## 気づいた事（動作、未修正）

- `vk-video.c` の `video_parameters_create`: `nvrtx_vk_object_retain(owner)` が失敗しても `max_sps`・`max_pps` を代入する。後の error の判定で破棄されるので害は無い。

## 確かめた事

- `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws243 build/ws243/vmunix` → rc 0、warning 0、`amd64 vmunix check: PASS`（cksum 1342715868）。
- host: nvdec 88/0、wire 32/0、plane-header 40/0、nvrtx-vk 86/0、nvrtx-vk-d3 209/0、nvrtx-recover 45/0。`git diff --check` 空。
