<!-- awesome-plan project=zedbsd record=beta2-triage -->

# ベータ2 の残り作業（2026-10-11 Q1 更新）

公開は 10/17（OSC 当日、朝から会場）なので、**10/16 中にリリースの準備を終える**。
体制（2026-10-11 ユーザー）: Q1＋P1（新規の実装）・P2（Bug）・P3（Pi 4 の VC6 の描画）＋T1（試験）＋ユーザー（UAT・判断）。WS203（Pi 4 の Ethernet）は Codex の別のセッション。この表は計画で、Queue の承認ではない。
見積もりは LW（1 LW ≈ エージェントの実時間 20 分）。完了した項目は消す。Q1 は進むたびに更新する。

## 日程

| 日 | やること |
| --- | --- |
| 10/10〜10/12 | 走っている Bug の直しと試験を終える → WS199（セキュリティキー）→ WS200 |
| **10/13** | 機能の凍結の目標。ベータなので UAT の Bug は直し切れなくてよく、code freeze はぎりぎりまで行わないこともある |
| 10/14〜10/15 | 最終回帰（QEMU）と 5330 の UAT、出た Bug を「直す／既知の問題に書く」で仕分け |
| **10/16** | 最終の image・配布物・license の一覧・release notes を確定。Vulkan Video は既定で ON（2026-10-11 ユーザー）。公開の手順の確認（公開はユーザーの指示で） |

## 今の状況

- 2026-10-11: [WS197](ws197/ws.md)（スマホ連携）は SMS・MMS の受信と表示（日本語・写真を実機で確認済み）、PBAP の連絡先と通話の履歴、Bluetooth の自動復旧まで main に入った。P1 の書きかけの差分は照合して必要な 1 点だけ merge（d5296c23e）。残りは HFP（通話）と MMS の写真・動画の送信。**HFP をベータ2 に入れるかはユーザーの判断待ち。**
- [BUG-287](bugs/BUG-287.md)（スマホ連携が「connecting」のまま）の直しは全部 main に入った。image を作り直してよい。
- WS199・WS200 は実装と QEMU の試験が済み、残りは次の UAT。
- WS083（Vulkan Video）は既定で ON（2026-10-11 ユーザー）。残りは p007 の hang の実機の確かめ。[WS202](ws202/ws.md)（libmedia だけで mp4・m4a）は software を main に統合済み、実機と受け入れの全項目が未確認。
- Raspberry Pi 4: [WS141](ws141/ws.md) の VC6 の描画の改善を P3 が Codex から引き継いだ（mouse の移動 300 ms → 33 ms → 16 ms）。
  - 2026-10-12: App Home の遷移 9 秒 → 1 秒（HAL の uncached の unmap の TLB の flush を 1 回 b17a753d4、kern_free の O(1) 4d3d7eddf、draw の pool）、drag は非常に速く、Settings の present-copy 305 → 2〜15 ms。3 回目の App Home の compositor の終了（record の上限 1MiB）と preview の hover の全面の再合成を直した（main f07c345d3、実機の確認待ち）。vkQueueSubmit の非同期化 [ws141-p016](ws141/phase016/phase.md) は review-4 を通り A1・A2 を実装、2026-10-12 ユーザーの判断で実機の確認の前に main に merge（d9e0c35af、今回の image で確かめる）、A3（lazy map）が続く。App Home の hover は変わったアイコンだけの damage（efe8f8b63）。
- WS157 Photos の削除の統合は P1 の q924。WS202 の再生の実機・QEMU の確認は T1-529。
- 残る既知の Bug は USB LAN の遅さ（BUG-222）。

## 必須

