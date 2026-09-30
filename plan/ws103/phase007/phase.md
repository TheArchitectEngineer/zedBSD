<!-- awesome-plan project=zedbsd record=ws103-p007 -->

# ws103-p007: 規約の全文、回帰、5330、V4

- Parent: [WS103](../ws.md)
- Status: in-progress
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

## 記録

（実行中）
