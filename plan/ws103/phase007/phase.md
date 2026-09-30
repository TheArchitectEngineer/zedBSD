<!-- awesome-plan project=zedbsd record=ws103-p007 -->

# ws103-p007: 規約の全文、回帰、5330、V4

- Parent: [WS103](../ws.md)
- Status: cleared（2026-10-01、q514-i01）
- Disposition: normal
- Queue: q514-i01
- Design: [design.md](../design.md) §2.7・§3 の p007

## 範囲

1. WS103 の全ての source の変更（p002 の前 `e27565f3~1` から今まで。compositor・libvulkan・probe・build の規則。生成した header と dispatch の表は道具の出力なので除く）を
   `plan/coding-style.md` の全文で見直し、違反を直す。
2. 回帰: C1・C9（WS099 の回帰の一覧）・Notes（WS079 の右上の swipe）・boot test、5330 の passthrough（V2）。
3. V4: WS103 の前（`e27565f3~1` の compositor・libvulkan・Vulkan の header）の image と今の image で、QEMU の Venus の import-launch と 5330 の measure-apps を比べる。

## 完了の基準

1. 規約の見直しの記録（範囲、見た項目、直した物、残した物と理由）。
2. build（warning 0）と host の試験（dedicated・gpu-zedbsd）。
3. 回帰の全ての PASS。
4. V4: 前と今の差が計測のばらつきの内（遅くなっていない）。差が出たら原因を分ける。
5. WS103 の達成基準 V1〜V4 の判定と、WS の完了の処理。

## 記録（2026-10-01、q514-i01、メインのエージェント Q1、ユーザーの自走の指示）

### 1. 規約の見直し（commit `d09860e0`）

範囲: `git diff ef343e64 HEAD -- userland/desktop/wayland userland/desktop/libvulkan userland/base/tests/gpu-forge platform/amd64/vmunix.mk plan/ws103/tests/*.c`
（生成物 `dispatch-table.inc`・`api-commands.tsv`・`include/libc/vulkan/*`・Noct の道具を除く）を、読むだけの subagent が `plan/coding-style.md` の全文（§14 の checklist）で見た。
直した物: gpu-forge の条件の中の関数の呼び出し・式で作る Boolean・段落の注釈と空行・構造体の初期化の区切り・loop の注釈・3 項の条件・void 関数の最後の return、
`display.c` の `&&`・`||` の混ざる条件の分割、`gpu-zedbsd.c` の `memcpy` の段落と `layout` の一度の初期化、`memory.c` の fd の消費の段落・関数の結果の直の return・`for` の 3 行、
`resources.c` の `for` の 3 行と段落の分割、`compose.c`・`zwl.h` の古い注釈、host の試験の注釈と return。
残した物（理由）: `device.c` の `strcmp` の段落と `import.c` の構造体の初期化の空行（周りの既存の code と同じ形）、`protocol.c` の decode の結果を import の連鎖で確かめる形
（既存の失敗の道の形を保つ）、名詞句の段落の注釈（規約は「通常は」動詞と目的語で、許容の範囲）、試験の `(void)param`（同じ試験の群の形で、`UNUSED_PARAMETER` を定義する header が届かない）。
問題なし: ANSI C の宣言、forward 宣言、公開・static 関数の注釈、型と file の変数の注釈、成功の return、multi-line の注釈、環境変数の切り替え無し。

### 2. V4 の対策（同じ commit）

QEMU の最初の計測で Model viewer の起動から最初の画像までが約 130 ms（3%）遅かった。原因の見当: dedicated の import の照合で libvulkan が image の memory の要求と行の配置を
問い合わせ直し、compositor の直前の問い合わせと二重（Venus では wire の往復、起動で 3 回の import）。design §2.7 のとおり、libvulkan の `struct vulkan_image` に
`requirements`・`color_layout`（level 0・layer 0 の色）を最初の問い合わせで覚える（image の答えは変わらない。失敗の答えは覚えない）。直した後に測り直して解消した（下）。

### 3. 確かめ

| 基準 | 結果 |
| --- | --- |
| 2 build と host の試験 | 最終の code: compositor・libvulkan・probe の warning 0、基準・forge・passthrough の image rc 0（desktop の warning 0）。`run-dedicated-host.sh`・`run-gpu-zedbsd-host.sh` PASS、`v1-check.sh` PASS、`git diff --check` 通過 |
| 3 回帰 | 見直しの前の今の image: C1 p126・c1-boot-shutdown PASS、**C9 の 10 本（p052・p053・p072・p076・p126・p128・p134・p137・p138・cursor-owner）全て PASS**、boot test PASS。最終の code: forge-guest PASS、fence-guest PASS（600 個が世代 1、63 秒）、p054 PASS、C1・C2 PASS、boot test PASS、5330 の `c5-hw.sh` PASS（34 回、最大 59 ms）。Notes は p006 で PASS |
| 4 V4 | QEMU の Venus（import-launch、5 回、2〜5 回目）: Model viewer 前 4015〜4241 ms → 最終 3905〜4008 ms、Files 前 2111〜2272 → 最終 2080〜2187 ms（遅くならない）。5330 の passthrough（measure-apps、前 5 run・今 5 run を交互に）: デスクトップだけ 60/s・窓 10 個 22〜23/s は同じ。C6 は試料 200 個ずつで中央値 前 56.4 ms・今 58.8 ms（run ごとの中央値 前 52.7〜62.0、今 53.2〜66.0）、Mann-Whitney z = −0.82（最終の code の 4 run だけでは −0.32）で、ばらつきの内。i915 の frame の道は WS103 で変わっていない（fence なし、import は起動の時だけ） |
| 5 V1〜V4 | V1: `v1-check.sh` PASS。V2: C1・C2・C9・Notes・boot test（QEMU）、5330 の passthrough の起動・login・10 個の app・App Home と Wiseview（実機の USB の単独の起動は未実施）。V3: 偽の buffer を断る（forge-guest、QEMU の Venus）と host の試験。V4: 上のとおり |

計測の生データ: `build/ws103/p007-launch-*`、`build/ws103/p007-measure-*`（C6 の試料は各 `measure.txt`）。前の image は `ef343e64` の compositor・libvulkan・Vulkan の header を
一時的に戻して作った（`build/ws103/p007-base-*.img`）。
