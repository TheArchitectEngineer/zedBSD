<!-- awesome-plan project=zedbsd record=ws309 -->

# WS309 Keiland の compositor の thread の構成を整理し、main loop が待たないようにする

- Master: [plan/master.md](../master.md)（2026-10-29 作成）。Primary Milestone: MG006。前身の調べ: [WS291](../ws291/ws.md) の
  [keiland-blocking.md](../ws291/keiland-blocking.md)（P2 q1661 の点検、branch `agent/p2-ws291i` にあり main に未 merge）。
- Status: planning（pool の N は決定、2026-10-29 q1662 P1 が設計の確定 → Phase の分割 → 実装）
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
| process | spawner | Vulkan を開く前に fork しておき、アプリの起動を頼まれる（main は fork しない） |

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

## Phase

設計の確定の後に分ける（未作成）。

## ユーザーの決定（2026-10-29、q1662）

- J1 ゲームモードの present: ユーザーの回答（クリック）「V2 swapchain 共有 (推し)」→ compositor が display の swapchain の image を external memory で export して client に渡し、client が直接描く（copy なし、標準の拡張の範囲）。libvulkan の WSI・display の変更は WS309 の範囲（Q1）。
- J2（queue は render と cursor の 2 本が mutex の下で使う）・J3（spawner が落ちたら作り直さず errno で失敗）・J4（seat 経由の入力の open は同期のまま、WS309 の後の Phase）: P1 の推しで Q1 が決めた（技術の判断）。
