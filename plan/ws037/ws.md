<!-- awesome-plan project=zedbsd record=ws037 -->

# WS037: nvrtx — NVIDIA GeForce RTX 2000 以降（Turing〜Blackwell）の GPU driver

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」: nvrtx は main にあり既定で on、RTX 2070 で passthrough の desktop と素の起動の desktop の session が通った。速さ・RTX 30/40・NVDEC・取り消しと復帰は別の WS）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG006
Related Milestones: なし
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

zedBSD の自前の GPU driver **nvrtx**（`src/drivers/gpu/nvrtx/`）で、NVIDIA の RTX 2000 以降（Turing SM75 を最初に。GTX 16xx を含む）の display と 3D・compute を成立させ、zedBSD の GPU の interface（`struct drv_gpu_interface`、i915 と同じ口）に載せる。最初の対象は RTX 2070（TU106）。範囲の外: Pascal 以前、video の encode・decode、複数 GPU、SLI。設計は [nvrtx-design.md](nvrtx-design.md)、試験機は [host.md](host.md)、license の監査は [nvrtx-license-audit.md](nvrtx-license-audit.md)。

## ユーザーの決定

- 2026-09-23（予約）: 「RTX 2070をターゲットにして、NVIDIA GPUのドライバを実装する。」
- 2026-10-04: 「rtx 2000以降を対象に、nvrtxというドライバを開発します。やりかたはVC4と同じです。WSを作ってください。」、「RTX 2070の実機があり、そのホストをあとで伝えるので、それさえわかれば作業を開始できるように」。Pascal 以前は範囲の外（Q1 の説明にユーザーが了承）。
- 2026-10-14: 「事情が変わりました。…RTX対応ができる可能性があります。やりましょう。」、「パススルーできるLinuxが必要ですか？もしそうなら、すぐにOSをインストールします。」
- 2026-10-14 夜: 「* R6 の「channel を残したまま止める」場合の片付け * 3D の初期状態（golden context） * display * shader の compiler ここまでベータ2に入れます。作業を続けてください。」、「ESP/FAT32分離が終わったら、RTX対応を2エージェントで実施してOKです。displayとcompilerは平行にできそうです。」
- 2026-10-15（p006）: 「R8の終了条件にユーザテストを入れません。あなたはパススルー環境でテストして、カーネルログから、ディスプレイ制御のエラーを見ることができ…」、「R8bについては、Nouveauのソースコードの初期化手順と寸分違わぬように初期化してみてください。」、「RTXで実画面にカラーバーが出ているのを目視しました。」
- 2026-10-15: 「nvrtxはmainでデフォルトで有効にしてください。」（CONFIG と runtime の renderer を既定で on、`nvrtx.vk=0` で off）。
- 2026-10-15（GSP の停止）: 「再起動の時にGSPを、止めます」、「GSPを止める処理については、WSにメモしておいて、あまり深追いしない」。
- 2026-10-15（速さ）: 「RTXでの描画が遅いです。RPi4と大差ないです。」→「F1〜F3 を進める」「計数の行を出す」。
- 2026-10-15（RTX 30・40）: 「nvrtxドライバは、RTX 20/30/40/50までのSMで対応できるようにしたいです。」「20/30/40は、30/40の動作確認なしで、とりあえず対応しておいてください。firmwareも追加しておきます。あと、20/30/40の全モデルで認識できるようにしてください。50は別WSを作り、あとでnvrtxに追加します。」「この作業はいったん記録してから開始してください。50はまだ開始しないでください。」→ RTX 30・40 は [WS242](../ws242/ws.md)、描画の速さは [WS241](../ws241/ws.md)、RTX 50 は [WS240](../ws240/ws.md) に分けた。
- 2026-10-15: 「サブエージェントP4を起動して、RTXコンパイラの作業をお願いします。」（p008）

### ライセンスの扱い（WS141 の vc4 と同じ方式）

GPL の source から作る作業の文書は `plan/ws037/temp/` に置き commit しない。code を書く前に作業の文書の定数を一括で独自の名前に変える。register・method・packet の形は MIT の source（nouveau の MIT の file、Mesa の NVK・NAK、open-gpu-kernel-modules・open-gpu-doc の MIT の側）から取り、file ごとに path・SHA-256・license を監査の表にする。GSP の firmware は `userland/firmware/nvidia-gsp/` に置き file から load する。zedBSD の code は Zlib。

