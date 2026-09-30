<!-- awesome-plan project=zedbsd record=ws103-p002 -->

# ws103-p002: 起動の問い合わせを VK_KHR_display へ、`--direct` の削除

- Parent: [WS103](../ws.md)
- Status: in-progress
- Disposition: normal
- Queue: q509-i01
- Design: [design.md](../design.md) §2.1・2.2、§3 の p002

## 範囲

compositor（`userland/desktop/wayland/`）の `GPU_GET_INFO`・`GPU_DISPLAY_QUERY`・`GPU_DISPLAY_MODE`・`GPU_DISPLAY_CLAIM`・`GPU_DISPLAY_PRESENT`・`GPU_DISPLAY_RELEASE` と
`--direct` の道（Vulkan が開けないときに落ちる道を含む）を消す。大きさの既定は選んだ display の `physicalResolution`。GPU の fd は buffer の import のために残る。
`ZWL GPU` の行と `ZWL PRESENT` を読む試験を直すか退役させる。

範囲の外: `RESOURCE_IMPORT`・`FENCE_QUERY`、libvulkan、HAL、toolchain。

## 完了の基準

1. 上の 6 つの ioctl と `--direct`・`schedule_direct`・`zwl_present`・`claim_display`・`zwl_unscan`・`lease` が compositor に無い（grep）。
2. amd64 の build が warning 0。
3. `--width` なしで、画面の大きさが今と同じ（QEMU の Venus）。
4. greeter → login → Log Out → greeter（WS099 C1 の QEMU の部分）。
5. boot test（`plan/tools/boot-test.sh`）。
6. 5330 の passthrough で起動と login の smoke。
7. 規約の全文（`plan/coding-style.md`）を変更に適用。

## 記録

（実行中）
