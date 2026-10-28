<!-- awesome-plan project=zedbsd record=ws309 -->

# WS309 Keiland の compositor の thread の構成を整理し、main loop が待たないようにする

- Master: [plan/master.md](../master.md)（2026-10-29 作成）。Primary Milestone: MG006。前身の調べ: [WS291](../ws291/ws.md) の
  [keiland-blocking.md](../ws291/keiland-blocking.md)（P2 q1661 の点検、branch `agent/p2-ws291i` にあり main に未 merge）。
- Status: incomplete（2026-10-29 q1662 P1: 設計 [design.md](design.md) 確定、p001〜p003・p005・p019 cleared、p004 in-progress、p006〜p018 planned）
- 目標: Keiland の compositor の main loop が frame を止める同期の待ちを持たないよう、thread を下の構成に整理する。ゲームモードの
  direct scanout は Vulkan display（`VK_KHR_display`）の仕様の中で実装する。

## ユーザーの決定（2026-10-28〜29、原文）

1. 「Keilandの直し方ですが、デバイスの検出はスレッドに追い出してください。他にもメインループにそういったブロッキング要素がないかチェックしてください。」
2. （P2 の点検の案 A〜G への答え）「すべてスレッド化しますが、いったん、全部列挙して、スレッドの数や分け方を検討しましょう。」
   （入力の検出の置き場所への答え）「先に回答したとおり、検討して整理します。アドホックにはしません。」
3. 「入力スレッドに入力デバイスの検出があるのはおかしいのでは。それだと入力が一瞬ブロックされます。I/O系はまとめていいです。むしろ入力デバイスの検出はI/O系にまとめるのがいのでは、と思ったのですが、poolということでスレッド数が明示されておらず、検討は必要と思います。」
4. 「なんでもfdのeventを多用するLinuxの設計が、POSIXの世界から逸脱していて、実用的だけど美しくないと、個人的に感じてしまいます。だから独自OSを作っているわけで。」
   「壁紙と画像、手書きの照合、の2つは、それぞれad-hocなスレッドに分離しましょう。さらに、blur、フォントの先読み、字形のラスタライズは、UXにクリティカルなので、単体の常駐スレッドにしましょう。」
5. 「確認ですが、現状でも、fence signalで非同期の通知はできているんですよね？提案のfd eventは不要ですよね？」（→ 不要と Q1 が確かめた）
   「daemonと接続するスレッドは、daemon接続ごとに単体に分離しましょう。」
6. 「手書きの照合はN文字のスレッドプールにしましょう。」
   「blurは単体のスレッドにしましょう。まずはCPUでいいです。将来GPUでぼかす実装を作って、どれくらい軽くなるのか調査しましょう。理由は、壁紙は非常に大きいので、GPU転送を考えると、高速化にならない可能性があることと、CPUで処理しても、一瞬であれば電力インパクトが小さく、排熱の問題も小さく、モバイルCPUも100ms未満であればスロットルが起こらず、しかもモバイルCPUでも1フレームのみのblurなら実用範囲の時間であると、経験的に思うからです。」
   「ゲームモードのpresentは単体の常設スレッドにしましょう。同期で待っていいです。presentの完了をpipeで知らせるのはちょっと美しくないけど、poll/selectでまとめて得られるからよさそうですか？」
7. 「(b)でよいです。コンポジタは純粋なVulkanのみに依存するという設計制約があるため、カーネルに機能追加したところで、それをVulkanの機能として使えないなら、呼び出しできないです。」
   （(b) = ゲームモードもアプリは buffer を compositor に送り、compositor が flip に積み、buffer が画面から外れたら release をアプリに返す。アプリは acquire で空きを待つ）
8. 「そうすると、ゲームモードについてはバックエンドがカーネルを呼んでもいいことにします。でも、せっかくならVulkan displayの仕様内で実装したいです。工数が多くてもかまいません。Vulkan displayをOSの正式APIに採用しているのが、このOSのウリの1つでもあるので。」