## 結果

- 文書（p001）: 初期化の順・GSP の RPC・channel の command の順・NAK の世代の差・interface の対応表・段の印（N0・N1・P0〜P9、[nvrtx-design.md](nvrtx-design.md) の 3 節）。
- driver（p003〜p007）: 定数の改名と骨格、GSP の起動（FWSEC-FRTS・booter・GSP-RM・RPC・停止）、VRAM の allocator・MMU・BAR1・golden context・channel・GPFIFO・semaphore の fence・回復、display（GOP の readout、GSP の RM の display で mode set・scanout・flip、cursor・event）、3D の投入と `drv_gpu_ops` への統合。
- shader の compiler（p008）: SPIR-V → SM75（NAK の事実）。desktop の全 shader（41 module）が compile でき、IR の参照の interpreter と一致（host）。
- 実機（RTX 2070、QEMU+VFIO の passthrough）: R5〜R7・R8b〜R8d・R9v・R9k が PASS、D6（T1-678）で Keiland の desktop が nvrtx の renderer で描かれた。方法 A（素の起動）で desktop の session 5/5（T1-684）。ユーザーが実の monitor でカラーバーを目視（p006）。
- 既定で on（`config/ci/config-amd64.mk`・`config/drivers/pci.drivers` は y、runtime の renderer も既定 on）。

## 制限・移管

- 方法 A の P4m（VRAM の BAR1 の自己試験）の間欠の失敗（T1-691、MMU の fault `00110802 at 0x1d0000`）: 2 回の手当て（console の window・PRAMIN の読み返し・6 round の回復と fault status の消去）と reboot での GSP の停止を build と host 試験まで。実機の確認は未実施。Bug の ticket は無い。
- 実機の確認は未実施: p003〜p005 の T の項目の一部、F1〜F3（速さ）。速さは WS241・WS291、RTX 30/40 は WS242、NVDEC は WS243、取り消しと復帰は WS250。
- `nvrtx.disp.own=1` を既定にするかはユーザーの判断のまま（未決）。
- p009（規約の全文・license と GPL との類似の監査・BLOB の確認）は WS の外の一括の見直しへ。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws037-p001 | 文書: 初期化の順・GSP の RPC・channel・NAK の世代の差・license の監査・段の印・試験の道 | in-progress のまま WS を閉じた（文書と review の反映済み） |
| ws037-p002 | 試験機（RTX 2070）の調査と試験の道 | planned のまま WS を閉じた（方式 B の host は用意され、実機の試験は p004 以降で実施） |
| ws037-p003 | 定数の一括の改名、driver の骨格、段の印 N0・N1 | in-progress のまま WS を閉じた（実装・build・host 試験、実機は後の段で通過） |
| ws037-p004 | GSP の起動（P0〜P3・P7） | in-progress のまま WS を閉じた（実装、実機の R0・R1 以降の段は後の run で通過） |
| ws037-p005 | VRAM・MMU・BAR1・golden context・channel・fence・回復（P4〜P6） | in-progress のまま WS を閉じた（R5〜R7 実機 PASS、P4m の間欠の失敗は上の制限） |
| ws037-p006 | display（P8） | in-progress のまま WS を閉じた（cleared の候補: R8b・R8c・R8d PASS、目視なしのユーザーの基準） |
| ws037-p007 | 3D・compute の投入と `drv_gpu_ops` への統合（P9）、desktop | in-progress のまま WS を閉じた（D3・D5・D6 実機 PASS、F1〜F3 は実機未実施） |
| ws037-p008 | shader の compiler（SPIR-V → SM75） | in-progress のまま WS を閉じた（全 41 module が compile、IR と一致。GPU の確かめは p007 の D6 で） |
| ws037-p009 | 規約の全文・license と GPL との類似の監査・BLOB の確認 | planning のまま WS を閉じた（WS の外の一括の見直しへ） |

## 記録の所在

Phase の計画・結果は削除する。git の commit `fb1f6afee` の `plan/ws037/` にある（P4m の解析は phase007 の終わりの節、compiler は phase008）。試験は `plan/tools/` へ移す（Q1 の OK の後）。Queue の履歴は [plan/history](../history/index.md)。
