<!-- awesome-plan project=zedbsd record=ws288-p011 -->

# ws288-p011 端から端の統合（Verilator の上で libvulkan → vk/ → firmware → RTL）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §8 の p011 の行（H6 の場）、§6.7、§6.8（試験の道）。
- 状態: planned（2026-10-25 09:59 Q1 新設）。p019 の merge の後に P3 に。
- 依存: p009・p027・p030（RTL）、p010（compiler）、p018（firmware、P2 の Verilator の firmware の harness `sim/verilator/fw_harness.cpp`・`fw-run.sh`）、p019（vk/、zedBSD の host 試験に model の GPU と stand-in の firmware）、p006（driver）。
- 承認: 2026-10-25 ユーザー「…そのあと、p010,p018,p019,p006,p011を実装します。…」「ゴールまで推しで進んでよいです。判断に迷ったら、Vulkan 1.0の原則論で進めてください。…」

## 範囲（design §8 の p011 の行）

- **場**: host で build した libvulkan（`userland/desktop/libvulkan/`、変えない）と zedBSD の driver（`src/drivers/gpu/ghci/`、`vk/` を含む）を、偽の drv_gpu（ioctl の代わりに関数、p019 の host 試験の model の形を広げる）でつなぎ、HCR と GPU の memory を Verilator の model（amaterrace-gpu の atg_top、CPU と firmware、KC705 の形）に DPI か共有の memory でつなぐ。
- **目標**: vkdemo（または同等の三角形・texture の program）の 1 frame を、libvulkan の API から描き、readback が CPU の参照と一致。加えて vkCmdFillBuffer・vkCmdCopyBuffer、compute の dispatch。
- **性能の最初の計測**: draw あたりの firmware の cycle、1 frame の往復。
- 見つかった食い違い（driver・firmware・RTL の境）は、その場で直すか、担当の file なら Q1 に送る。

## 受け入れ

vkdemo の 1 frame の readback が参照と一致（Verilator）、fill・copy・compute の試験が PASS、長い試験は T に回す。

## 記録

- 2026-10-25 09:59 Q1: 新設。