9. （2026-10-29 P1 の設計の判断の点への回答、Q1 経由）J1 game mode の Vulkan の形: ユーザーの回答（クリック）「V2 swapchain 共有 (推し)」。
   J4（Linux・FreeBSD の seat 経由の入力の open）: 推しのとおり当面は同期のまま残し、WS309 の後の Phase に置く（別の WS にしない）。J2・J3 は Q1 が「推しで OK（技術の判断）」。
   （Q1 の解釈: V2 = compositor が display の swapchain の image を external memory で export して client に渡し、client が直接描く。copy なし。libvulkan の WSI・display の部分を WS309 の範囲に含め P1 の所有に足す。）

- （Q1 の決定、2026-10-29、ユーザーの原文ではない）J5: 下の表の「spawner の process」は Q1 の解釈だったので取り下げ、io-file の thread から `posix_spawn`、pid・kill・`waitpid(WNOHANG)` は main（ユーザーには Q1 が伝え、覆れば P1 に知らせる）。
  J6: daemon の thread は main で blocking の I/O をする接続だけ（audiod・networkd・bluetoothd・phone・volumed の 5 本）。

## 決まった構成（解釈。上の原文が正）

| 種類 | thread | 持つ仕事 |
| --- | --- | --- |
| 常駐 | main | Wayland の protocol と状態だけ。待たない。完了は共通の wake pipe と poll で受ける |
| 常駐 | 描画 | GPU の queue の唯一の持ち主。取り込み・layout の遷移・shm の upload・片付けの GPU の待ちを命令として受ける |
| 常駐 | cursor | 今のまま |
| 常駐 | 画面の制御 | display の状態の変更の直列化（direct scanout の claim・release、swapchain の作り直し、modeset、回転、hotplug、backlight） |
| 常駐 | ゲームモードの present | present を同期で待つ。完了（前の buffer の release）は共通の wake pipe で main に |
| 常駐 | 入力 | evdev の read だけ（device の検出は入れない） |
| 常駐 | I/O-1 device | 入力の device の検出（scan・open・close）、電源の読み取り、`statfs`、network・電源・アカウントの処理 |
| 常駐 | I/O-2 file | 設定の読み書きと fsync（`displays.conf`・設定・`printers.conf`）、`apps.conf`・keymap・言語の file |
| 常駐 | font と字形 | 予備の font・emoji の先読み、字形の rasterize（face はこの thread だけが持つ） |
| 常駐 | blur | 壁紙のすりガラスの blur（まず CPU） |
| 常駐 | daemon ×5 | audiod、networkd、bluetoothd、bluetoothd（電話）、volumed。接続ごとに 1 本、blocking の I/O |
| pool | 手書きの照合 | N 本（N の決め方は未決）。同じ文字は最新の 1 件だけ |
| その都度 | 壁紙・画像の decode | 1 回ごと |
| process | （取り下げ、J5） | 起動は io-file の thread から posix_spawn（main は fork しない） |

- fd の event を kernel に足す案は取らない（GPU の完了は VkFence、client の完了は明示の同期の fence、present は thread が同期で待つ）。
- ゲームモード: 当面は backend（`libkeiland-backend-zedbsd/scanout-zedbsd.c`）が GPU の ioctl を直に呼んでよい（決定 8）。目標は
  Vulkan display（`VK_KHR_display` の swapchain、buffer が画面から外れた事は present fence（`VK_KHR_swapchain_maintenance1`）など
  Vulkan の機能）で実装する（工数は問わない）。複数の flip を queue に積む kernel の機能も、libvulkan が Vulkan の機能として出す形でだけ使う。

## Future Work の候補

- GPU で blur を実装し、CPU との速さ・電力を比べる（決定 6）。

## 未決

- （決定）手書きの照合の pool の N: 2026-10-29 01:3x ユーザーの回答（クリック）「CPU 数で固定 (推し)」→ 起動の時に CPU の数から決めた固定の本数（Q1 の選択肢の例: CPU の数 − 1、上限 4。具体の式は P1 の設計で）。
- P2 の q1661（`agent/p2-ws291i` 3d9306f02 の backend の入力の検出の worker、467f1cabc・1a0ead286 の記録）: アドホックにしない決定 2 により
  main に merge せず保留。この WS の I/O-1 の実装で置き換える。main には b574fbf6a（開けなかった node を覚える）が入っている。

## 設計

- [design.md](design.md)（2026-10-29 P1、thread の一覧・本数・待ちの点・queue と通知・ゲームモード・判断の点 J1〜J4）。

## Resume point

