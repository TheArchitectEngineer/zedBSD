<!-- awesome-plan project=zedbsd record=ws269-proposal-next-phases -->

# WS269 の残りの Phase の提案（2026-10-21 P1、q1506、下書き）

Phase の ID は Master の next から Q1 が割り当てる。ここでは仮の名前（N1〜N6）を使う。

## 今の到達点（板 93・94、main b9672d30d の SD image）

- kernel の driver: firmware の起動、render node、kernel の自己試験（compute check、render check の terminate-state → 三角形 → transfer → job check）が全部 ok。
- userland: gpu-selftest は 13 ok / 0 fail / 0 skip。kernel・node・vkcompute（128 workgroup）・image・clear・copy・draw・sample・depth・present-clear・present が ok。HDMI に vkdemo の cube の animation を出した。
- 原因と直しの要約は phase010q（`fb1f6afee` の `plan/ws269/phase010q/phase.md`） の先頭。

## (1) 完了の条件（ws.md）のうち未達の項目

| 条件 | 状態 | 残り |
| --- | --- | --- |
| 1. BXE-4-32 を認識し、電源・clock・reset・firmware で GPU を起こす | 達成（板 78 以降） | 起動の時間（render node まで約 22 s、診断を除いた既定でまだ測っていない）、594 MHz を既定にするか（WS256） |
| 2. Keiland が使う Vulkan の範囲（WS270 の desktop）で描画できる | **未達** | Keiland の desktop を VF2 の GPU で動かす（WS270 p006 の目視）。p011 の機能の不足（16 draw を越える pass の継続の板での確認、Keiland の pipeline の全部の compile、format の広げ（p010p の残り: raw 64/128 の copy・PBE の F16）、shared の WSI の道（p010l、dc8200 の zero copy）） |
| 3. VisionFive 2 の実機で確かめる | gpu-selftest と vkdemo までは達成 | vkdemo 以外の app（Keiland、Vulkan の sample）、安定性（連続の起動、長時間の描画、reset からの回復の板での確認）、性能（frame の時間の目安） |
| （暗黙）docs | 未達 | docs の GPU（VF2）の節: 対応する Vulkan の範囲、制限（下の「制限」）、gpu-selftest の使い方 |

制限として書く物:
- 1 queue（graphics・compute・transfer の family 1 つ）。
- API 1.0。
- compute は 1 submission の中の dispatch を fence で順序づける。
- linear の image を SAMPLED にしない。
- 1 sample。
- render target は 1 layer。

## (2) p010q の残件の処置

| 残件 | 処置 |
| --- | --- |
| fence kernel の lockup | 解決（K0 の shared の確保、q1501・q1502、板 91）。p010q で close |
| compute の手書き kernel の退行 | 解決（fence を戻した、P2 の q1503、板 93）。p010q で close |
| 診断の整理 | 済（q1493 の `POWERVR_DIAGNOSTICS`）。compute-check の probe の行が既定で出るかは P2 の側で確認して close |
| BUG-344（libc の fscanf） | 別の WS（libc の stdio）へ移す。WS269 は gpu-selftest の回避で依存しない |
| Linux の walk（IMG の stack の control stream の読み） | 保留。この kernel では /dev/mem・/proc/kcore が使えず、kernel module が要る。Future Work へ（再検討の条件: IMG と我々の差が再び要る不具合が出た時） |
| mesa-form の probe の整理 | compute-check の中の probe を、回帰に使う物（3 kernel の手書き kernel、compiled fill）と、開発の途中の物に分ける。後者は削除（試験の整理の基準）。P2 |

→ p010q は、上の close と移管を記録して clear できる見込み（Q1 の判定）。

## (3) 規約の全文の見直し（code-producing WS に必須）

p012 は 2026-10-20 に P1・P2 の前半・後半を行った。その後（2026-10-20 夕方〜10-21）に大きく変えた file を、もう一度全文で見る。対象と順序（変更の量の多い順、依存の少ない物から）:

1. 純粋な関数（host 試験がある物から。動作を変えない確認がしやすい）:
   - `compute-program.c/.h`、`render-program.c/.h`、`render-command.c/.h`、`interface.h`、`gpu-mmu.c/.h`、`render-sr-program.c/.h`
2. kernel の driver の本体:
   - `firmware-boot.c`、`commands.c`、`render.c`、`render-queue.c`、`render-runtime.c`、`render-target.c`、`render-context.c`、`render-job.c`、`render-private.h`、`powervr-private.h`
3. 自己試験:
   - `render-check.c`、`compute-check.c`（P2）
4. Vulkan の executor:
   - `vk-queue.c`、`vk-device.c`
5. compiler（P2）:
   - `shader-backend.c`、`shader-private.h`、`shader.h`
6. platform（WS256 と共有）:
   - `src/drivers/platform/visionfive2/vf2-gpu.c`、`vf2-pmu.c/.h`、`vf2-crg.c`
7. userland:
   - `userland/tests/gpu-selftest/main.c`、`userland/tests/vkimage/main.c`
8. Linux 側の道具（Python、規約の対象外）:
   - `plan/ws269/tests/p010-linux/*.py`。回帰に使わないので、整理の基準で残すか消すかを決める

担当の分け方の案: P1 は 1・2・3（render-check）・4・6・7、P2 は 3（compute-check）・5。

## (4) Phase の表の更新案（仮の名前）

| 仮の名前 | 目的 | 依存 | 担当の案 |
| --- | --- | --- | --- |
| N1 | p010q の clear（上の (2) の close と移管の記録、Future Work と BUG-344 の移管） | — | Q1・P1 |
| N2 | 安定性: 連続の起動（10 回）、vkdemo の長時間（30 分）、GPU の reset（意図的な lockup）からの回復を板で（負荷・耐久はユーザーに確かめてから夜間） | N1 | P1・T1 |
| N3 | Keiland の Vulkan の範囲を板で: p011 の残りを板で通し、WS270 p006 の desktop の目視へ渡す | N1、p011 | P2・P1 |
| N4 | 規約の全文の見直し（2 回目、上の (3)） | N2・N3 の code の変更の後 | P1・P2 |
| N5 | docs: VF2 の GPU の節（対応の範囲、制限、gpu-selftest） | N3 | P1 |
| N6 | 594 MHz・起動の時間（render node まで）の測りと既定（WS256 と相談） | — | P1 |

既存の Phase の扱い:
- p010m（仕上げ・受け入れ）は N2・N3 の後。
- p012 は N4 で上書きするか、p012 の 2 回目として続けるか（Q1 の判断）。
- p010k2・p010l・p010p の in-progress は、N3 の中で板の確認として扱う。