| 項目 | 状態 | LW | 担当 |
| --- | --- | --- | --- |
| [WS083](ws083/ws.md) Vulkan Video（H.264） | p001〜p006・p008 cleared（実機で全 stream 一致、1080p 相当 1 frame 約 5 ms）。2026-10-11 から既定で ON（ユーザー）。残り: p007 の hang の回復の実機の確かめ（hang の kernel を 1 回置く、手順は phase007、ユーザーの判断） | 0.5 | ユーザー・Q1 |
| [WS202](ws202/ws.md) 動画再生） | libmediaにH.264, AACを実装、動画app, 音楽appで利用。） | 4.0 | ユーザー |
| [WS197](ws197/ws.md) Bluetooth のスマホ連携（SMS の MAP・通話の HFP・連絡先の PBAP）（2026-10-10 ユーザー「WS197はbeta2.mdで必須に入れておいてください。」） | 2026-10-11: p001〜p005・p011〜p013 cleared。SMS・MMS（文と写真）の受信・表示、PBAP の電話帳・通話の履歴・Settings、Bluetooth の自動復旧は main。残り: p010 の写真・動画の送信（約 3）、HFP の制御 p006（12）と音 p007（25）、実機の UAT p008（Android）。**HFP をベータ2 に入れるかは判断待ち**。規約の p009 はベータ3 | 約 40（HFP を含む。除けば約 6） | 未割当 |
| [WS129](ws129/ws.md) p005・p013 release notes・既知の問題・利用の手引き | 下書き済み（[notes](../docs/release/zedbsd-1.0.0-beta2.md)・[known issues](../docs/release/zedbsd-1.0.0-beta2-known-issues.md)・[guide](../docs/release/zedbsd-1.0.0-beta2-guide.md)）。**ユーザーの review 待ち**。WS199・WS200 の機能を足し、RC で review の comment を消す | 1.5 | ユーザー・P1 |
| [WS129](ws129/ws.md) p006 最終回帰（release の image） | 10/14 | 3 | T1 |
| [WS129](ws129/ws.md) p008 公開の準備（tag・CI・配布物） | 手順は用意済み。10/16、公開はユーザーの指示 | 0.5 | Q1・P1 |
| 次の UAT で出る Bug の枠 | — | 5 | P1 |
| **計** | | **約 55 LW**（約 18 時間、うち WS197 が約 40。HFP をベータ3 に回すと約 18 LW） | |

## 次の UAT で確認してほしい事項（5330、WS199・WS200 の後の image）

| # | 項目 | 手順 | 期待 |
| --- | --- | --- | --- |
| 11 | [WS197](ws197/ws.md) スマホの SMS（Android） | 手順は ws197/ws.md の「5330 の UAT の手順」: Settings → Bluetooth でスマホを pairing して「Use as phone」→ Phone の app で受信・送信・同期、app を閉じている間の通知 | SMS が送受信でき、履歴が同期される |
| 12 | [WS197](ws197/ws.md) 写真・動画の MMS の送信と「＋」の添付の選択（2026-10-11、main e7ff23fba 以降の image） | 手順は [phase010](ws197/phase010/phase.md) の末尾: Phone の「＋」→ Photos & Videos で写真・動画を選ぶ（File… でも可）→ 文を添えて Send | スマホから相手に MMS が届く。1 通は元の bytes で合計 8 MiB まで（carrier の上限を越えるとスマホ側で失敗しうる） |
| 13 | [WS197](ws197/ws.md) HFP の通話の制御（音はまだ、p007） | 手順は [phase006](ws197/phase006/phase.md) の末尾: Settings の Bluetooth で「Also use calls」→ スマホに着信 → Phone の通話の card で Answer・経過時間・Keypad の DTMF・Hang Up。Phone を閉じた時の着信の通知。会話の Call で発信 | スマホの通話が PC の操作で応答・終話・発信できる（音はスマホ側のまま） |

## 既知の問題に書いて出す（ベータ3 以降）

| 項目 | 理由 |
| --- | --- |
| BUG-255（蓋を閉じた間の HDMI）、BUG-159（電池で 5 fps）、BUG-145（AX211 の DHCP）、BUG-165（5330 の DSDT） | 調査が長い・実機の時間が要る |
| WS201（/home の暗号化）、WS195（/opt/keiland）、WS196（useradd 等）、WS198（self-build）、[WS001 p045](ws001/phase045/phase.md) POSIX の header、[WS126](ws126/ws.md) Python | ベータ3 の列 |
| 規約の全文の見直しの Phase（各 WS） | ユーザーの決定でベータ3 |

## 運用

- 凍結の目標の 10/13 の後も、ベータなので UAT の Bug の直しは 10/16 の準備に間に合う範囲で続ける。新しい仕様の変更は「ベータ3 に回すか」を Q1 がユーザーに聞く。
- Q1 は進むたびにこの表を更新し、完了した項目を消す（2026-10-09 ユーザー「都度、beta2.mdを更新していただけると、進捗がわかって助かります」）。