- 2026-10-29 03:2x P1（q1662-i03、branch `agent/p1-ws309c`）: p005 cleared（言語・apps.conf・account・鍵・printers の open、QEMU `plan/ws309/tests/io-read-guest.sh` PASS）。p019 cleared（P2 の q1668）。次は p004。
- 2026-10-29 02:4x P1（q1662-i02、branch `agent/p1-ws309b`、d5fb0bd08・c3b274d51）: p005 は displays.conf（`saved` は書き終わりで答える）・desktop.conf の Log Out・printers.conf まで済み。
  次は p005 の残り（言語の catalog・apps.conf の読み、account・鍵の保存、[phase.md](phase005/phase.md) の「残り」）、その後 p004。QEMU は `plan/ws309/tests/io-file-guest.sh`。
- 2026-10-29 02:3x P1（q1662-i01）: p001・p002・p003 cleared、p019（shot の送りを thread へ）は QEMU で再現せず RDNA の確かめ待ち。
  次は p005（io-file: displays.conf の `saved` の答えを書き終わりで返す。displays-shell.c の `displays_pending` の保留の仕組みを広げる案）か p004（io-device の残り）。
  QEMU の image は `plan/ws309/tests/config-amd64-p1.mk`（BUILD=build/ws309/img）、試験は `plan/ws309/tests/` の io-device-guest.sh・shot-fifo.sh。

## Phase

| ID | 目的 | 状態 | 依存 |
| --- | --- | --- | --- |
| [ws309-p001](design.md) | 設計の確定（thread の一覧・本数・待ちの点・queue と通知・ゲームモード、敵対的レビューの反映） | cleared（判断の点 J1〜J6 は全て決定） | — |
| [ws309-p002](phase002/phase.md) | 共通の部品: thread の台帳・lane（常駐と pool）、host 試験 | cleared | p001 |
| [ws309-p003](phase003/phase.md) | io-device: 入力の device の検出と close を thread へ（zedBSD） | cleared（実機は未実施） | p002 |
| ws309-p004 | io-device の残り: 電源・network の詳細・machine・sysmon、input の detach の ack を wake で | planned | p003 |
| [ws309-p005](phase005/phase.md) | io-file: displays.conf・desktop.conf・printers.conf の書き込み（`saved` は書き終わりで答える）、apps.conf・言語・printers.conf の読み、account・鍵の保存 | cleared（account の実行と実機は未実施） | p002 |
| ws309-p006 | daemon ×5 の thread（zedBSD の backend、wake pipe を backend の open の前に） | planned | p002、J6 |
| ws309-p007 | font の thread: 予備の font・emoji の先読み、rasterize、color emoji の decode | planned | p002 |
| ws309-p008 | render を GPU の queue の唯一の持ち主に (1): fence 付きの task と queue の lock | planned | p002 |
| ws309-p009 | 同 (2)(3): 遅らせた破棄（serial・descriptor set・二重の image）、quiesce と main の `vkDeviceWaitIdle` を消す | planned | p008 |
| ws309-p010 | blur の thread とその都度の decode（壁紙・出力の大きさ・回転） | planned | p009 |
| ws309-p011 | 手書きの照合の pool（N = min(max(CPU−1,1),4)） | planned | p002 |
| ws309-p012 | display の thread（swapchain・modeset・回転・hotplug の列挙・scanout の可否と claim・release・backlight、seat の pause） | planned | p009 |
| ws309-p013 | present の常駐の thread（暫定の ioctl、共通の wake） | planned | p012 |
| ws309-p014 | process の起動（J5 の形）と clipboard の書き込みを poll で | planned | p005、J5 |
| ws309-p015 | game mode の V2: libvulkan の display の swapchain の image の export、compositor の受け渡し | planned | p013 |
| ws309-p016 | Linux・FreeBSD の seat 経由の入力の open（J4） | planned | p003 |
| ws309-p017 | QEMU の回帰（Venus: 基準・GPU の境界・`KWL THREADS`・`KWL PERF pass`）と遅さの注入の host 試験 | planned | p003〜p014 |
| ws309-p018 | 規約の全文の見直し | planned | 全て |
| [ws309-p019](phase019/phase.md) | 試験の screen capture の画素の送りを thread へ（keiland-shot の後の SURFACE_LOST、Q1 の依頼） | cleared（RDNA の実機は P2 の q1668） | p002 |

